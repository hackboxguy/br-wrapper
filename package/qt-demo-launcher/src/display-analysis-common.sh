#!/bin/sh
# Shared by the launcher's measurement flows (sourced, not run):
#   display-analysis-start-child.sh / display-analysis-report.sh  - Analyze Color Gamut, Local-Dimming APL
#   hdr-profile-start-child.sh / hdr-profile-worker.sh            - HDR Display Profile
# The callers set MICROPANEL_HOME and, for disp_cmd, DA_DISP_ADDR / DA_CMD_TIMEOUT / DA_CMD_RETRIES /
# DA_CMD_GAP, and define log().

MICROPANEL_HOME="${MICROPANEL_HOME:-/home/pi/micropanel}"
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
disp_cmd() {
    command="$1"
    arg="${2:-}"
    attempt=1
    response=""
    rc=1

    while [ "$attempt" -le "${DA_CMD_RETRIES:-5}" ]; do
        if [ "$attempt" -gt 1 ]; then
            sleep "${DA_CMD_GAP:-0.20}"
        fi

        if [ -n "$arg" ]; then
            response="$("$LAUNCHER_CLIENT_BIN" --srv="${DA_DISP_ADDR:-127.0.0.1:8082}" --command="$command" \
                --command-arg="$arg" --timeoutsec="${DA_CMD_TIMEOUT:-5}" 2>&1)"
        else
            response="$("$LAUNCHER_CLIENT_BIN" --srv="${DA_DISP_ADDR:-127.0.0.1:8082}" --command="$command" \
                --timeoutsec="${DA_CMD_TIMEOUT:-5}" 2>&1)"
        fi
        rc=$?
        if [ "$rc" -eq 0 ] && echo "$response" | grep -q "OK"; then
            sleep "${DA_CMD_GAP:-0.20}"
            return 0
        fi

        attempt=$((attempt + 1))
    done

    log "warning: disp-tester command failed: $command $arg attempts=${DA_CMD_RETRIES:-5} rc=$rc response=$response"
    sleep "${DA_CMD_GAP:-0.20}"
    return 1
}
