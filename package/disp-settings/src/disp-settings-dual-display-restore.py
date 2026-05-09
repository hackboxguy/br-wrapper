#!/usr/bin/env python3

import json
import math
import os
import pwd
import socket
import subprocess
import sys
import tempfile
import time


STATE_DIR = "/var/lib/disp-settings"
STATE_FILE = os.path.join(STATE_DIR, "dual-display-mode.json")
SECONDARY_SERVICE = "als-dimmer-pwm.service"
PRIMARY_PORT = 9000
SECONDARY_PORT = 9001
MIN_NITS = 20.0
NITS_STEP = 5.0
CONNECT_TIMEOUT_SECONDS = 1.0
STARTUP_WAIT_SECONDS = 15.0


def log(message):
    print(f"disp-settings-dual-display-restore: {message}", flush=True)


def ensure_state_dir():
    os.makedirs(STATE_DIR, exist_ok=True)
    try:
        pi_user = pwd.getpwnam("pi")
        os.chown(STATE_DIR, pi_user.pw_uid, pi_user.pw_gid)
    except KeyError:
        pass
    except PermissionError as exc:
        log(f"warning: failed to chown {STATE_DIR}: {exc}")
    os.chmod(STATE_DIR, 0o755)


def load_state():
    ensure_state_dir()
    if not os.path.exists(STATE_FILE):
        log("no saved dual-display state; nothing to restore")
        return None

    with open(STATE_FILE, "r", encoding="utf-8") as state_file:
        state = json.load(state_file)

    if not state.get("enabled", True):
        log("saved dual-display state is disabled; nothing to restore")
        return None

    try:
        nits = float(state["last_nits"])
    except (KeyError, TypeError, ValueError):
        log("saved dual-display state has no valid last_nits; skipping restore")
        return None

    if not math.isfinite(nits):
        log("saved dual-display state has non-finite last_nits; skipping restore")
        return None

    state["last_nits"] = nits
    return state


def save_state(state):
    ensure_state_dir()
    fd, tmp_path = tempfile.mkstemp(prefix=".dual-display-mode.",
                                   suffix=".json", dir=STATE_DIR)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as tmp_file:
            json.dump(state, tmp_file, separators=(",", ":"))
            tmp_file.write("\n")
        os.replace(tmp_path, STATE_FILE)
        try:
            pi_user = pwd.getpwnam("pi")
            os.chown(STATE_FILE, pi_user.pw_uid, pi_user.pw_gid)
        except KeyError:
            pass
        os.chmod(STATE_FILE, 0o644)
    finally:
        if os.path.exists(tmp_path):
            os.unlink(tmp_path)


def call_json(port, command, params=None):
    request = {"version": "1.0", "command": command}
    if params:
        request["params"] = params

    payload = (json.dumps(request, separators=(",", ":")) + "\n").encode("utf-8")
    with socket.create_connection(("127.0.0.1", port),
                                  timeout=CONNECT_TIMEOUT_SECONDS) as sock:
        sock.settimeout(CONNECT_TIMEOUT_SECONDS)
        sock.sendall(payload)
        response = bytearray()
        while b"\n" not in response:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response.extend(chunk)

    if not response:
        raise RuntimeError(f"port {port} did not respond to {command}")

    line = bytes(response).split(b"\n", 1)[0]
    reply = json.loads(line.decode("utf-8"))
    if reply.get("status") != "success":
        message = reply.get("message", "command failed")
        raise RuntimeError(f"port {port} {command} failed: {message}")

    return reply.get("data", {})


def wait_for_port(port):
    deadline = time.monotonic() + STARTUP_WAIT_SECONDS
    last_error = None
    while time.monotonic() < deadline:
        try:
            return call_json(port, "get_status")
        except (OSError, RuntimeError, json.JSONDecodeError) as exc:
            last_error = exc
            time.sleep(0.25)
    raise RuntimeError(f"port {port} not ready: {last_error}")


def start_secondary_service():
    subprocess.run(["systemctl", "start", SECONDARY_SERVICE],
                   check=True, timeout=10)


def read_max_nits(port):
    data = call_json(port, "get_calibration_info")
    if data.get("calibrated") is False:
        raise RuntimeError(f"port {port} has no brightness calibration")
    max_nits = float(data["max_nits"])
    if not math.isfinite(max_nits) or max_nits <= 0:
        raise RuntimeError(f"port {port} returned invalid max_nits")
    return max_nits


def rounded_nits(nits, max_nits):
    lower = min(MIN_NITS, max_nits)
    clamped = min(max(nits, lower), max_nits)
    rounded = round(clamped / NITS_STEP) * NITS_STEP
    return min(max(rounded, lower), max_nits)


def restore_dual_display(state):
    log("starting secondary als-dimmer instance")
    start_secondary_service()

    log("waiting for als-dimmer ports")
    wait_for_port(PRIMARY_PORT)
    wait_for_port(SECONDARY_PORT)

    log("setting both instances to manual mode")
    call_json(PRIMARY_PORT, "set_mode", {"mode": "manual"})
    call_json(SECONDARY_PORT, "set_mode", {"mode": "manual"})

    primary_max = read_max_nits(PRIMARY_PORT)
    secondary_max = read_max_nits(SECONDARY_PORT)
    shared_max = min(primary_max, secondary_max)
    target_nits = rounded_nits(state["last_nits"], shared_max)

    log(f"restoring dual-display brightness to {target_nits:.1f} nits")
    call_json(PRIMARY_PORT, "set_absolute_brightness", {"nits": target_nits})
    call_json(SECONDARY_PORT, "set_absolute_brightness", {"nits": target_nits})

    state["version"] = 1
    state["enabled"] = True
    state["last_nits"] = target_nits
    state["min_nits"] = MIN_NITS
    save_state(state)


def main():
    try:
        state = load_state()
        if state is None:
            return 0
        restore_dual_display(state)
        log("restore complete")
        return 0
    except Exception as exc:
        log(f"restore failed: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
