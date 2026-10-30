#!/bin/sh
# Shared by hdr-profile-start-child.sh (the gate) and hdr-profile-worker.sh (sourced, not run):
# what is connected, what profile the button will measure, and the image's constants.
#
# The display type is detected, not chosen (a wrong manual choice gives a wrong profile silently):
#   the display-test-framework config of the connected model (resolve_display_model: the EDID for
#   HDMI monitors, else the resolution) says
#   - display_type hdmi-ddc                  -> edgelit: an HDMI monitor, brightness and picture
#                                               settings over DDC/CI (its VCPs, recorded), one global
#                                               backlight (no dynamic contrast: check with the probe)
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
# disp-tester's patterns, in drive codes. A constant of the Pi image (the player build) and the
# display model, not of the session: measured once per model with sdr-hdr-shootout-demo
# tools/player-offset.py (22 grey windows played in dual-video-player, read with the i1, fitted
# against disp-tester's ramp; plan Appendices H, I) and kept in the model's framework config,
# hdr_profile.player_code_offset (and .signal_path), so a new model needs no code change.
# PLAYER_CODE_OFFSET / HDR_PROFILE_SIGNAL_PATH in hdr-profile.conf override them.
# A new dual-video-player build or another decode path needs new values.
player_code_offset_for() {   # player_code_offset_for <model> -> offset, or nothing when not measured
    if [ -n "${PLAYER_CODE_OFFSET:-}" ]; then
        echo "$PLAYER_CODE_OFFSET"
    elif [ -f "$FRAMEWORK_DIR/config/$1.json" ] && command -v jq >/dev/null 2>&1; then
        jq -r '.hdr_profile.player_code_offset // empty' "$FRAMEWORK_DIR/config/$1.json" 2>/dev/null
    fi
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

# hp_hdmi_settings <file>: an HDMI monitor's state for the profile (the runner's --display-settings,
# the profile's state.display_settings): its VCPs over DDC/CI (lib/ddc_state.py: brightness,
# contrast, colour preset = picture_mode, gains, sharpness, input), the EDID synopsis (what it
# claims: HDR EOTFs, max luminance) and notes. Sets HP_VCP_TEXT (one line for the prompt) and
# HP_EDID_SERIAL; 1 when the monitor does not answer on DDC/CI.
hp_hdmi_settings() {
    HP_VCP_TEXT="" HP_EDID_SERIAL=""
    python3 "$FRAMEWORK_DIR/lib/ddc_state.py" snapshot --out="$1.ddc" 2>/dev/null || { rm -f "$1.ddc"; return 1; }
    python3 "$FRAMEWORK_DIR/lib/edid_identify.py" --config-dir="$FRAMEWORK_DIR/config" --json > "$1.edid" 2>/dev/null
    hs_out="$(python3 - "$1" "$HP_MODEL" "$HP_SERIAL" "$(hp_cfg .hdr_profile.serial_is_type_label)" <<'PY'
import json, sys
out, model, serial, type_label = sys.argv[1:5]
s = json.load(open(out + ".ddc"))
try:
    edids = json.load(open(out + ".edid"))
except (OSError, ValueError):
    edids = []
e = next((x for x in edids if x.get("model") == model), None) or next((x for x in edids if x.get("connector") == s.get("connector")), None)
notes = [s.pop("note")]
if e:
    s["edid"] = {k: e.get(k) for k in ("connector", "manufacturer_id", "product_code", "model_field", "serial_number",
                                       "binary_serial_number", "image_size_mm", "colorimetry", "hdr_eotfs",
                                       "hdr_max_luminance_nits") if e.get(k) is not None}
    if "PQ" in (e.get("hdr_eotfs") or []):
        notes.append(f"the monitor's EDID advertises PQ (HDR10, max luminance ~{e.get('hdr_max_luminance_nits')} nits "
                     "claimed): this profile is of its SDR input path at these settings - the player sends SDR "
                     "BT.709 without an HDR infoframe")
if type_label == "true":
    notes.append(f"serial {serial} is a type label, not a unit serial: the EDID serial "
                 f"({(e or {}).get('serial_number') or '-'}, binary {(e or {}).get('binary_serial_number')}) is the "
                 "same stub on every unit of this type")
s["notes"] = notes
json.dump(s, open(out, "w"), indent=1)
v = s["vcp"]
g = lambda k: (v.get(k) or {}).get("value", "?")
print(f"VCP brightness {g('0x10')}, contrast {g('0x12')}, preset {(v.get('0x14') or {}).get('label') or g('0x14')}, "
      f"gains {g('0x16')}/{g('0x18')}/{g('0x1a')}, sharpness {g('0x87')}")
print((e or {}).get("serial_number") or "")
PY
)" || return 1
    rm -f "$1.ddc" "$1.edid"
    HP_VCP_TEXT="$(echo "$hs_out" | sed -n 1p)"
    HP_EDID_SERIAL="$(echo "$hs_out" | sed -n 2p)"
}

# The brightness the gate's Current-Brightness option takes: an HDMI monitor's VCP 0x10, else
# als-dimmer's
hp_current_brightness() {
    if [ "$HP_TYPE" = edgelit ]; then
        ddcutil -t getvcp 10 2>/dev/null | awk '$1 == "VCP" && $3 == "C" {print $4; f = 1; exit} END {exit !f}'
    else
        current_brightness
    fi
}

# hp_detect: sets HP_MODEL HP_TYPE HP_SERIAL HP_LD (on/off/none) HP_FPGA (yes/no) HP_SUBPIXEL HP_OFFSET
# HP_EST (expected duration text) and HP_UNSUPPORTED (empty, or why this display cannot be profiled yet);
# for an HDMI monitor also HP_SETTINGS (its display-settings file), HP_VCP_TEXT, HP_EDID_SERIAL
hp_detect() {
    HP_MODEL="$(resolve_display_model)"
    HP_TYPE="" HP_LD="none" HP_FPGA=yes HP_SUBPIXEL="" HP_OFFSET="" HP_EST="" HP_UNSUPPORTED="" HP_SETTINGS=""
    HP_SIGNAL_PATH="${HDR_PROFILE_SIGNAL_PATH:-$(hp_cfg .hdr_profile.signal_path)}"
    HP_SIGNAL_PATH="${HP_SIGNAL_PATH:-pi4-dvp-8bit-bt709-limited}"
    [ "$(hp_cfg .features.fpga_version)" = false ] && HP_FPGA=no
    if [ "$(hp_cfg .display_type)" = hdmi-ddc ]; then
        # An HDMI monitor: no IOC to ask for a serial - the EDID's, or the config's type label when
        # the EDID serial is a stub (hdr_profile.serial_label; DISPLAY_SERIAL in hdr-profile.conf wins).
        # Its picture modes / dynamic contrast cannot be read reliably: the profile is at its current
        # settings, recorded (the VCPs); brightness = VCP 0x10.
        HP_TYPE=edgelit HP_FPGA=no
        HP_SERIAL="${DISPLAY_SERIAL:-$(hp_cfg .hdr_profile.serial_label)}"
        HP_EST="about 45 minutes (20 min warm-up)"
        HP_SETTINGS="$HP_STATE_DIR/display-settings.json"
        mkdir -p "$HP_STATE_DIR" 2>/dev/null
        if ! hp_hdmi_settings "$HP_SETTINGS"; then
            HP_UNSUPPORTED="$HP_MODEL: the monitor does not answer on DDC/CI (ddcutil; als-dimmer on the bus?)"
            return 0
        fi
        [ -n "$HP_SERIAL" ] || HP_SERIAL="$HP_EDID_SERIAL"
        HP_OFFSET="$(player_code_offset_for "$HP_MODEL")"
        [ -n "$HP_OFFSET" ] || HP_UNSUPPORTED="no player code offset measured for $HP_MODEL on this image (config hdr_profile.player_code_offset)"
        return 0
    fi
    HP_SERIAL="${DISPLAY_SERIAL:-$(read_display_serial)}"
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
    HP_OFFSET="$(player_code_offset_for "$HP_MODEL")"
    [ -n "$HP_OFFSET" ] || HP_UNSUPPORTED="${HP_UNSUPPORTED:-no player code offset measured for $HP_MODEL on this image}"
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
