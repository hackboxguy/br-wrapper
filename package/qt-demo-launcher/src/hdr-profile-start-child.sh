#!/bin/sh
# disp-tester child gate of the launcher's "HDR Display Profile" -> "Measure Display Profile".
#
# Shows what will be measured (the detected display, its type and local-dimming state, the
# brightness, the expected duration) on a 10 % white box and waits for Start. Start runs the
# sensor locator (display-test-framework tests/02-validation/test-sensor-locate.sh): when the
# meter is more than 25 px off the centre it says which way to move it and waits for Start again
# (at most three rounds, then it refuses; SENSOR_LOCATE_TOLERANCE_PX in hdr-profile.conf widens the 25 px). On target it launches hdr-profile-worker.sh detached
# (the worker later stops disp-tester and shows touch-gallery) and keeps listening: the stop
# button cancels the measurement (the runner restores the display state).
#
# Option (disp-tester --child-option-name use-current-brightness): off = 100 %, on = the brightness
# als-dimmer reports at Start (BRIGHTNESS_VALUE for the worker), as Analyze Color Gamut.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKER="${HDR_PROFILE_WORKER:-$SCRIPT_DIR/hdr-profile-worker.sh}"
TIMEOUT="${HDR_PROFILE_GATE_TIMEOUT:-5}"
GATE_LOG="${HDR_PROFILE_GATE_LOG:-/tmp/hdr-profile-start-child.log}"
LOCATE_ROUNDS="${HDR_PROFILE_LOCATE_ROUNDS:-3}"
DA_DISP_ADDR="${DISP_TESTER_HOST:-127.0.0.1}:${DISP_TESTER_PORT:-8082}"
DA_CMD_TIMEOUT="$TIMEOUT" DA_CMD_RETRIES=5 DA_CMD_GAP=0.20
TITLE="HDR Display Profile"
USE_CURRENT_BRIGHTNESS=0
ROUND=0
WORKER_PID=""

. "$SCRIPT_DIR/hdr-profile-common.sh"

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$GATE_LOG"
}

LAUNCHER_CLIENT_BIN="$(detect_launcher_client)"
mkdir -p "$HP_STATE_DIR" 2>/dev/null
hp_detect
log "gate starting: model=$HP_MODEL type=$HP_TYPE serial=${HP_SERIAL:-none} ld=$HP_LD unsupported='${HP_UNSUPPORTED}'"

brightness_text() {
    if [ "$USE_CURRENT_BRIGHTNESS" -eq 1 ]; then
        level="$(current_brightness || true)"
        echo "Current-Brightness (${level:+$level%}${level:-unknown})"
    else
        echo "Full-Brightness (100%)"
    fi
}

prompt_text() {
    if [ -n "$HP_UNSUPPORTED" ]; then
        echo "$TITLE\\n$HP_MODEL\\n$HP_UNSUPPORTED"
        return
    fi
    state="local dimming $HP_LD"
    [ "$HP_TYPE" = oled ] && state="OLED, no local dimming"
    echo "$TITLE\\n$HP_MODEL  ${HP_SERIAL:-no serial}\\ntype $HP_TYPE, $state\\n$(brightness_text)\\nTakes $HP_EST\\nPlace the sensor on the white box\\nand click Start Measurement"
}

say() {   # say <colour> <text> (status re-enabled: the locator's overlay leaves it on autohide)
    disp_cmd set-metadata-status enable
    disp_cmd set-metadata-fontsize 28
    disp_cmd set-metadata-color "$1"
    disp_cmd set-metadata-text "$2"
}

setup_prompt() {
    log "showing placement prompt"
    disp_cmd pattern "whitebox percent 10"
    disp_cmd set-user-interaction enable
    disp_cmd set-metadata-status enable
    disp_cmd set-metadata-fontsize 28
    disp_cmd set-metadata-align center
    if hp_running; then
        say yellow "$TITLE\\nA profile measurement is already running\\n(lock $HP_LOCK_DIR)"
    elif [ -n "$HP_UNSUPPORTED" ]; then
        say red "$(prompt_text)"
    else
        say white "$(prompt_text)"
    fi
    disp_cmd set-child-action-active disable
}

# The sensor locator, one round: 0 = on target (HP_STATE_DIR/sensor-location.json), 1 = off (guidance
# shown), 2 = no sensor / error
locate_sensor() {
    dir="$HP_STATE_DIR/locate-$ROUND"
    rm -rf "$dir"
    mkdir -p "$dir"
    say white "$TITLE\\nLocating the sensor (round $ROUND of $LOCATE_ROUNDS, ~2 min)\\nDo not touch the sensor"
    DISPLAY_MODEL="$HP_MODEL" RESULTS_DIR="$dir" JSON_OUTPUT=0 DISPLAY_ANALYSIS_KEEP_PATTERN_APP=1 \
        LAUNCHER_CLIENT="$LAUNCHER_CLIENT_BIN" \
        "$FRAMEWORK_DIR/tests/02-validation/test-sensor-locate.sh" >> "$GATE_LOG" 2>&1
    loc_rc=$?
    res="$dir/artifacts/sensor-locate.json"
    log "locator round $ROUND: rc=$loc_rc $(jq -c '{sensor_px, offset_mm, footprint_px, guidance}' "$res" 2>/dev/null)"
    disp_cmd pattern "whitebox percent 10"
    if [ "$loc_rc" -eq 0 ]; then
        cp "$res" "$HP_STATE_DIR/sensor-location.json"
        return 0
    fi
    guidance="$(jq -r '.guidance // empty' "$res" 2>/dev/null)"
    if [ "$loc_rc" -eq 1 ] && [ -n "$guidance" ]; then
        say yellow "$TITLE\\nThe sensor is off the centre:\\n$guidance\\nthen click Start Measurement again"
        return 1
    fi
    say red "$TITLE\\n${guidance:-The sensor locator failed (see $GATE_LOG)}\\nthen click Start Measurement again"
    return 2
}

start_requested() {
    log "start requested (round $((ROUND + 1)))"
    if [ -n "$HP_UNSUPPORTED" ] || hp_running; then
        setup_prompt
        return
    fi
    if [ ! -x "$WORKER" ]; then
        say red "$TITLE\\nWorker script not found"
        disp_cmd set-child-action-active disable
        return
    fi
    if [ "$USE_CURRENT_BRIGHTNESS" -eq 1 ]; then
        level="$(current_brightness)" || {
            say red "Cannot read the current brightness\\n(als-dimmer not answering)\\nTurn the option off or retry"
            disp_cmd set-child-action-active disable
            return
        }
        BRIGHTNESS_VALUE="$level"
        export BRIGHTNESS_VALUE
    else
        unset BRIGHTNESS_VALUE
    fi
    disp_cmd set-child-action-active enable
    ROUND=$((ROUND + 1))
    if ! locate_sensor; then
        disp_cmd set-child-action-active disable
        if [ "$ROUND" -ge "$LOCATE_ROUNDS" ]; then
            say red "$TITLE\\nThe sensor is still not on the centre after $LOCATE_ROUNDS rounds:\\nplacement refused - exit, place the sensor and start again"
            HP_UNSUPPORTED="placement refused after $LOCATE_ROUNDS rounds"
        fi
        return
    fi
    say white "$TITLE\\nSensor on the centre (footprint $(jq -r '.footprint_px // "?"' "$HP_STATE_DIR/sensor-location.json") px)\\nMeasuring - $HP_EST"
    SENSOR_LOCATION="$HP_STATE_DIR/sensor-location.json"
    export SENSOR_LOCATION DISPLAY_MODEL="$HP_MODEL" DISPLAY_SERIAL="$HP_SERIAL"
    rm -f "$HP_STATE_DIR/cancel"
    # detached: the worker outlives this gate (it stops disp-tester to show the gallery)
    setsid nohup "$WORKER" >> "$GATE_LOG" 2>&1 < /dev/null &
    WORKER_PID=$!
    log "worker launched: pid=$WORKER_PID brightness=${BRIGHTNESS_VALUE:-100}"
}

cancel_requested() {
    if [ -n "$WORKER_PID" ] && kill -0 "$WORKER_PID" 2>/dev/null; then
        log "cancel requested"
        touch "$HP_STATE_DIR/cancel"
        say yellow "$TITLE\\nCancelling - restoring the display state..."
    else
        setup_prompt
    fi
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
            [ -n "$WORKER_PID" ] || disp_cmd set-metadata-text "$(prompt_text)"
        fi
    fi
    if echo "$line" | grep -q '"command"[[:space:]]*:[[:space:]]*"set_recording"'; then
        if echo "$line" | grep -q '"enabled"[[:space:]]*:[[:space:]]*true'; then
            start_requested
        elif echo "$line" | grep -q '"enabled"[[:space:]]*:[[:space:]]*false'; then
            cancel_requested
        fi
    fi
done

log "stdin closed; exiting gate"
exit 0
