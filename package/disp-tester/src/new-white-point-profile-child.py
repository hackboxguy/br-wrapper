#!/usr/bin/env python3
"""
new-white-point-profile-child.py

Child script for disp-tester that profiles the new `wp_adjust` FPGA block
(Q4.12 RGB gain registers, fpga-wp-adjust scalar v1 register map) against
spotread XYZ/xyY measurements. It is the wp_adjust counterpart of the legacy
white-point-profile-child.py (which sweeps the write-only wpx/wpy/wpz
registers); the legacy script is untouched.

The profiler:

1. Optionally measures the display's full-field red/green/blue primaries at
   unity gain (used later to seed D65/matching solves).
2. Sweeps each gain channel through --axis-values (Q4.12, unity 0x1000) while
   the other channels stay at unity, committing each point atomically at a
   frame boundary and averaging N valid samples per point.
3. Records per-sample and per-point CSV plus a JSON profile
   (schema disp-tester-white-point-profile-v2) for offline analysis:
   per-LSB response, linearity/monotonicity, and gamma estimation.

Default axis values mix fine steps (-4 and -16 LSB, to verify small-step
response against sensor noise) with coarse steps down to 0.875.

Backends (--backend): see new-white-point-match-child.py. 'simulate' (the
default until the new RTL is integrated) runs the full flow against a
simulated panel; 'i2cdev' talks to the FPGA new slave over /dev/i2c-N.
"""

import argparse
import csv
import datetime
import fcntl
import json
import math
import os
import random
import re
import select
import signal
import socket
import subprocess
import sys
import time


DEFAULT_DISP_HOST = os.environ.get("DISP_TESTER_HOST", "127.0.0.1")
DEFAULT_DISP_PORT = int(os.environ.get("DISP_TESTER_PORT", "8082"))
DEFAULT_RECORD_DIR = "/home/pi/test-data"
DEFAULT_COLORIMETER_MATCH = [
    "i1display",
    "i1 display",
    "i1display pro",
    "i1 display pro",
]
DEFAULT_COLORIMETER_USB_IDS = [
    "0765:5020",
]
DEFAULT_AXIS_VALUES = "0x0FFC,0x0FF0,0x0FC0,0x0F80,0x0F00,0x0E80,0x0E00"

# ---------------------------------------------------------------------------
# wp_adjust scalar v1 register map (fpga-wp-adjust docs/register-map.md,
# register-map revision 0x0113).
# ---------------------------------------------------------------------------
WP_ADJUST_ID = 0x57A1
WP_ADJUST_VERSION_MAJOR = 0x01
COMMIT_MAGIC = 0xCA1B
COMMIT_CANCEL_MAGIC = 0xC0FF
DEFAULTS_MAGIC = 0xD65D

REG_CONTROL_SHADOW = 0x00
REG_R_GAIN_SHADOW = 0x01
REG_G_GAIN_SHADOW = 0x02
REG_B_GAIN_SHADOW = 0x03
REG_R_GAIN_ACTIVE = 0x21
REG_G_GAIN_ACTIVE = 0x22
REG_B_GAIN_ACTIVE = 0x23
REG_ID = 0x70
REG_VERSION = 0x71
REG_STATUS = 0x72
REG_COMMIT = 0x7E
REG_DEFAULTS = 0x7F

STATUS_COMMIT_PENDING = 0x0001
STATUS_COMMIT_CONSUMED = 0x0002

GAIN_CHANNELS = ("r", "g", "b")
GAIN_SHADOW_REGS = {"r": REG_R_GAIN_SHADOW, "g": REG_G_GAIN_SHADOW, "b": REG_B_GAIN_SHADOW}
GAIN_ACTIVE_REGS = {"r": REG_R_GAIN_ACTIVE, "g": REG_G_GAIN_ACTIVE, "b": REG_B_GAIN_ACTIVE}

DEFAULT_INITIAL_TEXT = (
    "White Point Profiling (wp_adjust)\n"
    "Starting..."
)
DEFAULT_SENSOR_NOT_FOUND_TEXT = (
    "White Point Profiling (wp_adjust)\n"
    "i1 Display Pro Not Found\n"
    "Connect USB colorimeter"
)
DEFAULT_READY_TEXT = (
    "Place the Sensor on the Display and Click Start button"
)
DEFAULT_PROGRESS_TEXT = (
    "White Point Profiling (wp_adjust)\n"
    "Point {point}/{total}: {label}\n"
    "R=0x{r:04X} G=0x{g:04X} B=0x{b:04X}\n"
    "Sample {sample}/{samples}  Retry {failures}/{failure_budget}"
)
DEFAULT_COMPLETE_TEXT = (
    "White Point Profiling Done\n"
    "Points: {points}/{total}\n"
    "Output: {json_output}\n"
    "Tap screen for EXIT"
)
DEFAULT_ABORT_TEXT = (
    "White Point Profiling Aborted\n"
    "Points: {points}/{total}\n"
    "Reason: {reason}\n"
    "Tap screen for EXIT"
)

_FLOAT_RE = r"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)"
_XYZ_RE = re.compile(rf"Result is XYZ:\s*{_FLOAT_RE}\s+{_FLOAT_RE}\s+{_FLOAT_RE}")
_YXY_RE = re.compile(rf"Yxy:\s*{_FLOAT_RE}\s+{_FLOAT_RE}\s+{_FLOAT_RE}")
PROFILE_SCHEMA = "disp-tester-white-point-profile-v2"
SAMPLE_CSV_HEADER = [
    "point_index", "label", "r_gain", "g_gain", "b_gain",
    "sample_index", "timestamp", "X", "Y", "Z", "x", "y",
    "status", "error",
]
SUMMARY_CSV_HEADER = [
    "point_index", "label", "r_gain", "g_gain", "b_gain", "samples",
    "X", "Y", "Z", "x", "y", "std_x", "std_y", "std_Y",
    "status", "error",
]

_stop_requested = False
_stop_signal = None
_active_process = None


class ProfileAborted(RuntimeError):
    pass


def _signal_handler(signum, _frame):
    global _stop_requested, _stop_signal
    _stop_requested = True
    _stop_signal = signum
    terminate_active_process()


def install_signal_handlers():
    signal.signal(signal.SIGINT, _signal_handler)
    signal.signal(signal.SIGTERM, _signal_handler)


def stop_requested():
    return _stop_requested


def signal_exit_code():
    if _stop_signal == signal.SIGINT:
        return 130
    if _stop_signal == signal.SIGTERM:
        return 143
    return 1


def terminate_active_process():
    proc = _active_process
    if proc and proc.poll() is None:
        try:
            proc.terminate()
        except OSError:
            pass


class ControlPipe:
    def __init__(self):
        self.closed = False
        self.abort_requested = False
        self.restart_requested = False
        try:
            flags = fcntl.fcntl(sys.stdin.fileno(), fcntl.F_GETFL)
            fcntl.fcntl(sys.stdin.fileno(), fcntl.F_SETFL, flags | os.O_NONBLOCK)
        except (OSError, ValueError):
            self.closed = True

    def poll(self):
        if self.closed:
            return
        while True:
            try:
                ready, _, _ = select.select([sys.stdin], [], [], 0)
            except (OSError, ValueError):
                self.closed = True
                return
            if not ready:
                return
            try:
                line = sys.stdin.readline()
            except OSError:
                self.closed = True
                return
            if line == "":
                self.closed = True
                return
            self._handle_line(line)

    def _handle_line(self, line):
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            return
        if message.get("command") != "set_recording":
            return
        if bool(message.get("enabled")):
            self.restart_requested = True
        else:
            self.abort_requested = True


def interruptible_sleep(seconds, control=None):
    end = time.monotonic() + max(0.0, seconds)
    while not stop_requested():
        if control:
            control.poll()
            if control.abort_requested:
                raise ProfileAborted("operator requested abort")
        remaining = end - time.monotonic()
        if remaining <= 0:
            return True
        time.sleep(min(0.1, remaining))
    return False


class DispTesterClient:
    def __init__(self, host, port, timeout=1.0, gap=0.15):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.gap = gap
        self._last_call = 0.0

    def _wait_gap(self):
        elapsed = time.monotonic() - self._last_call
        if elapsed < self.gap:
            interruptible_sleep(self.gap - elapsed)

    def command(self, command, required=False):
        if stop_requested():
            raise InterruptedError("stop requested")

        self._wait_gap()
        response = ""
        try:
            with socket.create_connection((self.host, self.port),
                                          timeout=self.timeout) as sock:
                sock.settimeout(self.timeout)
                sock.sendall((command + "\n").encode("utf-8"))
                response = self._recv_line(sock)
        except OSError as e:
            if required:
                raise RuntimeError(f"disp-tester command failed: {command}: {e}")
            print(f"warning: disp-tester command failed: {command}: {e}",
                  file=sys.stderr)
        finally:
            self._last_call = time.monotonic()

        if required and not response.startswith("OK"):
            raise RuntimeError(f"disp-tester rejected {command!r}: {response!r}")
        return response

    def best_effort(self, command):
        try:
            self.command(command, required=False)
        except InterruptedError:
            raise
        except Exception as e:
            print(f"warning: disp-tester command failed: {e}", file=sys.stderr)

    def set_overlay_text(self, message):
        safe_message = str(message).replace("\n", "\\n")
        self.best_effort(f"set-metadata-text {safe_message}")

    def set_child_action_active(self, active):
        state = "enable" if active else "disable"
        self.best_effort(f"set-child-action-active {state}")

    def _recv_line(self, sock):
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(1024)
            if not chunk:
                break
            data += chunk
        return data.decode("utf-8", errors="replace").strip()


def one_line(text, max_len=160):
    collapsed = " ".join(str(text).split())
    if len(collapsed) > max_len:
        return collapsed[:max_len - 3] + "..."
    return collapsed


def now_iso():
    return datetime.datetime.now().astimezone().isoformat()


def read_text_file(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read().strip()
    except OSError:
        return ""


def normalize_usb_id(value):
    return str(value).lower().replace(":", "").replace("-", "").strip()


def usb_device_records(sysfs_root="/sys/bus/usb/devices"):
    try:
        entries = sorted(os.listdir(sysfs_root))
    except OSError:
        return []

    records = []
    for entry in entries:
        path = os.path.join(sysfs_root, entry)
        vendor = read_text_file(os.path.join(path, "idVendor"))
        product_id = read_text_file(os.path.join(path, "idProduct"))
        manufacturer = read_text_file(os.path.join(path, "manufacturer"))
        product = read_text_file(os.path.join(path, "product"))
        if not vendor and not product_id and not manufacturer and not product:
            continue
        records.append({
            "path": path,
            "usb_id": f"{vendor}:{product_id}".lower() if vendor and product_id else "",
            "text": f"{manufacturer} {product}".lower(),
        })
    return records


def find_colorimeter(match_terms, usb_ids, sysfs_root="/sys/bus/usb/devices"):
    wanted_ids = {normalize_usb_id(item) for item in usb_ids if item}
    wanted_terms = [term.lower() for term in match_terms if term]

    for record in usb_device_records(sysfs_root):
        if wanted_ids and normalize_usb_id(record["usb_id"]) in wanted_ids:
            return record
        if any(term in record["text"] for term in wanted_terms):
            return record
    return None


def require_colorimeter(display, args):
    if not args.require_colorimeter:
        return True
    device = find_colorimeter(args.colorimeter_match, args.colorimeter_usb_id,
                              args.usb_sysfs_root)
    if device:
        print(f"colorimeter found: {device['usb_id']} {device['text']}",
              file=sys.stderr)
        return True

    print("error: i1 Display Pro USB colorimeter not found", file=sys.stderr)
    display.best_effort(f"set-metadata-color {args.alert_color}")
    display.set_overlay_text(args.sensor_not_found_text)
    return False


# ---------------------------------------------------------------------------
# Color helpers (shared with the wp_adjust match child).
# ---------------------------------------------------------------------------

def xyY_to_XYZ(x, y, Y):
    if y <= 0.0:
        raise ValueError("xyY y must be greater than zero")
    return (x * Y / y, Y, (1.0 - x - y) * Y / y)


def XYZ_to_xy(X, Y, Z):
    total = X + Y + Z
    if total <= 0.0:
        raise ValueError("XYZ sum must be greater than zero")
    return (X / total, Y / total)


def _det3(m):
    (a, b, c), (d, e, f), (g, h, i) = m
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)


def solve_3x3(matrix, vector):
    det = _det3(matrix)
    if abs(det) < 1e-12:
        raise ValueError("RGB primary matrix is singular")
    cols = tuple(zip(*matrix))
    out = []
    for index in range(3):
        replaced = list(cols)
        replaced[index] = vector
        out.append(_det3(tuple(zip(*replaced))) / det)
    return tuple(out)


# ---------------------------------------------------------------------------
# wp_adjust register backends (shared design with the match child).
# ---------------------------------------------------------------------------

class SimulatedWpAdjust:
    def __init__(self, frac_bits=12, version=0x0113):
        self.frac_bits = frac_bits
        self.version = version
        self.unity = 1 << frac_bits
        self._reset()

    def _reset(self):
        self.shadow = {ch: self.unity for ch in GAIN_CHANNELS}
        self.shadow_control = 0
        self.active = {ch: self.unity for ch in GAIN_CHANNELS}
        self.active_control = 0
        self.commit_pending = False
        self.commit_consumed = False

    def read16(self, addr):
        if addr == REG_ID:
            return WP_ADJUST_ID
        if addr == REG_VERSION:
            return self.version
        if addr == REG_STATUS:
            value = (self.frac_bits << 8)
            value |= (self.active_control & 0x3) << 2
            value |= STATUS_COMMIT_CONSUMED if self.commit_consumed else 0
            value |= STATUS_COMMIT_PENDING if self.commit_pending else 0
            if self.commit_pending:
                self.active = dict(self.shadow)
                self.active_control = self.shadow_control
                self.commit_pending = False
                self.commit_consumed = True
            return value
        if addr == REG_CONTROL_SHADOW:
            return self.shadow_control
        if addr == 0x20:
            return self.active_control
        for ch in GAIN_CHANNELS:
            if addr == GAIN_SHADOW_REGS[ch]:
                return self.shadow[ch]
            if addr == GAIN_ACTIVE_REGS[ch]:
                return self.active[ch]
        return 0

    def write16(self, addr, value):
        value &= 0xFFFF
        if addr == REG_CONTROL_SHADOW:
            self.shadow_control = value & 0x3
        elif addr == REG_COMMIT and value == COMMIT_MAGIC:
            self.commit_pending = True
            self.commit_consumed = False
        elif addr == REG_COMMIT and value == COMMIT_CANCEL_MAGIC:
            self.commit_pending = False
            self.commit_consumed = False
        elif addr == REG_DEFAULTS and value == DEFAULTS_MAGIC:
            self._reset()
        else:
            for ch in GAIN_CHANNELS:
                if addr == GAIN_SHADOW_REGS[ch]:
                    self.shadow[ch] = value


class I2CDevWpAdjust:
    """Direct /dev/i2c-N access through the FPGA new slave (see the match
    child for the on-wire framing notes; --wp-page is TBD until the FPGA
    integration assigns the wp_adjust page)."""

    I2C_SLAVE_IOCTL = 0x0703

    def __init__(self, device, slave_addr, page):
        self.device = device
        self.slave_addr = slave_addr
        self.page = page
        self.fd = os.open(device, os.O_RDWR)
        fcntl.ioctl(self.fd, self.I2C_SLAVE_IOCTL, self.slave_addr)

    def read16(self, addr):
        os.write(self.fd, bytes([self.page, addr & 0xFF]))
        data = os.read(self.fd, 2)
        if len(data) != 2:
            raise RuntimeError(f"short I2C read at page 0x{self.page:02X} reg 0x{addr:02X}")
        return (data[0] << 8) | data[1]

    def write16(self, addr, value):
        value &= 0xFFFF
        payload = bytes([self.page, addr & 0xFF, (value >> 8) & 0xFF, value & 0xFF])
        if os.write(self.fd, payload) != len(payload):
            raise RuntimeError(f"short I2C write at page 0x{self.page:02X} reg 0x{addr:02X}")


class WpAdjustRegisters:
    def __init__(self, backend):
        self.backend = backend

    def probe(self, expected_frac_bits=12):
        device_id = self.backend.read16(REG_ID)
        if device_id != WP_ADJUST_ID:
            raise RuntimeError(f"unexpected wp_adjust ID 0x{device_id:04X}")
        version = self.backend.read16(REG_VERSION)
        if (version >> 8) & 0xFF != WP_ADJUST_VERSION_MAJOR:
            raise RuntimeError(f"unsupported wp_adjust register-map major in 0x{version:04X}")
        status = self.backend.read16(REG_STATUS)
        frac_bits = (status >> 8) & 0xFF
        if frac_bits != expected_frac_bits:
            raise RuntimeError(
                f"unexpected wp_adjust FRAC_BITS {frac_bits}, expected {expected_frac_bits}")
        return version, frac_bits

    def status(self):
        return self.backend.read16(REG_STATUS)

    def defaults(self):
        self.backend.write16(REG_DEFAULTS, DEFAULTS_MAGIC)

    def cancel_commit(self):
        self.backend.write16(REG_COMMIT, COMMIT_CANCEL_MAGIC)

    def write_gains_and_commit(self, fixed_gains, timeout_sec=2.0, poll_sec=0.02,
                               control=None):
        if self.status() & STATUS_COMMIT_PENDING:
            raise RuntimeError("refusing to write shadow registers while commit is pending")

        for ch in GAIN_CHANNELS:
            self.backend.write16(GAIN_SHADOW_REGS[ch], fixed_gains[ch])
        self.backend.write16(REG_CONTROL_SHADOW, 0x0001)

        for ch in GAIN_CHANNELS:
            got = self.backend.read16(GAIN_SHADOW_REGS[ch])
            if got != fixed_gains[ch]:
                raise RuntimeError(
                    f"shadow readback mismatch on {ch}: wrote 0x{fixed_gains[ch]:04X}, "
                    f"read 0x{got:04X}; not committing")

        self.backend.write16(REG_COMMIT, COMMIT_MAGIC)

        deadline = time.monotonic() + timeout_sec
        while True:
            status = self.status()
            if status & STATUS_COMMIT_CONSUMED or not status & STATUS_COMMIT_PENDING:
                return
            if time.monotonic() >= deadline:
                raise RuntimeError("commit was not consumed; is video/vsync running?")
            if control:
                control.poll()
                if control.abort_requested:
                    self.cancel_commit()
                    raise ProfileAborted("operator requested abort")
            time.sleep(poll_sec)


# ---------------------------------------------------------------------------
# Measurement (spotread or simulated panel).
# ---------------------------------------------------------------------------

def run_process_capture(argv, timeout, control=None):
    global _active_process

    if stop_requested():
        raise InterruptedError("stop requested")

    proc = subprocess.Popen(
        argv,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
    )
    _active_process = proc
    start = time.monotonic()

    try:
        while proc.poll() is None:
            if control:
                control.poll()
                if control.abort_requested:
                    terminate_active_process()
                    raise ProfileAborted("operator requested abort")
            if stop_requested():
                terminate_active_process()
                raise InterruptedError("stop requested")
            if time.monotonic() - start > timeout:
                proc.kill()
                out, _ = proc.communicate(timeout=1)
                raise subprocess.TimeoutExpired(argv, timeout, output=out)
            time.sleep(0.1)
        out, _ = proc.communicate(timeout=1)
        if proc.returncode != 0:
            raise subprocess.CalledProcessError(proc.returncode, argv, output=out)
        return out
    finally:
        if _active_process is proc:
            _active_process = None


def parse_spotread_measurement(text):
    yxy_match = _YXY_RE.search(text)
    if not yxy_match:
        return None

    y_value, x_value, y_chromaticity = (float(item) for item in yxy_match.groups())
    measurement = {"Y": y_value, "x": x_value, "y": y_chromaticity}

    xyz_match = _XYZ_RE.search(text)
    if xyz_match:
        x_t, y_t, z_t = (float(item) for item in xyz_match.groups())
        measurement.update({"X": x_t, "Y": y_t, "Z": z_t})
    return measurement


class SpotreadMeter:
    def __init__(self, args):
        self.args = args

    def measure(self, control=None):
        try:
            out = run_process_capture(["spotread", "-x", "-O"],
                                      self.args.spotread_timeout, control)
        except (InterruptedError, ProfileAborted):
            raise
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired,
                FileNotFoundError) as e:
            detail = getattr(e, "output", None) or str(e)
            return None, one_line(detail)

        measurement = parse_spotread_measurement(out)
        if measurement is None:
            return None, "spotread output did not contain Yxy"
        return measurement, ""


class SimulatedPanel:
    """Single simulated display whose unity white matches the measured
    12.3-nq1v1 baseline; channel outputs follow L = primary * gain^gamma."""

    def __init__(self, registers_backend, gamma=2.184, noise_seed=20260613):
        self.backend = registers_backend
        self.gamma = gamma
        self.rng = random.Random(noise_seed)
        self.pattern = "white"
        self.primaries = self._scale_primaries(
            {
                "r": (0.4360, 0.2225, 0.0139),
                "g": (0.3851, 0.7169, 0.0971),
                "b": (0.1431, 0.0606, 0.7139),
            },
            white_xy=(0.306786, 0.305357),
            white_Y=1050.9,
        )

    @staticmethod
    def _scale_primaries(base, white_xy, white_Y):
        target_white = xyY_to_XYZ(white_xy[0], white_xy[1], white_Y)
        matrix = tuple(
            tuple(base[ch][row] for ch in GAIN_CHANNELS)
            for row in range(3)
        )
        scales = solve_3x3(matrix, target_white)
        return {
            ch: tuple(base[ch][row] * scale for row in range(3))
            for ch, scale in zip(GAIN_CHANNELS, scales)
        }

    def _active_linear_gains(self):
        unity = float(self.backend.unity)
        enabled = bool(self.backend.active_control & 0x1)
        gains = {}
        for ch in GAIN_CHANNELS:
            code = (self.backend.active[ch] / unity) if enabled else 1.0
            gains[ch] = max(0.0, code) ** self.gamma
        return gains

    def measure(self, control=None):
        gains = self._active_linear_gains()
        if self.pattern == "white":
            channels = GAIN_CHANNELS
        elif self.pattern in ("red", "green", "blue"):
            channels = (self.pattern[0],)
        else:
            return None, f"simulated pattern {self.pattern!r} not supported"
        X = sum(self.primaries[ch][0] * gains[ch] for ch in channels)
        Y = sum(self.primaries[ch][1] * gains[ch] for ch in channels)
        Z = sum(self.primaries[ch][2] * gains[ch] for ch in channels)

        x, y = XYZ_to_xy(X, Y, Z)
        x += self.rng.gauss(0.0, 0.000015)
        y += self.rng.gauss(0.0, 0.000015)
        return {
            "X": X,
            "Y": Y + self.rng.gauss(0.0, 0.12),
            "Z": Z,
            "x": x,
            "y": y,
        }, ""


# ---------------------------------------------------------------------------
# Sampling and recording.
# ---------------------------------------------------------------------------

def measurement_valid(measurement, min_nits):
    if not measurement:
        return False
    for key in ("Y", "x", "y"):
        value = measurement.get(key)
        if not isinstance(value, (int, float)) or not math.isfinite(value):
            return False
    return (0.0 <= measurement["x"] <= 1.0 and
            0.0 <= measurement["y"] <= 1.0 and
            measurement["Y"] >= min_nits)


def mean(values):
    return sum(values) / len(values) if values else None


def population_stddev(values):
    if len(values) < 2:
        return 0.0 if values else None
    avg = mean(values)
    return math.sqrt(sum((v - avg) ** 2 for v in values) / len(values))


def summarize_samples(samples):
    average = {}
    stddev = {}
    for key in ("X", "Y", "Z", "x", "y"):
        values = [s[key] for s in samples
                  if isinstance(s.get(key), (int, float))]
        average[key] = mean(values)
        stddev[key] = population_stddev(values)
    return average, stddev


def format_float(value, precision=6):
    if value is None:
        return ""
    return f"{value:.{precision}f}"


class ProfileRecorder:
    def __init__(self, args, points, version):
        self.args = args
        self.points = points
        self.json_path, self.summary_path, self.samples_path = output_paths(args)
        self.summary_file = None
        self.samples_file = None
        self.summary_writer = None
        self.samples_writer = None
        self.data = {
            "schema": PROFILE_SCHEMA,
            "created_at": now_iso(),
            "script": "new-white-point-profile-child.py",
            "backend": args.backend,
            "fpga": {
                "register_map": "wp_adjust_scalar_v1",
                "version": f"0x{version:04X}",
                "gain_format": f"Q{16 - args.frac_bits}.{args.frac_bits}",
                "frac_bits": args.frac_bits,
                "unity_hex": f"0x{1 << args.frac_bits:04X}",
            },
            "parameters": {
                "samples_per_point": args.samples,
                "settle_seconds": args.settle_seconds,
                "sample_gap_seconds": args.sample_gap_seconds,
                "point_failure_budget": args.point_failure_budget,
                "spotread_timeout": args.spotread_timeout,
                "min_sample_nits": args.min_sample_nits,
                "axis_values": args.axis_values,
                "measure_primaries": args.measure_primaries,
            },
            "outputs": {
                "json": self.json_path,
                "summary_csv": self.summary_path,
                "samples_csv": self.samples_path,
            },
            "primaries_at_unity": None,
            "points": [],
            "status": "running",
        }

    def open(self):
        for path in (self.json_path, self.summary_path, self.samples_path):
            directory = os.path.dirname(os.path.abspath(path))
            if directory:
                os.makedirs(directory, exist_ok=True)

        self.summary_file = open(self.summary_path, "w", newline="")
        self.samples_file = open(self.samples_path, "w", newline="")
        self.summary_writer = csv.writer(self.summary_file)
        self.samples_writer = csv.writer(self.samples_file)
        self.summary_file.write("# disp-tester wp_adjust white-point profile summary\n")
        self.summary_file.write(f"# timestamp={self.data['created_at']}\n")
        self.summary_file.write(f"# json={self.json_path}\n")
        self.summary_writer.writerow(SUMMARY_CSV_HEADER)
        self.samples_file.write("# disp-tester wp_adjust white-point profile samples\n")
        self.samples_file.write(f"# timestamp={self.data['created_at']}\n")
        self.samples_file.write(f"# json={self.json_path}\n")
        self.samples_writer.writerow(SAMPLE_CSV_HEADER)
        self.flush()
        self.write_json()

    def set_primaries(self, primaries):
        self.data["primaries_at_unity"] = {
            ch: list(primaries[ch]) for ch in GAIN_CHANNELS
        }
        self.write_json()

    def add_point_result(self, point, samples, average, stddev,
                         status="ok", error=""):
        gains = point["gains"]
        self.data["points"].append({
            "index": point["index"],
            "label": point["label"],
            "gains": {ch: gains[ch] for ch in GAIN_CHANNELS},
            "gains_hex": {ch: f"0x{gains[ch]:04X}" for ch in GAIN_CHANNELS},
            "status": status,
            "error": error,
            "samples": samples,
            "average": average,
            "stddev": stddev,
        })
        self.summary_writer.writerow([
            point["index"], point["label"],
            gains["r"], gains["g"], gains["b"], len(samples),
            format_float(average.get("X")), format_float(average.get("Y")),
            format_float(average.get("Z")), format_float(average.get("x")),
            format_float(average.get("y")),
            format_float(stddev.get("x")), format_float(stddev.get("y")),
            format_float(stddev.get("Y")),
            status, error,
        ])
        self.flush()
        self.write_json()

    def write_sample_row(self, point, sample_index, sample,
                         status="ok", error=""):
        gains = point["gains"]
        self.samples_writer.writerow([
            point["index"], point["label"],
            gains["r"], gains["g"], gains["b"],
            sample_index, sample.get("timestamp", now_iso()),
            format_float(sample.get("X")), format_float(sample.get("Y")),
            format_float(sample.get("Z")), format_float(sample.get("x")),
            format_float(sample.get("y")),
            status, error,
        ])
        self.flush()

    def set_status(self, status, error=""):
        self.data["status"] = status
        if error:
            self.data["error"] = error
        self.data["updated_at"] = now_iso()
        self.write_json()

    def write_json(self):
        tmp_path = self.json_path + ".tmp"
        with open(tmp_path, "w", encoding="utf-8") as f:
            json.dump(self.data, f, indent=2, sort_keys=True)
            f.write("\n")
        os.replace(tmp_path, self.json_path)

    def flush(self):
        if self.summary_file:
            self.summary_file.flush()
        if self.samples_file:
            self.samples_file.flush()

    def close(self):
        self.flush()
        if self.summary_file:
            self.summary_file.close()
            self.summary_file = None
        if self.samples_file:
            self.samples_file.close()
            self.samples_file = None


def default_output_prefix(record_dir):
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    return os.path.join(record_dir, f"{ts}-wp-profile-v2")


def output_paths(args):
    prefix = args.output_prefix or default_output_prefix(args.record_dir)
    if prefix.endswith(".json"):
        prefix = prefix[:-5]
    return (prefix + ".json", prefix + "-summary.csv", prefix + "-samples.csv")


def build_profile_points(args, unity):
    points = []
    seen = set()

    def add(label, gains):
        key = (gains["r"], gains["g"], gains["b"])
        if key in seen:
            return
        seen.add(key)
        points.append({"index": len(points) + 1, "label": label,
                       "gains": dict(gains)})

    base = {ch: unity for ch in GAIN_CHANNELS}
    add("baseline", base)

    for channel in GAIN_CHANNELS:
        for value in args.axis_values:
            if value == unity:
                continue
            gains = dict(base)
            gains[channel] = value
            add(f"{channel}-0x{value:04X}", gains)

    for extra in args.point or []:
        add(extra["label"], extra["gains"])

    return points


def parse_gain_value(text):
    try:
        value = int(str(text), 0)
    except ValueError as e:
        raise argparse.ArgumentTypeError(str(e))
    if not 0 <= value <= 0xFFFF:
        raise argparse.ArgumentTypeError("gain value must be in [0, 0xFFFF]")
    return value


def parse_value_list(text):
    values = []
    for part in str(text).split(","):
        stripped = part.strip()
        if not stripped:
            continue
        value = parse_gain_value(stripped)
        if value not in values:
            values.append(value)
    if not values:
        raise argparse.ArgumentTypeError("value list must not be empty")
    return values


def parse_extra_point(text):
    parts = [part.strip() for part in str(text).split(",")]
    if len(parts) not in (3, 4):
        raise argparse.ArgumentTypeError("point must be r,g,b or label,r,g,b")
    if len(parts) == 3:
        values = [parse_gain_value(part) for part in parts]
        label = f"extra-0x{values[0]:04X}-0x{values[1]:04X}-0x{values[2]:04X}"
    else:
        label = parts[0] or "extra"
        values = [parse_gain_value(part) for part in parts[1:]]
    return {"label": label, "gains": {"r": values[0], "g": values[1], "b": values[2]}}


def collect_samples(display, meter, recorder, point, total, args, control):
    samples = []
    failures = 0
    first_error = ""
    gains = point["gains"]

    while len(samples) < args.samples:
        sample_index = len(samples) + 1
        display.set_overlay_text(args.progress_text.format(
            point=point["index"], total=total, label=point["label"],
            r=gains["r"], g=gains["g"], b=gains["b"],
            sample=sample_index, samples=args.samples,
            failures=failures, failure_budget=args.point_failure_budget,
        ))

        if args.require_colorimeter and not find_colorimeter(
            args.colorimeter_match, args.colorimeter_usb_id, args.usb_sysfs_root,
        ):
            measurement, error = None, "i1 Display Pro USB colorimeter disconnected"
        else:
            measurement, error = meter.measure(control)

        if not measurement_valid(measurement, args.min_sample_nits):
            failures += 1
            first_error = error or f"invalid measurement below {args.min_sample_nits:.1f} nits"
            recorder.write_sample_row(point, sample_index, {}, status="fail",
                                      error=first_error)
            print(f"    sample {sample_index}/{args.samples} failed "
                  f"({failures}/{args.point_failure_budget}): {first_error}",
                  file=sys.stderr)
            if failures >= args.point_failure_budget:
                raise RuntimeError(
                    f"{point['label']} failed after {failures} failed reads: {first_error}")
            interruptible_sleep(args.sample_failure_sleep, control)
            continue

        measurement["timestamp"] = now_iso()
        samples.append(measurement)
        recorder.write_sample_row(point, sample_index, measurement)
        print(f"    sample {sample_index}/{args.samples}: "
              f"Y={measurement['Y']:.2f} x={measurement['x']:.5f} y={measurement['y']:.5f}",
              file=sys.stderr)
        if sample_index < args.samples:
            interruptible_sleep(args.sample_gap_seconds, control)

    return samples, failures, first_error


def set_pattern(display, panel_sim, args, pattern, control):
    display.command(f"pattern {pattern}", required=not args.simulate)
    if panel_sim:
        panel_sim.pattern = pattern
    interruptible_sleep(args.settle_seconds, control)


def measure_primaries(display, meter, panel_sim, recorder, args, control):
    primaries = {}
    fake_point = {"index": 0, "label": "primary",
                  "gains": {ch: 1 << args.frac_bits for ch in GAIN_CHANNELS}}
    for pattern, ch in (("red", "r"), ("green", "g"), ("blue", "b")):
        set_pattern(display, panel_sim, args, pattern, control)
        fake_point["label"] = f"primary-{pattern}"
        samples, _failures, _err = collect_samples(
            display, meter, recorder, fake_point, 3, args, control)
        average, _stddev = summarize_samples(samples)
        if average.get("X") is None or average.get("Z") is None:
            primaries[ch] = list(xyY_to_XYZ(average["x"], average["y"], average["Y"]))
        else:
            primaries[ch] = [average["X"], average["Y"], average["Z"]]
    set_pattern(display, panel_sim, args, "white", control)
    return primaries


def wait_for_start(display, control, args, message):
    if args.auto_start:
        return True
    control.abort_requested = False
    control.restart_requested = False
    display.set_child_action_active(False)
    display.set_overlay_text(message)

    while not stop_requested():
        control.poll()
        if control.restart_requested:
            control.abort_requested = False
            control.restart_requested = False
            display.set_child_action_active(True)
            return True
        if control.abort_requested:
            control.abort_requested = False
        time.sleep(0.2)
    return False


def parse_int_auto(value):
    try:
        return int(str(value), 0)
    except ValueError as e:
        raise argparse.ArgumentTypeError(str(e))


def parse_args():
    parser = argparse.ArgumentParser(
        description="Profile wp_adjust Q4.12 gains against i1Display Pro measurements.")
    parser.add_argument("--disp-host", default=DEFAULT_DISP_HOST)
    parser.add_argument("--disp-port", type=int, default=DEFAULT_DISP_PORT)
    parser.add_argument("--disp-timeout", type=float, default=1.0)

    parser.add_argument("--backend", choices=("simulate", "i2cdev"),
                        default="simulate")
    parser.add_argument("--i2c-dev", default="/dev/i2c-1")
    parser.add_argument("--i2c-addr", type=parse_int_auto, default=0x1E)
    parser.add_argument("--wp-page", type=parse_int_auto, default=0x03)
    parser.add_argument("--frac-bits", type=int, default=12, dest="frac_bits")

    parser.add_argument("--axis-values", type=parse_value_list,
                        default=parse_value_list(DEFAULT_AXIS_VALUES),
                        help="Comma-separated Q4.12 values for each single-"
                             "channel sweep (hex ok). Default mixes -4/-16 LSB "
                             "fine steps with coarse steps down to 0.875.")
    parser.add_argument("--point", action="append", type=parse_extra_point,
                        help="Extra point as r,g,b or label,r,g,b (Q4.12 "
                             "values, hex ok). Can be repeated.")
    parser.add_argument("--measure-primaries", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Measure full-field R/G/B primaries at unity "
                             "before the gain sweep.")
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--settle-seconds", type=float, default=1.5)
    parser.add_argument("--sample-gap-seconds", type=float, default=0.2)
    parser.add_argument("--point-failure-budget", type=int, default=8)
    parser.add_argument("--sample-failure-sleep", type=float, default=2.0)
    parser.add_argument("--spotread-timeout", type=float, default=15.0)
    parser.add_argument("--min-sample-nits", type=float, default=1.0,
                        help="Validity floor (blue full-field is far dimmer "
                             "than white).")
    parser.add_argument("--commit-timeout", type=float, default=2.0)
    parser.add_argument("--record-dir", default=DEFAULT_RECORD_DIR)
    parser.add_argument("--output-prefix",
                        help="Output prefix; writes .json, -summary.csv, and "
                             "-samples.csv. Default: <record-dir>/<ts>-wp-profile-v2")
    parser.add_argument("--auto-start", action="store_true",
                        help="Skip operator Start gating (headless test/demo).")

    parser.add_argument("--initial-text", default=DEFAULT_INITIAL_TEXT)
    parser.add_argument("--sensor-not-found-text", default=DEFAULT_SENSOR_NOT_FOUND_TEXT)
    parser.add_argument("--ready-text", default=DEFAULT_READY_TEXT)
    parser.add_argument("--progress-text", default=DEFAULT_PROGRESS_TEXT)
    parser.add_argument("--complete-text", default=DEFAULT_COMPLETE_TEXT)
    parser.add_argument("--abort-text", default=DEFAULT_ABORT_TEXT)
    parser.add_argument("--progress-fontsize", type=int, default=24)
    parser.add_argument("--metadata-align", default="left")
    parser.add_argument("--normal-color", default="white")
    parser.add_argument("--alert-color", default="red")
    parser.add_argument("--require-colorimeter", action=argparse.BooleanOptionalAction,
                        default=None,
                        help="Default: required for i2cdev, not for simulate.")
    parser.add_argument("--colorimeter-match", action="append",
                        default=list(DEFAULT_COLORIMETER_MATCH))
    parser.add_argument("--colorimeter-usb-id", action="append",
                        default=list(DEFAULT_COLORIMETER_USB_IDS))
    parser.add_argument("--usb-sysfs-root", default="/sys/bus/usb/devices")
    parser.add_argument("--restore-defaults", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Write wp_adjust DEFAULTS (pass-through) on exit.")

    args = parser.parse_args()
    args.simulate = args.backend == "simulate"
    if args.require_colorimeter is None:
        args.require_colorimeter = not args.simulate
    return args


def validate_args(args):
    checks = (
        (args.samples > 0, "--samples must be > 0"),
        (args.point_failure_budget > 0, "--point-failure-budget must be > 0"),
        (args.spotread_timeout > 0, "--spotread-timeout must be > 0"),
        (args.commit_timeout > 0, "--commit-timeout must be > 0"),
        (args.settle_seconds >= 0 and args.sample_gap_seconds >= 0,
         "settle and sample gap must be >= 0"),
        (args.min_sample_nits > 0, "--min-sample-nits must be > 0"),
        (1 <= args.frac_bits <= 15, "--frac-bits must be in [1, 15]"),
    )
    ok = True
    for passed, message in checks:
        if not passed:
            print(f"error: {message}", file=sys.stderr)
            ok = False
    return ok


def main():
    install_signal_handlers()
    args = parse_args()
    if not validate_args(args):
        return 2

    display = DispTesterClient(args.disp_host, args.disp_port, args.disp_timeout)
    control = ControlPipe()
    unity = 1 << args.frac_bits

    panel_sim = None
    if args.backend == "simulate":
        backend = SimulatedWpAdjust(frac_bits=args.frac_bits)
        panel_sim = SimulatedPanel(backend)
        meter = panel_sim
        print("backend: simulate (simulated wp_adjust + panel/sensor)",
              file=sys.stderr)
    else:
        try:
            backend = I2CDevWpAdjust(args.i2c_dev, args.i2c_addr, args.wp_page)
        except OSError as e:
            print(f"error: cannot open {args.i2c_dev}: {e}", file=sys.stderr)
            return 5
        meter = SpotreadMeter(args)
        print(f"backend: i2cdev {args.i2c_dev} addr=0x{args.i2c_addr:02X} "
              f"page=0x{args.wp_page:02X}", file=sys.stderr)

    regs = WpAdjustRegisters(backend)
    points = build_profile_points(args, unity)
    recorder = None
    completed_points = 0
    aborted_reason = ""
    exit_code = 0

    try:
        display.command("pattern white", required=not args.simulate)
        # Unlike the legacy profiler, do NOT send "set-user-interaction
        # disable" here: the QML gates the overlay and the child-action
        # Start/Stop buttons on userInteractionEnabled, so disabling it hides
        # the buttons this flow depends on. Pattern locking is already
        # handled by the parent's --disable-pattern-navigation flag.
        display.best_effort("set-metadata-status enable")
        display.best_effort(f"set-metadata-fontsize {args.progress_fontsize}")
        display.best_effort(f"set-metadata-align {args.metadata_align}")
        display.best_effort(f"set-metadata-color {args.normal_color}")
        display.set_overlay_text(args.initial_text)

        if not require_colorimeter(display, args):
            aborted_reason = "i1 Display Pro USB colorimeter not found"
            exit_code = 3
            raise RuntimeError(aborted_reason)

        version, frac_bits = regs.probe(expected_frac_bits=args.frac_bits)
        print(f"wp_adjust probe ok: VERSION=0x{version:04X} FRAC_BITS={frac_bits}",
              file=sys.stderr)

        print("restoring wp_adjust pass-through defaults before profiling",
              file=sys.stderr)
        regs.defaults()

        if not wait_for_start(display, control, args, args.ready_text):
            aborted_reason = "start interrupted"
            raise RuntimeError(aborted_reason)

        recorder = ProfileRecorder(args, points, version)
        recorder.open()
        print(f"profile points: {len(points)}", file=sys.stderr)
        print(f"outputs: {recorder.json_path}", file=sys.stderr)

        if args.measure_primaries:
            primaries = measure_primaries(display, meter, panel_sim, recorder,
                                          args, control)
            recorder.set_primaries(primaries)

        for point in points:
            if stop_requested():
                aborted_reason = f"signal {_stop_signal}"
                break
            gains = point["gains"]
            print(f"[{point['index']}/{len(points)}] {point['label']} "
                  f"R=0x{gains['r']:04X} G=0x{gains['g']:04X} B=0x{gains['b']:04X}",
                  file=sys.stderr)
            regs.write_gains_and_commit(gains, timeout_sec=args.commit_timeout,
                                        control=control)
            if not interruptible_sleep(args.settle_seconds, control):
                raise InterruptedError("stop requested")

            samples, failures, first_error = collect_samples(
                display, meter, recorder, point, len(points), args, control)
            average, stddev = summarize_samples(samples)
            recorder.add_point_result(
                point, samples, average, stddev,
                status="ok" if failures == 0 else "ok-with-retries",
                error="" if failures == 0 else first_error)
            print(f"    avg: Y={average['Y']:.2f} x={average['x']:.5f} "
                  f"y={average['y']:.5f} std_x={stddev['x']:.6f} "
                  f"std_y={stddev['y']:.6f}", file=sys.stderr)
            completed_points += 1

        if not aborted_reason:
            recorder.set_status("complete")

    except (InterruptedError, ProfileAborted) as e:
        aborted_reason = str(e) or f"signal {_stop_signal}"
    except Exception as e:
        aborted_reason = str(e)
        print(f"error: {e}", file=sys.stderr)
    finally:
        if args.restore_defaults:
            try:
                print("restoring wp_adjust pass-through defaults", file=sys.stderr)
                regs.defaults()
            except Exception as e:
                restore_error = f"defaults restore failed: {one_line(e)}"
                print(f"warning: {restore_error}", file=sys.stderr)
                if not aborted_reason:
                    aborted_reason = restore_error

        if recorder:
            if aborted_reason:
                recorder.set_status("aborted", aborted_reason)
            recorder.close()

        if not stop_requested():
            if aborted_reason:
                display.best_effort(f"set-metadata-color {args.alert_color}")
                display.set_overlay_text(args.abort_text.format(
                    points=completed_points, total=len(points),
                    reason=aborted_reason))
            else:
                display.best_effort(f"set-metadata-color {args.normal_color}")
                display.set_overlay_text(args.complete_text.format(
                    points=completed_points, total=len(points),
                    json_output=recorder.json_path if recorder else ""))

        display.set_child_action_active(False)

    if stop_requested():
        print("stopped by signal; partial profile preserved", file=sys.stderr)
        return signal_exit_code()

    if aborted_reason:
        print(f"profile aborted: {aborted_reason}", file=sys.stderr)
        return exit_code or 1

    print("wp_adjust white-point profile complete", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
