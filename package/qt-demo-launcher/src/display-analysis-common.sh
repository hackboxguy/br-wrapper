#!/bin/sh
# Shared by the launcher's measurement flows (sourced, not run):
#   display-analysis-start-child.sh / display-analysis-report.sh  - Analyze Color Gamut, Local-Dimming APL
#   hdr-profile-start-child.sh / hdr-profile-worker.sh            - HDR Display Profile
# The callers set MICROPANEL_HOME and, for disp_cmd, DA_DISP_ADDR / DA_CMD_TIMEOUT / DA_CMD_RETRIES /
# DA_CMD_GAP, and define log().

MICROPANEL_HOME="${MICROPANEL_HOME:-/home/pi/micropanel}"
FRAMEWORK_DIR="${DISPLAY_TEST_FRAMEWORK:-$MICROPANEL_HOME/share/disptool/display-test-framework}"
DA_NL='
'
DEFAULT_1080P_MODEL="${DEFAULT_1080P_MODEL:-14.6-fhd-spartan7}"
DEFAULT_DISPLAY_MODEL="${DEFAULT_DISPLAY_MODEL:-$DEFAULT_1080P_MODEL}"

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

detect_disptool() {
    for t in "${DISPTOOL:-}" "$MICROPANEL_HOME/bin/disptool" /usr/bin/disptool /usr/local/bin/disptool; do
        [ -n "$t" ] && [ -x "$t" ] && echo "$t" && return 0
    done
    return 1
}

detect_als_dimmer_client() {
    for c in "${ALS_DIMMER_CLIENT:-}" /usr/bin/als-dimmer-client /home/pi/als-dimmer/bin/als-dimmer-client; do
        [ -n "$c" ] && [ -x "$c" ] && echo "$c" && return 0
    done
    return 1
}

# Current brightness in percent (0-100) as als-dimmer reports it; empty if unknown.
current_brightness() {
    client="$(detect_als_dimmer_client)" || return 1
    value="$("$client" --brightness 2>/dev/null | tr -dc '0-9')"
    case "$value" in
        ''|*[!0-9]*) return 1 ;;
    esac
    [ "$value" -le 100 ] || return 1
    echo "$value"
}

# Short chip serial of the display IOC (e.g. 0HD5-B2WV-JE6HH) via disptool;
# empty when the display has no IOC or its firmware does not provide one.
read_display_serial() {
    serial=""
    if tool="$(detect_disptool)"; then
        serial="$("$tool" --i2cdev="${I2C_DEVICE:-/dev/i2c-1}" --autotestformat --command chipid --device=ioc 2>/dev/null |
                  sed -n 's/^Short[[:space:]]*:[[:space:]]*\([0-9A-Z-]*\).*/\1/p' | head -n 1)"
    fi
    case "$serial" in
        ????-????-?????) echo "$serial" ;;
        *) echo "" ;;
    esac
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

# Display type pi-config-txt.sh recorded in config.txt; tells apart panels that
# share a resolution (14.6-fhd direct-drive vs 14.6-fhd-ej scan-mode EJ, 12.3-nq1
# vs 12.3-nq1v1.1, the 15.6" 0OD vs the 17" OLED-IB at 2560x1440).
configured_display_type() {
    for q in /usr/bin/pi-config-txt.sh "$MICROPANEL_HOME/usr/bin/pi-config-txt.sh"; do
        if [ -x "$q" ]; then
            "$q" --input=/boot/firmware/config.txt --query-config 2>/dev/null
            return 0
        fi
    done
    return 1
}

# The display-test-framework config name (config/<model>.json) of the connected display.
resolve_display_model() {
    if [ -n "${DISPLAY_MODEL:-}" ]; then
        echo "$DISPLAY_MODEL"
        return 0
    fi

    resolution="$(detect_display_resolution || true)"
    case "$resolution" in
        1920x720)
            case "$(configured_display_type || true)" in
                12.3-nq1v1.1) echo "12.3-nq1v1.1-lattice-lae3u25f" ;;
                *)            echo "12.3-nq1-lattice-ecp5" ;;
            esac
            ;;
        2560x1440)
            case "$(configured_display_type || true)" in
                oled-ib-17) echo "17-oled-ib" ;;
                *)          echo "15.6-0od-lattice-ecp5" ;;
            esac
            ;;
        2880x1620)
            echo "17-oled-ots"
            ;;
        3840x2400)
            echo "rtk-hg560t34"
            ;;
        1920x1080)
            case "$(configured_display_type || true)" in
                14.6-fhd-ej) echo "14.6-fhd-spartan7-ej" ;;
                *)           echo "$DEFAULT_1080P_MODEL" ;;
            esac
            ;;
        *)
            echo "$DEFAULT_DISPLAY_MODEL"
            ;;
    esac
}

# disp_cmd <command> [arg]: one disp-tester control command (pattern, set-metadata-*, ...), retried.
# Its variables are da_-prefixed: sh has no locals, and a caller's $rc must survive a disp_cmd.
# One command is one line on the wire: a newline in the argument goes as a literal \n (disp-tester
# turns it back into a line break in metadata text). A real newline split the text into several
# commands, each answered - disp-tester died of SIGPIPE answering a client that had gone (2.31).
# A command that still fails after the retries is logged and shown in red (da_alert), never silent.
disp_cmd() {
    da_command="$1"
    da_arg="${2:-}"
    case "$da_arg" in
        *"$DA_NL"*) da_arg="$(printf '%s' "$da_arg" | sed -e ':a' -e 'N' -e '$!ba' -e 's/\n/\\n/g')" ;;
    esac
    da_attempt=1
    da_response=""
    da_rc=1

    while [ "$da_attempt" -le "${DA_CMD_RETRIES:-5}" ]; do
        if [ "$da_attempt" -gt 1 ]; then
            sleep "${DA_CMD_GAP:-0.20}"
        fi

        if [ -n "$da_arg" ]; then
            da_response="$("$LAUNCHER_CLIENT_BIN" --srv="${DA_DISP_ADDR:-127.0.0.1:8082}" --command="$da_command" \
                --command-arg="$da_arg" --timeoutsec="${DA_CMD_TIMEOUT:-5}" 2>&1)"
        else
            da_response="$("$LAUNCHER_CLIENT_BIN" --srv="${DA_DISP_ADDR:-127.0.0.1:8082}" --command="$da_command" \
                --timeoutsec="${DA_CMD_TIMEOUT:-5}" 2>&1)"
        fi
        da_rc=$?
        if [ "$da_rc" -eq 0 ] && echo "$da_response" | grep -q "OK"; then
            sleep "${DA_CMD_GAP:-0.20}"
            return 0
        fi

        da_attempt=$((da_attempt + 1))
    done

    log "warning: disp-tester command failed: $da_command $da_arg attempts=${DA_CMD_RETRIES:-5} rc=$da_rc response=$da_response"
    sleep "${DA_CMD_GAP:-0.20}"
    da_alert "command failed: $da_command (rc=$da_rc $da_response)"
    return 1
}

# da_send <command> [arg]: one try, short timeout; prints disp-tester's answer (empty when none)
da_send() {
    if [ -n "${2:-}" ]; then
        "$LAUNCHER_CLIENT_BIN" --srv="${DA_DISP_ADDR:-127.0.0.1:8082}" --command="$1" --command-arg="$2" \
            --timeoutsec="${DA_SEND_TIMEOUT:-2}" 2>/dev/null | head -n 1
    else
        "$LAUNCHER_CLIENT_BIN" --srv="${DA_DISP_ADDR:-127.0.0.1:8082}" --command="$1" \
            --timeoutsec="${DA_SEND_TIMEOUT:-2}" 2>/dev/null | head -n 1
    fi
}

# da_alert <text>: a control failure in the log and, if disp-tester still answers, in red on the screen
da_alert() {
    log "error: $1"
    da_send set-metadata-status enable >/dev/null
    da_send set-metadata-color red >/dev/null
    da_send set-metadata-text "disp-tester: $1\\nExit and start again" >/dev/null
}

# sensor_outline_spec <model>: "sensor-outline width-mm W height-mm H" from the framework config's
# physical.active_width_mm / active_height_mm (disp-tester draws the i1 Display Pro at true size);
# fails when the config has no size
sensor_outline_spec() {
    so_cfg="$FRAMEWORK_DIR/config/$1.json"
    [ -f "$so_cfg" ] && command -v jq >/dev/null 2>&1 || return 1
    so_w="$(jq -r '.physical.active_width_mm // empty' "$so_cfg" 2>/dev/null)"
    so_h="$(jq -r '.physical.active_height_mm // empty' "$so_cfg" 2>/dev/null)"
    [ -n "$so_w" ] && [ -n "$so_h" ] || return 1
    echo "sensor-outline width-mm $so_w height-mm $so_h"
}

# placement_pattern <model>: the sensor outline, or the 10 % white box when the model's size is unknown
placement_pattern() {
    sensor_outline_spec "$1" || echo "whitebox percent 10"
}

# land_pattern <spec>: show a pattern and verify it with get-pattern (it must name the spec's first
# word) - the launcher starts disp-tester on black; a lost command must not leave it there unnoticed.
# Every attempt is logged with disp-tester's answers; a pattern that does not land goes to da_alert.
# DA_EXPECT_PID (the gates: $PPID, the disp-tester that started them): the port must be that
# disp-tester's - a second instance that could not bind the port, while an older one still held it,
# would otherwise have its gate verify the pattern on the older instance.
land_pattern() {
    lp_spec="$1"
    lp_name="${1%% *}"
    lp_try=1
    lp_got=""
    if [ -n "${DA_EXPECT_PID:-}" ]; then
        lp_pid="$(da_send get-pid)"
        if [ "$lp_pid" != "$DA_EXPECT_PID" ]; then
            da_alert "the control port is not this disp-tester's (pid ${lp_pid:-no answer}, expected $DA_EXPECT_PID)"
            return 1
        fi
    fi
    while [ "$lp_try" -le "${DA_LAND_TRIES:-10}" ]; do
        lp_set="$(da_send pattern "$lp_spec")"
        lp_got="$(da_send get-pattern)"
        log "landing $lp_name: attempt $lp_try: pattern -> ${lp_set:-no answer}; get-pattern -> ${lp_got:-no answer}"
        [ "$lp_got" = "$lp_name" ] && return 0
        lp_try=$((lp_try + 1))
        sleep "${DA_LAND_GAP:-0.30}"
    done
    da_alert "pattern $lp_name did not land (get-pattern: ${lp_got:-no answer})"
    return 1
}
