#!/usr/bin/env python3
"""
white-point-profile-child.py

Child script for disp-tester that profiles the FPGA white-point registers
(wpx/wpy/wpz) against spotread XYZ/xyY measurements. It assumes disp-tester is
already running as its parent, drives the parent socket for the full-white
pattern and operator overlay, writes FPGA registers through disptool, and saves
both per-sample and per-point summary data.

The profiler does not touch display brightness or als-dimmer state. Operators
should set the reference/target brightness externally before launching it.
"""

import argparse
import csv
import datetime
import json
import math
import os
import re
import signal
import socket
import subprocess
import sys
import time


DEFAULT_DISP_HOST = os.environ.get("DISP_TESTER_HOST", "127.0.0.1")
DEFAULT_DISP_PORT = int(os.environ.get("DISP_TESTER_PORT", "8082"))
DEFAULT_DISPTOOL = "/home/pi/micropanel/bin/disptool"
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
DEFAULT_INITIAL_TEXT = (
    "White Point Profiling\n"
    "Starting..."
)
DEFAULT_SENSOR_NOT_FOUND_TEXT = (
    "White Point Profiling\n"
    "i1 Display Pro Not Found\n"
    "Connect USB colorimeter"
)
DEFAULT_PLACEMENT_TEXT = (
    "White Point Profiling\n"
    "Place i1 Display Pro on target display\n"
    "Current: {nits_text} nits"
)
DEFAULT_PLACEMENT_SUCCESS_TEXT = (
    "White Point Profiling\n"
    "Target placement OK\n"
    "Initial: {nits:.1f} nits"
)
DEFAULT_PROGRESS_TEXT = (
    "White Point Profiling\n"
    "Point {point}/{total}: {label}\n"
    "wpx={wpx} wpy={wpy} wpz={wpz}\n"
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
PROFILE_SCHEMA = "disp-tester-white-point-profile-v1"
SAMPLE_CSV_HEADER = [
    "point_index",
    "label",
    "wpx",
    "wpy",
    "wpz",
    "sample_index",
    "timestamp",
    "X",
    "Y",
    "Z",
    "x",
    "y",
    "retries",
    "status",
    "error",
]
SUMMARY_CSV_HEADER = [
    "point_index",
    "label",
    "wpx",
    "wpy",
    "wpz",
    "samples",
    "X",
    "Y",
    "Z",
    "x",
    "y",
    "x_from_xyz",
    "y_from_xyz",
    "std_x",
    "std_y",
    "std_Y",
    "status",
    "error",
]

_stop_requested = False
_stop_signal = None
_active_process = None


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
    device = find_colorimeter(
        args.colorimeter_match,
        args.colorimeter_usb_id,
        args.usb_sysfs_root,
    )
    if device:
        print(f"colorimeter found: {device['usb_id']} {device['text']}",
              file=sys.stderr)
        return True

    print("error: i1 Display Pro USB colorimeter not found", file=sys.stderr)
    display.best_effort(f"set-metadata-color {args.alert_color}")
    display.set_overlay_text(args.sensor_not_found_text)
    return False


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


def parse_spotread_measurement(text):
    yxy_match = _YXY_RE.search(text)
    if not yxy_match:
        return None

    y_value, x_value, y_chromaticity = (
        float(item) for item in yxy_match.groups()
    )
    measurement = {
        "Y": y_value,
        "x": x_value,
        "y": y_chromaticity,
    }

    xyz_match = _XYZ_RE.search(text)
    if xyz_match:
        x_tristimulus, y_tristimulus, z_tristimulus = (
            float(item) for item in xyz_match.groups()
        )
        measurement.update({
            "X": x_tristimulus,
            "Y": y_tristimulus,
            "Z": z_tristimulus,
        })

    return measurement


def measure_spotread(timeout):
    try:
        out = run_process_capture(["spotread", "-x", "-O"], timeout)
    except InterruptedError:
        raise
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired,
            FileNotFoundError) as e:
        detail = getattr(e, "output", None) or str(e)
        return None, one_line(detail)

    measurement = parse_spotread_measurement(out)
    if measurement is None:
        return None, "spotread output did not contain Yxy"
    return measurement, ""


def as_float(value):
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    return result if math.isfinite(result) else None


def measurement_valid(measurement, min_nits):
    if not measurement:
        return False
    for key in ("X", "Y", "Z", "x", "y"):
        value = as_float(measurement.get(key))
        if value is None:
            return False
    return 0.0 <= measurement["x"] <= 1.0 and 0.0 <= measurement["y"] <= 1.0 and measurement["Y"] >= min_nits


def measure_valid_sample(args):
    last_error = ""
    for attempt in range(args.max_retries + 1):
        if args.require_colorimeter and not find_colorimeter(
            args.colorimeter_match,
            args.colorimeter_usb_id,
            args.usb_sysfs_root,
        ):
            last_error = "i1 Display Pro USB colorimeter disconnected"
            if attempt >= args.max_retries:
                return None, attempt, last_error
        else:
            measurement, error = measure_spotread(args.spotread_timeout)
            if measurement_valid(measurement, args.min_sample_nits):
                return measurement, attempt, ""
            last_error = error or f"invalid measurement below {args.min_sample_nits:.1f} nits"

        if attempt < args.max_retries:
            print(f"    retry {attempt + 1}/{args.max_retries}: {last_error}",
                  file=sys.stderr)
            if not interruptible_sleep(args.retry_sleep):
                raise InterruptedError("stop requested")

    return None, args.max_retries, last_error


def disptool_argv(args, command, value):
    argv = [
        args.disptool,
        "--device=fpga",
        f"--command={command}",
        f"--value={value}",
    ]
    if args.i2cdev:
        argv.insert(1, f"--i2cdev={args.i2cdev}")
    return argv


def write_white_point(args, command, value):
    argv = disptool_argv(args, command, value)
    try:
        out = run_process_capture(argv, args.disptool_timeout)
    except InterruptedError:
        raise
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired,
            FileNotFoundError) as e:
        detail = getattr(e, "output", None) or str(e)
        raise RuntimeError(f"{command}={value} write failed: {one_line(detail)}")
    if out.strip():
        print(f"disptool {command}={value}: {one_line(out, 200)}",
              file=sys.stderr)


def write_white_point_triplet(args, registers):
    for command in ("wpx", "wpy", "wpz"):
        write_white_point(args, command, int(registers[command]))


def parse_register_value(text):
    try:
        value = int(str(text), 0)
    except ValueError as e:
        raise argparse.ArgumentTypeError(str(e))
    if not 0 <= value <= 256:
        raise argparse.ArgumentTypeError("register value must be in [0, 256]")
    return value


def parse_value_list(text):
    values = []
    for part in str(text).split(","):
        stripped = part.strip()
        if not stripped:
            continue
        value = parse_register_value(stripped)
        if value not in values:
            values.append(value)
    if not values:
        raise argparse.ArgumentTypeError("value list must not be empty")
    return values


def parse_extra_point(text):
    parts = [part.strip() for part in str(text).split(",")]
    if len(parts) not in (3, 4):
        raise argparse.ArgumentTypeError(
            "point must be wpx,wpy,wpz or label,wpx,wpy,wpz"
        )

    if len(parts) == 3:
        values = [parse_register_value(part) for part in parts]
        label = f"extra-{values[0]}-{values[1]}-{values[2]}"
    else:
        label = parts[0] or "extra"
        values = [parse_register_value(part) for part in parts[1:]]
    return {
        "label": label,
        "registers": {"wpx": values[0], "wpy": values[1], "wpz": values[2]},
    }


def build_profile_points(args):
    base = {"wpx": 256, "wpy": 256, "wpz": 256}
    points = []
    seen = set()

    def add(label, registers):
        key = (registers["wpx"], registers["wpy"], registers["wpz"])
        if key in seen:
            return
        seen.add(key)
        points.append({
            "index": len(points) + 1,
            "label": label,
            "registers": dict(registers),
        })

    add("baseline", base)

    for channel in ("wpx", "wpy", "wpz"):
        for value in args.axis_values:
            if value == 256:
                continue
            registers = dict(base)
            registers[channel] = value
            add(f"{channel}-{value}", registers)

    if args.include_pairs:
        for first, second in (("wpx", "wpy"), ("wpx", "wpz"), ("wpy", "wpz")):
            registers = dict(base)
            registers[first] = args.pair_value
            registers[second] = args.pair_value
            add(f"{first}-{second}-{args.pair_value}", registers)

    if args.include_all:
        registers = {
            "wpx": args.pair_value,
            "wpy": args.pair_value,
            "wpz": args.pair_value,
        }
        add(f"all-{args.pair_value}", registers)

    for extra in args.point or []:
        add(extra["label"], extra["registers"])

    return points


def mean(values):
    return sum(values) / len(values) if values else None


def population_stddev(values):
    if len(values) < 2:
        return 0.0 if values else None
    avg = mean(values)
    return math.sqrt(sum((value - avg) ** 2 for value in values) / len(values))


def summarize_samples(samples):
    average = {}
    stddev = {}
    for key in ("X", "Y", "Z", "x", "y"):
        values = [sample[key] for sample in samples if key in sample]
        average[key] = mean(values)
        stddev[key] = population_stddev(values)

    xyz_sum = average["X"] + average["Y"] + average["Z"]
    if xyz_sum > 0:
        average["x_from_xyz"] = average["X"] / xyz_sum
        average["y_from_xyz"] = average["Y"] / xyz_sum
    else:
        average["x_from_xyz"] = None
        average["y_from_xyz"] = None

    return average, stddev


def format_float(value, precision=6):
    if value is None:
        return ""
    return f"{value:.{precision}f}"


class ProfileRecorder:
    def __init__(self, args, points):
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
            "script": "white-point-profile-child.py",
            "disptool": args.disptool,
            "pattern": args.pattern,
            "parameters": {
                "samples_per_point": args.samples,
                "settle_seconds": args.settle_seconds,
                "sample_gap_seconds": args.sample_gap_seconds,
                "max_retries": args.max_retries,
                "point_failure_budget": args.point_failure_budget,
                "spotread_timeout": args.spotread_timeout,
                "min_sample_nits": args.min_sample_nits,
                "placement_min_nits": args.placement_min_nits,
                "axis_values": args.axis_values,
                "include_pairs": args.include_pairs,
                "include_all": args.include_all,
                "pair_value": args.pair_value,
            },
            "outputs": {
                "json": self.json_path,
                "summary_csv": self.summary_path,
                "samples_csv": self.samples_path,
            },
            "points": [],
            "status": "running",
        }

    def __enter__(self):
        for path in (self.json_path, self.summary_path, self.samples_path):
            directory = os.path.dirname(os.path.abspath(path))
            if directory:
                os.makedirs(directory, exist_ok=True)

        self.summary_file = open(self.summary_path, "w", newline="")
        self.samples_file = open(self.samples_path, "w", newline="")
        self.summary_writer = csv.writer(self.summary_file)
        self.samples_writer = csv.writer(self.samples_file)
        self.summary_file.write("# disp-tester white-point profile summary\n")
        self.summary_file.write(f"# timestamp={self.data['created_at']}\n")
        self.summary_file.write(f"# json={self.json_path}\n")
        self.summary_writer.writerow(SUMMARY_CSV_HEADER)
        self.samples_file.write("# disp-tester white-point profile samples\n")
        self.samples_file.write(f"# timestamp={self.data['created_at']}\n")
        self.samples_file.write(f"# json={self.json_path}\n")
        self.samples_writer.writerow(SAMPLE_CSV_HEADER)
        self.flush()
        self.write_json()
        return self

    def __exit__(self, _exc_type, _exc, _tb):
        self.close()

    def add_point_result(self, point, samples, average, stddev,
                         status="ok", error=""):
        result = {
            "index": point["index"],
            "label": point["label"],
            "registers": dict(point["registers"]),
            "status": status,
            "error": error,
            "samples": samples,
            "average": average,
            "stddev": stddev,
        }
        self.data["points"].append(result)

        registers = point["registers"]
        self.summary_writer.writerow([
            point["index"],
            point["label"],
            registers["wpx"],
            registers["wpy"],
            registers["wpz"],
            len(samples),
            format_float(average.get("X")),
            format_float(average.get("Y")),
            format_float(average.get("Z")),
            format_float(average.get("x")),
            format_float(average.get("y")),
            format_float(average.get("x_from_xyz")),
            format_float(average.get("y_from_xyz")),
            format_float(stddev.get("x")),
            format_float(stddev.get("y")),
            format_float(stddev.get("Y")),
            status,
            error,
        ])
        self.flush()
        self.write_json()

    def write_sample_row(self, point, sample_index, sample, retries,
                         status="ok", error=""):
        registers = point["registers"]
        self.samples_writer.writerow([
            point["index"],
            point["label"],
            registers["wpx"],
            registers["wpy"],
            registers["wpz"],
            sample_index,
            sample.get("timestamp", now_iso()),
            format_float(sample.get("X")),
            format_float(sample.get("Y")),
            format_float(sample.get("Z")),
            format_float(sample.get("x")),
            format_float(sample.get("y")),
            retries,
            status,
            error,
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
    return os.path.join(record_dir, f"{ts}-white-point-profile")


def output_paths(args):
    prefix = args.output_prefix or default_output_prefix(args.record_dir)
    if prefix.endswith(".json"):
        prefix = prefix[:-5]
    return (
        prefix + ".json",
        prefix + "-summary.csv",
        prefix + "-samples.csv",
    )


def validate_output_paths(args):
    try:
        for path in output_paths(args):
            directory = os.path.dirname(os.path.abspath(path))
            if directory:
                os.makedirs(directory, exist_ok=True)
            with open(path, "a"):
                pass
        return True
    except OSError as e:
        print(f"error: cannot write profile outputs: {e}", file=sys.stderr)
        return False


def setup_display(display, args):
    if args.pattern:
        display.command(f"pattern {args.pattern}", required=True)
    display.best_effort("set-user-interaction disable")
    display.best_effort("set-metadata-status enable")
    display.best_effort(f"set-metadata-fontsize {args.progress_fontsize}")
    display.best_effort(f"set-metadata-align {args.metadata_align}")
    display.best_effort(f"set-metadata-color {args.normal_color}")
    display.set_overlay_text(args.initial_text)


def restore_display(display, args):
    if args.restore_user_interaction:
        display.best_effort("set-user-interaction enable")


def format_placement_message(template, args, nits=None):
    nits_text = f"{nits:.1f}" if nits is not None else "waiting"
    return template.format(
        nits=nits if nits is not None else 0.0,
        nits_text=nits_text,
        min_nits=args.placement_min_nits,
    )


def wait_for_colorimeter_placement(display, args):
    if not args.placement_check:
        return True

    display.best_effort(f"set-metadata-color {args.normal_color}")
    display.set_overlay_text(format_placement_message(args.placement_text, args))

    if not interruptible_sleep(args.placement_settle_seconds):
        return False

    start = time.monotonic()
    while not stop_requested():
        measurement, error = measure_spotread(args.spotread_timeout)
        nits = measurement.get("Y") if measurement else None
        if measurement_valid(measurement, args.placement_min_nits):
            display.set_overlay_text(format_placement_message(
                args.placement_success_text,
                args,
                nits=nits,
            ))
            print(f"placement check OK: {nits:.2f} nits", file=sys.stderr)
            return interruptible_sleep(args.placement_success_seconds)

        if error:
            print(f"placement read failed: {error}", file=sys.stderr)
        display.set_overlay_text(format_placement_message(
            args.placement_text,
            args,
            nits=nits,
        ))

        if args.placement_timeout_seconds > 0:
            elapsed = time.monotonic() - start
            if elapsed >= args.placement_timeout_seconds:
                print("error: colorimeter placement check timed out",
                      file=sys.stderr)
                return False

        if not interruptible_sleep(args.placement_retry_seconds):
            return False

    return False


def progress_message(template, point, total, sample_index, args, failures=0):
    registers = point["registers"]
    return template.format(
        point=point["index"],
        total=total,
        label=point["label"],
        wpx=registers["wpx"],
        wpy=registers["wpy"],
        wpz=registers["wpz"],
        sample=sample_index,
        samples=args.samples,
        failures=failures,
        failure_budget=args.point_failure_budget,
    )


def profile_point(display, recorder, point, total, args):
    registers = point["registers"]
    print(
        f"[{point['index']}/{total}] {point['label']} "
        f"wpx={registers['wpx']} wpy={registers['wpy']} wpz={registers['wpz']}",
        file=sys.stderr,
    )
    display.set_overlay_text(progress_message(args.progress_text, point, total, 0, args))
    write_white_point_triplet(args, registers)
    if not interruptible_sleep(args.settle_seconds):
        raise InterruptedError("stop requested")

    samples = []
    first_error = ""
    failures = 0
    attempts = 0

    while len(samples) < args.samples:
        sample_index = len(samples) + 1
        display.set_overlay_text(progress_message(
            args.progress_text,
            point,
            total,
            sample_index,
            args,
            failures=failures,
        ))
        measurement, retries, error = measure_valid_sample(args)
        if measurement is None:
            attempts += 1
            failures += 1
            first_error = error or "measurement failed"
            recorder.write_sample_row(
                point,
                sample_index,
                {},
                retries,
                status="fail",
                error=first_error,
            )
            print(
                f"    sample {sample_index}/{args.samples} failed "
                f"({failures}/{args.point_failure_budget}): {first_error}",
                file=sys.stderr,
            )
            if failures >= args.point_failure_budget:
                raise RuntimeError(
                    f"{point['label']} sample {sample_index} failed after "
                    f"{failures} failed read groups: {first_error}"
                )
            if not interruptible_sleep(args.sample_failure_sleep):
                raise InterruptedError("stop requested")
            continue

        attempts += 1
        measurement["timestamp"] = now_iso()
        measurement["sample_index"] = sample_index
        samples.append(measurement)
        recorder.write_sample_row(point, sample_index, measurement, retries)
        print(
            f"    sample {sample_index}/{args.samples}: "
            f"Y={measurement['Y']:.2f} x={measurement['x']:.5f} y={measurement['y']:.5f}",
            file=sys.stderr,
        )
        if sample_index < args.samples:
            if not interruptible_sleep(args.sample_gap_seconds):
                raise InterruptedError("stop requested")

    if not samples:
        raise RuntimeError(f"{point['label']} produced no valid samples: {first_error}")

    average, stddev = summarize_samples(samples)
    recorder.add_point_result(
        point,
        samples,
        average,
        stddev,
        status="ok" if failures == 0 else "ok-with-retries",
        error="" if failures == 0 else first_error,
    )
    print(
        f"    avg: Y={average['Y']:.2f} x={average['x']:.5f} y={average['y']:.5f} "
        f"std_x={stddev['x']:.6f} std_y={stddev['y']:.6f} "
        f"read_groups={attempts} failures={failures}",
        file=sys.stderr,
    )


def validate_args(args):
    if args.samples <= 0:
        print("error: --samples must be > 0", file=sys.stderr)
        return False
    if args.max_retries < 0:
        print("error: --max-retries must be >= 0", file=sys.stderr)
        return False
    if args.point_failure_budget <= 0:
        print("error: --point-failure-budget must be > 0", file=sys.stderr)
        return False
    if args.retry_sleep < 0:
        print("error: --retry-sleep must be >= 0", file=sys.stderr)
        return False
    if args.sample_failure_sleep < 0:
        print("error: --sample-failure-sleep must be >= 0", file=sys.stderr)
        return False
    if args.spotread_timeout <= 0:
        print("error: --spotread-timeout must be > 0", file=sys.stderr)
        return False
    if args.disptool_timeout <= 0:
        print("error: --disptool-timeout must be > 0", file=sys.stderr)
        return False
    if args.settle_seconds < 0 or args.sample_gap_seconds < 0:
        print("error: settle and sample gap must be >= 0", file=sys.stderr)
        return False
    if args.min_sample_nits <= 0 or args.placement_min_nits <= 0:
        print("error: nits thresholds must be > 0", file=sys.stderr)
        return False
    if not (8 <= args.progress_fontsize <= 48):
        print("error: --progress-fontsize must be in [8, 48]", file=sys.stderr)
        return False
    if args.metadata_align not in ("left", "center", "right"):
        print("error: --metadata-align must be left, center, or right",
              file=sys.stderr)
        return False
    if not os.path.isabs(args.disptool):
        print("error: --disptool must be an absolute path", file=sys.stderr)
        return False
    return True


def parse_args():
    parser = argparse.ArgumentParser(
        description="Profile FPGA wpx/wpy/wpz against i1Display Pro XYZ/xyY measurements.",
    )
    parser.add_argument("--disp-host", default=DEFAULT_DISP_HOST,
                        help="disp-tester host.")
    parser.add_argument("--disp-port", type=int, default=DEFAULT_DISP_PORT,
                        help="disp-tester TCP port.")
    parser.add_argument("--disp-timeout", type=float, default=1.0,
                        help="disp-tester command timeout.")
    parser.add_argument("--pattern", default="white",
                        help="disp-tester pattern to show while profiling.")
    parser.add_argument("--disptool", default=DEFAULT_DISPTOOL,
                        help="Absolute path to disptool.")
    parser.add_argument("--i2cdev", default="",
                        help="Optional disptool --i2cdev value.")
    parser.add_argument("--disptool-timeout", type=float, default=5.0,
                        help="Timeout for each disptool write.")
    parser.add_argument("--axis-values", type=parse_value_list,
                        default=parse_value_list("255,254,253,252,251,250,249,248,247,246"),
                        help="Comma-separated values used for each single-axis sweep.")
    parser.add_argument("--include-pairs", action=argparse.BooleanOptionalAction,
                        default=False,
                        help="Include pair reductions at --pair-value.")
    parser.add_argument("--include-all", action=argparse.BooleanOptionalAction,
                        default=False,
                        help="Include all-register reduction at --pair-value.")
    parser.add_argument("--pair-value", type=parse_register_value, default=224,
                        help="Register value used for pair/all interaction points.")
    parser.add_argument("--point", action="append", type=parse_extra_point,
                        help="Extra point as wpx,wpy,wpz or label,wpx,wpy,wpz. Can be repeated.")
    parser.add_argument("--samples", type=int, default=5,
                        help="Valid spotread samples per profile point.")
    parser.add_argument("--settle-seconds", type=float, default=1.5,
                        help="Seconds to wait after each register triplet write.")
    parser.add_argument("--sample-gap-seconds", type=float, default=0.2,
                        help="Seconds between valid spotread samples.")
    parser.add_argument("--max-retries", type=int, default=5,
                        help="spotread retries for one read group before that group is counted as failed.")
    parser.add_argument("--point-failure-budget", type=int, default=8,
                        help="Failed read groups allowed per profile point while collecting valid samples.")
    parser.add_argument("--retry-sleep", type=float, default=0.8,
                        help="Seconds between spotread retries within one read group.")
    parser.add_argument("--sample-failure-sleep", type=float, default=2.0,
                        help="Seconds to wait after a failed read group before trying again.")
    parser.add_argument("--spotread-timeout", type=float, default=15.0,
                        help="Timeout for each spotread invocation.")
    parser.add_argument("--min-sample-nits", type=float, default=5.0,
                        help="Minimum Y/nits accepted as a valid profile sample.")
    parser.add_argument("--fail-point-on-sample-error", action=argparse.BooleanOptionalAction,
                        default=False,
                        help="Deprecated; points now retry until enough valid samples are collected or --point-failure-budget is exhausted.")
    parser.add_argument("--placement-check", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Wait until spotread sees full-white target placement.")
    parser.add_argument("--placement-min-nits", type=float, default=50.0,
                        help="Minimum Y/nits accepted for target placement.")
    parser.add_argument("--placement-settle-seconds", type=float, default=1.0,
                        help="Seconds to wait after showing white before placement reads.")
    parser.add_argument("--placement-retry-seconds", type=float, default=2.0,
                        help="Seconds between placement reads.")
    parser.add_argument("--placement-success-seconds", type=float, default=1.0,
                        help="Seconds to show placement success before profiling.")
    parser.add_argument("--placement-timeout-seconds", type=float, default=0.0,
                        help="Abort placement check after this many seconds. 0 waits until exit.")
    parser.add_argument("--record-dir", default=DEFAULT_RECORD_DIR,
                        help="Directory for default profile outputs.")
    parser.add_argument("--output-prefix",
                        help="Output prefix. Writes .json, -summary.csv, and -samples.csv.")
    parser.add_argument("--initial-text", default=DEFAULT_INITIAL_TEXT,
                        help="Initial overlay text.")
    parser.add_argument("--sensor-not-found-text", default=DEFAULT_SENSOR_NOT_FOUND_TEXT,
                        help="Overlay text when the i1 Display Pro is not found.")
    parser.add_argument("--placement-text", default=DEFAULT_PLACEMENT_TEXT,
                        help="Overlay shown while waiting for target placement.")
    parser.add_argument("--placement-success-text", default=DEFAULT_PLACEMENT_SUCCESS_TEXT,
                        help="Overlay shown after placement succeeds.")
    parser.add_argument("--progress-text", default=DEFAULT_PROGRESS_TEXT,
                        help="Overlay shown while profiling.")
    parser.add_argument("--complete-text", default=DEFAULT_COMPLETE_TEXT,
                        help="Overlay shown after profiling completes.")
    parser.add_argument("--abort-text", default=DEFAULT_ABORT_TEXT,
                        help="Overlay shown when profiling aborts.")
    parser.add_argument("--progress-fontsize", type=int, default=24,
                        help="Overlay font size.")
    parser.add_argument("--metadata-align", default="left",
                        help="Overlay alignment: left, center, or right.")
    parser.add_argument("--normal-color", default="white",
                        help="Overlay color during normal operation.")
    parser.add_argument("--alert-color", default="red",
                        help="Overlay color for errors.")
    parser.add_argument("--require-colorimeter", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Require an i1 Display Pro USB colorimeter.")
    parser.add_argument("--colorimeter-match", action="append",
                        default=list(DEFAULT_COLORIMETER_MATCH),
                        help="USB manufacturer/product substring accepted as a colorimeter.")
    parser.add_argument("--colorimeter-usb-id", action="append",
                        default=list(DEFAULT_COLORIMETER_USB_IDS),
                        help="USB vendor:product ID accepted as a colorimeter.")
    parser.add_argument("--usb-sysfs-root", default="/sys/bus/usb/devices",
                        help="USB sysfs root for colorimeter detection.")
    parser.add_argument("--restore-white-point", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Restore wpx/wpy/wpz to 256 on exit.")
    parser.add_argument("--restore-user-interaction", action=argparse.BooleanOptionalAction,
                        default=False,
                        help="Re-enable disp-tester touch navigation on exit.")
    return parser.parse_args()


def main():
    install_signal_handlers()
    args = parse_args()
    if not validate_args(args):
        return 2

    display = DispTesterClient(args.disp_host, args.disp_port, args.disp_timeout)
    points = build_profile_points(args)
    recorder = None
    completed_points = 0
    aborted_reason = ""
    exit_code = 0

    try:
        setup_display(display, args)

        if not require_colorimeter(display, args):
            aborted_reason = "i1 Display Pro USB colorimeter not found"
            exit_code = 3
            raise RuntimeError(aborted_reason)

        print("resetting white point to 256/256/256 before profiling",
              file=sys.stderr)
        write_white_point_triplet(args, {"wpx": 256, "wpy": 256, "wpz": 256})

        if not wait_for_colorimeter_placement(display, args):
            aborted_reason = "colorimeter placement check failed"
            raise RuntimeError(aborted_reason)

        if not validate_output_paths(args):
            aborted_reason = "cannot write profile outputs"
            exit_code = 4
            raise RuntimeError(aborted_reason)

        recorder = ProfileRecorder(args, points)
        recorder.__enter__()

        print(f"profile points: {len(points)}", file=sys.stderr)
        print(f"outputs: {recorder.json_path}", file=sys.stderr)

        for point in points:
            if stop_requested():
                aborted_reason = f"signal {_stop_signal}"
                break
            profile_point(display, recorder, point, len(points), args)
            completed_points += 1

        if not aborted_reason:
            recorder.set_status("complete")

    except InterruptedError:
        aborted_reason = f"signal {_stop_signal}"
    except Exception as e:
        aborted_reason = str(e)
        print(f"error: {e}", file=sys.stderr)
    finally:
        if args.restore_white_point:
            try:
                print("restoring white point to 256/256/256", file=sys.stderr)
                write_white_point_triplet(args, {"wpx": 256, "wpy": 256, "wpz": 256})
            except Exception as e:
                restore_error = f"white-point restore failed: {one_line(e)}"
                print(f"warning: {restore_error}", file=sys.stderr)
                if not aborted_reason:
                    aborted_reason = restore_error

        if recorder:
            if aborted_reason:
                recorder.set_status("aborted", aborted_reason)
            recorder.__exit__(None, None, None)

        if not stop_requested():
            if aborted_reason:
                display.best_effort(f"set-metadata-color {args.alert_color}")
                if args.abort_text:
                    display.set_overlay_text(args.abort_text.format(
                        points=completed_points,
                        total=len(points),
                        reason=aborted_reason,
                        json_output=recorder.json_path if recorder else "",
                    ))
            elif args.complete_text:
                display.best_effort(f"set-metadata-color {args.normal_color}")
                display.set_overlay_text(args.complete_text.format(
                    points=completed_points,
                    total=len(points),
                    json_output=recorder.json_path if recorder else "",
                ))

        restore_display(display, args)

    if stop_requested():
        print("stopped by signal; partial profile preserved", file=sys.stderr)
        return signal_exit_code()

    if aborted_reason:
        print(f"profile aborted: {aborted_reason}", file=sys.stderr)
        return exit_code or 1

    print("white-point profile complete", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
