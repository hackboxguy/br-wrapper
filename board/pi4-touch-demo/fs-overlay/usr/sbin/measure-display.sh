#!/bin/sh
# Enhanced Color Measurement Script for Display Analysis
# Optimized for Pi4 Buildroot with i1Display Pro
# Dependencies: spotread (ArgyllCMS), optional: mplayclt, temperature/power sensors
# NEW: DS18B20 temperature sensor support

# Default configuration - can be overridden by config file or command line
LOOPCOUNT="1"
INTERVAL="none"
TEMPERED="none"
TEMP2="none"
POWER="none"
WFILE="none"
RFILE="none"
GFILE="none"
BFILE="none"
CFILE="none"
MFILE="none"
YFILE="none"
STARTUPIMG="none"
MEASUREONLY="no"
SENSORONLY="no"
BRLEVEL="100"
NOHEADER="no"
QUIET="${QUIET:-no}"
MYPATH="$(pwd)"
PATTERNPATH="$MYPATH/patterns"

# Enhanced configuration options
MEASUREMENT_DELAY="${MEASUREMENT_DELAY:-5}"
RETRY_DELAY="${RETRY_DELAY:-3}"
MAX_RETRIES="${MAX_RETRIES:-3}"
SPOTREAD_TIMEOUT="${SPOTREAD_TIMEOUT:-30}"
SPOTREAD_CMD="${SPOTREAD_CMD:-spotread}"
DEBUG="${DEBUG:-no}"
AUTO_USB_RESET="${AUTO_USB_RESET:-yes}"

# New JSON format options
FORMAT="${FORMAT:-csv}"
COMPACT="${COMPACT:-no}"

# disp-tester integration options
PATTERN_SOURCE="${PATTERN_SOURCE:-mplayclt}"
DISP_TESTER_HOST="${DISP_TESTER_HOST:-127.0.0.1}"
DISP_TESTER_PORT="${DISP_TESTER_PORT:-8080}"

# Hardware identifiers for i1Display Pro
I1_VENDOR_ID="0765"
I1_PRODUCT_ID="5020"

# Global variables for JSON session tracking
JSON_SESSION_START=""
JSON_MEASUREMENTS=""
JSON_MEASUREMENT_COUNT=0

# Global variables for temperature sensor tracking
TEMP2_ENABLED="no"

# Usage information
USAGE="Enhanced Color Measurement Script for Pi4/Buildroot

USAGE: $0 [OPTIONS]

PATH OPTIONS:
    --mypath=PATH           Base working directory (default: current dir)
    --patternpath=PATH      Directory containing pattern files (default: ./patterns)

MEASUREMENT OPTIONS:
    --measureonly=yes/no    Skip display control, measurement only (default: no)
    --sensor-only=yes/no    Just take one sensor reading, no pattern control (default: no)
    --loop=COUNT           Number of measurement cycles (default: 1)
    --interval=SECONDS     Delay between measurement cycles (default: none)
    --noheader=yes/no      Skip CSV header output (default: no)
    --quiet=yes/no         Suppress all log messages (default: no)
    --brlevel=LEVEL        Brightness level for logging (default: 100)

OUTPUT FORMAT OPTIONS:
    --format=csv/json      Output format (default: csv)
    --compact=yes/no       Compact JSON output - one line per measurement (default: no)
                          Only applies when --format=json

PATTERN SOURCE OPTIONS:
    --pattern-source=TYPE  Pattern display method: mplayclt or disp-tester (default: mplayclt)
    --disp-tester-host=IP  disp-tester daemon host IP (default: 127.0.0.1)
    --disp-tester-port=N   disp-tester daemon port (default: 8080)

PATTERN FILES (use 'none' to skip):
    --wfile=FILE           White pattern file (mplayclt) or any value to enable (disp-tester)
    --rfile=FILE           Red pattern file (mplayclt) or any value to enable (disp-tester)
    --gfile=FILE           Green pattern file (mplayclt) or any value to enable (disp-tester)
    --bfile=FILE           Blue pattern file (mplayclt) or any value to enable (disp-tester)
    --cfile=FILE           Cyan pattern file (mplayclt) or any value to enable (disp-tester)
    --mfile=FILE           Magenta pattern file (mplayclt) or any value to enable (disp-tester)
    --yfile=FILE           Yellow pattern file (mplayclt) or any value to enable (disp-tester)
    --startupimg=FILE      Initial pattern to display

HARDWARE OPTIONS:
    --temp=BINARY/ds18b20  Temperature sensor: binary name or 'ds18b20' for DS18B20
    --temp2=ds18b20/ID     Second temperature sensor: 'ds18b20' (first) or device ID (28-xxxxxxxxxxxx)
    --power=BINARY         Power measurement binary name

ADVANCED OPTIONS:
    --debug=yes/no         Enable debug output (default: no)
    --spotread-cmd=CMD     spotread command to use (default: spotread)
    --measurement-delay=S  Delay after pattern display (default: 5)
    --retry-delay=S        Delay between retry attempts (default: 3)
    --max-retries=N        Maximum measurement attempts (default: 3)
    --timeout=S            Timeout for spotread command (default: 30)
    --usb-reset=yes/no     Auto reset USB device on failure (default: yes)

TEMPERATURE SENSOR EXAMPLES:
    # Use temper USB sensor (legacy)
    $0 --temp=tempered --sensor-only=yes

    # Use first available DS18B20 sensor
    $0 --temp=ds18b20 --sensor-only=yes

    # Use specific DS18B20 device as primary sensor
    $0 --temp=28-3ce1e38018ca --sensor-only=yes

    # Use two DS18B20 sensors
    $0 --temp=ds18b20 --temp2=28-3ce1e38018ca --sensor-only=yes --format=json

    # Mixed: temper primary, DS18B20 secondary
    $0 --temp=tempered --temp2=ds18b20 --wfile=white.png

EXAMPLES:
    # Just measure current sensor value (CSV format - default)
    $0 --sensor-only=yes --quiet=yes --noheader=yes

    # JSON formatted single measurement with DS18B20
    $0 --sensor-only=yes --format=json --temp=ds18b20

    # Two temperature sensors with JSON output
    $0 --sensor-only=yes --format=json --temp=ds18b20 --temp2=28-3ce1e38018ca

    # Compact JSON for streaming/logging
    $0 --sensor-only=yes --format=json --compact=yes --loop=3

    # Basic white measurement with JSON output
    $0 --wfile=white.png --measureonly=yes --format=json

    # disp-tester based measurement
    $0 --pattern-source=disp-tester --wfile=enable --rfile=enable --gfile=enable --format=json

    # Full RGB measurement with CSV output (traditional)
    $0 --wfile=white.png --rfile=red.png --gfile=green.png --bfile=blue.png --loop=3 --format=csv

    # Automated measurement with temperature monitoring (JSON)
    $0 --wfile=white.png --temp=tempered --interval=60 --loop=10 --format=json"

# Logging functions
log_error() {
    [ "$QUIET" != "yes" ] && echo "ERROR: $*" >&2
}

log_info() {
    [ "$SENSORONLY" != "yes" ] && [ "$QUIET" != "yes" ] && echo "INFO: $*" >&2
}

log_debug() {
    [ "$DEBUG" = "yes" ] && [ "$QUIET" != "yes" ] && echo "DEBUG: $*" >&2
}

# DS18B20 temperature sensor functions
find_first_ds18b20() {
    for device in /sys/bus/w1/devices/28-*; do
        if [ -d "$device" ] && [ -f "$device/temperature" ]; then
            basename "$device"
            return 0
        fi
    done
    return 1
}

validate_ds18b20_device() {
    local device_id="$1"
    local device_path="/sys/bus/w1/devices/$device_id"

    if [ -d "$device_path" ] && [ -f "$device_path/temperature" ]; then
        return 0
    else
        return 1
    fi
}

read_ds18b20_temperature() {
    local device_spec="$1"  # "ds18b20", "first", or specific device ID like "28-xxxxxxxxxxxx"
    local device_id=""

    # Determine which device to use
    case "$device_spec" in
        "ds18b20"|"first")
            device_id=$(find_first_ds18b20)
            if [ $? -ne 0 ] || [ -z "$device_id" ]; then
                echo "ERROR"
                return 1
            fi
            ;;
        28-*)
            device_id="$device_spec"
            if ! validate_ds18b20_device "$device_id"; then
                echo "ERROR"
                return 1
            fi
            ;;
        *)
            echo "ERROR"
            return 1
            ;;
    esac

    log_debug "Reading DS18B20 temperature from device: $device_id"

    # Read temperature from device
    local temp_file="/sys/bus/w1/devices/$device_id/temperature"
    if [ -r "$temp_file" ]; then
        local temp_millidegrees=$(cat "$temp_file" 2>/dev/null)
        if [ $? -eq 0 ] && [ -n "$temp_millidegrees" ] && [ "$temp_millidegrees" -eq "$temp_millidegrees" ] 2>/dev/null; then
            # Convert millidegrees to degrees with 3 decimal places
            local temp_degrees=$(echo "scale=3; $temp_millidegrees/1000" | bc -l 2>/dev/null)
            if [ $? -eq 0 ] && [ -n "$temp_degrees" ]; then
                echo "$temp_degrees"
                return 0
            fi
        fi
    fi

    echo "ERROR"
    return 1
}

# Enhanced temperature reading function
read_temperature() {
    local temp_sensor="$1"
    local tempered_path="$2"

    case "$temp_sensor" in
        "ds18b20")
            read_ds18b20_temperature "ds18b20"
            ;;
        28-*)
            read_ds18b20_temperature "$temp_sensor"
            ;;
        *)
            # Legacy temper sensor
            if [ -x "$tempered_path" ]; then
                "$tempered_path" 2>/dev/null | awk '{print $4}' 2>/dev/null || echo "ERROR"
            else
                echo "N/A"
            fi
            ;;
    esac
}

# JSON utility functions
json_escape() {
    echo "$1" | sed 's/\\/\\\\/g; s/"/\\"/g; s/\t/\\t/g; s/\n/\\n/g; s/\r/\\r/g'
}

format_json_number() {
    local value="$1"
    if [ "$value" = "ERROR" ] || [ "$value" = "N/A" ]; then
        echo "\"$value\""
    else
        echo "$value"
    fi
}

format_json_measurement() {
    local timestamp="$1"
    local temp="$2"
    local color="$3"
    local x_val="$4"
    local y_val="$5"
    local z_val="$6"
    local yc_val="$7"
    local x_chr="$8"
    local y_chr="$9"
    local voltage="${10}"
    local current="${11}"
    local br_level="${12}"
    local is_error="${13:-no}"
    local temp2="${14:-}"

    if [ "$COMPACT" = "yes" ]; then
        # Compact JSON format - single line
        if [ "$is_error" = "yes" ]; then
            if [ "$TEMP2_ENABLED" = "yes" ]; then
                echo "{\"timestamp\":\"$(json_escape "$timestamp")\",\"color\":\"$(json_escape "$color")\",\"error\":\"measurement_failed\",\"temp\":\"$(json_escape "$temp")\",\"temp2\":\"$(json_escape "$temp2")\",\"voltage\":\"$(json_escape "$voltage")\",\"current\":\"$(json_escape "$current")\",\"brightness\":$br_level}"
            else
                echo "{\"timestamp\":\"$(json_escape "$timestamp")\",\"color\":\"$(json_escape "$color")\",\"error\":\"measurement_failed\",\"temp\":\"$(json_escape "$temp")\",\"voltage\":\"$(json_escape "$voltage")\",\"current\":\"$(json_escape "$current")\",\"brightness\":$br_level}"
            fi
        else
            if [ "$TEMP2_ENABLED" = "yes" ]; then
                echo "{\"timestamp\":\"$(json_escape "$timestamp")\",\"color\":\"$(json_escape "$color")\",\"XYZ\":{\"X\":$x_val,\"Y\":$y_val,\"Z\":$z_val},\"Yxy\":{\"Y\":$yc_val,\"x\":$x_chr,\"y\":$y_chr},\"luminance\":$yc_val,\"temp\":\"$(json_escape "$temp")\",\"temp2\":\"$(json_escape "$temp2")\",\"voltage\":\"$(json_escape "$voltage")\",\"current\":\"$(json_escape "$current")\",\"brightness\":$br_level}"
            else
                echo "{\"timestamp\":\"$(json_escape "$timestamp")\",\"color\":\"$(json_escape "$color")\",\"XYZ\":{\"X\":$x_val,\"Y\":$y_val,\"Z\":$z_val},\"Yxy\":{\"Y\":$yc_val,\"x\":$x_chr,\"y\":$y_chr},\"luminance\":$yc_val,\"temp\":\"$(json_escape "$temp")\",\"voltage\":\"$(json_escape "$voltage")\",\"current\":\"$(json_escape "$current")\",\"brightness\":$br_level}"
            fi
        fi
    else
        # Pretty JSON format
        if [ "$is_error" = "yes" ]; then
            if [ "$TEMP2_ENABLED" = "yes" ]; then
                cat << EOF
    {
      "timestamp": "$(json_escape "$timestamp")",
      "measurement": {
        "color": "$(json_escape "$color")",
        "error": "measurement_failed"
      },
      "environment": {
        "temperature": "$(json_escape "$temp")",
        "temperature2": "$(json_escape "$temp2")",
        "power": {
          "voltage": "$(json_escape "$voltage")",
          "current": "$(json_escape "$current")"
        }
      },
      "settings": {
        "brightness_level": $br_level
      }
    }
EOF
            else
                cat << EOF
    {
      "timestamp": "$(json_escape "$timestamp")",
      "measurement": {
        "color": "$(json_escape "$color")",
        "error": "measurement_failed"
      },
      "environment": {
        "temperature": "$(json_escape "$temp")",
        "power": {
          "voltage": "$(json_escape "$voltage")",
          "current": "$(json_escape "$current")"
        }
      },
      "settings": {
        "brightness_level": $br_level
      }
    }
EOF
            fi
        else
            if [ "$TEMP2_ENABLED" = "yes" ]; then
                cat << EOF
    {
      "timestamp": "$(json_escape "$timestamp")",
      "measurement": {
        "color": "$(json_escape "$color")",
        "colorspace": {
          "XYZ": {
            "X": $x_val,
            "Y": $y_val,
            "Z": $z_val
          },
          "Yxy": {
            "Y": $yc_val,
            "x": $x_chr,
            "y": $y_chr
          },
          "luminance_cd_m2": $yc_val
        }
      },
      "environment": {
        "temperature": "$(json_escape "$temp")",
        "temperature2": "$(json_escape "$temp2")",
        "power": {
          "voltage": "$(json_escape "$voltage")",
          "current": "$(json_escape "$current")"
        }
      },
      "settings": {
        "brightness_level": $br_level
      }
    }
EOF
            else
                cat << EOF
    {
      "timestamp": "$(json_escape "$timestamp")",
      "measurement": {
        "color": "$(json_escape "$color")",
        "colorspace": {
          "XYZ": {
            "X": $x_val,
            "Y": $y_val,
            "Z": $z_val
          },
          "Yxy": {
            "Y": $yc_val,
            "x": $x_chr,
            "y": $y_chr
          },
          "luminance_cd_m2": $yc_val
        }
      },
      "environment": {
        "temperature": "$(json_escape "$temp")",
        "power": {
          "voltage": "$(json_escape "$voltage")",
          "current": "$(json_escape "$current")"
        }
      },
      "settings": {
        "brightness_level": $br_level
      }
    }
EOF
            fi
        fi
    fi
}

json_session_init() {
    if [ "$FORMAT" = "json" ] && [ "$COMPACT" != "yes" ]; then
        JSON_SESSION_START=$(date "+%m/%d/%y %H:%M:%S")
        JSON_MEASUREMENTS=""
        JSON_MEASUREMENT_COUNT=0
    fi
}

json_session_add_measurement() {
    local measurement_json="$1"

    if [ "$FORMAT" = "json" ] && [ "$COMPACT" != "yes" ]; then
        if [ $JSON_MEASUREMENT_COUNT -gt 0 ]; then
            JSON_MEASUREMENTS="${JSON_MEASUREMENTS},"
        fi
        JSON_MEASUREMENTS="${JSON_MEASUREMENTS}${measurement_json}"
        JSON_MEASUREMENT_COUNT=$((JSON_MEASUREMENT_COUNT + 1))
    fi
}

json_session_output() {
    if [ "$FORMAT" = "json" ] && [ "$COMPACT" != "yes" ]; then
        local mode="full"
        [ "$SENSORONLY" = "yes" ] && mode="sensor-only"

        if [ $JSON_MEASUREMENT_COUNT -eq 1 ]; then
            # Single measurement - output measurement directly
            echo "$JSON_MEASUREMENTS"
        else
            # Multiple measurements - output session format
            cat << EOF
{
  "session_info": {
    "start_time": "$(json_escape "$JSON_SESSION_START")",
    "measurement_count": $JSON_MEASUREMENT_COUNT,
    "mode": "$mode"
  },
  "measurements": [
$JSON_MEASUREMENTS
  ]
}
EOF
        fi
    fi
}

# Color mapping for disp-tester
get_disp_tester_color() {
    case "$1" in
        "W") echo "white" ;;
        "R") echo "red" ;;
        "G") echo "green" ;;
        "B") echo "blue" ;;
        "C") echo "cyan" ;;
        "M") echo "magenta" ;;
        "Y") echo "yellow" ;;
        *) echo "unknown" ;;
    esac
}

# Send command to disp-tester daemon
send_disp_tester_command() {
    local command="$1"
    local response

    log_debug "Sending disp-tester command: $command"

    # Send command with 1 second timeout
    response=$(echo "$command" | nc -q 1 "$DISP_TESTER_HOST" "$DISP_TESTER_PORT" 2>/dev/null)
    local nc_exit_code=$?

    if [ $nc_exit_code -ne 0 ]; then
        log_error "Failed to connect to disp-tester at ${DISP_TESTER_HOST}:${DISP_TESTER_PORT}"
        log_error "Ensure disp-tester daemon is running and accessible"
        exit 1
    fi

    echo "$response"
}

# Display pattern using disp-tester
display_pattern_disp_tester() {
    local color_name="$1"

    log_debug "Displaying pattern via disp-tester: $color_name"

    # Send pattern command
    send_disp_tester_command "pattern $color_name" >/dev/null

    # Wait for pattern to stabilize
    sleep "$MEASUREMENT_DELAY"

    # Verify pattern is displayed correctly
    local current_pattern=$(send_disp_tester_command "get-pattern")
    current_pattern=$(echo "$current_pattern" | tr -d '\r\n' | tr -d ' ')

    if [ "$current_pattern" != "$color_name" ]; then
        log_error "Pattern verification failed: expected '$color_name', got '$current_pattern'"
        exit 1
    fi

    log_debug "Pattern verified: $color_name"
    return 0
}

# Display measurement metadata on screen
display_measurement_metadata() {
    local x_val="$1"
    local y_val="$2"
    local z_val="$3"

    if [ "$PATTERN_SOURCE" = "disp-tester" ]; then
        local metadata_text="Measurement in Progress\\nX:${x_val}\\nY:${y_val}\\nZ:${z_val}"
        log_debug "Displaying measurement metadata: X:$x_val Y:$y_val Z:$z_val"
        send_disp_tester_command "set-metadata-text $metadata_text" >/dev/null
    fi
}

# Platform detection and optimization
detect_platform() {
    if [ -f /proc/device-tree/model ] && grep -q "Raspberry Pi 4" /proc/device-tree/model 2>/dev/null; then
        log_info "Detected Raspberry Pi 4 - optimizing USB power management"
        # Disable USB autosuspend for better instrument reliability
        for dev in /sys/bus/usb/devices/*/power/control; do
            [ -w "$dev" ] && echo 'on' > "$dev" 2>/dev/null
        done
        return 0
    fi
    log_debug "Platform detection: Generic Linux system"
}

# Check if required commands are available
check_command() {
    command -v "$1" >/dev/null 2>&1 || {
        log_error "Required command '$1' not found"
        return 1
    }
}

# Validate file exists and is readable
validate_file() {
    local file="$1"
    local name="$2"

    if [ "$file" != "none" ]; then
        if [ ! -f "$file" ]; then
            log_error "$name file not found: $file"
            return 1
        elif [ ! -r "$file" ]; then
            log_error "$name file not readable: $file"
            return 1
        fi
    fi
    return 0
}

# Check for i1Display Pro USB device
check_i1_device() {
    if command -v lsusb >/dev/null 2>&1; then
        if lsusb | grep -q "${I1_VENDOR_ID}:${I1_PRODUCT_ID}"; then
            log_debug "i1Display Pro found (${I1_VENDOR_ID}:${I1_PRODUCT_ID})"
            return 0
        else
            log_error "i1Display Pro not found (VID:${I1_VENDOR_ID} PID:${I1_PRODUCT_ID})"
            log_info "Available USB devices:"
            lsusb 2>/dev/null || log_info "lsusb command not available"
            return 1
        fi
    else
        log_debug "lsusb not available - skipping USB device check"
        return 0
    fi
}

# Reset USB device (for communication recovery)
reset_usb_device() {
    log_info "Attempting to reset i1Display Pro USB device..."

    for dev in /sys/bus/usb/devices/*/idVendor; do
        if [ -f "$dev" ] && [ "$(cat "$dev" 2>/dev/null)" = "$I1_VENDOR_ID" ]; then
            local dir=$(dirname "$dev")
            if [ -f "$dir/idProduct" ] && [ "$(cat "$dir/idProduct" 2>/dev/null)" = "$I1_PRODUCT_ID" ]; then
                if [ -w "$dir/authorized" ]; then
                    echo 0 > "$dir/authorized" 2>/dev/null
                    sleep 1
                    echo 1 > "$dir/authorized" 2>/dev/null
                    sleep 2
                    log_info "Reset USB device ${I1_VENDOR_ID}:${I1_PRODUCT_ID}"
                    return 0
                else
                    log_debug "Cannot write to $dir/authorized - insufficient permissions"
                fi
            fi
        fi
    done

    log_error "Could not reset USB device - device not found or insufficient permissions"
    return 1
}

# Output measurement in requested format
output_measurement() {
    local timestamp="$1"
    local temp="$2"
    local color="$3"
    local x_val="$4"
    local y_val="$5"
    local z_val="$6"
    local yc_val="$7"
    local x_chr="$8"
    local y_chr="$9"
    local voltage="${10}"
    local current="${11}"
    local br_level="${12}"
    local is_error="${13:-no}"
    local temp2="${14:-}"

    if [ "$FORMAT" = "json" ]; then
        local measurement_json=$(format_json_measurement "$timestamp" "$temp" "$color" "$x_val" "$y_val" "$z_val" "$yc_val" "$x_chr" "$y_chr" "$voltage" "$current" "$br_level" "$is_error" "$temp2")

        if [ "$COMPACT" = "yes" ]; then
            echo "$measurement_json"
        else
            json_session_add_measurement "$measurement_json"
        fi
    else
        # CSV format with optional temp2 column
        if [ "$TEMP2_ENABLED" = "yes" ]; then
            echo "$timestamp,$temp,$color,$x_val,$y_val,$z_val,$yc_val,$x_chr,$y_chr,$voltage,$current,$br_level,$temp2"
        else
            echo "$timestamp,$temp,$color,$x_val,$y_val,$z_val,$yc_val,$x_chr,$y_chr,$voltage,$current,$br_level"
        fi
    fi
}

# Simple sensor-only measurement (no pattern control)
measure_sensor_only() {
    local temp="N/A"
    local temp2="N/A"
    local voltage="N/A"
    local current="N/A"

    # Get timestamp
    local timestamp=$(date "+%m/%d/%y,%H:%M:%S")

    # Measure primary temperature if sensor available
    temp=$(read_temperature "$TEMPERED" "$TEMPEREDPATH")
    log_debug "Primary temperature: $temp"

    # Measure secondary temperature if enabled
    if [ "$TEMP2_ENABLED" = "yes" ]; then
        temp2=$(read_temperature "$TEMP2" "")
        log_debug "Secondary temperature: $temp2"
    fi

    # Measure power if available
    if [ -x "$POWERPATH" ]; then
        local power_output=$("$POWERPATH" status 2>/dev/null)
        voltage=$(echo "$power_output" | awk '{print $2}' 2>/dev/null || echo "N/A")
        current=$(echo "$power_output" | awk '{print $5}' 2>/dev/null || echo "N/A")
    fi

    # Attempt measurement with retries (like main function)
    local attempt=1
    local measurement=""

    while [ $attempt -le "$MAX_RETRIES" ]; do
        log_debug "Sensor measurement attempt $attempt/$MAX_RETRIES"

        # Get spotread output
        local spotread_output=$(measure_with_spotread "$SPOTREAD_TIMEOUT" "$SPOTREAD_CMD")
        local spotread_exit_code=$?

        # Parse the measurement
        if [ $spotread_exit_code -eq 0 ] && [ -n "$spotread_output" ]; then
            measurement=$(parse_measurement "$spotread_output")

            if [ -n "$measurement" ]; then
                log_debug "Successful sensor measurement: $measurement"
                break
            fi
        fi

        log_debug "Sensor measurement attempt $attempt failed"

        # Try USB reset on communication failure if enabled
        if [ "$AUTO_USB_RESET" = "yes" ] && [ $attempt -eq 2 ]; then
            reset_usb_device
        fi

        attempt=$((attempt + 1))
        [ $attempt -le "$MAX_RETRIES" ] && sleep "$RETRY_DELAY"
    done

    # Format and output results
    if [ -n "$measurement" ]; then
        local x_val=$(echo "$measurement" | awk '{print $1}')
        local y_val=$(echo "$measurement" | awk '{print $2}')
        local z_val=$(echo "$measurement" | awk '{print $3}')
        local yc_val=$(echo "$measurement" | awk '{print $4}')
        local x_chr=$(echo "$measurement" | awk '{print $5}')
        local y_chr=$(echo "$measurement" | awk '{print $6}')

        output_measurement "$timestamp" "$temp" "SENSOR" "$x_val" "$y_val" "$z_val" "$yc_val" "$x_chr" "$y_chr" "$voltage" "$current" "$BRLEVEL" "no" "$temp2"
        return 0
    else
        log_error "Sensor measurement failed after $MAX_RETRIES attempts"
        output_measurement "$timestamp" "$temp" "SENSOR" "ERROR" "ERROR" "ERROR" "ERROR" "ERROR" "ERROR" "$voltage" "$current" "$BRLEVEL" "yes" "$temp2"
        return 1
    fi
}

# Display pattern using mplayclt or disp-tester
display_pattern() {
    local pattern_file="$1"
    local color_prefix="$2"
    local mplayclt_path="$3"

    if [ "$PATTERN_SOURCE" = "disp-tester" ]; then
        # Use disp-tester for pattern display
        local color_name=$(get_disp_tester_color "$color_prefix")
        if [ "$color_name" = "unknown" ]; then
            log_error "Unknown color prefix for disp-tester: $color_prefix"
            return 1
        fi
        display_pattern_disp_tester "$color_name"
        return $?
    else
        # Use mplayclt for pattern display (original behavior)
        if [ "$pattern_file" = "none" ] || [ ! -f "$pattern_file" ]; then
            return 0
        fi

        if [ -x "$mplayclt_path" ]; then
            log_debug "Displaying pattern: $pattern_file"
            if "$mplayclt_path" --showimg=none --showimg="$pattern_file" >/dev/null 2>&1; then
                # Wait for pattern to stabilize (only for mplayclt)
                sleep "$MEASUREMENT_DELAY"
                return 0
            else
                log_error "Failed to display pattern with mplayclt: $pattern_file"
                return 1
            fi
        else
            log_info "Pattern: $(basename "$pattern_file") (mplayclt not available)"
            return 0
        fi
    fi
}

# Enhanced spotread measurement with robust parsing
measure_with_spotread() {
    local timeout_duration="$1"
    local spotread_cmd="$2"

    # Use timeout if available, otherwise rely on spotread's own timeout
    if command -v timeout >/dev/null 2>&1; then
        timeout "$timeout_duration" "$spotread_cmd" -x -O 2>/dev/null
    else
        "$spotread_cmd" -x -O 2>/dev/null
    fi
}

# Parse spotread output and extract XYZ and Yxy values
parse_measurement() {
    local output="$1"

    echo "$output" | awk '
    /Result is XYZ:/ {
        # Remove "Result is", "XYZ:", "Yxy:", and commas
        gsub(/Result is |XYZ:|Yxy:|,/, "")

        # Ensure we have at least 6 numeric values
        if (NF >= 6) {
            # Print X Y Z Yc x y (6 values)
            printf "%.6f %.6f %.6f %.6f %.6f %.6f\n", $1, $2, $3, $4, $5, $6
            exit 0
        }
    }
    END { exit 1 }'
}

# Main color measurement function with comprehensive error handling
measure_color() {
    local pattern_file="$1"
    local color_prefix="$2"
    local tempered_path="$3"
    local mplayclt_path="$4"
    local power_path="$5"
    local br_level="$6"

    log_debug "Starting measurement for color: $color_prefix"

    # Display pattern with error checking
    if [ "$SENSORONLY" != "yes" ]; then
        if ! display_pattern "$pattern_file" "$color_prefix" "$mplayclt_path"; then
            log_error "Failed to display pattern for $color_prefix"
            return 1
        fi

        # For mplayclt, measurement delay is handled in display_pattern
        # For disp-tester, delay and verification are handled in display_pattern_disp_tester
    fi

    # Get timestamp
    local timestamp=$(date "+%m/%d/%y,%H:%M:%S")

    # Measure primary temperature if sensor available
    local temp=$(read_temperature "$TEMPERED" "$tempered_path")
    log_debug "Primary temperature: $temp"

    # Measure secondary temperature if enabled
    local temp2="N/A"
    if [ "$TEMP2_ENABLED" = "yes" ]; then
        temp2=$(read_temperature "$TEMP2" "")
        log_debug "Secondary temperature: $temp2"
    fi

    # Measure power if available
    local voltage="N/A"
    local current="N/A"
    if [ -x "$power_path" ]; then
        local power_output=$("$power_path" status 2>/dev/null)
        voltage=$(echo "$power_output" | awk '{print $2}' 2>/dev/null || echo "N/A")
        current=$(echo "$power_output" | awk '{print $5}' 2>/dev/null || echo "N/A")
        log_debug "Power: ${voltage}V, ${current}A"
    fi

    # Attempt color measurement with retries
    local attempt=1
    local measurement=""

    while [ $attempt -le "$MAX_RETRIES" ]; do
        log_debug "Measurement attempt $attempt/$MAX_RETRIES for $color_prefix"

        # Get spotread output
        local spotread_output=$(measure_with_spotread "$SPOTREAD_TIMEOUT" "$SPOTREAD_CMD")
        local spotread_exit_code=$?

        # Parse the measurement
        if [ $spotread_exit_code -eq 0 ] && [ -n "$spotread_output" ]; then
            measurement=$(parse_measurement "$spotread_output")

            if [ -n "$measurement" ]; then
                log_debug "Successful measurement for $color_prefix: $measurement"
                break
            fi
        fi

        log_error "Measurement attempt $attempt failed for $color_prefix"

        # Try USB reset on communication failure if enabled
        if [ "$AUTO_USB_RESET" = "yes" ] && [ $attempt -eq 2 ]; then
            reset_usb_device
        fi

        attempt=$((attempt + 1))
        [ $attempt -le "$MAX_RETRIES" ] && sleep "$RETRY_DELAY"
    done

    # Format and output results
    if [ -n "$measurement" ]; then
        local x_val=$(echo "$measurement" | awk '{print $1}')
        local y_val=$(echo "$measurement" | awk '{print $2}')
        local z_val=$(echo "$measurement" | awk '{print $3}')
        local yc_val=$(echo "$measurement" | awk '{print $4}')
        local x_chr=$(echo "$measurement" | awk '{print $5}')
        local y_chr=$(echo "$measurement" | awk '{print $6}')

        output_measurement "$timestamp" "$temp" "$color_prefix" "$x_val" "$y_val" "$z_val" "$yc_val" "$x_chr" "$y_chr" "$voltage" "$current" "$br_level" "no" "$temp2"

        # Display measurement metadata on disp-tester if successful
        display_measurement_metadata "$x_val" "$y_val" "$z_val"

        return 0
    else
        log_error "All measurement attempts failed for $color_prefix"
        output_measurement "$timestamp" "$temp" "$color_prefix" "ERROR" "ERROR" "ERROR" "ERROR" "ERROR" "ERROR" "$voltage" "$current" "$br_level" "yes" "$temp2"
        return 1
    fi
}

# Load configuration file if it exists
load_config() {
    local config_file="${MYPATH}/measure-color.conf"
    if [ -f "$config_file" ]; then
        log_info "Loading configuration from: $config_file"
        . "$config_file"
    fi
}

# Parse command line arguments
parse_arguments() {
    local noargs="yes"

    for arg in "$@"; do
        case "$arg" in
            --mypath=*) MYPATH="${arg#*=}"; noargs="no" ;;
            --patternpath=*) PATTERNPATH="${arg#*=}"; noargs="no" ;;
            --loop=*) LOOPCOUNT="${arg#*=}"; noargs="no" ;;
            --noheader=*) NOHEADER="${arg#*=}"; noargs="no" ;;
            --quiet=*) QUIET="${arg#*=}"; noargs="no" ;;
            --interval=*) INTERVAL="${arg#*=}"; noargs="no" ;;
            --temp=*) TEMPERED="${arg#*=}"; noargs="no" ;;
            --temp2=*) TEMP2="${arg#*=}"; TEMP2_ENABLED="yes"; noargs="no" ;;
            --power=*) POWER="${arg#*=}"; noargs="no" ;;
            --wfile=*) WFILE="${arg#*=}"; noargs="no" ;;
            --rfile=*) RFILE="${arg#*=}"; noargs="no" ;;
            --gfile=*) GFILE="${arg#*=}"; noargs="no" ;;
            --bfile=*) BFILE="${arg#*=}"; noargs="no" ;;
            --cfile=*) CFILE="${arg#*=}"; noargs="no" ;;
            --mfile=*) MFILE="${arg#*=}"; noargs="no" ;;
            --yfile=*) YFILE="${arg#*=}"; noargs="no" ;;
            --brlevel=*) BRLEVEL="${arg#*=}"; noargs="no" ;;
            --startupimg=*) STARTUPIMG="${arg#*=}"; noargs="no" ;;
            --sensor-only=*) SENSORONLY="${arg#*=}"; noargs="no" ;;
            --measureonly=*) MEASUREONLY="${arg#*=}"; noargs="no" ;;
            --debug=*) DEBUG="${arg#*=}"; noargs="no" ;;
            --spotread-cmd=*) SPOTREAD_CMD="${arg#*=}"; noargs="no" ;;
            --measurement-delay=*) MEASUREMENT_DELAY="${arg#*=}"; noargs="no" ;;
            --retry-delay=*) RETRY_DELAY="${arg#*=}"; noargs="no" ;;
            --max-retries=*) MAX_RETRIES="${arg#*=}"; noargs="no" ;;
            --timeout=*) SPOTREAD_TIMEOUT="${arg#*=}"; noargs="no" ;;
            --usb-reset=*) AUTO_USB_RESET="${arg#*=}"; noargs="no" ;;
            --format=*) FORMAT="${arg#*=}"; noargs="no" ;;
            --compact=*) COMPACT="${arg#*=}"; noargs="no" ;;
            --pattern-source=*) PATTERN_SOURCE="${arg#*=}"; noargs="no" ;;
            --disp-tester-host=*) DISP_TESTER_HOST="${arg#*=}"; noargs="no" ;;
            --disp-tester-port=*) DISP_TESTER_PORT="${arg#*=}"; noargs="no" ;;
            --help|-h) echo "$USAGE"; exit 0 ;;
            *) log_error "Unknown option: $arg"; echo "$USAGE"; exit 1 ;;
        esac
    done

    if [ "$noargs" = "yes" ]; then
        echo "$USAGE"
        exit 0
    fi

    # Validate format option
    case "$FORMAT" in
        csv|json) ;;
        *) log_error "Invalid format: $FORMAT (must be 'csv' or 'json')"; exit 1 ;;
    esac

    # Validate compact option
    case "$COMPACT" in
        yes|no) ;;
        *) log_error "Invalid compact option: $COMPACT (must be 'yes' or 'no')"; exit 1 ;;
    esac

    # Validate pattern source option
    case "$PATTERN_SOURCE" in
        mplayclt|disp-tester) ;;
        *) log_error "Invalid pattern source: $PATTERN_SOURCE (must be 'mplayclt' or 'disp-tester')"; exit 1 ;;
    esac

    # Validate DS18B20 specific device IDs
    if [ "$TEMPERED" != "none" ] && [ "$TEMPERED" != "ds18b20" ] && ! echo "$TEMPERED" | grep -q "^28-"; then
        # It's a legacy temper sensor name, keep as is
        :
    fi

    if [ "$TEMP2_ENABLED" = "yes" ]; then
        if [ "$TEMP2" != "ds18b20" ] && ! echo "$TEMP2" | grep -q "^28-"; then
            log_error "Invalid temp2 value: $TEMP2 (must be 'ds18b20' or device ID like '28-xxxxxxxxxxxx')"
            exit 1
        fi
    fi
}

# Validate configuration and setup paths
setup_environment() {
    # Setup paths
    local temperedpath="${MYPATH}/Output/usb-tempered/utils/${TEMPERED}"
    local powerpath="${MYPATH}/binaries/${POWER}"
    local mplayclt="${MYPATH}/brbox/output/bin/mplayclt"

    # Validate executables (only for non-DS18B20 temperature sensors)
    case "$TEMPERED" in
        "ds18b20"|28-*)
            temperedpath="ds18b20"  # Special marker for DS18B20
            ;;
        *)
            [ ! -x "$temperedpath" ] && temperedpath="none"
            ;;
    esac

    [ ! -x "$powerpath" ] && powerpath="none"
    [ ! -x "$mplayclt" ] && mplayclt="none"

    # Check for bc command if DS18B20 sensors are being used
    local need_bc="no"
    if [ "$TEMPERED" = "ds18b20" ] || echo "$TEMPERED" | grep -q "^28-"; then
        need_bc="yes"
    fi
    if [ "$TEMP2_ENABLED" = "yes" ] && ([ "$TEMP2" = "ds18b20" ] || echo "$TEMP2" | grep -q "^28-"); then
        need_bc="yes"
    fi

    if [ "$need_bc" = "yes" ]; then
        if ! check_command "bc"; then
            log_error "bc command required for DS18B20 temperature conversion but not found"
            log_info "Please install bc package: apt-get install bc (or equivalent for your system)"
            exit 1
        fi
    fi

    # Skip pattern validation in sensor-only mode
    if [ "$SENSORONLY" = "yes" ]; then
        TEMPEREDPATH="$temperedpath"
        POWERPATH="$powerpath"
        MPLAYCLT="$mplayclt"
        return 0
    fi

    # Setup pattern file paths based on pattern source
    if [ "$PATTERN_SOURCE" = "disp-tester" ]; then
        # For disp-tester, pattern files are just enable/disable flags
        # We don't need to validate actual files, just check if they're not "none"
        WFILEPATH="$WFILE"
        RFILEPATH="$RFILE"
        GFILEPATH="$GFILE"
        BFILEPATH="$BFILE"
        CFILEPATH="$CFILE"
        MFILEPATH="$MFILE"
        YFILEPATH="$YFILE"
    else
        # For mplayclt, validate actual pattern files
        local wfilepath="${PATTERNPATH}/${WFILE}"
        local rfilepath="${PATTERNPATH}/${RFILE}"
        local gfilepath="${PATTERNPATH}/${GFILE}"
        local bfilepath="${PATTERNPATH}/${BFILE}"
        local cfilepath="${PATTERNPATH}/${CFILE}"
        local mfilepath="${PATTERNPATH}/${MFILE}"
        local yfilepath="${PATTERNPATH}/${YFILE}"

        # Validate pattern files
        validate_file "$wfilepath" "White pattern" || wfilepath="none"
        validate_file "$rfilepath" "Red pattern" || rfilepath="none"
        validate_file "$gfilepath" "Green pattern" || gfilepath="none"
        validate_file "$bfilepath" "Blue pattern" || bfilepath="none"
        validate_file "$cfilepath" "Cyan pattern" || cfilepath="none"
        validate_file "$mfilepath" "Magenta pattern" || mfilepath="none"
        validate_file "$yfilepath" "Yellow pattern" || yfilepath="none"

        WFILEPATH="$wfilepath"
        RFILEPATH="$rfilepath"
        GFILEPATH="$gfilepath"
        BFILEPATH="$bfilepath"
        CFILEPATH="$cfilepath"
        MFILEPATH="$mfilepath"
        YFILEPATH="$yfilepath"
    fi

    # Set global variables
    TEMPEREDPATH="$temperedpath"
    POWERPATH="$powerpath"
    MPLAYCLT="$mplayclt"
}

# Main execution starts here
main() {
    # Load configuration and parse arguments
    load_config
    parse_arguments "$@"

    # Platform detection and optimization
    detect_platform

    # Validate environment
    setup_environment

    # Initialize JSON session tracking
    json_session_init

    # Check for required commands
    if ! check_command "$SPOTREAD_CMD"; then
        log_error "spotread command not found: $SPOTREAD_CMD"
        log_info "Please ensure ArgyllCMS is installed and spotread is in PATH"
        exit 1
    fi

    # Check for i1Display Pro
    check_i1_device || log_error "Proceeding anyway - device may still work"

    # Handle sensor-only mode
    if [ "$SENSORONLY" = "yes" ]; then
        log_info "Sensor-only mode - taking measurements"
        if [ "$FORMAT" = "csv" ] && [ "$NOHEADER" = "no" ]; then
            if [ "$TEMP2_ENABLED" = "yes" ]; then
                echo "DATE,TIME,temp,Sampled-Color,X,Y,Z,Y,x,y,voltage,current,brightnesslevel,temp2"
            else
                echo "DATE,TIME,temp,Sampled-Color,X,Y,Z,Y,x,y,voltage,current,brightnesslevel"
            fi
        fi

        # Sensor-only measurement loop
        local cycle=1
        local failed_measurements=0

        while [ $cycle -le "$LOOPCOUNT" ]; do
            log_debug "Sensor measurement cycle $cycle/$LOOPCOUNT"

            if ! measure_sensor_only; then
                failed_measurements=$((failed_measurements + 1))
            fi

            # Wait between cycles if requested
            if [ "$INTERVAL" != "none" ] && [ $cycle -lt "$LOOPCOUNT" ]; then
                log_debug "Waiting $INTERVAL seconds before next measurement..."
                sleep "$INTERVAL"
            fi

            cycle=$((cycle + 1))
        done

        json_session_output

        if [ $failed_measurements -gt 0 ]; then
            log_error "Some sensor measurements failed"
            exit 1
        fi
        exit 0
    fi

    # Initialize display if needed
    if [ "$MEASUREONLY" = "no" ] && [ "$STARTUPIMG" != "none" ]; then
        if [ "$PATTERN_SOURCE" = "disp-tester" ]; then
            # For disp-tester, show white pattern as startup
            display_pattern_disp_tester "white"
        else
            display_pattern "$WFILEPATH" "W" "$MPLAYCLT"
        fi
        sleep 2
    fi

    # Print CSV header (only for CSV format)
    if [ "$FORMAT" = "csv" ] && [ "$NOHEADER" = "no" ]; then
        if [ "$TEMP2_ENABLED" = "yes" ]; then
            echo "DATE,TIME,temp,Sampled-Color,X,Y,Z,Y,x,y,voltage,current,brightnesslevel,temp2"
        else
            echo "DATE,TIME,temp,Sampled-Color,X,Y,Z,Y,x,y,voltage,current,brightnesslevel"
        fi
    fi

    # Main measurement loop
    local cycle=1
    local total_measurements=0
    local failed_measurements=0

    while [ $cycle -le "$LOOPCOUNT" ]; do
        log_info "Starting measurement cycle $cycle/$LOOPCOUNT"

        # Measure each color pattern
        for color_config in "W:$WFILEPATH" "R:$RFILEPATH" "G:$GFILEPATH" "B:$BFILEPATH" "C:$CFILEPATH" "M:$MFILEPATH" "Y:$YFILEPATH"; do
            local color_prefix="${color_config%%:*}"
            local file_path="${color_config#*:}"

            # Check if this color should be measured
            local should_measure="no"
            if [ "$PATTERN_SOURCE" = "disp-tester" ]; then
                # For disp-tester, measure if the pattern file argument is not "none"
                [ "$file_path" != "none" ] && should_measure="yes"
            else
                # For mplayclt, measure if the file exists
                [ "$file_path" != "none" ] && [ -f "$file_path" ] && should_measure="yes"
            fi

            if [ "$should_measure" = "yes" ]; then
                total_measurements=$((total_measurements + 1))

                if ! measure_color "$file_path" "$color_prefix" "$TEMPEREDPATH" "$MPLAYCLT" "$POWERPATH" "$BRLEVEL"; then
                    failed_measurements=$((failed_measurements + 1))
                fi

                # Brief pause between color measurements
                sleep 2
            fi
        done

        # Wait between cycles if requested
        if [ "$INTERVAL" != "none" ] && [ $cycle -lt "$LOOPCOUNT" ]; then
            log_info "Waiting $INTERVAL seconds before next cycle..."
            sleep "$INTERVAL"
        fi

        cycle=$((cycle + 1))
    done

    # Output JSON session data if applicable
    json_session_output

    # Cleanup display
    if [ "$MEASUREONLY" = "no" ]; then
        if [ "$PATTERN_SOURCE" = "disp-tester" ]; then
            # Clear metadata and show black pattern
            send_disp_tester_command "set-metadata-text " >/dev/null 2>&1
            send_disp_tester_command "pattern black" >/dev/null 2>&1
        elif [ -x "$MPLAYCLT" ]; then
            "$MPLAYCLT" --showimg=none >/dev/null 2>&1
        fi
    fi

    # Report summary
    log_info "Measurement complete: $total_measurements total, $failed_measurements failed"

    if [ $failed_measurements -gt 0 ]; then
        log_error "Some measurements failed - check instrument connection and USB permissions"
        exit 1
    fi

    exit 0
}

# Execute main function with all arguments
main "$@"
