#!/bin/sh
# Display Gamut Measurement Wrapper Script
# Uses measure-display.sh to perform automated gamut measurements with pattern-generator

# Hardware identifiers for i1Display Pro
I1_VENDOR_ID="0765"
I1_PRODUCT_ID="5020"

# Default values
APPLAUNCHER="127.0.0.1:8081"
STARTLOOP="3"
ENDLOOP="3"
WHITESHIFTLOOP=""
OUTPUTFOLDER=""
WHITESHIFTINTERVAL=""
TEMP="ds18b20"
TEMP2=""
VERBOSE="no"

# Usage information
USAGE="Display Gamut Measurement Wrapper

USAGE: $0 [OPTIONS]

REQUIRED OPTIONS:
    --whiteshiftloop=COUNT     Number of loops for white shift measurement
    --outputfolder=PATH        Base output directory path
    --whiteshiftinterval=SEC   Interval between white shift measurements

OPTIONAL OPTIONS:
    --applauncher=HOST:PORT     App launcher daemon host:port (default: 127.0.0.1:8081)
    --startloop=COUNT          Number of loops for initial gamut measurement (default: 3)
    --endloop=COUNT            Number of loops for final gamut measurement (default: 3)
    --temp=ds18b20/ID          Primary temperature sensor: 'ds18b20' (first available) or device ID (28-xxxxxxxxxxxx) (default: ds18b20)
    --temp2=ds18b20/ID         Second temperature sensor: 'ds18b20' (first available) or device ID (28-xxxxxxxxxxxx)
    --verbose                  Enable detailed logging

EXAMPLE:
    $0 --applauncher=127.0.0.1:8081 --startloop=4 --endloop=3 --whiteshiftloop=400 --outputfolder=/mnt/data --whiteshiftinterval=10

TEMPERATURE SENSOR EXAMPLES:
    # Single DS18B20 sensor (primary only, using first available)
    $0 --whiteshiftloop=100 --outputfolder=/mnt/data --whiteshiftinterval=5

    # Single DS18B20 sensor with specific device ID
    $0 --temp=28-3ce1e38018ca --whiteshiftloop=100 --outputfolder=/mnt/data --whiteshiftinterval=5

    # Two DS18B20 sensors (specific primary + first available secondary)
    $0 --temp=28-3ce1e38018ca --temp2=ds18b20 --whiteshiftloop=100 --outputfolder=/mnt/data --whiteshiftinterval=5

    # Two specific DS18B20 sensors
    $0 --temp=28-3ce1e38018ca --temp2=28-3ce10457528d --whiteshiftloop=100 --outputfolder=/mnt/data --whiteshiftinterval=5"

# Logging functions
log_error() {
    echo "ERROR: $*" >&2
}

log_info() {
    if [ "$VERBOSE" = "yes" ]; then
        echo "INFO: $*"
    fi
}

log_basic() {
    echo "$*"
}

# Check for i1Display Pro USB device
check_i1_device() {
    if command -v lsusb >/dev/null 2>&1; then
        if lsusb | grep -q "${I1_VENDOR_ID}:${I1_PRODUCT_ID}"; then
            log_info "i1Display Pro found (${I1_VENDOR_ID}:${I1_PRODUCT_ID})"
            return 0
        else
            log_error "i1Display Pro not found (VID:${I1_VENDOR_ID} PID:${I1_PRODUCT_ID})"
            if [ "$VERBOSE" = "yes" ]; then
                log_error "Available USB devices:"
                lsusb 2>/dev/null || log_error "lsusb command not available"
            fi
            return 1
        fi
    else
        log_error "lsusb not available - cannot check USB device"
        return 1
    fi
}

# Send command to app launcher
send_app_command() {
    local command="$1"
    local host_port="$2"
    local host="${host_port%:*}"
    local port="${host_port#*:}"

    echo "$command" | nc -q 0 "$host" "$port" 2>/dev/null
    return $?
}

# Check if pattern-generator is running
check_pattern_generator() {
    local applauncher="$1"
    local response

    response=$(send_app_command "get-running-app" "$applauncher")
    if [ $? -ne 0 ]; then
        log_error "Failed to communicate with app launcher at $applauncher"
        return 1
    fi

    if [ "$response" = "pattern-generator" ]; then
        log_info "Pattern-generator is already running"
        return 0
    else
        log_info "Pattern-generator not running (current: $response)"
        return 1
    fi
}

# Stop current app
stop_current_app() {
    local applauncher="$1"

    log_info "Stopping current app..."
    send_app_command "stop-app" "$applauncher"
    if [ $? -ne 0 ]; then
        log_error "Failed to stop current app"
        return 1
    fi
    return 0
}

# Start pattern-generator
start_pattern_generator() {
    local applauncher="$1"

    log_info "Starting pattern-generator..."
    send_app_command "start-app pattern-generator" "$applauncher"
    if [ $? -ne 0 ]; then
        log_error "Failed to start pattern-generator"
        return 1
    fi
    log_info "Pattern-generator started successfully"

    # Give pattern-generator time to initialize
    log_info "Waiting for pattern-generator to initialize..."
    sleep 3

    return 0
}

# Parse command line arguments
parse_arguments() {
    local noargs="yes"

    for arg in "$@"; do
        case "$arg" in
            --applauncher=*) APPLAUNCHER="${arg#*=}"; noargs="no" ;;
            --startloop=*) STARTLOOP="${arg#*=}"; noargs="no" ;;
            --endloop=*) ENDLOOP="${arg#*=}"; noargs="no" ;;
            --whiteshiftloop=*) WHITESHIFTLOOP="${arg#*=}"; noargs="no" ;;
            --outputfolder=*) OUTPUTFOLDER="${arg#*=}"; noargs="no" ;;
            --whiteshiftinterval=*) WHITESHIFTINTERVAL="${arg#*=}"; noargs="no" ;;
            --temp=*) TEMP="${arg#*=}"; noargs="no" ;;
            --temp2=*) TEMP2="${arg#*=}"; noargs="no" ;;
            --verbose) VERBOSE="yes"; noargs="no" ;;
            --help|-h) echo "$USAGE"; exit 0 ;;
            *) log_error "Unknown option: $arg"; echo "$USAGE"; exit 1 ;;
        esac
    done

    if [ "$noargs" = "yes" ]; then
        echo "$USAGE"
        exit 0
    fi

    # Validate required arguments
    if [ -z "$WHITESHIFTLOOP" ] || [ -z "$OUTPUTFOLDER" ] || [ -z "$WHITESHIFTINTERVAL" ]; then
        log_error "Missing required arguments"
        echo "$USAGE"
        exit 1
    fi

    # Validate numeric arguments
    if ! echo "$STARTLOOP" | grep -q '^[0-9]\+$'; then
        log_error "startloop must be a positive integer"
        exit 1
    fi
    if ! echo "$ENDLOOP" | grep -q '^[0-9]\+$'; then
        log_error "endloop must be a positive integer"
        exit 1
    fi
    if ! echo "$WHITESHIFTLOOP" | grep -q '^[0-9]\+$'; then
        log_error "whiteshiftloop must be a positive integer"
        exit 1
    fi
    if ! echo "$WHITESHIFTINTERVAL" | grep -q '^[0-9]\+$'; then
        log_error "whiteshiftinterval must be a positive integer"
        exit 1
    fi
}

# Main function
main() {
    # Parse arguments
    parse_arguments "$@"

    log_basic "Starting gamut measurement..."
    log_info "Starting display gamut measurement sequence"
    log_info "App launcher: $APPLAUNCHER"
    log_info "Output folder: $OUTPUTFOLDER"

    # Step 1: Check for i1Display Pro USB device
    log_basic "Checking USB sensor..."
    log_info "Checking for i1Display Pro USB device..."
    if ! check_i1_device; then
        log_error "i1Display Pro not found - exiting"
        exit 1
    fi

    # Step 2: Check if pattern-generator is running
    log_basic "Setting up pattern generator..."
    log_info "Checking pattern-generator status..."
    if ! check_pattern_generator "$APPLAUNCHER"; then
        # Step 3: Stop current app and start pattern-generator
        if ! stop_current_app "$APPLAUNCHER"; then
            log_error "Failed to stop current app - exiting"
            exit 1
        fi

        # Step 4: Start pattern-generator
        if ! start_pattern_generator "$APPLAUNCHER"; then
            log_error "Failed to start pattern-generator - exiting"
            exit 1
        fi
    fi

    # Step 5: Create timestamped output directory
    local datetime=$(date "+%d%m%y-%H%M%S")
    local output_dir="${OUTPUTFOLDER}/${datetime}"

    log_info "Creating output directory: $output_dir"
    if ! mkdir -p "$output_dir"; then
        log_error "Failed to create output directory: $output_dir"
        exit 1
    fi

    # Build measure-display.sh command arguments
    local temp_args="--temp=$TEMP"
    if [ -n "$TEMP2" ]; then
        temp_args="$temp_args --temp2=$TEMP2"
        log_info "Using dual temperature sensors: $TEMP + $TEMP2"
    else
        log_info "Using single temperature sensor: $TEMP"
    fi

    # Step 6: Initial gamut measurement (startloop)
    if [ "$STARTLOOP" -gt 0 ]; then
        log_basic "Measuring initial gamut (WRGB, $STARTLOOP loops)..."
        log_info "Starting startloop measurement (WRGB, $STARTLOOP loops)..."
        if ! measure-display.sh --pattern-source=disp-tester --wfile=enable --rfile=enable --gfile=enable --bfile=enable --loop="$STARTLOOP" --measurement-delay=1 --quiet=yes $temp_args > "$output_dir/start-gamut.csv"; then
            log_error "Initial gamut measurement failed - exiting"
            exit 1
        fi
        log_info "Initial gamut measurement completed"
    else
        log_info "Skipping initial gamut measurement (startloop=0)"
    fi

    # Step 7: White shift measurement (always required)
    log_basic "Measuring white shift ($WHITESHIFTLOOP loops, ${WHITESHIFTINTERVAL}s intervals)..."
    log_info "Starting whiteshift measurement (White only, $WHITESHIFTLOOP loops, ${WHITESHIFTINTERVAL}s interval)..."
    log_info "Full command: measure-display.sh --pattern-source=disp-tester --wfile=enable --loop=$WHITESHIFTLOOP --measurement-delay=1 --quiet=yes --interval=$WHITESHIFTINTERVAL $temp_args"
    if ! measure-display.sh --pattern-source=disp-tester --wfile=enable --loop="$WHITESHIFTLOOP" --measurement-delay=1 --quiet=yes --interval="$WHITESHIFTINTERVAL" $temp_args > "$output_dir/whiteshift.csv"; then
        log_error "White shift measurement failed - exiting"
        exit 1
    fi
    log_info "White shift measurement completed"

    # Step 8: Final gamut measurement (endloop)
    if [ "$ENDLOOP" -gt 0 ]; then
        log_basic "Measuring final gamut (WRGB, $ENDLOOP loops)..."
        log_info "Starting endloop measurement (WRGB, $ENDLOOP loops)..."
        if ! measure-display.sh --pattern-source=disp-tester --wfile=enable --rfile=enable --gfile=enable --bfile=enable --loop="$ENDLOOP" --measurement-delay=1 --quiet=yes $temp_args > "$output_dir/end-gamut.csv"; then
            log_error "Final gamut measurement failed - exiting"
            exit 1
        fi
        log_info "Final gamut measurement completed"
    else
        log_info "Skipping final gamut measurement (endloop=0)"
    fi

    # Success - adjust summary based on what was actually measured
    local files_created=""
    if [ "$STARTLOOP" -gt 0 ]; then
        files_created="$files_created\n  - start-gamut.csv (initial WRGB measurement)"
    fi
    files_created="$files_created\n  - whiteshift.csv (white shift over time)"
    if [ "$ENDLOOP" -gt 0 ]; then
        files_created="$files_created\n  - end-gamut.csv (final WRGB measurement)"
    fi

    log_basic "Measurement completed! Results in: $output_dir"
    log_info "Display gamut measurement sequence completed successfully"
    log_info "Results saved in: $output_dir"
    log_info "Files created:$files_created"

    exit 0
}

# Execute main function with all arguments
main "$@"
