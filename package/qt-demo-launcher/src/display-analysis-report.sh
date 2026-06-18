#!/bin/sh
# Run a display characterization suite, render a report PNG, and show it.

set -u

MICROPANEL_HOME="${MICROPANEL_HOME:-/home/pi/micropanel}"
OUTPUT_ROOT="${DISPLAY_ANALYSIS_OUTPUT_ROOT:-/tmp/display-analysis}"
ARCHIVE_ROOT="${DISPLAY_ANALYSIS_REPORT_ARCHIVE_ROOT:-/home/pi/test-reports}"
LAUNCHER_ADDR="${LAUNCHER_ADDR:-127.0.0.1:8081}"
GALLERY_ADDR="${GALLERY_ADDR:-127.0.0.1:8086}"
PATTERN_GEN_ADDR="${PATTERN_GEN_ADDR:-127.0.0.1:8082}"
TIMEOUT="${DISPLAY_ANALYSIS_TIMEOUT:-10}"
CONTROL_TIMEOUT="${DISPLAY_ANALYSIS_CONTROL_TIMEOUT:-2}"
CONTROL_CMD_GAP="${DISPLAY_ANALYSIS_CMD_GAP:-0.15}"
CONTROL_CMD_RETRIES="${DISPLAY_ANALYSIS_RENDER_CMD_RETRIES:-2}"
DEFAULT_1080P_MODEL="${DEFAULT_1080P_MODEL:-14.6-fhd-spartan7}"
DEFAULT_DISPLAY_MODEL="${DEFAULT_DISPLAY_MODEL:-$DEFAULT_1080P_MODEL}"

RUNNER="${DISPLAY_TEST_RUNNER:-$MICROPANEL_HOME/share/disptool/display-test-framework/runners/run-tests.sh}"
REPORT_CARD="${DISPLAY_REPORT_CARD:-$MICROPANEL_HOME/lib/disp-report-card/display_report_card.py}"

usage() {
    echo "Usage: $0 color-gamut|local-dimming-apl"
}

suite_arg="${1:-color-gamut}"
case "$suite_arg" in
    color-gamut|gamut)
        RUN_SUITE="color-gamut"
        SUITE_DIR_NAME="color-gamut"
        REPORT_PANELS="gamut,zoom_gamut"
        REPORT_TITLE="Color Gamut Analysis"
        DEFAULT_RENDERING_APP_ID="display-analysis-color-gamut-rendering"
        DEFAULT_GALLERY_APP_ID="display-analysis-report-gallery"
        LEGACY_LOG_DIR="/tmp/gamut-test"
        LEGACY_LOG_NAME="analyze-color-gamut.log"
        ;;
    local-dimming-apl|local_dimming_apl|apl)
        RUN_SUITE="local_dimming_apl"
        SUITE_DIR_NAME="local-dimming-apl"
        REPORT_PANELS="local_dimming_apl"
        REPORT_TITLE="Local Dimming APL Analysis"
        DEFAULT_RENDERING_APP_ID="display-analysis-local-dimming-apl-rendering"
        DEFAULT_GALLERY_APP_ID="display-analysis-report-gallery"
        LEGACY_LOG_DIR=""
        LEGACY_LOG_NAME=""
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

RENDERING_APP_ID="${DISPLAY_ANALYSIS_RENDERING_APP_ID:-$DEFAULT_RENDERING_APP_ID}"
GALLERY_APP_ID="${GALLERY_APP_ID:-$DEFAULT_GALLERY_APP_ID}"

SUITE_DIR="$OUTPUT_ROOT/$SUITE_DIR_NAME"
LOCK_DIR="$SUITE_DIR/.analysis.lock"
RUN_ID="run-$(date +%Y%m%d-%H%M%S)"
RESULTS_DIR="$SUITE_DIR/$RUN_ID"
PNG_PATH="$SUITE_DIR/$RUN_ID.png"
ARCHIVE_DIR="$ARCHIVE_ROOT/$SUITE_DIR_NAME"
ARCHIVE_PNG_PATH="$ARCHIVE_DIR/$RUN_ID.png"
COMMON_ARCHIVE_PNG_PATH="$ARCHIVE_ROOT/$RUN_ID-$SUITE_DIR_NAME.png"
LATEST_DIR_LINK="$SUITE_DIR/latest"
LATEST_PNG_LINK="$SUITE_DIR/latest.png"
DISPLAY_PNG_PATH="$PNG_PATH"
LOG_FILE="$RESULTS_DIR/analyze-$SUITE_DIR_NAME.log"
LEGACY_LOG=""

mkdir -p "$SUITE_DIR" 2>/dev/null || {
    echo "ERROR: failed to create $SUITE_DIR" >&2
    exit 1
}

if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    echo "Display analysis already running for $SUITE_DIR_NAME" >> "$SUITE_DIR/analysis-busy.log"
    exit 0
fi

mkdir -p "$RESULTS_DIR" 2>/dev/null || {
    echo "ERROR: failed to create $RESULTS_DIR" >&2
    rmdir "$LOCK_DIR" 2>/dev/null || true
    exit 1
}

if [ -n "$LEGACY_LOG_DIR" ]; then
    mkdir -p "$LEGACY_LOG_DIR" 2>/dev/null || true
    LEGACY_LOG="$LEGACY_LOG_DIR/$LEGACY_LOG_NAME"
fi

cleanup() {
    status=$?
    if [ "$status" -ne 0 ] && [ -n "${LAUNCHER_CLIENT_BIN:-}" ]; then
        "$LAUNCHER_CLIENT_BIN" --srv="$LAUNCHER_ADDR" --command=stop-app \
            --timeoutsec="$TIMEOUT" >/dev/null 2>&1 || true
    fi
    if [ -n "$LEGACY_LOG" ] && [ -f "$LOG_FILE" ]; then
        cp "$LOG_FILE" "$LEGACY_LOG" 2>/dev/null || true
    fi
    rm -f "$LOCK_DIR/pid" 2>/dev/null || true
    rmdir "$LOCK_DIR" 2>/dev/null || true
    exit "$status"
}
trap cleanup EXIT

echo "$$" > "$LOCK_DIR/pid" 2>/dev/null || true

log() {
    line="$(date '+%Y-%m-%d %H:%M:%S') $*"
    echo "$line" >> "$LOG_FILE"
}

run_logged() {
    log "RUN: $*"
    "$@" >> "$LOG_FILE" 2>&1
}

detect_launcher_client() {
    if [ -n "${LAUNCHER_CLIENT:-}" ] && [ -x "$LAUNCHER_CLIENT" ]; then
        echo "$LAUNCHER_CLIENT"
    elif [ -x "/usr/bin/launcher-client" ]; then
        echo "/usr/bin/launcher-client"
    elif [ -x "$MICROPANEL_HOME/usr/bin/launcher-client" ]; then
        echo "$MICROPANEL_HOME/usr/bin/launcher-client"
    elif [ -x "$MICROPANEL_HOME/build/launcher-client" ]; then
        echo "$MICROPANEL_HOME/build/launcher-client"
    else
        echo "launcher-client"
    fi
}

detect_display_resolution() {
    if [ -r /sys/class/graphics/fb0/virtual_size ]; then
        value="$(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null | tr ',' 'x' | tr -d '[:space:]')"
        case "$value" in
            [0-9]*x[0-9]*)
                echo "$value"
                return 0
                ;;
        esac
    fi

    if [ -r /sys/class/graphics/fb0/modes ]; then
        value="$(sed -n 's/.*:\([0-9][0-9]*x[0-9][0-9]*\).*/\1/p' /sys/class/graphics/fb0/modes 2>/dev/null | head -n 1)"
        if [ -n "$value" ]; then
            echo "$value"
            return 0
        fi
    fi

    return 1
}

resolve_display_model() {
    if [ -n "${DISPLAY_MODEL:-}" ]; then
        echo "$DISPLAY_MODEL"
        return 0
    fi

    resolution="$(detect_display_resolution || true)"
    case "$resolution" in
        1920x720)
            echo "12.3-nq1-lattice-ecp5"
            ;;
        2560x1440)
            echo "15.6-0od-lattice-ecp5"
            ;;
        3840x2400)
            echo "rtk-hg560t34"
            ;;
        1920x1080)
            echo "$DEFAULT_1080P_MODEL"
            ;;
        *)
            echo "$DEFAULT_DISPLAY_MODEL"
            ;;
    esac
}

gallery_running() {
    "$LAUNCHER_CLIENT_BIN" --srv="$GALLERY_ADDR" --command=get-count \
        --timeoutsec="$TIMEOUT" >/dev/null 2>&1
}

wait_for_gallery() {
    i=0
    while [ "$i" -lt "$TIMEOUT" ]; do
        if gallery_running; then
            return 0
        fi
        i=$((i + 1))
        sleep 1
    done
    return 1
}

disp_cmd() {
    command="$1"
    arg="${2:-}"
    attempt=1
    response=""
    rc=1

    while [ "$attempt" -le "$CONTROL_CMD_RETRIES" ]; do
        if [ "$attempt" -gt 1 ]; then
            sleep "$CONTROL_CMD_GAP"
        fi

        if [ -n "$arg" ]; then
            response="$("$LAUNCHER_CLIENT_BIN" --srv="$PATTERN_GEN_ADDR" \
                --command="$command" --command-arg="$arg" \
                --timeoutsec="$CONTROL_TIMEOUT" 2>&1)"
        else
            response="$("$LAUNCHER_CLIENT_BIN" --srv="$PATTERN_GEN_ADDR" \
                --command="$command" --timeoutsec="$CONTROL_TIMEOUT" 2>&1)"
        fi
        rc=$?
        if [ "$rc" -eq 0 ] && echo "$response" | grep -q "OK"; then
            sleep "$CONTROL_CMD_GAP"
            return 0
        fi

        attempt=$((attempt + 1))
    done

    log "WARNING: disp-tester command failed: $command $arg attempts=$CONTROL_CMD_RETRIES rc=$rc response=$response"
    return 1
}

show_existing_rendering_screen() {
    if ! disp_cmd pattern black; then
        return 1
    fi

    disp_cmd set-user-interaction disable || true
    disp_cmd set-metadata-status enable || true
    disp_cmd set-metadata-align center || true
    disp_cmd set-metadata-fontsize 32 || true
    disp_cmd set-metadata-color white || true
    disp_cmd set-metadata-text "$REPORT_TITLE\\nRendering report..." || true
    log "Reused active disp-tester as rendering screen"
    return 0
}

stop_current_app() {
    "$LAUNCHER_CLIENT_BIN" --srv="$LAUNCHER_ADDR" --command=stop-app \
        --timeoutsec="$TIMEOUT" >/dev/null 2>&1 || true
}

show_rendering_screen() {
    if show_existing_rendering_screen; then
        return 0
    fi

    stop_current_app

    response="$("$LAUNCHER_CLIENT_BIN" --srv="$LAUNCHER_ADDR" \
        --command=start-app --command-arg="$RENDERING_APP_ID" \
        --timeoutsec="$TIMEOUT" 2>&1)"
    if ! echo "$response" | grep -q "OK"; then
        log "WARNING: failed to start rendering screen app '$RENDERING_APP_ID': $response"
        return 1
    fi

    log "Started rendering screen app: $RENDERING_APP_ID"
    return 0
}

show_report() {
    stop_current_app
    sleep 0.5

    response="$("$LAUNCHER_CLIENT_BIN" --srv="$LAUNCHER_ADDR" \
        --command=start-app --command-arg="$GALLERY_APP_ID" \
        --timeoutsec="$TIMEOUT" 2>&1)"
    if ! echo "$response" | grep -q "OK"; then
        log "ERROR: failed to start report gallery app '$GALLERY_APP_ID': $response"
        return 1
    fi

    if ! wait_for_gallery; then
        log "ERROR: touch-gallery did not become responsive"
        return 1
    fi

    sleep 1
    response="$("$LAUNCHER_CLIENT_BIN" --srv="$GALLERY_ADDR" \
        --command=display --command-arg="$DISPLAY_PNG_PATH" \
        --timeoutsec="$TIMEOUT" 2>&1)"
    if ! echo "$response" | grep -q "OK"; then
        log "ERROR: failed to display report PNG: $response"
        return 1
    fi

    return 0
}

archive_report_png() {
    archive_status=0

    if ! mkdir -p "$ARCHIVE_DIR" 2>/dev/null; then
        log "WARNING: failed to create report archive directory: $ARCHIVE_DIR"
        return 1
    fi

    if ! cp "$PNG_PATH" "$ARCHIVE_PNG_PATH" 2>/dev/null; then
        log "WARNING: failed to archive report PNG to: $ARCHIVE_PNG_PATH"
        archive_status=1
    else
        DISPLAY_PNG_PATH="$ARCHIVE_PNG_PATH"
        log "Archived suite report PNG: $ARCHIVE_PNG_PATH"
    fi

    if ! cp "$PNG_PATH" "$COMMON_ARCHIVE_PNG_PATH" 2>/dev/null; then
        log "WARNING: failed to archive report PNG to combined gallery: $COMMON_ARCHIVE_PNG_PATH"
        archive_status=1
    else
        DISPLAY_PNG_PATH="$COMMON_ARCHIVE_PNG_PATH"
        log "Archived combined report PNG: $COMMON_ARCHIVE_PNG_PATH"
    fi

    return "$archive_status"
}

LAUNCHER_CLIENT_BIN="$(detect_launcher_client)"
export LAUNCHER_CLIENT="$LAUNCHER_CLIENT_BIN"
export MICROPANEL_HOME
export DISPLAY_MODEL="$(resolve_display_model)"
export DISPLAY_ANALYSIS_KEEP_PATTERN_APP="${DISPLAY_ANALYSIS_KEEP_PATTERN_APP:-1}"
if [ "$RUN_SUITE" = "local_dimming_apl" ]; then
    export LOCAL_DIMMING_SWEEP_MODE="${LOCAL_DIMMING_SWEEP_MODE:-absolute_apl}"
fi

ln -sfn "$RESULTS_DIR" "$LATEST_DIR_LINK" 2>/dev/null || true

log "Starting display analysis"
log "Suite: $RUN_SUITE"
log "Title: $REPORT_TITLE"
if [ "$RUN_SUITE" = "local_dimming_apl" ]; then
    log "Local dimming sweep mode: ${LOCAL_DIMMING_SWEEP_MODE:-percent_apl}"
fi
log "Display model: $DISPLAY_MODEL"
log "Runner: $RUNNER"
log "Report card: $REPORT_CARD"
log "Rendering app: $RENDERING_APP_ID"
log "Gallery app: $GALLERY_APP_ID"
log "Results: $RESULTS_DIR"
log "PNG: $PNG_PATH"
log "Archive PNG: $ARCHIVE_PNG_PATH"
log "Combined archive PNG: $COMMON_ARCHIVE_PNG_PATH"

if [ ! -x "$RUNNER" ]; then
    log "ERROR: runner not found or not executable: $RUNNER"
    exit 1
fi

if [ ! -f "$REPORT_CARD" ]; then
    log "ERROR: report card script not found: $REPORT_CARD"
    exit 1
fi

run_logged "$RUNNER" "$RUN_SUITE" "--output=$RESULTS_DIR/"
run_rc=$?
if [ "$run_rc" -ne 0 ]; then
    log "WARNING: test runner exited with status $run_rc; attempting report render if artifacts exist"
fi

if [ ! -f "$RESULTS_DIR/summary.json" ]; then
    log "ERROR: missing summary.json; not rendering report"
    exit 1
fi

show_rendering_screen || true

if command -v python3 >/dev/null 2>&1; then
    run_logged python3 "$REPORT_CARD" --input "$RESULTS_DIR" --output "$PNG_PATH" --panels "$REPORT_PANELS"
    report_rc=$?
else
    run_logged "$REPORT_CARD" --input "$RESULTS_DIR" --output "$PNG_PATH" --panels "$REPORT_PANELS"
    report_rc=$?
fi

if [ "$report_rc" -ne 0 ]; then
    log "ERROR: report renderer exited with status $report_rc"
    exit 1
fi

if [ ! -f "$PNG_PATH" ]; then
    log "ERROR: report PNG was not created"
    exit 1
fi

ln -sfn "$PNG_PATH" "$LATEST_PNG_LINK" 2>/dev/null || true
log "Report rendered successfully"
archive_report_png || true

if ! show_report; then
    log "ERROR: failed to show report in touch-gallery"
    exit 1
fi

log "Display analysis completed"
exit 0
