#!/bin/sh
#
# Re-initialize the full 983 FPDLink + display pipeline
#
# Supports all three deserializer configurations:
#   --mode=988        (983+988, config_mode=1, TDDI touch via I2C passthrough)
#   --mode=984        (983+984, config_mode=0, REM_INTB forwarding)
#   --mode=988-video  (983+988, config_mode=2, video only -- no touch driver
#                      steps at all; same deserializer address and the same 988
#                      register set as --mode=988)
#
# Optional:
#   --skip-hdmi-toggle  Skip Pi4 HDMI off/on (if not using HDMI-to-DP converter)
#
# Usage:
#   sudo ./re-init-983-pipeline.sh --mode=988
#   sudo ./re-init-983-pipeline.sh --mode=984 --skip-hdmi-toggle
#   sudo ./re-init-983-pipeline.sh --mode=988 --touch-unbind
#   sudo ./re-init-983-pipeline.sh --mode=988-video
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
TOUCH_UNBIND=0
# Whether this mode has a touch controller at all.  The QVue has none, so
# unbinding / removing / reloading himax_mmi would only print failures.
HAS_TOUCH=1

for arg in "$@"; do
    case $arg in
        --mode=988)
            MODE="988"
            CONFIG_MODE=1
            ;;
        --mode=988-video)
            MODE="988-video"
            CONFIG_MODE=2
            HAS_TOUCH=0
            ;;
        --mode=984)
            MODE="984"
            CONFIG_MODE=0
            ;;
        --skip-hdmi-toggle)
            SKIP_HDMI=1
            ;;
        --touch-unbind)
            TOUCH_UNBIND=1
            ;;
        --help|-h)
            echo "Usage: $0 --mode={988|984|988-video} [--skip-hdmi-toggle]"
            echo ""
            echo "  --mode=988           983+988 deserializer (config_mode=1)"
            echo "  --mode=984           983+984 deserializer (config_mode=0)"
            echo "  --mode=988-video     983+988 video only, no touch (config_mode=2)"
            echo "  --skip-hdmi-toggle   Skip Pi4 HDMI off/on cycle"
            echo "  --touch-unbind       Unbind himax_tp I2C before rmmod (debug: touch recovery)"
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
    echo "Error: --mode is required (988, 984 or 988-video)"
    echo "Run '$0 --help' for usage."
    exit 1
fi

if [ $HAS_TOUCH -eq 0 ] && [ $TOUCH_UNBIND -eq 1 ]; then
    echo "Error: --touch-unbind makes no sense with --mode=988-video (no touch controller)"
    exit 1
fi

echo "=== Re-initializing 983+${MODE} FPDLink Pipeline ==="
echo "  config_mode=$CONFIG_MODE  hdmi_toggle=$([ $SKIP_HDMI -eq 0 ] && echo yes || echo skip)  touch=$([ $HAS_TOUCH -eq 1 ] && echo yes || echo none)  touch_unbind=$([ $TOUCH_UNBIND -eq 1 ] && echo yes || echo no)"
echo ""

# Step 1: Stop Qt application (via qt-launcher network command)
echo "Step 1: Stopping Qt application..."
echo "stop-app" | nc localhost 8081 2>/dev/null
sleep 0.5

# Step 2: Stop Qt launcher
echo "Step 2: Stopping Qt launcher..."
/etc/init.d/S99qt-launcher stop 2>/dev/null
sleep 0.5

# Step 2b: Unbind himax_tp I2C driver (optional, for touch recovery debugging)
if [ $TOUCH_UNBIND -eq 1 ]; then
    echo "Step 2b: Unbinding himax_tp from 1-0048..."
    echo "1-0048" > /sys/bus/i2c/drivers/himax_tp/unbind 2>/dev/null
    sleep 0.3
fi

# Step 3: Remove touch driver
if [ $HAS_TOUCH -eq 1 ]; then
    echo "Step 3: Removing himax_mmi..."
    rmmod himax_mmi 2>/dev/null
    sleep 0.3
else
    echo "Step 3: No touch controller in this mode, skipped"
fi

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
if [ $HAS_TOUCH -eq 1 ]; then
    echo "Step 8: Loading himax_mmi..."
    modprobe himax_mmi
    sleep 1
else
    echo "Step 8: No touch controller in this mode, skipped"
fi

# Step 9: Restart Qt launcher
echo "Step 9: Starting Qt launcher..."
/etc/init.d/S99qt-launcher start 2>/dev/null
sleep 1

echo ""
echo "=== 983+${MODE} pipeline re-initialization complete ==="
echo ""
