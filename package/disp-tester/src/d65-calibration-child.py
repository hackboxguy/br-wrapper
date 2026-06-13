#!/usr/bin/env python3
"""
d65-calibration-child.py

Child script for disp-tester that calibrates a single display's white point
toward an absolute target (default D65, x=0.3127 y=0.3290) using the new
`wp_adjust` FPGA block (Q4.12 RGB gains, fpga-wp-adjust scalar v1 register
map). This is the PRD section-11 calibration flow: measure white, measure the
display's R/G/B primaries, solve a headroom-preserving linear-light
correction, convert to code-domain gains with the display gamma, upload with
readback-verify and frame-boundary COMMIT, then iterate with damping until
the tolerance is met.

Unlike new-white-point-match-child.py (relative match to a second reference
display), this script needs only one display and one sensor placement.

The calibration result is written as a *wp-cal-v1 schema conformant* profile
(fpga-wp-adjust host/schema/wp-cal-v1.schema.json), so the fpga-wp-adjust
boot loader (host/wp_load.py) can replay it at boot once a hardware backend
exists. A separate session log (iterations, primaries, raw samples) is
written alongside for analysis.

Backends (--backend): see new-white-point-match-child.py. 'simulate' (the
default until the new RTL is integrated) runs the full flow against a
simulated panel; 'i2cdev' talks to the FPGA new slave over /dev/i2c-N.
"""

import argparse
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
DEFAULT_CALIBRATION_OUTPUT = "/home/pi/system-settings/wp-cal-d65.json"
DEFAULT_COLORIMETER_MATCH = [
    "i1display",
    "i1 display",
    "i1display pro",
    "i1 display pro",
]
DEFAULT_COLORIMETER_USB_IDS = [
    "0765:5020",
]

D65_XY = (0.3127, 0.3290)

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
    "D65 Calibration (wp_adjust)\n"
    "Starting..."
)
DEFAULT_SENSOR_NOT_FOUND_TEXT = (
    "D65 Calibration (wp_adjust)\n"
    "i1 Display Pro Not Found\n"
    "Connect USB colorimeter"
)
DEFAULT_READY_TEXT = (
    "Place the Sensor on the Display and Click Start button"
)
DEFAULT_SAMPLING_TEXT = (
    "D65 Calibration (wp_adjust)\n"
    "{stage}\n"
    "Sample {sample}/{samples}  Retry {failures}/{failure_budget}"
)
DEFAULT_MATCHING_TEXT = (
    "D65 Calibration (wp_adjust)\n"
    "Iteration {iteration}/{max_iterations}\n"
    "R=0x{r:04X} G=0x{g:04X} B=0x{b:04X}\n"
    "dx={dx:+.5f} dy={dy:+.5f}"
)
DEFAULT_COMPLETE_TEXT = (
    "D65 Calibration done!\n"
    "R=0x{r:04X} G=0x{g:04X} B=0x{b:04X}\n"
    "dx={dx:+.5f} dy={dy:+.5f} du'v'={duv:.5f}\n"
    "Press Start or EXIT"
)
DEFAULT_BEST_EFFORT_TEXT = (
    "D65 Calibration best effort\n"
    "R=0x{r:04X} G=0x{g:04X} B=0x{b:04X}\n"
    "dx={dx:+.5f} dy={dy:+.5f} du'v'={duv:.5f}\n"
    "{save_status}\n"
    "Press Start or EXIT"
)
DEFAULT_ABORT_TEXT = (
    "D65 Calibration Aborted\n"
    "{reason}\n"
    "Press Restart or EXIT"
)

_FLOAT_RE = r"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)"
_XYZ_RE = re.compile(rf"Result is XYZ:\s*{_FLOAT_RE}\s+{_FLOAT_RE}\s+{_FLOAT_RE}")
_YXY_RE = re.compile(rf"Yxy:\s*{_FLOAT_RE}\s+{_FLOAT_RE}\s+{_FLOAT_RE}")
SESSION_SCHEMA = "disp-tester-d65-calibration-v1"

_stop_requested = False
_stop_signal = None
_active_process = None


class CalibrationAborted(RuntimeError):
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

    def reset_for_run(self):
        self.abort_requested = False
        self.restart_requested = False

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
                raise CalibrationAborted("operator requested abort")
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


def now_utc_iso():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


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
# Color math (ported from fpga-wp-adjust host/wp_math.py, stdlib only).
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


def xy_to_uv(x, y):
    denom = -2.0 * x + 12.0 * y + 3.0
    return (4.0 * x / denom, 9.0 * y / denom)


def delta_uv(a_xy, b_xy):
    ua, va = xy_to_uv(a_xy[0], a_xy[1])
    ub, vb = xy_to_uv(b_xy[0], b_xy[1])
    return math.hypot(ua - ub, va - vb)


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


def solve_linear_gains(primaries, target_xy, target_Y=None):
    white_Y = sum(primaries[ch][1] for ch in GAIN_CHANNELS)
    if target_Y is None:
        target_Y = white_Y
    target = xyY_to_XYZ(target_xy[0], target_xy[1], target_Y)
    matrix = tuple(
        tuple(primaries[ch][row] for ch in GAIN_CHANNELS)
        for row in range(3)
    )
    solved = solve_3x3(matrix, target)
    return dict(zip(GAIN_CHANNELS, solved))


def normalize_to_headroom(gains, max_gain=1.0):
    largest = max(gains.values())
    if largest <= 0.0:
        raise ValueError("at least one gain must be positive")
    scale = max_gain / largest if largest > max_gain else 1.0
    return {ch: value * scale for ch, value in gains.items()}


def linear_to_code_domain(gains, gamma):
    return {ch: max(0.0, value) ** (1.0 / gamma) for ch, value in gains.items()}


def clamp_code_gains(gains, min_gain, max_gain):
    clamped = {}
    was_clamped = False
    for ch, value in gains.items():
        bounded = min(max(value, min_gain), max_gain)
        if bounded != value:
            was_clamped = True
        clamped[ch] = bounded
    return clamped, was_clamped


def gain_to_fixed(gain, frac_bits):
    if gain < 0.0:
        raise ValueError("gain must be non-negative")
    raw = int(math.floor(gain * (1 << frac_bits) + 0.5))
    if raw > 0xFFFF:
        raise ValueError("gain does not fit in 16-bit fixed point")
    return raw


def fixed_to_gain(raw, frac_bits):
    return raw / float(1 << frac_bits)


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
        # Page-3 byte mapping: 2 bytes per logical register, big-endian
        # (byte_addr = logical << 1). See fpga-wp-adjust
        # docs/i2c-master-sw-integration-guideline.md section 1.2.
        os.write(self.fd, bytes([self.page, (addr << 1) & 0xFF]))
        data = os.read(self.fd, 2)
        if len(data) != 2:
            raise RuntimeError(f"short I2C read at page 0x{self.page:02X} reg 0x{addr:02X}")
        return (data[0] << 8) | data[1]

    def write16(self, addr, value):
        value &= 0xFFFF
        payload = bytes([self.page, (addr << 1) & 0xFF, (value >> 8) & 0xFF, value & 0xFF])
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

    def read_active_gains(self):
        return {ch: self.backend.read16(GAIN_ACTIVE_REGS[ch]) for ch in GAIN_CHANNELS}

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
        got_control = self.backend.read16(REG_CONTROL_SHADOW)
        if got_control != 0x0001:
            raise RuntimeError(
                f"shadow readback mismatch on CONTROL: read 0x{got_control:04X}; not committing")

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
                    raise CalibrationAborted("operator requested abort")
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
                    raise CalibrationAborted("operator requested abort")
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
        except (InterruptedError, CalibrationAborted):
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

    def __init__(self, registers_backend, gamma=2.184, noise_seed=20260614):
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
# Sampling.
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


def collect_average(display, meter, args, control, stage, min_nits):
    samples = []
    failures = 0
    first_error = ""

    while len(samples) < args.samples:
        sample_index = len(samples) + 1
        display.set_overlay_text(args.sampling_text.format(
            stage=stage, sample=sample_index, samples=args.samples,
            failures=failures, failure_budget=args.point_failure_budget,
        ))

        if args.require_colorimeter and not find_colorimeter(
            args.colorimeter_match, args.colorimeter_usb_id, args.usb_sysfs_root,
        ):
            measurement, error = None, "i1 Display Pro USB colorimeter disconnected"
        else:
            measurement, error = meter.measure(control)

        if not measurement_valid(measurement, min_nits):
            failures += 1
            first_error = error or f"invalid measurement below {min_nits:.1f} nits"
            print(f"    {stage} sample {sample_index}/{args.samples} failed "
                  f"({failures}/{args.point_failure_budget}): {first_error}",
                  file=sys.stderr)
            if failures >= args.point_failure_budget:
                raise RuntimeError(f"{stage} failed: {first_error}")
            interruptible_sleep(args.sample_failure_sleep, control)
            continue

        measurement["timestamp"] = now_iso()
        samples.append(measurement)
        print(f"    {stage} sample {sample_index}/{args.samples}: "
              f"Y={measurement['Y']:.2f} x={measurement['x']:.5f} y={measurement['y']:.5f}",
              file=sys.stderr)
        if sample_index < args.samples:
            interruptible_sleep(args.sample_gap_seconds, control)

    average = {}
    for key in ("X", "Y", "Z", "x", "y"):
        values = [s[key] for s in samples if isinstance(s.get(key), (int, float))]
        average[key] = sum(values) / len(values) if values else None
    return {"average": average, "samples": samples, "failures": failures}


def set_pattern(display, panel_sim, args, pattern, control):
    display.command(f"pattern {pattern}", required=not args.simulate)
    if panel_sim:
        panel_sim.pattern = pattern
    interruptible_sleep(args.settle_seconds, control)


def measure_primaries(display, meter, panel_sim, args, control):
    primaries = {}
    for pattern, ch in (("red", "r"), ("green", "g"), ("blue", "b")):
        set_pattern(display, panel_sim, args, pattern, control)
        result = collect_average(display, meter, args, control,
                                 f"Measuring {pattern} primary",
                                 args.min_primary_nits)
        avg = result["average"]
        if avg.get("X") is None or avg.get("Z") is None:
            primaries[ch] = xyY_to_XYZ(avg["x"], avg["y"], avg["Y"])
        else:
            primaries[ch] = (avg["X"], avg["Y"], avg["Z"])
    set_pattern(display, panel_sim, args, "white", control)
    return primaries


def generic_primaries_for_white(white_xy, white_Y):
    return SimulatedPanel._scale_primaries(
        {
            "r": (0.4360, 0.2225, 0.0139),
            "g": (0.3851, 0.7169, 0.0971),
            "b": (0.1431, 0.0606, 0.7139),
        },
        white_xy=white_xy,
        white_Y=white_Y,
    )


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


def write_json_atomic(path, payload):
    directory = os.path.dirname(os.path.abspath(path))
    if directory:
        os.makedirs(directory, exist_ok=True)
    tmp_path = path + ".tmp"
    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True)
        f.write("\n")
    os.replace(tmp_path, path)


def xy_error(target_xy, measured_xy):
    dx = measured_xy[0] - target_xy[0]
    dy = measured_xy[1] - target_xy[1]
    return dx, dy, math.hypot(dx, dy)


def session_output_path(calibration_output):
    base = calibration_output
    if base.endswith(".json"):
        base = base[:-5]
    return base + "-session.json"


def wp_cal_v1_profile(args, initial_avg, final_avg, best, converged, status):
    """Build a profile conformant to fpga-wp-adjust wp-cal-v1.schema.json
    (additionalProperties is false there, so only schema fields appear)."""

    unity = 1 << args.frac_bits
    return {
        "format_version": 1,
        "panel_id": args.panel_id,
        "panel_serial": args.panel_serial,
        "profile_name": f"{args.panel_id}-d65-calibration",
        "created_utc": now_utc_iso(),
        "target_white": {
            "name": args.target_name,
            "x": args.target_x,
            "y": args.target_y,
        },
        "calibration_condition": {
            "pattern": "100_percent_full_field_white",
            "brightness_percent": args.brightness_percent,
            "local_dimming": args.local_dimming,
            "vision_boost": args.vision_boost,
            "backlight_temp_c": None,
        },
        "fpga": {
            "register_map": "wp_adjust_scalar_v1",
            "gain_format": f"Q{16 - args.frac_bits}.{args.frac_bits}",
            "frac_bits": args.frac_bits,
            "unity_hex": f"0x{unity:04X}",
        },
        "gains": {ch: int(best["fixed"][ch]) for ch in GAIN_CHANNELS},
        "gain_metadata": {
            "assumed_gamma": args.gamma,
            "code_domain_float": {ch: best["code"][ch] for ch in GAIN_CHANNELS},
            "q_format_hex": {ch: f"0x{best['fixed'][ch]:04X}" for ch in GAIN_CHANNELS},
        },
        "offsets": {"enabled": False, "r": 0, "g": 0, "b": 0},
        "verification": {
            "initial_xyY": {"x": initial_avg["x"], "y": initial_avg["y"],
                            "Y": initial_avg["Y"]},
            "final_xyY": ({"x": final_avg["x"], "y": final_avg["y"],
                           "Y": final_avg["Y"]} if final_avg else None),
            "inside_report_card_tolerance": None,
            "xy_distance_to_d65": best["distance"],
            "converged": converged,
        },
        "notes": [
            "Generated by d65-calibration-child.py (wp_adjust flow).",
            f"Backend: {args.backend}; status: {status}.",
            "Loadable at boot by fpga-wp-adjust host/wp_load.py.",
        ],
    }


def run_calibration(display, regs, meter, panel_sim, control, args, version):
    control.reset_for_run()
    display.set_child_action_active(False)
    display.best_effort(f"set-metadata-color {args.normal_color}")
    target_xy = (args.target_x, args.target_y)

    print("restoring wp_adjust pass-through defaults before calibration",
          file=sys.stderr)
    regs.defaults()
    set_pattern(display, panel_sim, args, "white", control)

    if not wait_for_start(display, control, args, args.ready_text):
        raise CalibrationAborted("start interrupted")

    initial = collect_average(display, meter, args, control,
                              "Measuring display white", args.min_sample_nits)
    initial_avg = initial["average"]

    if args.measure_primaries:
        primaries = measure_primaries(display, meter, panel_sim, args, control)
    else:
        primaries = generic_primaries_for_white(
            (initial_avg["x"], initial_avg["y"]), initial_avg["Y"])

    dx, dy, distance = xy_error(target_xy, (initial_avg["x"], initial_avg["y"]))
    best = {
        "fixed": {ch: 1 << args.frac_bits for ch in GAIN_CHANNELS},
        "code": {ch: 1.0 for ch in GAIN_CHANNELS},
        "dx": dx, "dy": dy, "distance": distance,
        "delta_uv": delta_uv(target_xy, (initial_avg["x"], initial_avg["y"])),
    }
    best_avg = initial_avg
    print(f"initial error to {args.target_name}: dx={dx:+.6f} dy={dy:+.6f} "
          f"distance={distance:.6f} du'v'={best['delta_uv']:.6f}", file=sys.stderr)

    iterations = []
    status = "calibrated" if distance <= args.tolerance_xy else "not_calibrated"
    linear_gains = {ch: 1.0 for ch in GAIN_CHANNELS}
    clamped_any = False

    for iteration in range(1, max(1, args.max_corrections) + 1):
        if status == "calibrated":
            break

        scaled_primaries = {
            ch: tuple(component * linear_gains[ch] for component in primaries[ch])
            for ch in GAIN_CHANNELS
        }
        correction = solve_linear_gains(scaled_primaries, target_xy)
        correction = {ch: max(1e-6, value) ** args.damping
                      for ch, value in correction.items()}
        linear_gains = normalize_to_headroom(
            {ch: linear_gains[ch] * correction[ch] for ch in GAIN_CHANNELS})

        code_gains = linear_to_code_domain(linear_gains, args.gamma)
        code_gains, clamped = clamp_code_gains(code_gains, args.min_gain, args.max_gain)
        clamped_any = clamped_any or clamped
        fixed = {ch: gain_to_fixed(code_gains[ch], args.frac_bits)
                 for ch in GAIN_CHANNELS}

        display.set_overlay_text(args.matching_text.format(
            iteration=iteration, max_iterations=args.max_corrections,
            r=fixed["r"], g=fixed["g"], b=fixed["b"], dx=dx, dy=dy))
        print(f"iteration {iteration}: writing R=0x{fixed['r']:04X} "
              f"G=0x{fixed['g']:04X} B=0x{fixed['b']:04X}"
              f"{' (clamped)' if clamped else ''}", file=sys.stderr)

        regs.write_gains_and_commit(fixed, timeout_sec=args.commit_timeout,
                                    control=control)
        interruptible_sleep(args.settle_seconds, control)

        measured = collect_average(display, meter, args, control,
                                   f"Measuring candidate {iteration}",
                                   args.min_sample_nits)
        meas_avg = measured["average"]
        meas_xy = (meas_avg["x"], meas_avg["y"])
        dx, dy, distance = xy_error(target_xy, meas_xy)
        duv = delta_uv(target_xy, meas_xy)

        active = regs.read_active_gains()
        linear_gains = {
            ch: fixed_to_gain(active[ch], args.frac_bits) ** args.gamma
            for ch in GAIN_CHANNELS
        }

        iterations.append({
            "iteration": iteration,
            "fixed_gains": {ch: fixed[ch] for ch in GAIN_CHANNELS},
            "code_gains": code_gains,
            "clamped": clamped,
            "measured": measured,
            "dx": dx, "dy": dy, "distance": distance, "delta_uv": duv,
        })
        print(f"iteration {iteration}: measured dx={dx:+.6f} dy={dy:+.6f} "
              f"distance={distance:.6f} du'v'={duv:.6f}", file=sys.stderr)

        if distance < best["distance"]:
            best = {"fixed": dict(fixed), "code": dict(code_gains),
                    "dx": dx, "dy": dy, "distance": distance, "delta_uv": duv}
            best_avg = meas_avg

        if distance <= args.tolerance_xy:
            status = "calibrated"
            break

    if status != "calibrated" and iterations:
        if all(best["fixed"][ch] == 1 << args.frac_bits for ch in GAIN_CHANNELS):
            regs.defaults()
        else:
            regs.write_gains_and_commit(best["fixed"],
                                        timeout_sec=args.commit_timeout,
                                        control=control)

    converged = status == "calibrated"
    should_write = converged or args.write_best_effort
    payload_status = status if converged else (
        "best_effort" if should_write else status)

    profile = wp_cal_v1_profile(args, initial_avg,
                                best_avg if iterations or converged else None,
                                best, converged, payload_status)
    session = {
        "schema": SESSION_SCHEMA,
        "created_at": now_iso(),
        "status": payload_status,
        "backend": args.backend,
        "fpga_version": f"0x{version:04X}",
        "target": {"name": args.target_name, "x": args.target_x, "y": args.target_y},
        "tolerance_xy": args.tolerance_xy,
        "gamma": args.gamma,
        "gain_clamped": clamped_any,
        "initial": initial,
        "target_primaries": {ch: list(primaries[ch]) for ch in GAIN_CHANNELS},
        "iterations": iterations,
        "final": {
            "gains_hex": {ch: f"0x{best['fixed'][ch]:04X}" for ch in GAIN_CHANNELS},
            "dx": best["dx"], "dy": best["dy"],
            "distance_xy": best["distance"], "delta_uv": best["delta_uv"],
        },
        "calibration_output": args.calibration_output,
    }

    if should_write:
        write_json_atomic(args.calibration_output, profile)
        print(f"wrote calibration profile: {args.calibration_output}", file=sys.stderr)
        session_path = session_output_path(args.calibration_output)
        write_json_atomic(session_path, session)
        print(f"wrote session log: {session_path}", file=sys.stderr)

    display.set_child_action_active(False)
    text_args = dict(r=best["fixed"]["r"], g=best["fixed"]["g"], b=best["fixed"]["b"],
                     dx=best["dx"], dy=best["dy"], duv=best["delta_uv"])
    if converged:
        display.best_effort(f"set-metadata-color {args.normal_color}")
        display.set_overlay_text(args.complete_text.format(**text_args))
    else:
        display.best_effort(f"set-metadata-color {args.alert_color}")
        display.set_overlay_text(args.best_effort_text.format(
            save_status="Calibration saved" if should_write else "Calibration not written",
            **text_args))

    return converged


def idle_until_restart(control):
    control.abort_requested = False
    control.restart_requested = False
    while not stop_requested():
        control.poll()
        if control.restart_requested:
            return True
        time.sleep(0.2)
    return False


def setup_display(display, args):
    display.command("pattern white", required=not args.simulate)
    display.best_effort("set-metadata-status enable")
    display.best_effort(f"set-metadata-fontsize {args.progress_fontsize}")
    display.best_effort(f"set-metadata-align {args.metadata_align}")
    display.best_effort(f"set-metadata-color {args.normal_color}")
    display.set_overlay_text(args.initial_text)


def parse_int_auto(value):
    try:
        return int(str(value), 0)
    except ValueError as e:
        raise argparse.ArgumentTypeError(str(e))


def parse_args():
    parser = argparse.ArgumentParser(
        description="Calibrate display white point toward D65 via wp_adjust.")
    parser.add_argument("--disp-host", default=DEFAULT_DISP_HOST)
    parser.add_argument("--disp-port", type=int, default=DEFAULT_DISP_PORT)
    parser.add_argument("--disp-timeout", type=float, default=1.0)

    parser.add_argument("--backend", choices=("simulate", "i2cdev"),
                        default="simulate")
    parser.add_argument("--i2c-dev", default="/dev/i2c-1")
    parser.add_argument("--i2c-addr", type=parse_int_auto, default=0x1E)
    parser.add_argument("--wp-page", type=parse_int_auto, default=0x03)
    parser.add_argument("--frac-bits", type=int, default=12, dest="frac_bits")

    parser.add_argument("--target-name", default="D65")
    parser.add_argument("--target-x", type=float, default=D65_XY[0])
    parser.add_argument("--target-y", type=float, default=D65_XY[1])
    parser.add_argument("--tolerance-xy", type=float, default=0.005,
                        help="PRD-preferred absolute target: xy distance to "
                             "D65 below 0.005.")
    parser.add_argument("--panel-id", default="12-3-nq1v1")
    parser.add_argument("--panel-serial", default="unknown-or-real-serial")
    parser.add_argument("--brightness-percent", type=float, default=100.0)
    parser.add_argument("--local-dimming", default="enabled")
    parser.add_argument("--vision-boost", default="not_present")

    parser.add_argument("--calibration-output", default=DEFAULT_CALIBRATION_OUTPUT,
                        help="wp-cal-v1 schema profile output; a -session.json "
                             "log is written alongside.")
    parser.add_argument("--write-best-effort", action=argparse.BooleanOptionalAction,
                        default=True)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--settle-seconds", type=float, default=1.5)
    parser.add_argument("--sample-gap-seconds", type=float, default=0.2)
    parser.add_argument("--point-failure-budget", type=int, default=8)
    parser.add_argument("--sample-failure-sleep", type=float, default=2.0)
    parser.add_argument("--spotread-timeout", type=float, default=15.0)
    parser.add_argument("--min-sample-nits", type=float, default=5.0)
    parser.add_argument("--min-primary-nits", type=float, default=1.0)
    parser.add_argument("--max-corrections", type=int, default=5)
    parser.add_argument("--damping", type=float, default=0.7)
    parser.add_argument("--gamma", type=float, default=2.184)
    parser.add_argument("--min-gain", type=float, default=0.5)
    parser.add_argument("--max-gain", type=float, default=1.0)
    parser.add_argument("--commit-timeout", type=float, default=2.0)
    parser.add_argument("--measure-primaries", action=argparse.BooleanOptionalAction,
                        default=True)
    parser.add_argument("--auto-start", action="store_true")
    parser.add_argument("--exit-after-match", action="store_true")

    parser.add_argument("--initial-text", default=DEFAULT_INITIAL_TEXT)
    parser.add_argument("--sensor-not-found-text", default=DEFAULT_SENSOR_NOT_FOUND_TEXT)
    parser.add_argument("--ready-text", default=DEFAULT_READY_TEXT)
    parser.add_argument("--sampling-text", default=DEFAULT_SAMPLING_TEXT)
    parser.add_argument("--matching-text", default=DEFAULT_MATCHING_TEXT)
    parser.add_argument("--complete-text", default=DEFAULT_COMPLETE_TEXT)
    parser.add_argument("--best-effort-text", default=DEFAULT_BEST_EFFORT_TEXT)
    parser.add_argument("--abort-text", default=DEFAULT_ABORT_TEXT)
    parser.add_argument("--progress-fontsize", type=int, default=24)
    parser.add_argument("--metadata-align", default="left")
    parser.add_argument("--normal-color", default="white")
    parser.add_argument("--alert-color", default="red")
    parser.add_argument("--require-colorimeter", action=argparse.BooleanOptionalAction,
                        default=None)
    parser.add_argument("--colorimeter-match", action="append",
                        default=list(DEFAULT_COLORIMETER_MATCH))
    parser.add_argument("--colorimeter-usb-id", action="append",
                        default=list(DEFAULT_COLORIMETER_USB_IDS))
    parser.add_argument("--usb-sysfs-root", default="/sys/bus/usb/devices")

    args = parser.parse_args()
    args.simulate = args.backend == "simulate"
    if args.require_colorimeter is None:
        args.require_colorimeter = not args.simulate
    return args


def validate_args(args):
    checks = (
        (args.samples > 0, "--samples must be > 0"),
        (args.point_failure_budget > 0, "--point-failure-budget must be > 0"),
        (args.tolerance_xy > 0, "--tolerance-xy must be > 0"),
        (args.max_corrections > 0, "--max-corrections must be > 0"),
        (0.0 < args.damping <= 1.0, "--damping must be in (0, 1]"),
        (args.gamma > 0, "--gamma must be > 0"),
        (0.0 <= args.min_gain <= args.max_gain, "invalid gain limits"),
        (0.0 < args.target_y <= 1.0 and 0.0 <= args.target_x <= 1.0 and
         args.target_x + args.target_y <= 1.0, "invalid target chromaticity"),
        (args.spotread_timeout > 0, "--spotread-timeout must be > 0"),
        (args.commit_timeout > 0, "--commit-timeout must be > 0"),
        (1 <= args.frac_bits <= 15, "--frac-bits must be in [1, 15]"),
        (os.path.isabs(args.calibration_output),
         "--calibration-output must be an absolute path"),
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

    panel_sim = None
    if args.backend == "simulate":
        backend = SimulatedWpAdjust(frac_bits=args.frac_bits)
        panel_sim = SimulatedPanel(backend, gamma=args.gamma)
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

    try:
        setup_display(display, args)
        if not require_colorimeter(display, args):
            return 3

        try:
            version, frac_bits = regs.probe(expected_frac_bits=args.frac_bits)
        except RuntimeError as e:
            print(f"error: wp_adjust probe failed: {e}", file=sys.stderr)
            display.best_effort(f"set-metadata-color {args.alert_color}")
            display.set_overlay_text(args.abort_text.format(reason=one_line(e)))
            return 4
        print(f"wp_adjust probe ok: VERSION=0x{version:04X} FRAC_BITS={frac_bits}",
              file=sys.stderr)

        while not stop_requested():
            try:
                run_calibration(display, regs, meter, panel_sim, control, args,
                                version)
            except CalibrationAborted as e:
                print(f"calibration aborted: {e}", file=sys.stderr)
                try:
                    regs.defaults()
                except Exception as defaults_error:
                    print(f"warning: defaults after abort failed: {defaults_error}",
                          file=sys.stderr)
                display.set_child_action_active(False)
                display.best_effort(f"set-metadata-color {args.alert_color}")
                display.set_overlay_text(args.abort_text.format(reason=str(e)))
            except InterruptedError:
                raise
            except Exception as e:
                print(f"calibration failed: {e}", file=sys.stderr)
                display.set_child_action_active(False)
                display.best_effort(f"set-metadata-color {args.alert_color}")
                display.set_overlay_text(args.abort_text.format(reason=one_line(e)))

            if args.exit_after_match:
                break
            if not idle_until_restart(control):
                break
    except InterruptedError:
        print("stopped by signal", file=sys.stderr)
        return signal_exit_code()
    finally:
        display.set_child_action_active(False)

    return 0


if __name__ == "__main__":
    sys.exit(main())
