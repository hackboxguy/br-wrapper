#!/usr/bin/env python3
"""
als-dimmer-sweep-child.py

Child script for disp-tester. It assumes disp-tester has already been started
by qt-demo-launcher and uses the parent disp-tester socket to show a white
pattern plus progress overlay while sweeping als-dimmer brightness and reading
spotread luminance values.

The script intentionally does not start or stop qt-demo-launcher/disp-tester.
On normal completion it leaves disp-tester running with a completion overlay.
On SIGTERM/SIGINT it restores the original als-dimmer mode/brightness as
quickly as possible and exits, allowing disp-tester to finish shutting down.
"""

import argparse
import csv
import ctypes
import datetime
import fcntl
import json
import math
import os
import re
import signal
import socket
import subprocess
import sys
import time


DEFAULT_ALS_SOCKET = "/tmp/als-dimmer.sock"
DEFAULT_DISP_HOST = os.environ.get("DISP_TESTER_HOST", "127.0.0.1")
DEFAULT_DISP_PORT = int(os.environ.get("DISP_TESTER_PORT", "8082"))
DEFAULT_ALS_CONFIG = "/home/pi/als-dimmer/etc/als-dimmer/config.json"
DEFAULT_CALIBRATION_DIR = "/home/pi/als-dimmer/etc/als-dimmer/calibrations"
DEFAULT_CALIBRATION_FALLBACK = "dimmer_neutral.csv"
DEFAULT_RESTART_COMMAND = "systemctl restart als-dimmer"
DEFAULT_I2C_TEMP_BUS = "/dev/i2c-1"
DEFAULT_I2C_TEMP_ADDRESS = 0x66
DEFAULT_I2C_TEMP_REGISTER = 0x1002
CONFIG_CALIBRATION_MAP = {
    "config_fpga_opti4001_dimmer2048.json": "dimmer_neutral.csv",
    "config_opti4001_boepwm.json": "boe_pwm_2khz_reference.csv",
    "config_opti4001_ddcutil_rtk_hg560t34.json": "dimmer_15_6_rtk_hg560t34.csv",
}
DEFAULT_PROGRESS_TEXT = (
    "Brightness Calibration in Progress\\n"
    "Step: {step}/{total}\\n"
    "Brightness: {brightness_pct}%"
)
DEFAULT_COMPLETE_TEXT = (
    "Brightness Calibration Complete\\n"
    "Rows: {rows}/{total}\\n"
    "Output: {output}"
)
DEFAULT_CALIBRATION_SUCCESS_TEXT = (
    "Brightness Calibration Success\\n"
    "Calibration Applied\\n"
    "als-dimmer restarted"
)
DEFAULT_ABORT_TEXT = (
    "Brightness Calibration Aborted\\n"
    "Rows: {rows}/{total}\\n"
    "Reason: {reason}"
)
DEFAULT_SENSOR_NOT_FOUND_TEXT = (
    "i1 Display Pro Not Found\\n"
    "Connect USB colorimeter\\n"
    "Sweep not started"
)
DEFAULT_SENSOR_DISCONNECTED_TEXT = (
    "i1 Display Pro Disconnected\\n"
    "Reconnect USB colorimeter\\n"
    "Sweep aborted"
)
DEFAULT_PLACEMENT_TEXT = (
    "Place i1 Display Pro\\n"
    "At center of white screen\\n"
    "Current: {nits_text} nits"
)
DEFAULT_PLACEMENT_SUCCESS_TEXT = (
    "Colorimeter Placement OK\\n"
    "Initial: {nits:.1f} nits\\n"
    "Starting sweep"
)
DEFAULT_PLACEMENT_FAILED_TEXT = (
    "Colorimeter Placement Failed\\n"
    "Reading below {min_nits:.0f} nits\\n"
    "Sweep not started"
)
DEFAULT_COLORIMETER_MATCH = [
    "i1display",
    "i1 display",
    "i1display pro",
    "i1 display pro",
]
DEFAULT_COLORIMETER_USB_IDS = [
    "0765:5020",
]
CSV_HEADER = ["brightness_pct", "nits", "status", "backlight_temp_c", "retries"]

I2C_RDWR = 0x0707
I2C_M_RD = 0x0001

_YXY_RE = re.compile(r"Result is XYZ:.*Yxy:\s*([0-9.]+)")

_stop_requested = False
_stop_signal = None
_active_process = None


class ColorimeterDisconnectedError(RuntimeError):
    pass


class I2cMsg(ctypes.Structure):
    _fields_ = [
        ("addr", ctypes.c_uint16),
        ("flags", ctypes.c_uint16),
        ("len", ctypes.c_uint16),
        ("buf", ctypes.POINTER(ctypes.c_uint8)),
    ]


class I2cRdwrIoctlData(ctypes.Structure):
    _fields_ = [
        ("msgs", ctypes.POINTER(I2cMsg)),
        ("nmsgs", ctypes.c_uint32),
    ]


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


def interruptible_sleep(seconds):
    end = time.monotonic() + max(0.0, seconds)
    while not stop_requested():
        remaining = end - time.monotonic()
        if remaining <= 0:
            return True
        time.sleep(min(0.1, remaining))
    return False


class JsonLineClient:
    def __init__(self, socket_path=None, host="127.0.0.1", port=9000, timeout=2.0):
        if socket_path:
            self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self._sock.settimeout(timeout)
            self._sock.connect(socket_path)
        else:
            self._sock = socket.create_connection((host, port), timeout=timeout)
        self._sock.settimeout(timeout)
        self._buffer = b""

    def call(self, command, **params):
        if stop_requested():
            raise InterruptedError("stop requested")

        msg = {"command": command}
        if params:
            msg["params"] = params

        self._sock.sendall((json.dumps(msg) + "\n").encode("utf-8"))
        while b"\n" not in self._buffer:
            chunk = self._sock.recv(4096)
            if not chunk:
                raise RuntimeError("als-dimmer closed connection")
            self._buffer += chunk

        line, self._buffer = self._buffer.split(b"\n", 1)
        return json.loads(line.decode("utf-8"))

    def close(self):
        try:
            self._sock.close()
        except OSError:
            pass


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
            with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
                sock.settimeout(self.timeout)
                sock.sendall((command + "\n").encode("utf-8"))
                response = self._recv_line(sock)
        except OSError as e:
            if required:
                raise RuntimeError(f"disp-tester command failed: {command}: {e}")
            print(f"warning: disp-tester command failed: {command}: {e}", file=sys.stderr)
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
        if message is None:
            return
        safe_message = str(message).replace("\n", "\\n")
        self.best_effort(f"set-metadata-text {safe_message}")

    def _recv_line(self, sock):
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(1024)
            if not chunk:
                break
            data += chunk
        return data.decode("utf-8", errors="replace").strip()


class CsvRecorder:
    def __init__(self, path, args, output_type, total_steps):
        self.path = path
        self.args = args
        self.output_type = output_type
        self.total_steps = total_steps
        self.file = None
        self.writer = None
        self.rows = 0

    def __enter__(self):
        directory = os.path.dirname(os.path.abspath(self.path))
        if directory:
            os.makedirs(directory, exist_ok=True)
        self.file = open(self.path, "w", newline="")
        self.writer = csv.writer(self.file)
        self._write_header()
        self.flush()
        return self

    def __exit__(self, _exc_type, _exc, _tb):
        self.close()

    def _write_header(self):
        self.file.write("# als-dimmer brightness-to-nits sweep\n")
        self.file.write("# script=als-dimmer-sweep-child.py\n")
        if self.args.label:
            self.file.write(f"# label={self.args.label}\n")
        self.file.write(f"# output_type={self.output_type}\n")
        self.file.write(f"# timestamp={datetime.datetime.now().astimezone().isoformat()}\n")
        self.file.write(
            f"# start={self.args.start} end={self.args.end} step={self.args.step} "
            f"settle_seconds={self.args.settle_seconds} max_retries={self.args.max_retries}\n"
        )
        self.file.write(f"# planned_steps={self.total_steps}\n")
        if self.args.temp_cmd:
            self.file.write(f"# temp_source_cmd={one_line(self.args.temp_cmd, 200)}\n")
        elif self.args.temp_source_description:
            self.file.write(
                f"# temp_source={one_line(self.args.temp_source_description, 200)}\n"
            )
        self.writer.writerow(CSV_HEADER)

    def write_row(self, pct, nits, status, temp, retries):
        nits_str = f"{nits:.4f}" if nits is not None else ""
        temp_str = f"{temp:.2f}" if temp is not None else ""
        self.writer.writerow([pct, nits_str, status, temp_str, retries])
        self.rows += 1
        self.flush()

    def write_comment(self, key, value):
        if self.file:
            self.file.write(f"# {key}={one_line(str(value), 200)}\n")
            self.flush()

    def flush(self):
        if self.file:
            self.file.flush()

    def close(self):
        if self.file:
            self.flush()
            self.file.close()
            self.file = None


def one_line(text, max_len):
    collapsed = " ".join(str(text).split())
    if len(collapsed) > max_len:
        return collapsed[:max_len - 3] + "..."
    return collapsed


def default_output_path():
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    return f"/tmp/als-dimmer-sweep-{ts}.csv"


def validate_output_path(path):
    try:
        directory = os.path.dirname(os.path.abspath(path))
        if directory:
            os.makedirs(directory, exist_ok=True)
        with open(path, "a"):
            pass
        return True
    except OSError as e:
        print(f"error: cannot write output CSV {path}: {e}", file=sys.stderr)
        return False


def read_text_file(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read().strip()
    except OSError:
        return ""


def normalize_usb_id(value):
    return value.lower().replace(":", "").replace("-", "").strip()


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


def find_configured_colorimeter(args):
    return find_colorimeter(args.colorimeter_match, args.colorimeter_usb_id,
                            args.usb_sysfs_root)


def show_status_message(display, args, message):
    display.best_effort("set-metadata-status enable")
    display.best_effort(f"set-metadata-fontsize {args.progress_fontsize}")
    display.set_overlay_text(message)


def check_colorimeter_or_report(display, args):
    if not args.require_colorimeter:
        return True

    device = find_configured_colorimeter(args)
    if device:
        print(f"colorimeter found: {device['usb_id']} {device['text']}", file=sys.stderr)
        return True

    print("error: i1 Display Pro USB colorimeter not found", file=sys.stderr)
    show_status_message(display, args, args.sensor_not_found_text)
    return False


def check_colorimeter_still_connected(display, args):
    if not args.require_colorimeter or not args.check_colorimeter_during_sweep:
        return True

    if find_configured_colorimeter(args):
        return True

    print("error: i1 Display Pro USB colorimeter disconnected", file=sys.stderr)
    show_status_message(display, args, args.sensor_disconnected_text)
    return False


def require_colorimeter_still_connected(display, args):
    if not check_colorimeter_still_connected(display, args):
        raise ColorimeterDisconnectedError(
            "i1 Display Pro USB colorimeter disconnected"
        )


def build_step_list(start, end, step):
    if start == end:
        return [start]
    if start > end:
        steps = list(range(start, end - 1, -abs(step)))
        if steps[-1] != end:
            steps.append(end)
    else:
        steps = list(range(start, end + 1, abs(step)))
        if steps[-1] != end:
            steps.append(end)
    return steps


def parse_y_from_spotread(text):
    match = _YXY_RE.search(text)
    return float(match.group(1)) if match else None


def run_process_capture(argv, timeout, shell=False):
    global _active_process

    if stop_requested():
        raise InterruptedError("stop requested")

    proc = subprocess.Popen(
        argv,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        shell=shell,
        text=True,
        errors="replace",
    )
    _active_process = proc
    start = time.monotonic()

    try:
        while proc.poll() is None:
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


def run_admin_command(argv, timeout, use_sudo=True, shell=False):
    if use_sudo and hasattr(os, "geteuid") and os.geteuid() != 0:
        if shell:
            argv = ["sudo", "-n", "sh", "-c", argv]
            shell = False
        else:
            argv = ["sudo", "-n"] + argv
    return run_process_capture(argv, timeout, shell=shell)


def measure_nits(max_retries, retry_sleep_s, spotread_timeout, before_attempt=None):
    retries = 0
    for attempt in range(max_retries + 1):
        if before_attempt:
            before_attempt()

        try:
            out = run_process_capture(["spotread", "-x", "-O"], spotread_timeout)
        except InterruptedError:
            raise
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired,
                FileNotFoundError) as e:
            out = getattr(e, "output", None) or str(e)

        nits = parse_y_from_spotread(out)
        if nits is not None:
            return nits, retries

        retries += 1
        if attempt < max_retries and not stop_requested():
            print(f"    retry {retries}/{max_retries} (spotread parse failed)",
                  file=sys.stderr)
            interruptible_sleep(retry_sleep_s)
    return None, retries


def read_temp(temp_cmd):
    if not temp_cmd or stop_requested():
        return None
    try:
        out = run_process_capture(temp_cmd, timeout=5, shell=True).strip()
    except (InterruptedError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired, FileNotFoundError):
        return None
    match = re.search(r"-?\d+(\.\d+)?", out)
    return float(match.group(0)) if match else None


def parse_int_auto(value):
    try:
        return int(str(value), 0)
    except ValueError as e:
        raise argparse.ArgumentTypeError(str(e))


def i2c_read_register(bus_path, address, register, length):
    write_buf = (ctypes.c_uint8 * 2)(
        (register >> 8) & 0xFF,
        register & 0xFF,
    )
    read_buf = (ctypes.c_uint8 * length)()
    messages = (I2cMsg * 2)(
        I2cMsg(
            ctypes.c_uint16(address),
            ctypes.c_uint16(0),
            ctypes.c_uint16(len(write_buf)),
            ctypes.cast(write_buf, ctypes.POINTER(ctypes.c_uint8)),
        ),
        I2cMsg(
            ctypes.c_uint16(address),
            ctypes.c_uint16(I2C_M_RD),
            ctypes.c_uint16(length),
            ctypes.cast(read_buf, ctypes.POINTER(ctypes.c_uint8)),
        ),
    )
    ioctl_data = I2cRdwrIoctlData(
        ctypes.cast(messages, ctypes.POINTER(I2cMsg)),
        ctypes.c_uint32(len(messages)),
    )

    with open(bus_path, "rb+", buffering=0) as bus:
        fcntl.ioctl(bus.fileno(), I2C_RDWR, ioctl_data)

    return bytes(read_buf)


def read_i2c_backlight_temp(args):
    if stop_requested():
        return None
    raw = i2c_read_register(
        args.i2c_temp_bus,
        args.i2c_temp_address,
        args.i2c_temp_register,
        2,
    )
    value = int.from_bytes(raw, byteorder="big", signed=True)
    temp = value / 10.0
    if not math.isfinite(temp):
        raise ValueError("temperature is not finite")
    return temp


def setup_temperature_sampler(args):
    args.temp_source_description = ""
    if args.temp_cmd is not None:
        return lambda: read_temp(args.temp_cmd)
    if not args.i2c_temp:
        return lambda: None

    source = (
        f"i2c:{args.i2c_temp_bus}@0x{args.i2c_temp_address:02X}"
        f"/0x{args.i2c_temp_register:04X}"
    )
    try:
        temp = read_i2c_backlight_temp(args)
    except (OSError, ValueError) as e:
        print(f"warning: backlight temperature sensor unavailable ({source}): {e}",
              file=sys.stderr)
        return lambda: None

    args.temp_source_description = source
    print(f"backlight temperature sensor found: {source} ({temp:.1f}C)",
          file=sys.stderr)

    warned = False

    def sample():
        nonlocal warned
        try:
            return read_i2c_backlight_temp(args)
        except (OSError, ValueError) as e:
            if not warned:
                print(f"warning: backlight temperature read failed: {e}",
                      file=sys.stderr)
                warned = True
            return None

    return sample


def response_ok(response):
    return isinstance(response, dict) and response.get("status") == "success"


def connect_als(args):
    socket_path = args.als_socket
    if socket_path is None and os.path.exists(DEFAULT_ALS_SOCKET):
        socket_path = DEFAULT_ALS_SOCKET

    target = f"socket={socket_path}" if socket_path else f"tcp={args.als_host}:{args.als_port}"
    print(f"connecting to als-dimmer ({target})...", file=sys.stderr)
    return JsonLineClient(socket_path, args.als_host, args.als_port, args.als_timeout)


def restore_als(client, original_mode, original_brightness, no_restore):
    if no_restore or client is None:
        return
    if original_mode is None:
        return
    try:
        if original_mode == "manual":
            client.call("set_brightness", brightness=original_brightness)
        else:
            client.call("set_mode", mode="auto")
    except Exception as e:
        print(f"warning: failed to restore als-dimmer state: {e}", file=sys.stderr)


def resolve_calibration_target(args):
    if args.calibration_target:
        if os.path.isabs(args.calibration_target):
            return args.calibration_target, "explicit"
        return os.path.join(args.calibration_dir, args.calibration_target), "explicit"

    if os.path.exists(args.calibration_config) or os.path.islink(args.calibration_config):
        resolved = os.path.realpath(args.calibration_config)
        config_name = os.path.basename(resolved)
    else:
        resolved = args.calibration_config
        config_name = os.path.basename(args.calibration_config)

    filename = CONFIG_CALIBRATION_MAP.get(config_name, args.calibration_fallback)
    return os.path.join(args.calibration_dir, filename), resolved


def install_calibration(args):
    target, source_config = resolve_calibration_target(args)
    target_dir = os.path.dirname(os.path.abspath(target))
    use_sudo = not args.no_calibration_sudo

    print(f"installing calibration: {args.output} -> {target}", file=sys.stderr)
    print(f"selected from config: {source_config}", file=sys.stderr)

    try:
        run_admin_command(["mkdir", "-p", target_dir], timeout=5, use_sudo=use_sudo)
        run_admin_command(["cp", "-f", args.output, target], timeout=10, use_sudo=use_sudo)
        if args.restart_command:
            print(f"restarting als-dimmer: {args.restart_command}", file=sys.stderr)
            run_admin_command(args.restart_command, timeout=15, use_sudo=use_sudo, shell=True)
    except InterruptedError:
        raise
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired,
            FileNotFoundError, OSError) as e:
        detail = getattr(e, "output", None) or str(e)
        return False, target, one_line(detail, 160)

    return True, target, "installed"


def validate_sweep_csv(path, expected_steps, args):
    try:
        if not os.path.exists(path):
            return False, "output CSV missing"
        if os.path.getsize(path) == 0:
            return False, "output CSV empty"

        with open(path, "r", newline="", encoding="utf-8", errors="replace") as f:
            data_lines = (
                line for line in f
                if line.strip() and not line.lstrip().startswith("#")
            )
            reader = csv.reader(data_lines)
            header = next(reader, None)
            rows = list(reader)
    except OSError as e:
        return False, f"cannot read output CSV: {one_line(str(e), 120)}"
    except csv.Error as e:
        return False, f"cannot parse output CSV: {one_line(str(e), 120)}"

    if header != CSV_HEADER:
        return False, "unexpected CSV header"
    if len(rows) != len(expected_steps):
        return False, f"expected {len(expected_steps)} rows, found {len(rows)}"

    expected = [int(step) for step in expected_steps]
    observed = []
    nits_by_pct = {}
    nits_values = []

    for row_index, row in enumerate(rows, start=1):
        if len(row) != len(CSV_HEADER):
            return False, f"row {row_index} has {len(row)} columns"

        try:
            brightness_pct = int(row[0])
        except ValueError:
            return False, f"row {row_index} has invalid brightness"

        status = row[2].strip()
        if status != "OK":
            return False, f"row {row_index} status is {status or 'empty'}"

        try:
            nits = float(row[1])
        except ValueError:
            return False, f"row {row_index} has invalid nits"

        if not math.isfinite(nits) or nits < 0:
            return False, f"row {row_index} has invalid nits"

        if row[3].strip():
            try:
                temp = float(row[3])
            except ValueError:
                return False, f"row {row_index} has invalid temperature"
            if not math.isfinite(temp):
                return False, f"row {row_index} has invalid temperature"

        try:
            retries = int(row[4])
        except ValueError:
            return False, f"row {row_index} has invalid retries"
        if retries < 0:
            return False, f"row {row_index} has invalid retries"

        observed.append(brightness_pct)
        nits_by_pct[brightness_pct] = nits
        nits_values.append(nits)

    if observed != expected:
        return False, "brightness steps do not match the requested sweep"
    if len(set(observed)) != len(observed):
        return False, "duplicate brightness steps found"

    if args.placement_check and 100 in nits_by_pct:
        fullscale_nits = nits_by_pct[100]
        if fullscale_nits < args.placement_min_nits:
            return False, (
                f"100% row is {fullscale_nits:.1f} nits, "
                f"below {args.placement_min_nits:.0f}"
            )

    if len(nits_values) > 1 and max(nits_values) <= min(nits_values):
        return False, "nits did not change across sweep"

    return True, "ok"


def format_placement_message(template, args, nits=None, attempt=1,
                             elapsed_seconds=0.0):
    nits_text = f"{nits:.1f}" if nits is not None else "NA"
    return template.format(
        nits=nits if nits is not None else 0.0,
        nits_text=nits_text,
        min_nits=args.placement_min_nits,
        brightness_pct=args.placement_brightness,
        attempt=attempt,
        elapsed_seconds=elapsed_seconds,
    )


def wait_for_colorimeter_placement(client, display, args):
    if not args.placement_check:
        return True

    show_status_message(display, args,
                        format_placement_message(args.placement_text, args))

    response = client.call("set_brightness", brightness=int(args.placement_brightness))
    if not response_ok(response):
        raise RuntimeError(f"placement set_brightness failed: {response}")

    if not interruptible_sleep(args.placement_settle_seconds):
        raise InterruptedError("stop requested")

    start = time.monotonic()
    attempt = 1
    while not stop_requested():
        require_colorimeter_still_connected(display, args)

        elapsed = time.monotonic() - start
        nits, _retries = measure_nits(0, args.retry_sleep, args.spotread_timeout)
        if nits is not None and nits >= args.placement_min_nits:
            if args.placement_success_text:
                display.set_overlay_text(
                    format_placement_message(args.placement_success_text, args,
                                             nits, attempt, elapsed)
                )
            print(f"placement check OK: {nits:.2f} nits", file=sys.stderr)
            return True

        if args.placement_text:
            display.set_overlay_text(
                format_placement_message(args.placement_text, args,
                                         nits, attempt, elapsed)
            )

        if args.placement_timeout_seconds > 0 and elapsed >= args.placement_timeout_seconds:
            if args.placement_failed_text:
                display.set_overlay_text(
                    format_placement_message(args.placement_failed_text, args,
                                             nits, attempt, elapsed)
                )
            print("error: colorimeter placement check timed out", file=sys.stderr)
            return False

        attempt += 1
        if not interruptible_sleep(args.placement_retry_seconds):
            raise InterruptedError("stop requested")

    raise InterruptedError("stop requested")


def setup_display(display, args):
    if args.pattern:
        display.command(f"pattern {args.pattern}", required=True)
    if args.progress_text:
        display.best_effort("set-metadata-status enable")
        display.best_effort(f"set-metadata-fontsize {args.progress_fontsize}")
        display.set_overlay_text(args.initial_text)


def update_progress(display, args, step, total, brightness_pct):
    if not args.progress_text:
        return
    try:
        message = args.progress_text.format(
            step=step,
            total=total,
            brightness_pct=brightness_pct,
        )
    except (KeyError, IndexError) as e:
        print(f"warning: invalid progress text template: {e}", file=sys.stderr)
        return
    display.set_overlay_text(message)


def validate_args(args):
    if not (0 <= args.start <= 100 and 0 <= args.end <= 100):
        print("error: --start and --end must be in [0, 100]", file=sys.stderr)
        return False
    if args.step <= 0:
        print("error: --step must be > 0", file=sys.stderr)
        return False
    if not (0 <= args.placement_brightness <= 100):
        print("error: --placement-brightness must be in [0, 100]", file=sys.stderr)
        return False
    if args.placement_min_nits <= 0:
        print("error: --placement-min-nits must be > 0", file=sys.stderr)
        return False
    if args.placement_settle_seconds < 0:
        print("error: --placement-settle-seconds must be >= 0", file=sys.stderr)
        return False
    if args.placement_retry_seconds <= 0:
        print("error: --placement-retry-seconds must be > 0", file=sys.stderr)
        return False
    if args.placement_timeout_seconds < 0:
        print("error: --placement-timeout-seconds must be >= 0", file=sys.stderr)
        return False
    if args.max_retries < 0:
        print("error: --max-retries must be >= 0", file=sys.stderr)
        return False
    if args.zero_nits_max_retries < 0:
        print("error: --zero-nits-max-retries must be >= 0", file=sys.stderr)
        return False
    if not (0 <= args.i2c_temp_address <= 0x7F):
        print("error: --i2c-temp-address must be a 7-bit I2C address",
              file=sys.stderr)
        return False
    if not (0 <= args.i2c_temp_register <= 0xFFFF):
        print("error: --i2c-temp-register must be in [0, 0xFFFF]",
              file=sys.stderr)
        return False
    if not (8 <= args.progress_fontsize <= 48):
        print("error: --progress-fontsize must be in [8, 48]", file=sys.stderr)
        return False
    if args.progress_text:
        try:
            args.progress_text.format(step=1, total=1, brightness_pct=100)
        except (KeyError, IndexError) as e:
            print(f"error: invalid --progress-text placeholder: {e}", file=sys.stderr)
            return False

    test_values = {
        "rows": 1,
        "total": 1,
        "output": args.output,
        "reason": "test",
        "calibration_target": "/tmp/test.csv",
        "calibration_status": "test",
        "nits": 300.0,
        "nits_text": "300.0",
        "min_nits": args.placement_min_nits,
        "brightness_pct": args.placement_brightness,
        "attempt": 1,
        "elapsed_seconds": 0.0,
    }
    for name in ("complete_text", "calibration_success_text",
                 "abort_text", "sensor_not_found_text",
                 "sensor_disconnected_text",
                 "placement_text", "placement_success_text",
                 "placement_failed_text"):
        template = getattr(args, name)
        if template:
            try:
                template.format(**test_values)
            except (KeyError, IndexError) as e:
                print(f"error: invalid --{name.replace('_', '-')} placeholder: {e}",
                      file=sys.stderr)
                return False

    return True


def parse_args():
    parser = argparse.ArgumentParser(
        description="Child-script ALS brightness sweep for disp-tester.",
    )

    parser.add_argument("--als-socket", default=None,
                        help=f"als-dimmer Unix socket. If omitted, {DEFAULT_ALS_SOCKET} is used when present.")
    parser.add_argument("--als-host", default="127.0.0.1",
                        help="als-dimmer TCP host when no Unix socket is used.")
    parser.add_argument("--als-port", type=int, default=9000,
                        help="als-dimmer TCP port when no Unix socket is used.")
    parser.add_argument("--als-timeout", type=float, default=1.0,
                        help="als-dimmer socket timeout in seconds.")

    parser.add_argument("--disp-host", default=DEFAULT_DISP_HOST,
                        help="Parent disp-tester host.")
    parser.add_argument("--disp-port", type=int, default=DEFAULT_DISP_PORT,
                        help="Parent disp-tester port.")
    parser.add_argument("--disp-timeout", type=float, default=1.0,
                        help="disp-tester command timeout in seconds.")

    parser.add_argument("--start", type=int, default=100,
                        help="Starting brightness percent.")
    parser.add_argument("--end", type=int, default=0,
                        help="Ending brightness percent.")
    parser.add_argument("--step", type=int, default=1,
                        help="Step size in percent.")
    parser.add_argument("--ascending", action="store_true",
                        help="Sweep low to high instead of high to low.")

    parser.add_argument("--settle-seconds", type=float, default=3.0,
                        help="Seconds to wait after each brightness set.")
    parser.add_argument("--warmup-seconds", type=float, default=0.0,
                        help="Extra warmup at the first brightness before step 1.")
    parser.add_argument("--max-retries", type=int, default=5,
                        help="spotread parse retries per row.")
    parser.add_argument("--retry-sleep", type=float, default=1.0,
                        help="Seconds between spotread retries.")
    parser.add_argument("--spotread-timeout", type=float, default=15.0,
                        help="Timeout for each spotread attempt.")
    parser.add_argument("--zero-nits-on-zero-fail", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Treat a failed spotread at exactly 0%% brightness as 0.0 nits.")
    parser.add_argument("--zero-nits-max-retries", type=int, default=0,
                        help="spotread parse retries at 0%% brightness before using the zero-nits fallback.")
    parser.add_argument("--max-consecutive-failures", type=int, default=10,
                        help="Abort after this many consecutive failed rows.")

    parser.add_argument("--pattern", default="white",
                        help="disp-tester pattern to show before sweep. Empty string skips setup.")
    parser.add_argument("--progress-text", default=DEFAULT_PROGRESS_TEXT,
                        help="disp-tester progress overlay template. Empty string disables overlay.")
    parser.add_argument("--initial-text", default="Starting calibration sweep...",
                        help="Initial overlay text.")
    parser.add_argument("--complete-text", default=DEFAULT_COMPLETE_TEXT,
                        help="Completion overlay template. Empty string disables completion overlay.")
    parser.add_argument("--calibration-success-text", default=DEFAULT_CALIBRATION_SUCCESS_TEXT,
                        help="Overlay shown after calibration CSV install and als-dimmer restart succeed.")
    parser.add_argument("--abort-text", default=DEFAULT_ABORT_TEXT,
                        help="Abort overlay template. Empty string disables abort overlay.")
    parser.add_argument("--sensor-not-found-text", default=DEFAULT_SENSOR_NOT_FOUND_TEXT,
                        help="Overlay shown when the required USB colorimeter is not detected.")
    parser.add_argument("--sensor-disconnected-text", default=DEFAULT_SENSOR_DISCONNECTED_TEXT,
                        help="Overlay shown when the USB colorimeter is disconnected during the sweep.")
    parser.add_argument("--placement-check", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Require an initial bright reading before starting the sweep.")
    parser.add_argument("--placement-min-nits", type=float, default=250.0,
                        help="Minimum initial white-screen reading required before sweep starts.")
    parser.add_argument("--placement-brightness", type=int, default=100,
                        help="Brightness percent used for the placement check.")
    parser.add_argument("--placement-settle-seconds", type=float, default=3.0,
                        help="Seconds to wait after setting placement brightness before first read.")
    parser.add_argument("--placement-retry-seconds", type=float, default=2.0,
                        help="Seconds between placement-check reads.")
    parser.add_argument("--placement-timeout-seconds", type=float, default=0.0,
                        help="Abort placement check after this many seconds. 0 waits until user exits.")
    parser.add_argument("--placement-text", default=DEFAULT_PLACEMENT_TEXT,
                        help="Overlay shown while waiting for colorimeter placement.")
    parser.add_argument("--placement-success-text", default=DEFAULT_PLACEMENT_SUCCESS_TEXT,
                        help="Overlay shown when placement check passes.")
    parser.add_argument("--placement-failed-text", default=DEFAULT_PLACEMENT_FAILED_TEXT,
                        help="Overlay shown when placement check times out.")
    parser.add_argument("--progress-fontsize", type=int, default=32,
                        help="disp-tester metadata font size.")
    parser.add_argument("--require-colorimeter", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Require an i1 Display Pro USB colorimeter before starting the sweep.")
    parser.add_argument("--check-colorimeter-during-sweep", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Re-check the USB colorimeter before measurements and abort cleanly if disconnected.")
    parser.add_argument("--colorimeter-match", action="append",
                        default=list(DEFAULT_COLORIMETER_MATCH),
                        help="Case-insensitive USB manufacturer/product substring accepted as the colorimeter.")
    parser.add_argument("--colorimeter-usb-id", action="append",
                        default=list(DEFAULT_COLORIMETER_USB_IDS),
                        help="Accepted colorimeter USB VID:PID. Can be repeated.")
    parser.add_argument("--usb-sysfs-root", default="/sys/bus/usb/devices",
                        help="USB sysfs root used for colorimeter detection.")

    parser.add_argument("--temp-cmd",
                        help="Shell command that prints backlight temperature in degC. Overrides native I2C temp sampling.")
    parser.add_argument("--i2c-temp", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Sample backlight temperature from the display_manager I2C diagnostics registers.")
    parser.add_argument("--i2c-temp-bus", default=DEFAULT_I2C_TEMP_BUS,
                        help="Linux I2C device used for native backlight temperature sampling.")
    parser.add_argument("--i2c-temp-address", type=parse_int_auto,
                        default=DEFAULT_I2C_TEMP_ADDRESS,
                        help="7-bit I2C address for display_manager temperature diagnostics.")
    parser.add_argument("--i2c-temp-register", type=parse_int_auto,
                        default=DEFAULT_I2C_TEMP_REGISTER,
                        help="16-bit register containing signed temperature x10.")
    parser.add_argument("--label", default="",
                        help="Free-form label written into CSV header.")
    parser.add_argument("--output", default=default_output_path(),
                        help="Path to write CSV. Defaults to /tmp/als-dimmer-sweep-<timestamp>.csv.")
    parser.add_argument("--no-restore", action="store_true",
                        help="Do not restore original als-dimmer state on exit.")
    parser.add_argument("--install-calibration", action="store_true",
                        help="After a successful sweep, copy the CSV into the ALS calibration folder and restart als-dimmer.")
    parser.add_argument("--calibration-config", default=DEFAULT_ALS_CONFIG,
                        help="ALS config.json path used to infer the calibration target filename.")
    parser.add_argument("--calibration-dir", default=DEFAULT_CALIBRATION_DIR,
                        help="Directory that stores ALS calibration CSV files.")
    parser.add_argument("--calibration-target",
                        help="Override calibration target CSV path or filename.")
    parser.add_argument("--calibration-fallback", default=DEFAULT_CALIBRATION_FALLBACK,
                        help="Fallback calibration filename when config symlink is not recognized.")
    parser.add_argument("--restart-command", default=DEFAULT_RESTART_COMMAND,
                        help="Command used after installing calibration. Empty string skips restart.")
    parser.add_argument("--no-calibration-sudo", action="store_true",
                        help="Do not use non-interactive sudo for calibration install/restart.")

    return parser.parse_args()


def main():
    install_signal_handlers()
    args = parse_args()
    if not validate_args(args):
        return 2

    display = DispTesterClient(args.disp_host, args.disp_port, args.disp_timeout)
    if not check_colorimeter_or_report(display, args):
        return 3

    if args.ascending:
        steps = build_step_list(min(args.start, args.end), max(args.start, args.end), args.step)
    else:
        steps = build_step_list(max(args.start, args.end), min(args.start, args.end), args.step)

    client = None
    recorder = None
    original_mode = None
    original_brightness = None
    output_type = "unknown"
    aborted_reason = None
    calibration_status = "not requested"
    calibration_target = ""
    failed_rows = 0
    suppress_abort_overlay = False
    zero_nits_fallback_used = False

    try:
        client = connect_als(args)

        status = client.call("get_status")
        if not response_ok(status):
            raise RuntimeError(f"get_status failed: {status}")
        original_mode = status["data"]["mode"]
        original_brightness = status["data"]["brightness"]

        config = client.call("get_config")
        if response_ok(config):
            output_type = config["data"].get("output_type", "unknown")

        print(f"als-dimmer: output_type={output_type} original mode={original_mode} "
              f"brightness={original_brightness}%", file=sys.stderr)

        if original_mode != "manual":
            response = client.call("set_mode", mode="manual")
            if not response_ok(response):
                raise RuntimeError(f"set_mode manual failed: {response}")

        setup_display(display, args)

        if not wait_for_colorimeter_placement(client, display, args):
            aborted_reason = "colorimeter placement check failed"
            suppress_abort_overlay = True
            raise RuntimeError(aborted_reason)

        if not validate_output_path(args.output):
            raise RuntimeError(f"cannot write output CSV {args.output}")

        temperature_sampler = setup_temperature_sampler(args)
        recorder = CsvRecorder(args.output, args, output_type, len(steps))
        recorder.__enter__()

        if args.warmup_seconds > 0 and steps and not stop_requested():
            response = client.call("set_brightness", brightness=int(steps[0]))
            if not response_ok(response):
                raise RuntimeError(f"warmup set_brightness failed: {response}")
            print(f"warmup: brightness {steps[0]}%, sleeping {args.warmup_seconds}s",
                  file=sys.stderr)
            interruptible_sleep(args.warmup_seconds)

        print(f"sweeping {len(steps)} steps -> {args.output}", file=sys.stderr)
        consecutive_failures = 0

        for index, pct in enumerate(steps, start=1):
            if stop_requested():
                aborted_reason = f"signal {_stop_signal}"
                break

            if not check_colorimeter_still_connected(display, args):
                aborted_reason = "i1 Display Pro USB colorimeter disconnected"
                suppress_abort_overlay = True
                break

            response = client.call("set_brightness", brightness=int(pct))
            if not response_ok(response):
                reason = response.get("message", "set_brightness failed")
                print(f"  [{index}/{len(steps)}] {pct}% FAIL: {reason}", file=sys.stderr)
                recorder.write_row(pct, None, "FAIL", None, 0)
                failed_rows += 1
                consecutive_failures += 1
                if consecutive_failures >= args.max_consecutive_failures:
                    aborted_reason = f"{consecutive_failures} consecutive brightness failures"
                    break
                continue

            update_progress(display, args, index, len(steps), int(pct))

            if not interruptible_sleep(args.settle_seconds):
                aborted_reason = f"signal {_stop_signal}"
                break

            if not check_colorimeter_still_connected(display, args):
                aborted_reason = "i1 Display Pro USB colorimeter disconnected"
                suppress_abort_overlay = True
                break

            pct_int = int(pct)
            zero_fallback_row = pct_int == 0 and args.zero_nits_on_zero_fail
            measurement_max_retries = (
                args.zero_nits_max_retries if zero_fallback_row
                else args.max_retries
            )
            measurement_guard = None if zero_fallback_row else (
                lambda: require_colorimeter_still_connected(display, args)
            )

            nits, retries = measure_nits(
                measurement_max_retries,
                args.retry_sleep,
                args.spotread_timeout,
                before_attempt=measurement_guard,
            )

            if nits is None and zero_fallback_row:
                nits = 0.0
                zero_nits_fallback_used = True
                print("    zero-nits fallback: treating failed 0% spotread as 0.0 nits",
                      file=sys.stderr)

            temp = temperature_sampler()
            row_status = "OK" if nits is not None else "FAIL"

            if nits is None:
                failed_rows += 1
                consecutive_failures += 1
            else:
                consecutive_failures = 0

            recorder.write_row(pct, nits, row_status, temp, retries)
            nits_text = f"{nits:>9.4f}" if nits is not None else "       NA"
            temp_text = f" temp={temp:.1f}C" if temp is not None else ""
            print(f"  [{index:>3}/{len(steps)}] {pct:>3}% -> nits={nits_text} "
                  f"({row_status}, retries={retries}){temp_text}", file=sys.stderr)

            if consecutive_failures >= args.max_consecutive_failures:
                aborted_reason = f"{consecutive_failures} consecutive measurement failures"
                break

    except ColorimeterDisconnectedError as e:
        aborted_reason = str(e)
        suppress_abort_overlay = True
    except InterruptedError:
        aborted_reason = f"signal {_stop_signal}"
    except Exception as e:
        aborted_reason = str(e)
        print(f"error: {e}", file=sys.stderr)
    finally:
        rows_written = recorder.rows if recorder else 0
        if recorder:
            if aborted_reason:
                recorder.write_comment("aborted", aborted_reason)
            else:
                if zero_nits_fallback_used:
                    recorder.write_comment(
                        "zero_nits_fallback",
                        "spotread_failed_at_0_percent",
                    )
                recorder.write_comment("completed", "true")
            recorder.__exit__(None, None, None)

        restore_als(client, original_mode, original_brightness, args.no_restore)
        if client:
            client.close()

        if not stop_requested():
            if not aborted_reason and args.install_calibration:
                if rows_written != len(steps):
                    calibration_status = f"skipped: only {rows_written}/{len(steps)} rows captured"
                    aborted_reason = calibration_status
                elif failed_rows:
                    calibration_status = f"skipped: {failed_rows} failed rows"
                    aborted_reason = calibration_status
                else:
                    csv_ok, csv_reason = validate_sweep_csv(args.output, steps, args)
                    if not csv_ok:
                        calibration_status = f"skipped: {csv_reason}"
                        aborted_reason = f"calibration sanity check failed: {csv_reason}"
                    else:
                        try:
                            ok, calibration_target, calibration_status = install_calibration(args)
                            if not ok:
                                aborted_reason = f"calibration install failed: {calibration_status}"
                        except InterruptedError:
                            aborted_reason = f"signal {_stop_signal}"

            if aborted_reason:
                if args.abort_text and not suppress_abort_overlay:
                    display.set_overlay_text(args.abort_text.format(
                        rows=rows_written,
                        total=len(steps),
                        reason=aborted_reason,
                        output=args.output,
                        calibration_target=calibration_target,
                        calibration_status=calibration_status,
                    ))
            elif args.install_calibration and args.calibration_success_text:
                display.set_overlay_text(args.calibration_success_text.format(
                    rows=rows_written,
                    total=len(steps),
                    output=args.output,
                    calibration_target=calibration_target,
                    calibration_status=calibration_status,
                ))
            elif args.complete_text:
                display.set_overlay_text(args.complete_text.format(
                    rows=rows_written,
                    total=len(steps),
                    output=args.output,
                    calibration_target=calibration_target,
                    calibration_status=calibration_status,
                ))

    if stop_requested():
        print("stopped by signal; partial CSV preserved", file=sys.stderr)
        return signal_exit_code()

    if aborted_reason:
        print(f"sweep aborted: {aborted_reason}", file=sys.stderr)
        return 1

    print(f"sweep complete -> {args.output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
