#!/bin/sh
# disp-tester child gate for display-analysis-report.sh.
#
# Shows an operator placement prompt, waits for the disp-tester child-action
# Start button, then launches the long-running analysis worker detached from
# disp-tester so the worker can later stop disp-tester and show touch-gallery.

set -u

MICROPANEL_HOME="${MICROPANEL_HOME:-/home/pi/micropanel}"
SUITE="${1:-color-gamut}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPORT_SCRIPT="${DISPLAY_ANALYSIS_REPORT_SCRIPT:-$SCRIPT_DIR/display-analysis-report.sh}"
DISP_ADDR="${DISP_TESTER_HOST:-127.0.0.1}:${DISP_TESTER_PORT:-8082}"
TIMEOUT="${DISPLAY_ANALYSIS_GATE_TIMEOUT:-5}"
GATE_LOG="${DISPLAY_ANALYSIS_GATE_LOG:-/tmp/display-analysis-start-child.log}"
case "$SUITE" in
    color-gamut|gamut)
        DEFAULT_TITLE="Color Gamut Analysis"
        ;;
    local-dimming-apl|local_dimming_apl|apl)
        DEFAULT_TITLE="Local Dimming APL Analysis"
        ;;
    *)
        DEFAULT_TITLE="Display Analysis"
        ;;
esac
ANALYSIS_TITLE="${DISPLAY_ANALYSIS_TITLE:-$DEFAULT_TITLE}"
PLACEMENT_TEXT="${DISPLAY_ANALYSIS_PLACEMENT_TEXT:-Place the USB sensor on the white box\\nand click Start Measurement}"
STARTING_TEXT="${DISPLAY_ANALYSIS_STARTING_TEXT:-$ANALYSIS_TITLE\\nMeasurement running...}"
DISP_CMD_GAP="${DISPLAY_ANALYSIS_CMD_GAP:-0.20}"
DISP_CMD_RETRIES="${DISPLAY_ANALYSIS_CMD_RETRIES:-5}"

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$GATE_LOG"
}

detect_launcher_client() {
    if [ -n "${LAUNCHER_CLIENT:-}" ] && [ -x "$LAUNCHER_CLIENT" ]; then
        echo "$LAUNCHER_CLIENT"
    elif [ -x "/usr/bin/launcher-client" ]; then
        echo "/usr/bin/launcher-client"
    elif [ -n "${MICROPANEL_HOME:-}" ] && [ -x "$MICROPANEL_HOME/usr/bin/launcher-client" ]; then
        echo "$MICROPANEL_HOME/usr/bin/launcher-client"
    elif [ -n "${MICROPANEL_HOME:-}" ] && [ -x "$MICROPANEL_HOME/build/launcher-client" ]; then
        echo "$MICROPANEL_HOME/build/launcher-client"
    else
        echo "launcher-client"
    fi
}

LAUNCHER_CLIENT_BIN="$(detect_launcher_client)"
log "gate starting: suite=$SUITE disp=$DISP_ADDR launcher-client=$LAUNCHER_CLIENT_BIN report=$REPORT_SCRIPT"

disp_cmd() {
    command="$1"
    arg="${2:-}"
    attempt=1
    response=""
    rc=1

    while [ "$attempt" -le "$DISP_CMD_RETRIES" ]; do
        if [ "$attempt" -gt 1 ]; then
            sleep "$DISP_CMD_GAP"
        fi

        if [ -n "$arg" ]; then
            response="$("$LAUNCHER_CLIENT_BIN" --srv="$DISP_ADDR" --command="$command" \
                --command-arg="$arg" --timeoutsec="$TIMEOUT" 2>&1)"
        else
            response="$("$LAUNCHER_CLIENT_BIN" --srv="$DISP_ADDR" --command="$command" \
                --timeoutsec="$TIMEOUT" 2>&1)"
        fi
        rc=$?
        if [ "$rc" -eq 0 ] && echo "$response" | grep -q "OK"; then
            sleep "$DISP_CMD_GAP"
            return 0
        fi

        attempt=$((attempt + 1))
    done

    log "warning: disp-tester command failed: $command $arg attempts=$DISP_CMD_RETRIES rc=$rc response=$response"
    sleep "$DISP_CMD_GAP"
    return 1
}

setup_prompt() {
    log "showing placement prompt"
    disp_cmd pattern "whitebox percent 10"
    disp_cmd set-metadata-status enable
    disp_cmd set-metadata-fontsize 28
    disp_cmd set-metadata-align center
    disp_cmd set-metadata-color white
    disp_cmd set-metadata-text "$PLACEMENT_TEXT"
    disp_cmd set-child-action-active disable
}

start_worker() {
    log "start requested"
    disp_cmd set-metadata-text "$STARTING_TEXT"
    disp_cmd set-child-action-active enable

    if [ ! -x "$REPORT_SCRIPT" ]; then
        log "error: worker script not executable: $REPORT_SCRIPT"
        disp_cmd set-metadata-color red
        disp_cmd set-metadata-text "$ANALYSIS_TITLE\\nWorker script not found"
        exit 1
    fi

    nohup "$REPORT_SCRIPT" "$SUITE" >> "$GATE_LOG" 2>&1 &
    worker_pid=$!
    log "worker launched: pid=$worker_pid"

    wait "$worker_pid"
    worker_rc=$?
    log "worker exited: rc=$worker_rc"
    disp_cmd set-child-action-active disable
    exit "$worker_rc"
}

setup_prompt

while IFS= read -r line; do
    log "control: $line"
    if echo "$line" | grep -q '"command"[[:space:]]*:[[:space:]]*"set_recording"'; then
        if echo "$line" | grep -q '"enabled"[[:space:]]*:[[:space:]]*true'; then
            start_worker
            exit 0
        fi
        if echo "$line" | grep -q '"enabled"[[:space:]]*:[[:space:]]*false'; then
            setup_prompt
        fi
    fi
done

log "stdin closed; exiting gate"
exit 0
