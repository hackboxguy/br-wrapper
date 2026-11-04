#!/bin/sh
# Choose the als-dimmer configuration for the display this head unit drives,
# then exec the daemon with it.
#
# als-dimmer itself is display-agnostic by design (plan D6.2): it takes a
# --config path and knows nothing about which panel is attached. The head unit
# does know -- config.txt was written for a specific display and
# pi-config-txt.sh reports which -- so the mapping lives here, in the image
# packaging, and is one line per display type.
#
# Why this is ExecStart and not ExecStartPre writing an env file: systemd sets
# up a service's environment, EnvironmentFile included, before ExecStartPre
# runs, so a file written by ExecStartPre is not in scope for ExecStart's
# ${VARIABLE} expansion. The env file is still written, because it is a useful
# thing to look at after the fact, but what actually selects the config is this
# script exec'ing the daemon.

set -e

# The tree is not always at the packaged prefix: the bench images keep both
# als-dimmer and micropanel under /home/pi.
for d in /usr/etc/als-dimmer /etc/als-dimmer /home/pi/als-dimmer/etc/als-dimmer; do
    [ -d "$d" ] && ALS_ETC="$d" && break
done
for b in /usr/bin/als-dimmer /home/pi/als-dimmer/bin/als-dimmer; do
    [ -x "$b" ] && ALS_BIN="$b" && break
done
for q in /usr/bin/pi-config-txt.sh /home/pi/micropanel/usr/bin/pi-config-txt.sh; do
    [ -x "$q" ] && QUERY="$q" && break
done

if [ -z "$ALS_ETC" ] || [ -z "$ALS_BIN" ]; then
    echo "als-dimmer-select-config: cannot locate als-dimmer (bin=$ALS_BIN etc=$ALS_ETC)" >&2
    exit 1
fi

DEFAULT_CONFIG="$ALS_ETC/config.json"
DISPLAY_TYPE=""
if [ -n "$QUERY" ]; then
    DISPLAY_TYPE=$("$QUERY" --input=/boot/firmware/config.txt --query-config 2>/dev/null || true)
fi

# Which firmware runs on a 3x QVue's MCU, without ever reading 0x32 (any read
# there hangs the stock firmware and the 988 until a power cycle) and without
# scanning: a WRITE of a register pointer to 0x66 is ACKed only by
# rh850-baremetal firmware (display_manager, qvue_legacy) -- the stock firmware
# has no slave at 0x66 -- and then the first 4 bytes of APP_NAME (0x0034) say
# which one ("disp" / "qvue"; 4 bytes, so the 983/988 0x67 read rule cannot
# bite). Prints display_manager, qvue_legacy, stock or unknown.
#
# At boot the hh983 driver may still be bringing the link up (its first probe
# can fail and retry ~6 s later), so wait for it to be bound and for the 988 to
# answer at 0x2C before deciding; a 0x66 NAK is retried before it counts as
# "stock". unknown (no link within the wait) selects the display_manager
# config, the release the field units carry.
qvue_mcu_firmware() {
    command -v i2ctransfer >/dev/null 2>&1 || { echo unknown; return; }
    bus=1; t=0
    while [ $t -lt 45 ]; do
        if [ -e /sys/bus/i2c/drivers/hh983-serializer/1-0018 ] &&
           i2ctransfer -y $bus w1@0x2c 0x00 r1@0x2c >/dev/null 2>&1; then
            break
        fi
        sleep 1; t=$((t + 1))
    done
    [ $t -ge 45 ] && { echo unknown; return; }
    n=0
    while [ $n -lt 5 ]; do
        if i2ctransfer -y $bus w2@0x66 0x00 0x34 >/dev/null 2>&1; then
            name=$(i2ctransfer -y $bus w2@0x66 0x00 0x34 r4@0x66 2>/dev/null || true)
            i2ctransfer -y $bus r1@0x66 >/dev/null 2>&1   # 983/988 write guard
            case "$name" in
                "0x64 0x69 0x73 0x70") echo display_manager ;;
                "0x71 0x76 0x75 0x65") echo qvue_legacy ;;
                *)                     echo display_manager ;;   # ours, unnamed (pre-2026-11-01)
            esac
            return
        fi
        sleep 1; n=$((n + 1))
    done
    echo stock
}

# One line per display type. Anything not listed -- including the "unknown" the
# query returns when it cannot tell -- keeps the config it has always used, so
# adding a display here cannot change what an existing one does.
#
# edid: an HDMI monitor that brings its own EDID (no FPGA, no dimmer at 0x1d - the default config
# would drive a phantom I2C dimmer and report "Brightness 0 %"). Its brightness is the monitor's
# VCP 0x10 over DDC/CI: with an OPT4001 ambient sensor on the head unit (0x44 on i2c-1) the ddcutil
# config dims it; without one there is nothing to do, so no daemon runs (it would only hold the DDC
# bus that the profile runner and ddcutil need). Brightness by hand: ddcutil setvcp 10 <0-100>.
case "$DISPLAY_TYPE" in
    ots-oled-17) CONFIG="$ALS_ETC/config_ioc_opt5001_tcon_ots17.json" ;;
    oled-ib-17)  CONFIG="$ALS_ETC/config_ioc_opt5001_tcon_oledib17.json" ;;
    # 3x-qvue: no ambient sensor and no FPGA dimmer at 0x1d -- the default config
    # would write a phantom dimmer twice a second. MANUAL-only, driven by the
    # Stream Deck keys. Which output depends on the QVue's MCU firmware
    # (qvue_mcu_firmware below): rh850-baremetal display_manager -> qvue_ioc at
    # 0x66 (config_ioc_qvue_3x.json, the default); the stock firmware or
    # qvue_legacy -> the vendor protocol at 0x32 (config_null_qvue_3x.json).
    3x-qvue)
        QVUE_FW=$(qvue_mcu_firmware)
        case "$QVUE_FW" in
            stock|qvue_legacy) CONFIG="$ALS_ETC/config_null_qvue_3x.json" ;;
            *)                 CONFIG="$ALS_ETC/config_ioc_qvue_3x.json" ;;
        esac
        echo "als-dimmer-select-config: 3x-qvue MCU firmware: $QVUE_FW" ;;
    edid)
        if i2cdetect -y -r 1 0x44 0x44 2>/dev/null | grep -q ' 44'; then
            CONFIG="$ALS_ETC/config_opti4001_ddcutil.json"
        else
            mkdir -p /run/als-dimmer
            echo "ALS_DIMMER_CONFIG=none" > /run/als-dimmer/env
            echo "als-dimmer-select-config: display 'edid' (HDMI monitor) without an ambient sensor (no OPT4001 at 0x44 on i2c-1): not running als-dimmer; the monitor's brightness is its VCP 0x10 (ddcutil)"
            exit 0   # a clean exit: the unit (Restart=on-failure) stays stopped
        fi ;;
    *)           CONFIG="$DEFAULT_CONFIG" ;;
esac

# A display type we recognise but whose config was not installed is a packaging
# mistake, not a reason to run the wrong config silently.
if [ ! -r "$CONFIG" ]; then
    echo "als-dimmer-select-config: display '$DISPLAY_TYPE' selects $CONFIG, which is missing; falling back to $DEFAULT_CONFIG" >&2
    CONFIG="$DEFAULT_CONFIG"
fi

mkdir -p /run/als-dimmer
echo "ALS_DIMMER_CONFIG=$CONFIG" > /run/als-dimmer/env

echo "als-dimmer-select-config: display '${DISPLAY_TYPE:-unqueried}' -> $CONFIG"
exec "$ALS_BIN" --config "$CONFIG"
