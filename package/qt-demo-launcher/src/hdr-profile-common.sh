#!/bin/sh
# Shared by hdr-profile-start-child.sh (the gate) and hdr-profile-worker.sh (sourced, not run):
# what is connected, what profile the button will measure, and the image's constants.
#
# The display type is detected, not chosen (a wrong manual choice gives a wrong profile silently):
#   the display-test-framework config of the connected model (resolve_display_model) says
#   - display_type hdmi-ddc                  -> edge-lit HDMI panel: not yet (HP_UNSUPPORTED)
#   - no FPGA and a 17-oled-* model          -> oled (subpixel layout from the config's panel.subpixel)
#   - an FPGA: local dimming read back       -> localdimming, profiled in the state it is in (on / off;
#     through disptool --device=fpganew)        a direct-drive panel with dimming off is still a
#                                               localdimming profile with state off - the generator
#                                               renders it as edge-lit)

. "$(dirname "$0")/display-analysis-common.sh"

# Site overrides (persistent: /home/pi/system-settings is on /data), shell assignments, e.g.
#   export SENSOR_LOCATE_TOLERANCE_PX=40    the sensor locator's tolerance (default 25 px)
HP_CONF="${HDR_PROFILE_CONF:-/home/pi/system-settings/hdr-profile.conf}"
# shellcheck disable=SC1090
[ -f "$HP_CONF" ] && . "$HP_CONF"

HP_STATE_DIR="${HDR_PROFILE_STATE_DIR:-/tmp/hdr-profile}"
HP_LOCK_DIR="$HP_STATE_DIR/lock"
PROFILE_ROOT="${HDR_PROFILE_ROOT:-/home/pi/display-profiles}"

# The player code offset: what dual-video-player's 8-bit BT.709 limited decode shows against
# disp-tester's patterns, in drive codes. A constant of the Pi image (the player build), not of the
# session: measured once per display model with sdr-hdr-shootout-demo tools/player-offset.py (22 grey
# windows played in dual-video-player, read with the i1, fitted against disp-tester's ramp; plan
# Appendices H, I). A new dual-video-player build or another decode path needs a new value.
HP_SIGNAL_PATH="${HDR_PROFILE_SIGNAL_PATH:-pi4-dvp-8bit-bt709-limited}"
player_code_offset_for() {   # player_code_offset_for <model> -> offset, or nothing when not measured
    case "$1" in
        14.6-fhd-spartan7) echo "-0.5" ;;    # .153, EPV5-7AXQ-VDJ5R, 2026-10-25
        17-oled-ib)        echo "-0.68" ;;   # .52, 42TW-J1C7-8KX2T, 2026-10-25
        *) return 1 ;;
    esac
}

hp_cfg() {   # hp_cfg <jq path>: a value of the model's framework config, empty when absent
    cfg="$FRAMEWORK_DIR/config/$HP_MODEL.json"
    [ -f "$cfg" ] && command -v jq >/dev/null 2>&1 || return 0
    v=$(jq -r "$1" "$cfg" 2>/dev/null)
    [ "$v" = null ] || echo "$v"
}

hp_fpga() {   # hp_fpga <command>: disptool on the 0x1E FPGA slave
    tool="$(detect_disptool)" || return 1
    "$tool" --i2cdev="${I2C_DEVICE:-/dev/i2c-1}" --autotestformat --device=fpganew --command "$1" 2>/dev/null
}

# hp_detect: sets HP_MODEL HP_TYPE HP_SERIAL HP_LD (on/off/none) HP_FPGA (yes/no) HP_SUBPIXEL HP_OFFSET
# HP_EST (expected duration text) and HP_UNSUPPORTED (empty, or why this display cannot be profiled yet)
hp_detect() {
    HP_MODEL="$(resolve_display_model)"
    HP_SERIAL="${DISPLAY_SERIAL:-$(read_display_serial)}"
    HP_TYPE="" HP_LD="none" HP_FPGA=yes HP_SUBPIXEL="" HP_OFFSET="" HP_EST="" HP_UNSUPPORTED=""
    [ "$(hp_cfg .features.fpga_version)" = false ] && HP_FPGA=no
    if [ "$(hp_cfg .display_type)" = hdmi-ddc ]; then
        # Edge-lit HDMI panels (run-tests-ddcutil.sh, als-dimmer's ddcutil backend): not yet. When they
        # come: ddcutil detect at start, EDID model / serial for the name, the readable VCPs into the
        # runner's --display-settings, a warning that dynamic contrast / picture modes cannot be read
        # reliably (the profile is at the panel's current settings), brightness through VCP 0x10 only
        # if the panel answers.
        HP_TYPE=edgelit
        HP_UNSUPPORTED="edge-lit HDMI displays: not yet"
        return 0
    fi
    if [ ! -f "$FRAMEWORK_DIR/config/$HP_MODEL.json" ]; then
        HP_UNSUPPORTED="no display-test-framework config for $HP_MODEL"
        return 0
    fi
    case "$HP_MODEL" in
        17-oled-*)
            HP_TYPE=oled
            HP_SUBPIXEL="$(hp_cfg .panel.subpixel)"
            [ -n "$HP_SUBPIXEL" ] || HP_UNSUPPORTED="$HP_MODEL: subpixel layout unknown (config panel.subpixel)"
            HP_EST="about 1 hour (20 min warm-up)"
            ;;
        *)
            if [ "$HP_FPGA" = no ]; then
                HP_UNSUPPORTED="$HP_MODEL: no FPGA and not an OLED - not yet"
                return 0
            fi
            HP_TYPE=localdimming
            out="$(hp_fpga localdim)"
            if echo "$out" | grep -qi disabled; then HP_LD=off
            elif echo "$out" | grep -qi enabled; then HP_LD=on
            else HP_UNSUPPORTED="local dimming state unreadable (disptool --device=fpganew)"; fi
            if [ "$HP_LD" = on ]; then
                HP_EST="70 to 125 minutes (warm-up 20-75 min)"
            else
                HP_EST="35 to 90 minutes (warm-up 20-75 min)"
            fi
            ;;
    esac
    HP_OFFSET="$(player_code_offset_for "$HP_MODEL")" \
        || HP_UNSUPPORTED="${HP_UNSUPPORTED:-no player code offset measured for $HP_MODEL on this image}"
    return 0
}

# Brightness the session will use: BRIGHTNESS_VALUE (the gate's Current-Brightness option), else 100.
hp_backlight() {
    echo "${BRIGHTNESS_VALUE:-100}"
}

# A profile measurement in progress: the worker's lock with a live pid.
hp_running() {
    [ -d "$HP_LOCK_DIR" ] || return 1
    pid="$(cat "$HP_LOCK_DIR/pid" 2>/dev/null)"
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}
