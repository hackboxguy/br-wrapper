#!/bin/sh
#
# Toggle Pi4 HDMI output on/off
#
# Uses fbdev blank (FB_BLANK_POWERDOWN) to stop/start the HDMI signal.
# This forces the HDMI-to-DP converter to drop and re-train DP,
# providing the fresh DP source signal needed after a 983/988 reset.
#
# Usage:
#   toggle-pi-hdmi.sh off    # HDMI signal stops
#   toggle-pi-hdmi.sh on     # HDMI signal resumes
#   toggle-pi-hdmi.sh cycle  # off -> 1s -> on (default)
#

FB_BLANK="/sys/class/graphics/fb0/blank"

if [ ! -f "$FB_BLANK" ]; then
    echo "Error: $FB_BLANK not found"
    exit 1
fi

ACTION=${1:-cycle}

case $ACTION in
    off)
        printf "4" | tee "$FB_BLANK" > /dev/null 2>&1
        echo "HDMI OFF (FB_BLANK_POWERDOWN)"
        ;;
    on)
        printf "0" | tee "$FB_BLANK" > /dev/null 2>&1
        echo "HDMI ON (FB_BLANK_UNBLANK)"
        ;;
    cycle)
        printf "4" | tee "$FB_BLANK" > /dev/null 2>&1
        echo "HDMI OFF..."
        sleep 1
        printf "0" | tee "$FB_BLANK" > /dev/null 2>&1
        echo "HDMI ON"
        ;;
    *)
        echo "Usage: $0 {off|on|cycle}"
        exit 1
        ;;
esac
