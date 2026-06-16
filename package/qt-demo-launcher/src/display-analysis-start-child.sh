#!/bin/sh
# disp-tester child gate for display-analysis-report.sh.
#
# Shows an operator placement prompt, waits for the disp-tester child-action
# Start button, then launches the long-running analysis worker detached from
# disp-tester so the worker can later stop disp-tester and show touch-gallery.

set -u

SUITE="${1:-color-gamut}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPORT_SCRIPT="${DISPLAY_ANALYSIS_REPORT_SCRIPT:-$SCRIPT_DIR/display-analysis-report.sh}"
DISP_ADDR="${DISP_TESTER_HOST:-127.0.0.1}:${DISP_TESTER_PORT:-8082}"
TIMEOUT="${DISPLAY_ANALYSIS_GATE_TIMEOUT:-5}"
PLACEMENT_TEXT="${DISPLAY_ANALYSIS_PLACEMENT_TEXT:-Place the sensor on the Whitebox\\nand Click on Start Measurement}"
STARTING_TEXT="${DISPLAY_ANALYSIS_STARTING_TEXT:-Color Gamut Analysis\\nStarting measurement...}"

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

disp_cmd() {
    command="$1"
    arg="${2:-}"
    if [ -n "$arg" ]; then
        "$LAUNCHER_CLIENT_BIN" --srv="$DISP_ADDR" --command="$command" \
            --command-arg="$arg" --timeoutsec="$TIMEOUT" >/dev/null 2>&1 || true
    else
        "$LAUNCHER_CLIENT_BIN" --srv="$DISP_ADDR" --command="$command" \
            --timeoutsec="$TIMEOUT" >/dev/null 2>&1 || true
    fi
}

setup_prompt() {
    disp_cmd pattern "whitebox percent 10"
    disp_cmd set-metadata-status enable
    disp_cmd set-metadata-fontsize 28
    disp_cmd set-metadata-align center
    disp_cmd set-metadata-color white
    disp_cmd set-metadata-text "$PLACEMENT_TEXT"
    disp_cmd set-child-action-active disable
}

start_worker() {
    disp_cmd set-metadata-text "$STARTING_TEXT"
    disp_cmd set-child-action-active enable

    if [ ! -x "$REPORT_SCRIPT" ]; then
        disp_cmd set-metadata-color red
        disp_cmd set-metadata-text "Color Gamut Analysis\\nWorker script not found"
        exit 1
    fi

    nohup "$REPORT_SCRIPT" "$SUITE" >/tmp/display-analysis-start-child.log 2>&1 &
}

setup_prompt

while IFS= read -r line; do
    case "$line" in
        *'"command":"set_recording"'*'"enabled":true*|*'"enabled":true'*'"command":"set_recording"'*)
            start_worker
            exit 0
            ;;
        *'"command":"set_recording"'*'"enabled":false*|*'"enabled":false'*'"command":"set_recording"'*)
            setup_prompt
            ;;
        *)
            ;;
    esac
done

exit 0
