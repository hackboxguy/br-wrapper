#!/usr/bin/env python3
"""
live-measurements-child.py

Read-only child script for disp-tester. It shows a white pattern, samples
spotread xyY, display-manager backlight temperature, and als-dimmer absolute
brightness, then updates the parent disp-tester metadata overlay.
"""

import argparse
import ctypes
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
DEFAULT_ALS_HOST = "127.0.0.1"
DEFAULT_ALS_PORT = 9000
DEFAULT_DISP_HOST = os.environ.get("DISP_TESTER_HOST", "127.0.0.1")
DEFAULT_DISP_PORT = int(os.environ.get("DISP_TESTER_PORT", "8082"))
DEFAULT_I2C_TEMP_BUS = "/dev/i2c-1"
DEFAULT_I2C_TEMP_ADDRESS = 0x66
DEFAULT_I2C_TEMP_REGISTER = 0x1002
DEFAULT_COLORIMETER_MATCH = [
    "i1display",
    "i1 display",
    "i1display pro",
    "i1 display pro",
]
DEFAULT_COLORIMETER_USB_IDS = [
    "0765:5020",
]
DEFAULT_SENSOR_NOT_FOUND_TEXT = (
    "Live Measurement\n"
    "i1 Display Pro Not Found\n"
    "Connect USB colorimeter"
)
DEFAULT_INITIAL_TEXT = (
    "Live Measurement\n"
    "Starting..."
)

I2C_RDWR = 0x0707
I2C_M_RD = 0x0001
_FLOAT_RE = r"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)"
_YXY_RE = re.compile(rf"Yxy:\s*{_FLOAT_RE}\s+{_FLOAT_RE}\s+{_FLOAT_RE}")

_stop_requested = False
_stop_signal = None
_active_process = None


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
    def __init__(self, socket_path=None, host="127.0.0.1", port=9000,
                 timeout=2.0):
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


class AlsReader:
    def __init__(self, args):
        self.args = args
        self.client = None

    def close(self):
        if self.client:
            self.client.close()
            self.client = None

    def _connect(self):
        socket_path = self.args.als_socket
        if socket_path is None and os.path.exists(DEFAULT_ALS_SOCKET):
            socket_path = DEFAULT_ALS_SOCKET
        self.client = JsonLineClient(
            socket_path,
            self.args.als_host,
            self.args.als_port,
            self.args.als_timeout,
        )

    def call(self, command, **params):
        last_error = None
        for _attempt in range(2):
            if self.client is None:
                self._connect()
            try:
                return self.client.call(command, **params)
            except InterruptedError:
                raise
            except (OSError, RuntimeError, json.JSONDecodeError) as e:
                last_error = e
                self.close()
        raise RuntimeError(last_error)


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

    def _recv_line(self, sock):
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(1024)
            if not chunk:
                break
            data += chunk
        return data.decode("utf-8", errors="replace").strip()


def one_line(text, max_len=120):
    collapsed = " ".join(str(text).split())
    if len(collapsed) > max_len:
        return collapsed[:max_len - 3] + "..."
    return collapsed


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


def run_process_capture(argv, timeout):
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


def parse_yxy_from_spotread(text):
    match = _YXY_RE.search(text)
    if not match:
        return None
    y_value, x_value, y_chromaticity = (float(item) for item in match.groups())
    return {
        "Y": y_value,
        "x": x_value,
        "y": y_chromaticity,
    }


def measure_spotread(timeout):
    try:
        out = run_process_capture(["spotread", "-x", "-O"], timeout)
    except InterruptedError:
        raise
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired,
            FileNotFoundError) as e:
        detail = getattr(e, "output", None) or str(e)
        return None, one_line(detail)

    measurement = parse_yxy_from_spotread(out)
    if measurement is None:
        return None, "spotread output did not contain Yxy"
    return measurement, ""


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


def read_temp(temp_cmd):
    if temp_cmd is None or stop_requested():
        return None
    try:
        out = run_process_capture(["sh", "-c", temp_cmd], timeout=5).strip()
    except (InterruptedError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired, FileNotFoundError):
        return None
    match = re.search(_FLOAT_RE, out)
    return float(match.group(1)) if match else None


def setup_temperature_sampler(args):
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


def read_als_snapshot(als_reader):
    try:
        status = als_reader.call("get_status")
        absolute = als_reader.call("get_absolute_brightness")
    except InterruptedError:
        raise
    except Exception as e:
        return {}, {}, one_line(e)

    status_data = status.get("data", {}) if response_ok(status) else {}
    absolute_data = absolute.get("data", {}) if response_ok(absolute) else {}
    error = ""
    if not response_ok(status):
        error = one_line(status.get("message", "get_status failed"))
    elif not response_ok(absolute):
        error = one_line(absolute.get("message", "get_absolute_brightness failed"))

    return status_data, absolute_data, error


def as_float(value):
    if value is None:
        return None
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    return result if math.isfinite(result) else None


def fmt_float(value, precision=1, suffix=""):
    number = as_float(value)
    if number is None:
        return "NA"
    return f"{number:.{precision}f}{suffix}"


def delta_percent(measurement, status_data, absolute_data):
    if not measurement:
        return None
    absolute_nits = as_float(absolute_data.get("nits"))
    if absolute_nits is None:
        absolute_nits = as_float(status_data.get("nits"))
    if absolute_nits is None or absolute_nits <= 0:
        return None
    return ((measurement["Y"] - absolute_nits) / absolute_nits) * 100.0


def overlay_alert_needed(measurement, spotread_error, status_data,
                         absolute_data, als_error, args):
    if als_error:
        return True
    if spotread_error and not measurement:
        return True
    pct = delta_percent(measurement, status_data, absolute_data)
    return pct is not None and abs(pct) > args.delta_alert_percent


def format_live_message(measurement, spotread_error, temp, status_data,
                        absolute_data, als_error):
    lines = ["Live Measurement"]

    if measurement:
        measured_nits = measurement["Y"]
        lines.append(f"Measured: {measured_nits:.1f} nits")
        lines.append(f"x: {measurement['x']:.4f}  y: {measurement['y']:.4f}")
    elif spotread_error:
        measured_nits = None
        lines.append("Measured: sensor error")
    else:
        measured_nits = None
        lines.append("Measured: waiting")

    brightness_pct = status_data.get("brightness")
    absolute_nits = as_float(absolute_data.get("nits"))
    if absolute_nits is None:
        absolute_nits = as_float(status_data.get("nits"))

    calibrated = absolute_data.get("calibrated", status_data.get("calibrated"))
    if absolute_nits is not None:
        lines.append(
            f"ALS absolute: {absolute_nits:.1f} nits @ {fmt_float(brightness_pct, 0, '%')}"
        )
    elif calibrated is False:
        lines.append(f"ALS absolute: uncalibrated @ {fmt_float(brightness_pct, 0, '%')}")
    else:
        lines.append(f"ALS absolute: NA @ {fmt_float(brightness_pct, 0, '%')}")

    if measured_nits is not None and absolute_nits is not None:
        delta = measured_nits - absolute_nits
        if absolute_nits > 0:
            pct = (delta / absolute_nits) * 100.0
            lines.append(f"Delta: {delta:+.1f} nits ({pct:+.1f}%)")
        else:
            lines.append(f"Delta: {delta:+.1f} nits")

    lines.append(f"BL Temp: {fmt_float(temp, 1, ' C')}")

    if als_error:
        lines.append("ALS: read error")

    return "\n".join(lines)


def setup_display(display, args):
    if args.pattern:
        display.command(f"pattern {args.pattern}", required=True)
    display.best_effort("set-metadata-status enable")
    display.best_effort(f"set-metadata-fontsize {args.progress_fontsize}")
    display.best_effort(f"set-metadata-align {args.metadata_align}")
    display.best_effort(f"set-metadata-color {args.normal_color}")
    display.set_overlay_text(args.initial_text)


def validate_args(args):
    if args.interval_seconds < 0:
        print("error: --interval-seconds must be >= 0", file=sys.stderr)
        return False
    if args.spotread_timeout <= 0:
        print("error: --spotread-timeout must be > 0", file=sys.stderr)
        return False
    if args.max_samples < 0:
        print("error: --max-samples must be >= 0", file=sys.stderr)
        return False
    if args.delta_alert_percent < 0:
        print("error: --delta-alert-percent must be >= 0", file=sys.stderr)
        return False
    if not (8 <= args.progress_fontsize <= 48):
        print("error: --progress-fontsize must be in [8, 48]", file=sys.stderr)
        return False
    if args.metadata_align not in ("left", "center", "right"):
        print("error: --metadata-align must be left, center, or right",
              file=sys.stderr)
        return False
    if not (0 <= args.i2c_temp_address <= 0x7F):
        print("error: --i2c-temp-address must be a 7-bit I2C address",
              file=sys.stderr)
        return False
    if not (0 <= args.i2c_temp_register <= 0xFFFF):
        print("error: --i2c-temp-register must be in [0, 0xFFFF]",
              file=sys.stderr)
        return False
    return True


def parse_args():
    parser = argparse.ArgumentParser(
        description="Live xyY/nits monitor child script for disp-tester.",
    )
    parser.add_argument("--als-socket", default=None,
                        help=f"als-dimmer Unix socket. If omitted, {DEFAULT_ALS_SOCKET} is used when present.")
    parser.add_argument("--als-host", default=DEFAULT_ALS_HOST,
                        help="als-dimmer TCP host when no Unix socket is used.")
    parser.add_argument("--als-port", type=int, default=DEFAULT_ALS_PORT,
                        help="als-dimmer TCP port when no Unix socket is used.")
    parser.add_argument("--als-timeout", type=float, default=1.0,
                        help="als-dimmer socket timeout in seconds.")

    parser.add_argument("--disp-host", default=DEFAULT_DISP_HOST,
                        help="Parent disp-tester host.")
    parser.add_argument("--disp-port", type=int, default=DEFAULT_DISP_PORT,
                        help="Parent disp-tester port.")
    parser.add_argument("--disp-timeout", type=float, default=1.0,
                        help="disp-tester command timeout in seconds.")

    parser.add_argument("--pattern", default="white",
                        help="disp-tester pattern to show. Empty string skips setup.")
    parser.add_argument("--interval-seconds", type=float, default=2.0,
                        help="Seconds to wait after each overlay update before the next spotread.")
    parser.add_argument("--spotread-timeout", type=float, default=15.0,
                        help="Timeout for each spotread attempt.")
    parser.add_argument("--max-samples", type=int, default=0,
                        help="Stop after this many samples. 0 runs until disp-tester exits.")
    parser.add_argument("--initial-text", default=DEFAULT_INITIAL_TEXT,
                        help="Initial overlay text.")
    parser.add_argument("--sensor-not-found-text", default=DEFAULT_SENSOR_NOT_FOUND_TEXT,
                        help="Overlay shown when the USB colorimeter is not detected.")
    parser.add_argument("--progress-fontsize", type=int, default=24,
                        help="disp-tester metadata font size.")
    parser.add_argument("--metadata-align", default="left",
                        help="disp-tester metadata alignment: left, center, or right.")
    parser.add_argument("--normal-color", default="white",
                        help="disp-tester metadata color used when values are in range.")
    parser.add_argument("--alert-color", default="red",
                        help="disp-tester metadata color used for sensor errors or large delta.")
    parser.add_argument("--delta-alert-percent", type=float, default=5.0,
                        help="Turn overlay alert color when measured-vs-ALS delta exceeds this percent.")

    parser.add_argument("--require-colorimeter", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Require an i1 Display Pro USB colorimeter before each spotread.")
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
                        help="Sample backlight temperature from display_manager I2C diagnostics.")
    parser.add_argument("--i2c-temp-bus", default=DEFAULT_I2C_TEMP_BUS,
                        help="Linux I2C device used for native backlight temperature sampling.")
    parser.add_argument("--i2c-temp-address", type=parse_int_auto,
                        default=DEFAULT_I2C_TEMP_ADDRESS,
                        help="7-bit I2C address for display_manager temperature diagnostics.")
    parser.add_argument("--i2c-temp-register", type=parse_int_auto,
                        default=DEFAULT_I2C_TEMP_REGISTER,
                        help="16-bit register containing signed temperature x10.")

    return parser.parse_args()


def main():
    install_signal_handlers()
    args = parse_args()
    if not validate_args(args):
        return 2

    display = DispTesterClient(args.disp_host, args.disp_port, args.disp_timeout)
    als_reader = AlsReader(args)
    temperature_sampler = setup_temperature_sampler(args)
    samples = 0
    last_sensor_missing = False

    try:
        setup_display(display, args)

        while not stop_requested():
            if args.max_samples and samples >= args.max_samples:
                break

            if args.require_colorimeter and not find_colorimeter(
                args.colorimeter_match,
                args.colorimeter_usb_id,
                args.usb_sysfs_root,
            ):
                if not last_sensor_missing:
                    print("warning: i1 Display Pro USB colorimeter not found",
                          file=sys.stderr)
                last_sensor_missing = True
                temp = temperature_sampler()
                status_data, absolute_data, als_error = read_als_snapshot(als_reader)
                message = format_live_message(
                    None,
                    "sensor not found",
                    temp,
                    status_data,
                    absolute_data,
                    als_error,
                )
                if args.sensor_not_found_text:
                    message = args.sensor_not_found_text + "\n" + "\n".join(
                        message.splitlines()[2:]
                    )
                display.best_effort(f"set-metadata-color {args.alert_color}")
                display.set_overlay_text(message)
                if not interruptible_sleep(args.interval_seconds):
                    break
                continue

            last_sensor_missing = False
            measurement, spotread_error = measure_spotread(args.spotread_timeout)
            temp = temperature_sampler()
            status_data, absolute_data, als_error = read_als_snapshot(als_reader)

            message = format_live_message(
                measurement,
                spotread_error,
                temp,
                status_data,
                absolute_data,
                als_error,
            )
            alert = overlay_alert_needed(
                measurement,
                spotread_error,
                status_data,
                absolute_data,
                als_error,
                args,
            )
            color = args.alert_color if alert else args.normal_color
            display.best_effort(f"set-metadata-color {color}")
            display.set_overlay_text(message)
            samples += 1

            measured = measurement["Y"] if measurement else None
            target = as_float(absolute_data.get("nits"))
            print(
                f"sample {samples}: measured={fmt_float(measured, 2)} "
                f"als={fmt_float(target, 2)} temp={fmt_float(temp, 1)}",
                file=sys.stderr,
            )

            if not interruptible_sleep(args.interval_seconds):
                break

    except InterruptedError:
        pass
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        display.best_effort(f"set-metadata-color {args.alert_color}")
        display.set_overlay_text(f"Live Measurement\nError: {one_line(e, 80)}")
        return 1
    finally:
        als_reader.close()

    if stop_requested():
        return signal_exit_code()
    return 0


if __name__ == "__main__":
    sys.exit(main())
