#!/usr/bin/env python3
"""
white-point-match-child.py

Child script for disp-tester that matches the target display white point to a
reference display using an i1 Display Pro and the FPGA wpx/wpy/wpz controls.

The Pi HDMI output is expected to feed both displays. The script shows a white
pattern through the parent disp-tester, waits for the operator to press Start
before each reference/target measurement phase, then adjusts only target FPGA
white-point registers. Brightness is not matched.
"""

import argparse
import csv
import datetime
import fcntl
import json
import math
import os
import re
import select
import signal
import socket
import subprocess
import sys
import time


DEFAULT_DISP_HOST = os.environ.get("DISP_TESTER_HOST", "127.0.0.1")
DEFAULT_DISP_PORT = int(os.environ.get("DISP_TESTER_PORT", "8082"))
DEFAULT_DISPTOOL = "/home/pi/micropanel/bin/disptool"
DEFAULT_PROFILE_JSON = "/home/pi/test-data/wp-profile.json"
DEFAULT_CALIBRATION_OUTPUT = (
    "/home/pi/als-dimmer/etc/als-dimmer/calibrations/"
    "white-point-calibration.json"
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
DEFAULT_MODEL = {
    "wpx": {"x": 0.00155369, "y": 0.00263626},
    "wpy": {"x": 0.00003638, "y": -0.00138203},
    "wpz": {"x": -0.00070231, "y": -0.00016416},
}

DEFAULT_INITIAL_TEXT = (
    "Color Matching\n"
    "Starting..."
)
DEFAULT_SENSOR_NOT_FOUND_TEXT = (
    "Color Matching\n"
    "i1 Display Pro Not Found\n"
    "Connect USB colorimeter"
)
DEFAULT_REFERENCE_READY_TEXT = (
    "Place the Sensor on Reference Display and Click Start button"
)
DEFAULT_TARGET_READY_TEXT = (
    "Now connect Sensor on Target Display and Click Start button"
)
DEFAULT_SAMPLING_TEXT = (
    "Color Matching\n"
    "{stage}\n"
    "Sample {sample}/{samples}  Retry {failures}/{failure_budget}"
)
DEFAULT_MATCHING_TEXT = (
    "Color Matching\n"
    "Iteration {iteration}/{max_iterations}\n"
    "wpx={wpx} wpy={wpy} wpz={wpz}\n"
    "dx={dx:+.5f} dy={dy:+.5f}"
)
DEFAULT_COMPLETE_TEXT = (
    "Color Matching done!\n"
    "wpx={wpx} wpy={wpy} wpz={wpz}\n"
    "dx={dx:+.5f} dy={dy:+.5f}\n"
    "Press Start or EXIT"
)
DEFAULT_BEST_EFFORT_TEXT = (
    "Color Matching best effort\n"
    "wpx={wpx} wpy={wpy} wpz={wpz}\n"
    "dx={dx:+.5f} dy={dy:+.5f}\n"
    "{save_status}\n"
    "Press Start or EXIT"
)
DEFAULT_ABORT_TEXT = (
    "Color Matching Aborted\n"
    "{reason}\n"
    "Press Restart or EXIT"
)

_FLOAT_RE = r"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)"
_XYZ_RE = re.compile(rf"Result is XYZ:\s*{_FLOAT_RE}\s+{_FLOAT_RE}\s+{_FLOAT_RE}")
_YXY_RE = re.compile(rf"Yxy:\s*{_FLOAT_RE}\s+{_FLOAT_RE}\s+{_FLOAT_RE}")
CALIBRATION_SCHEMA = "als-dimmer-white-point-calibration-v1"
COMMANDS = ("wpx", "wpy", "wpz")

_stop_requested = False
_stop_signal = None
_active_process = None


class MatchAborted(RuntimeError):
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
            print(f"warning: ignoring invalid child control line: {line.strip()}",
                  file=sys.stderr)
            return
        if message.get("command") != "set_recording":
            print(f"warning: ignoring child control command: {message}",
                  file=sys.stderr)
            return
        enabled = bool(message.get("enabled"))
        if enabled:
            self.restart_requested = True
        else:
            self.abort_requested = True


def interruptible_sleep(seconds, control=None):
    end = time.monotonic() + max(0.0, seconds)
    while not stop_requested():
        if control:
            control.poll()
            if control.abort_requested:
                raise MatchAborted("operator requested abort")
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


def normalize_calibration_output(path):
    if path == "etc/als-dimmer/calibrations/white-point-calibration.json":
        return DEFAULT_CALIBRATION_OUTPUT

    duplicate_prefix = "/home/pi/als-dimmer/etc/als-dimmer/etc/als-dimmer/"
    canonical_prefix = "/home/pi/als-dimmer/etc/als-dimmer/"
    while path.startswith(duplicate_prefix):
        path = canonical_prefix + path[len(duplicate_prefix):]
    return path


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
                    raise MatchAborted("operator requested abort")
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


def measure_spotread(timeout, control=None):
    try:
        out = run_process_capture(["spotread", "-x", "-O"], timeout, control)
    except (InterruptedError, MatchAborted):
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
    return (
        0.0 <= measurement["x"] <= 1.0 and
        0.0 <= measurement["y"] <= 1.0 and
        measurement["Y"] >= min_nits
    )


def measure_valid_sample(args, control, min_nits):
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
            measurement, error = measure_spotread(args.spotread_timeout, control)
            if measurement_valid(measurement, min_nits):
                return measurement, attempt, ""
            last_error = error or f"invalid measurement below {min_nits:.1f} nits"

        if attempt < args.max_retries:
            print(f"    retry {attempt + 1}/{args.max_retries}: {last_error}",
                  file=sys.stderr)
            if not interruptible_sleep(args.retry_sleep, control):
                raise InterruptedError("stop requested")

    return None, args.max_retries, last_error


def average_samples(samples):
    average = {}
    stddev = {}
    for key in ("X", "Y", "Z", "x", "y"):
        values = [sample[key] for sample in samples if key in sample]
        if not values:
            average[key] = None
            stddev[key] = None
            continue
        avg = sum(values) / len(values)
        average[key] = avg
        if len(values) < 2:
            stddev[key] = 0.0
        else:
            stddev[key] = math.sqrt(
                sum((value - avg) ** 2 for value in values) / len(values)
            )

    xyz_sum = average["X"] + average["Y"] + average["Z"]
    if xyz_sum > 0:
        average["x_from_xyz"] = average["X"] / xyz_sum
        average["y_from_xyz"] = average["Y"] / xyz_sum
    return average, stddev


def collect_average(display, args, control, stage, min_nits):
    samples = []
    failures = 0
    first_error = ""

    while len(samples) < args.samples:
        sample_index = len(samples) + 1
        display.set_overlay_text(args.sampling_text.format(
            stage=stage,
            sample=sample_index,
            samples=args.samples,
            failures=failures,
            failure_budget=args.point_failure_budget,
        ))

        measurement, retries, error = measure_valid_sample(args, control, min_nits)
        if measurement is None:
            failures += 1
            first_error = error or "measurement failed"
            print(
                f"    {stage} sample {sample_index}/{args.samples} failed "
                f"({failures}/{args.point_failure_budget}): {first_error}",
                file=sys.stderr,
            )
            if failures >= args.point_failure_budget:
                raise RuntimeError(f"{stage} failed: {first_error}")
            interruptible_sleep(args.sample_failure_sleep, control)
            continue

        measurement["timestamp"] = now_iso()
        measurement["sample_index"] = sample_index
        measurement["retries"] = retries
        samples.append(measurement)
        print(
            f"    {stage} sample {sample_index}/{args.samples}: "
            f"Y={measurement['Y']:.2f} x={measurement['x']:.5f} y={measurement['y']:.5f}",
            file=sys.stderr,
        )
        if sample_index < args.samples:
            interruptible_sleep(args.sample_gap_seconds, control)

    average, stddev = average_samples(samples)
    return {
        "average": average,
        "stddev": stddev,
        "samples": samples,
        "failures": failures,
        "first_error": first_error,
    }


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


def write_white_point(args, command, value, control=None):
    argv = disptool_argv(args, command, value)
    try:
        out = run_process_capture(argv, args.disptool_timeout, control)
    except (InterruptedError, MatchAborted):
        raise
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired,
            FileNotFoundError) as e:
        detail = getattr(e, "output", None) or str(e)
        raise RuntimeError(f"{command}={value} write failed: {one_line(detail)}")
    if out.strip():
        print(f"disptool {command}={value}: {one_line(out, 200)}",
              file=sys.stderr)


def write_white_point_triplet(args, registers, control=None):
    for command in COMMANDS:
        write_white_point(args, command, int(registers[command]), control)


def reductions_to_registers(reductions):
    return {command: 256 - int(reductions[command]) for command in COMMANDS}


def registers_to_reductions(registers):
    return {command: 256 - int(registers[command]) for command in COMMANDS}


def xy_error(reference, target):
    dx = target["x"] - reference["x"]
    dy = target["y"] - reference["y"]
    distance = math.sqrt(dx * dx + dy * dy)
    return dx, dy, distance


def load_profile_model(path):
    if not path:
        return dict(DEFAULT_MODEL), "built-in"
    if not os.path.exists(path):
        return dict(DEFAULT_MODEL), "built-in"

    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"warning: cannot load profile model {path}: {e}", file=sys.stderr)
        return dict(DEFAULT_MODEL), "built-in"

    points = data.get("points", [])
    baseline = next(
        (point for point in points
         if point.get("registers", {}) == {"wpx": 256, "wpy": 256, "wpz": 256}),
        None,
    )
    if not baseline:
        return dict(DEFAULT_MODEL), "built-in"

    base_avg = baseline.get("average", {})
    model = {}
    for command in COMMANDS:
        profile_points = []
        for point in points:
            registers = point.get("registers", {})
            if registers.get(command) == 256:
                continue
            if any(registers.get(other) != 256 for other in COMMANDS if other != command):
                continue
            profile_points.append(point)
        if not profile_points:
            return dict(DEFAULT_MODEL), "built-in"

        ds = [256 - int(point["registers"][command]) for point in profile_points]
        denom = sum(d * d for d in ds)
        if denom <= 0:
            return dict(DEFAULT_MODEL), "built-in"

        slopes = {}
        for key in ("x", "y"):
            slopes[key] = sum(
                d * (point["average"][key] - base_avg[key])
                for d, point in zip(ds, profile_points)
            ) / denom
        model[command] = slopes

    return model, path


def predicted_xy(current_xy, current_reductions, candidate_reductions, model):
    x = current_xy["x"]
    y = current_xy["y"]
    for command in COMMANDS:
        delta = candidate_reductions[command] - current_reductions[command]
        x += model[command]["x"] * delta
        y += model[command]["y"] * delta
    return {"x": x, "y": y}


def solve_candidate(reference_xy, current_xy, current_reductions, model, args):
    best = None
    max_reduction = args.max_reduction
    for rx in range(max_reduction + 1):
        for ry in range(max_reduction + 1):
            for rz in range(max_reduction + 1):
                candidate = {"wpx": rx, "wpy": ry, "wpz": rz}
                pred = predicted_xy(current_xy, current_reductions, candidate, model)
                dx = pred["x"] - reference_xy["x"]
                dy = pred["y"] - reference_xy["y"]
                distance = math.sqrt(dx * dx + dy * dy)
                movement = sum(abs(candidate[c] - current_reductions[c]) for c in COMMANDS)
                score = distance + movement * 1e-7
                if best is None or score < best["score"]:
                    best = {
                        "reductions": candidate,
                        "registers": reductions_to_registers(candidate),
                        "predicted": pred,
                        "predicted_dx": dx,
                        "predicted_dy": dy,
                        "predicted_distance": distance,
                        "score": score,
                    }
    return best


def wait_for_start(display, control, message):
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


def calibration_payload(args, model, model_source, reference, target_initial,
                        iterations, final_registers, status, final_error):
    return {
        "schema": CALIBRATION_SCHEMA,
        "created_at": now_iso(),
        "status": status,
        "wpx": int(final_registers["wpx"]),
        "wpy": int(final_registers["wpy"]),
        "wpz": int(final_registers["wpz"]),
        "tolerance_xy": args.tolerance_xy,
        "final_dx": final_error["dx"],
        "final_dy": final_error["dy"],
        "final_distance": final_error["distance"],
        "reference": reference,
        "target_initial": target_initial,
        "iterations": iterations,
        "model": {
            "source": model_source,
            "slopes": model,
            "max_reduction": args.max_reduction,
        },
    }


def write_calibration(path, payload):
    directory = os.path.dirname(os.path.abspath(path))
    if directory:
        os.makedirs(directory, exist_ok=True)
    tmp_path = path + ".tmp"
    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True)
        f.write("\n")
    os.replace(tmp_path, path)


def setup_display(display, args):
    if args.pattern:
        display.command(f"pattern {args.pattern}", required=True)
    display.best_effort("set-metadata-status enable")
    display.best_effort(f"set-metadata-fontsize {args.progress_fontsize}")
    display.best_effort(f"set-metadata-align {args.metadata_align}")
    display.best_effort(f"set-metadata-color {args.normal_color}")
    display.set_overlay_text(args.initial_text)


def run_match(display, control, args, model, model_source):
    control.reset_for_run()
    display.set_child_action_active(False)
    display.best_effort(f"set-metadata-color {args.normal_color}")

    print("resetting white point to 256/256/256 before reference",
          file=sys.stderr)
    write_white_point_triplet(args, {"wpx": 256, "wpy": 256, "wpz": 256}, control)
    interruptible_sleep(args.settle_seconds, control)

    if not wait_for_start(display, control, args.reference_ready_text):
        raise MatchAborted("reference start interrupted")

    reference = collect_average(
        display,
        args,
        control,
        "Measuring reference display",
        args.min_sample_nits,
    )
    reference_xy = reference["average"]

    if not wait_for_start(display, control, args.target_ready_text):
        raise MatchAborted("target start interrupted")

    print("resetting target white point to 256/256/256 before target measurement",
          file=sys.stderr)
    write_white_point_triplet(args, {"wpx": 256, "wpy": 256, "wpz": 256}, control)
    interruptible_sleep(args.settle_seconds, control)

    target_initial = collect_average(
        display,
        args,
        control,
        "Measuring target display",
        args.min_sample_nits,
    )

    current_reductions = {"wpx": 0, "wpy": 0, "wpz": 0}
    current_xy = target_initial["average"]
    current_registers = reductions_to_registers(current_reductions)
    best_registers = dict(current_registers)
    best_result = target_initial
    best_dx, best_dy, best_distance = xy_error(reference_xy, current_xy)
    iterations = []

    print(
        f"initial target error: dx={best_dx:+.6f} dy={best_dy:+.6f} "
        f"distance={best_distance:.6f}",
        file=sys.stderr,
    )

    if best_distance <= args.tolerance_xy:
        status = "matched"
    else:
        status = "not_matched"

    max_iterations = max(1, args.max_corrections)
    for iteration in range(1, max_iterations + 1):
        if best_distance <= args.tolerance_xy:
            break

        candidate = solve_candidate(
            reference_xy,
            current_xy,
            current_reductions,
            model,
            args,
        )
        if candidate["reductions"] == current_reductions:
            print("model found no improving register move; stopping corrections",
                  file=sys.stderr)
            break
        registers = candidate["registers"]

        display.set_overlay_text(args.matching_text.format(
            iteration=iteration,
            max_iterations=max_iterations,
            wpx=registers["wpx"],
            wpy=registers["wpy"],
            wpz=registers["wpz"],
            dx=candidate["predicted_dx"],
            dy=candidate["predicted_dy"],
        ))

        print(
            f"iteration {iteration}: writing wpx={registers['wpx']} "
            f"wpy={registers['wpy']} wpz={registers['wpz']} "
            f"predicted dx={candidate['predicted_dx']:+.6f} "
            f"dy={candidate['predicted_dy']:+.6f}",
            file=sys.stderr,
        )

        write_white_point_triplet(args, registers, control)
        interruptible_sleep(args.settle_seconds, control)
        measured = collect_average(
            display,
            args,
            control,
            f"Measuring candidate {iteration}",
            args.min_sample_nits,
        )
        dx, dy, distance = xy_error(reference_xy, measured["average"])
        iterations.append({
            "iteration": iteration,
            "candidate": candidate,
            "measured": measured,
            "dx": dx,
            "dy": dy,
            "distance": distance,
        })

        print(
            f"iteration {iteration}: measured dx={dx:+.6f} dy={dy:+.6f} "
            f"distance={distance:.6f}",
            file=sys.stderr,
        )

        current_reductions = dict(candidate["reductions"])
        current_registers = dict(registers)
        current_xy = measured["average"]

        if distance < best_distance:
            best_distance = distance
            best_dx = dx
            best_dy = dy
            best_registers = dict(registers)
            best_result = measured

        if distance <= args.tolerance_xy:
            status = "matched"
            break

    if status != "matched":
        # Restore the best measured point before reporting. This can be the
        # unadjusted baseline if no candidate improved the match.
        write_white_point_triplet(args, best_registers, control)
        interruptible_sleep(args.settle_seconds, control)

    final_error = {
        "dx": best_dx,
        "dy": best_dy,
        "distance": best_distance,
    }
    should_write = status == "matched" or args.write_best_effort
    payload_status = status
    if status != "matched" and should_write:
        payload_status = "best_effort"
    payload = calibration_payload(
        args,
        model,
        model_source,
        reference,
        target_initial,
        iterations,
        best_registers,
        payload_status,
        final_error,
    )
    payload["target_final"] = best_result

    if should_write:
        write_calibration(args.calibration_output, payload)
        print(f"wrote calibration: {args.calibration_output}", file=sys.stderr)

    display.set_child_action_active(False)
    if status == "matched":
        display.best_effort(f"set-metadata-color {args.normal_color}")
        display.set_overlay_text(args.complete_text.format(
            wpx=best_registers["wpx"],
            wpy=best_registers["wpy"],
            wpz=best_registers["wpz"],
            dx=best_dx,
            dy=best_dy,
            distance=best_distance,
            output=args.calibration_output,
        ))
    else:
        display.best_effort(f"set-metadata-color {args.alert_color}")
        display.set_overlay_text(args.best_effort_text.format(
            wpx=best_registers["wpx"],
            wpy=best_registers["wpy"],
            wpz=best_registers["wpz"],
            dx=best_dx,
            dy=best_dy,
            distance=best_distance,
            output=args.calibration_output,
            save_status="Calibration saved" if should_write else "Calibration not written",
        ))

    return status == "matched"


def idle_until_restart(control):
    control.abort_requested = False
    control.restart_requested = False
    while not stop_requested():
        control.poll()
        if control.restart_requested:
            return True
        time.sleep(0.2)
    return False


def parse_int_auto(value):
    try:
        return int(str(value), 0)
    except ValueError as e:
        raise argparse.ArgumentTypeError(str(e))


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
    if args.max_reduction < 0 or args.max_reduction > 256:
        print("error: --max-reduction must be in [0, 256]", file=sys.stderr)
        return False
    if args.max_corrections <= 0:
        print("error: --max-corrections must be > 0", file=sys.stderr)
        return False
    if args.tolerance_xy <= 0:
        print("error: --tolerance-xy must be > 0", file=sys.stderr)
        return False
    if args.retry_sleep < 0 or args.sample_failure_sleep < 0:
        print("error: retry sleeps must be >= 0", file=sys.stderr)
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
    if args.min_sample_nits <= 0:
        print("error: --min-sample-nits must be > 0", file=sys.stderr)
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
    if not os.path.isabs(args.calibration_output):
        print("error: --calibration-output must be an absolute path",
              file=sys.stderr)
        return False
    return True


def parse_args():
    parser = argparse.ArgumentParser(
        description="Match target white point to a reference display.",
    )
    parser.add_argument("--disp-host", default=DEFAULT_DISP_HOST)
    parser.add_argument("--disp-port", type=int, default=DEFAULT_DISP_PORT)
    parser.add_argument("--disp-timeout", type=float, default=1.0)
    parser.add_argument("--pattern", default="white")
    parser.add_argument("--disptool", default=DEFAULT_DISPTOOL)
    parser.add_argument("--i2cdev", default="")
    parser.add_argument("--disptool-timeout", type=float, default=5.0)
    parser.add_argument("--profile-json", default=DEFAULT_PROFILE_JSON,
                        help="Optional white-point profile JSON for model slopes.")
    parser.add_argument("--calibration-output", default=DEFAULT_CALIBRATION_OUTPUT,
                        help="Calibration JSON written on matched and best-effort runs.")
    parser.add_argument("--write-best-effort", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Write calibration JSON even when tolerance is not reached.")
    parser.add_argument("--samples", type=int, default=5,
                        help="Valid spotread samples per average.")
    parser.add_argument("--settle-seconds", type=float, default=1.5)
    parser.add_argument("--sample-gap-seconds", type=float, default=0.2)
    parser.add_argument("--max-retries", type=int, default=5)
    parser.add_argument("--point-failure-budget", type=int, default=8)
    parser.add_argument("--retry-sleep", type=float, default=0.8)
    parser.add_argument("--sample-failure-sleep", type=float, default=2.0)
    parser.add_argument("--spotread-timeout", type=float, default=15.0)
    parser.add_argument("--min-sample-nits", type=float, default=5.0)
    parser.add_argument("--tolerance-xy", type=float, default=0.002)
    parser.add_argument("--max-reduction", type=parse_int_auto, default=10,
                        help="Maximum per-register reduction searched from 256.")
    parser.add_argument("--max-corrections", type=int, default=3,
                        help="Maximum measured correction iterations.")
    parser.add_argument("--initial-text", default=DEFAULT_INITIAL_TEXT)
    parser.add_argument("--sensor-not-found-text", default=DEFAULT_SENSOR_NOT_FOUND_TEXT)
    parser.add_argument("--reference-ready-text", default=DEFAULT_REFERENCE_READY_TEXT)
    parser.add_argument("--target-ready-text", default=DEFAULT_TARGET_READY_TEXT)
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
                        default=True)
    parser.add_argument("--colorimeter-match", action="append",
                        default=list(DEFAULT_COLORIMETER_MATCH))
    parser.add_argument("--colorimeter-usb-id", action="append",
                        default=list(DEFAULT_COLORIMETER_USB_IDS))
    parser.add_argument("--usb-sysfs-root", default="/sys/bus/usb/devices")
    parser.add_argument("--exit-after-match", action="store_true",
                        help="Exit child after one run instead of waiting for restart.")
    args = parser.parse_args()
    args.calibration_output = normalize_calibration_output(args.calibration_output)
    return args


def main():
    install_signal_handlers()
    args = parse_args()
    if not validate_args(args):
        return 2

    display = DispTesterClient(args.disp_host, args.disp_port, args.disp_timeout)
    control = ControlPipe()
    model, model_source = load_profile_model(args.profile_json)
    print(f"white-point model source: {model_source}", file=sys.stderr)
    print(f"white-point model slopes: {model}", file=sys.stderr)

    try:
        setup_display(display, args)
        if not require_colorimeter(display, args):
            return 3

        while not stop_requested():
            try:
                run_match(display, control, args, model, model_source)
            except MatchAborted as e:
                print(f"match aborted: {e}", file=sys.stderr)
                display.set_child_action_active(False)
                display.best_effort(f"set-metadata-color {args.alert_color}")
                display.set_overlay_text(args.abort_text.format(reason=str(e)))
            except InterruptedError:
                raise
            except Exception as e:
                print(f"match failed: {e}", file=sys.stderr)
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
