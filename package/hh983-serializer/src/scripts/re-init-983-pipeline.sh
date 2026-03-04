#!/bin/sh
#
# Re-initialize the full 983 FPDLink + display pipeline
#
# Supports both deserializer configurations:
#   --mode=988  (983+988, config_mode=1, TDDI touch via I2C passthrough)
#   --mode=984  (983+984, config_mode=0, REM_INTB forwarding)
#
# Optional:
#   --skip-hdmi-toggle  Skip Pi4 HDMI off/on (if not using HDMI-to-DP converter)
#
# Usage:
#   sudo ./re-init-983-pipeline.sh --mode=988
#   sudo ./re-init-983-pipeline.sh --mode=984 --skip-hdmi-toggle
#

# --- HDMI toggle via fbdev blank ---
FB_BLANK="/sys/class/graphics/fb0/blank"

hdmi_off() {
    if [ -f "$FB_BLANK" ]; then
        printf "4" | tee "$FB_BLANK" > /dev/null 2>&1
        echo "  HDMI OFF (FB_BLANK_POWERDOWN)"
    else
        echo "  WARNING: $FB_BLANK not found, cannot toggle HDMI"
    fi
}

hdmi_on() {
    if [ -f "$FB_BLANK" ]; then
        printf "0" | tee "$FB_BLANK" > /dev/null 2>&1
        echo "  HDMI ON (FB_BLANK_UNBLANK)"
    else
        echo "  WARNING: $FB_BLANK not found, cannot toggle HDMI"
    fi
}

# --- Parse arguments ---
MODE=""
CONFIG_MODE=""
SKIP_HDMI=0

for arg in "$@"; do
    case $arg in
        --mode=988)
            MODE="988"
            CONFIG_MODE=1
            ;;
        --mode=984)
            MODE="984"
            CONFIG_MODE=0
            ;;
        --skip-hdmi-toggle)
            SKIP_HDMI=1
            ;;
        --help|-h)
            echo "Usage: $0 --mode={988|984} [--skip-hdmi-toggle]"
            echo ""
            echo "  --mode=988           983+988 deserializer (config_mode=1)"
            echo "  --mode=984           983+984 deserializer (config_mode=0)"
            echo "  --skip-hdmi-toggle   Skip Pi4 HDMI off/on cycle"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Run '$0 --help' for usage."
            exit 1
            ;;
    esac
done

if [ -z "$MODE" ]; then
    echo "Error: --mode is required (988 or 984)"
    echo "Run '$0 --help' for usage."
    exit 1
fi

echo "=== Re-initializing 983+${MODE} FPDLink Pipeline ==="
echo "  config_mode=$CONFIG_MODE  hdmi_toggle=$([ $SKIP_HDMI -eq 0 ] && echo yes || echo skip)"
echo ""

# Step 1: Stop Qt application (via qt-launcher network command)
echo "Step 1: Stopping Qt application..."
echo "stop-app" | nc localhost 8081 2>/dev/null
sleep 0.5

# Step 2: Stop Qt launcher
echo "Step 2: Stopping Qt launcher..."
/etc/init.d/S99qt-launcher stop 2>/dev/null
sleep 0.5

# Step 3: Remove touch driver
echo "Step 3: Removing himax_mmi..."
rmmod himax_mmi 2>/dev/null
sleep 0.3

# Step 4: Remove serializer driver
echo "Step 4: Removing hh983-serializer..."
rmmod hh983_serializer 2>/dev/null
sleep 1

# Step 5: Toggle Pi4 HDMI off (converter drops DP output)
if [ $SKIP_HDMI -eq 0 ]; then
    echo "Step 5: Pi4 HDMI -> OFF..."
    hdmi_off
    sleep 1
else
    echo "Step 5: HDMI toggle skipped"
fi

# Step 6: Reload serializer driver (probe does: GPIO toggle + reset + HPD + full init)
echo "Step 6: Loading hh983-serializer (config_mode=$CONFIG_MODE)..."
modprobe hh983_serializer config_mode=$CONFIG_MODE
sleep 1

# Step 7: Toggle Pi4 HDMI on (fresh DP signal -> converter -> 983)
if [ $SKIP_HDMI -eq 0 ]; then
    echo "Step 7: Pi4 HDMI -> ON..."
    hdmi_on
    sleep 2
else
    echo "Step 7: HDMI toggle skipped"
fi

# Step 8: Reload touch driver
echo "Step 8: Loading himax_mmi..."
modprobe himax_mmi
sleep 1

# Step 9: Restart Qt launcher
echo "Step 9: Starting Qt launcher..."
/etc/init.d/S99qt-launcher start 2>/dev/null
sleep 1

echo ""
echo "=== 983+${MODE} pipeline re-initialization complete ==="
echo ""
