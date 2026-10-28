#!/bin/sh
# disp-tester child gate for display-analysis-report.sh.
#
# Shows an operator placement prompt under the i1 Display Pro's outline at true
# size (verified with get-pattern; the 10 % white box when the model config has
# no physical size), waits for the disp-tester child-action Start button, then launches the long-running analysis worker detached from
# disp-tester so the worker can later stop disp-tester and show touch-gallery.
#
# Optional toggle (disp-tester --child-option-name use-current-brightness):
# off by default, the suite sets its own brightness (color-gamut: 100%). When
# on, the brightness als-dimmer reports at Start is handed to the worker as
# BRIGHTNESS_VALUE, so the gamut is measured at the level in use.

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
PLACEMENT_TEXT="${DISPLAY_ANALYSIS_PLACEMENT_TEXT:-}"
STARTING_TEXT="${DISPLAY_ANALYSIS_STARTING_TEXT:-$ANALYSIS_TITLE\\nMeasurement running...}"
DISP_CMD_GAP="${DISPLAY_ANALYSIS_CMD_GAP:-0.20}"
DISP_CMD_RETRIES="${DISPLAY_ANALYSIS_CMD_RETRIES:-5}"
# shared helpers (launcher-client, als-dimmer brightness, disp_cmd)
. "$SCRIPT_DIR/display-analysis-common.sh"
DA_DISP_ADDR="$DISP_ADDR" DA_CMD_TIMEOUT="$TIMEOUT" DA_CMD_RETRIES="$DISP_CMD_RETRIES" DA_CMD_GAP="$DISP_CMD_GAP"
DA_EXPECT_PID="$PPID"   # disp-tester started this gate: land_pattern checks the port is its
USE_CURRENT_BRIGHTNESS=0

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$GATE_LOG"
}

# The placement prompt; the gamut suite also says which brightness it will use.
prompt_text() {
    case "$SUITE" in
        color-gamut|gamut) ;;
        *) printf '%s' "$PLACEMENT_TEXT"; return 0 ;;
    esac
    if [ "$USE_CURRENT_BRIGHTNESS" -eq 1 ]; then
        level="$(current_brightness || true)"
        printf '%s' "$PLACEMENT_TEXT\\nCurrent-Brightness (${level:+$level%}${level:-unknown})"
    else
        printf '%s' "$PLACEMENT_TEXT\\nFull-Brightness (100%)"
    fi
}

LAUNCHER_CLIENT_BIN="$(detect_launcher_client)"
PLACEMENT="$(placement_pattern "$(resolve_display_model)")"
if [ -z "$PLACEMENT_TEXT" ]; then
    case "$PLACEMENT" in
        sensor-outline*) PLACEMENT_TEXT="Place the USB sensor on its outline (cable up)\\nand click Start Measurement" ;;
        *)               PLACEMENT_TEXT="Place the USB sensor on the white box\\nand click Start Measurement" ;;
    esac
fi
log "gate starting: suite=$SUITE disp=$DISP_ADDR launcher-client=$LAUNCHER_CLIENT_BIN report=$REPORT_SCRIPT placement='$PLACEMENT'"

is_apl_suite() {
    case "$SUITE" in
        local-dimming-apl|local_dimming_apl|apl)
            return 0
            ;;
    esac

    return 1
}

setup_prompt() {
    log "showing placement prompt"
    land_pattern "$PLACEMENT"
    disp_cmd set-user-interaction enable
    disp_cmd set-metadata-position bottom-center
    disp_cmd set-metadata-status enable
    disp_cmd set-metadata-fontsize 28
    disp_cmd set-metadata-align center
    disp_cmd set-metadata-color white
    disp_cmd set-metadata-text "$(prompt_text)"
    disp_cmd set-child-action-active disable
}

start_worker() {
    log "start requested"

    if [ ! -x "$REPORT_SCRIPT" ]; then
        log "error: worker script not executable: $REPORT_SCRIPT"
        disp_cmd set-metadata-color red
        disp_cmd set-metadata-text "$ANALYSIS_TITLE\\nWorker script not found"
        exit 1
    fi

    if [ "$USE_CURRENT_BRIGHTNESS" -eq 1 ]; then
        level="$(current_brightness)" || {
            log "error: current brightness requested but als-dimmer did not report one"
            disp_cmd set-metadata-color red
            disp_cmd set-metadata-text "Cannot read the current brightness\\n(als-dimmer not answering)\\nTurn the option off or retry"
            disp_cmd set-child-action-active disable
            return 1
        }
        BRIGHTNESS_VALUE="$level"
        export BRIGHTNESS_VALUE
        log "measuring at the current brightness: $level%"
    else
        unset BRIGHTNESS_VALUE
        log "measuring at the suite's default brightness"
    fi

    disp_cmd set-metadata-position bottom-right   # status text away from the measurement windows
    if is_apl_suite; then
        disp_cmd clear-metadata-text
        disp_cmd set-metadata-status disable
        disp_cmd set-user-interaction disable
        disp_cmd set-child-action-active enable
    else
        disp_cmd set-child-action-active enable
        disp_cmd set-metadata-text "$STARTING_TEXT"
    fi

    nohup "$REPORT_SCRIPT" "$SUITE" >> "$GATE_LOG" 2>&1 &
    worker_pid=$!
    log "worker launched: pid=$worker_pid"

    wait "$worker_pid"
    worker_rc=$?
    log "worker exited: rc=$worker_rc"
    disp_cmd set-child-action-active disable
    if is_apl_suite; then
        disp_cmd set-user-interaction enable
        disp_cmd set-metadata-status autohide
    fi
    exit "$worker_rc"
}

setup_prompt

while IFS= read -r line; do
    log "control: $line"
    if echo "$line" | grep -q '"command"[[:space:]]*:[[:space:]]*"set_option"'; then
        if echo "$line" | grep -q '"name"[[:space:]]*:[[:space:]]*"use-current-brightness"'; then
            if echo "$line" | grep -q '"enabled"[[:space:]]*:[[:space:]]*true'; then
                USE_CURRENT_BRIGHTNESS=1
            else
                USE_CURRENT_BRIGHTNESS=0
            fi
            log "option use-current-brightness=$USE_CURRENT_BRIGHTNESS"
            disp_cmd set-metadata-text "$(prompt_text)"
        fi
    fi
    if echo "$line" | grep -q '"command"[[:space:]]*:[[:space:]]*"set_recording"'; then
        if echo "$line" | grep -q '"enabled"[[:space:]]*:[[:space:]]*true'; then
            start_worker && exit 0
            continue
        fi
        if echo "$line" | grep -q '"enabled"[[:space:]]*:[[:space:]]*false'; then
            setup_prompt
        fi
    fi
done

log "stdin closed; exiting gate"
exit 0
