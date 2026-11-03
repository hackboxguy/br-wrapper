#!/bin/sh
# Pattern Generator tile: disp-tester with the connected panel's active area (--panel-size-mm), so the
# i1 Display Pro outline (sensor-outline) and whiteboxmm are drawn at true size when navigated to.
# The size is panel_size_mm (display-analysis-common.sh): the framework config's active area, else its
# diagonal and the framebuffer resolution. Unknown: disp-tester without it (the outline shows a hint).
# Arguments are passed on to disp-tester.

MICROPANEL_HOME="${MICROPANEL_HOME:-/home/pi/micropanel}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/display-analysis-common.sh"

# disp-tester: the launcher's install prefix (<prefix>/share/qt-apps -> <prefix>/bin), else /usr/bin
DISP_TESTER="${DISP_TESTER:-}"
if [ -z "$DISP_TESTER" ]; then
    for t in "$SCRIPT_DIR/../../bin/disp-tester" /usr/bin/disp-tester; do
        if [ -x "$t" ]; then
            DISP_TESTER="$t"
            break
        fi
    done
fi
[ -n "$DISP_TESTER" ] || { echo "pattern-generator: disp-tester not found" >&2; exit 1; }

# exec: the launcher tracks and stops the process it started
if size="$(panel_size_mm "$(resolve_display_model)")"; then
    exec "$DISP_TESTER" --panel-size-mm "${size% *}x${size#* }" "$@"
fi
exec "$DISP_TESTER" "$@"
