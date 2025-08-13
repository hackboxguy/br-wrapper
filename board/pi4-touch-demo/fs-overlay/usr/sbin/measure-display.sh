#!/bin/sh
# Enhanced Color Measurement Script for Display Analysis
# Optimized for Pi4 Buildroot with i1Display Pro
# Dependencies: spotread (ArgyllCMS), optional: mplayclt, temperature/power sensors

# Default configuration - can be overridden by config file or command line
LOOPCOUNT="1"
INTERVAL="none"
TEMPERED="none"
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

# Hardware identifiers for i1Display Pro
I1_VENDOR_ID="0765"
I1_PRODUCT_ID="5020"

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

PATTERN FILES (use 'none' to skip):
    --wfile=FILE           White pattern file
    --rfile=FILE           Red pattern file  
    --gfile=FILE           Green pattern file
    --bfile=FILE           Blue pattern file
    --cfile=FILE           Cyan pattern file
    --mfile=FILE           Magenta pattern file
    --yfile=FILE           Yellow pattern file
    --startupimg=FILE      Initial pattern to display

HARDWARE OPTIONS:
    --temp=BINARY          Temperature sensor binary name
    --power=BINARY         Power measurement binary name

ADVANCED OPTIONS:
    --debug=yes/no         Enable debug output (default: no)
    --spotread-cmd=CMD     spotread command to use (default: spotread)
    --measurement-delay=S  Delay after pattern display (default: 5)
    --retry-delay=S        Delay between retry attempts (default: 3)
    --max-retries=N        Maximum measurement attempts (default: 3)
    --timeout=S            Timeout for spotread command (default: 30)
    --usb-reset=yes/no     Auto reset USB device on failure (default: yes)

EXAMPLES:
    # Just measure current sensor value (no pattern control, clean output)
    $0 --sensor-only=yes --quiet=yes --noheader=yes
    
    # Basic white measurement
    $0 --wfile=white.png --measureonly=yes
    
    # Full RGB measurement with 3 cycles
    $0 --wfile=white.png --rfile=red.png --gfile=green.png --bfile=blue.png --loop=3
    
    # Automated measurement with temperature monitoring
    $0 --wfile=white.png --temp=tempered --interval=60 --loop=10"

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

# Simple sensor-only measurement (no pattern control)
measure_sensor_only() {
    local temp="N/A"
    local voltage="N/A" 
    local current="N/A"
    
    # Get timestamp
    local timestamp=$(date "+%m/%d/%y,%H:%M:%S")
    
    # Measure temperature if sensor available
    if [ -x "$TEMPEREDPATH" ]; then
        temp=$("$TEMPEREDPATH" 2>/dev/null | awk '{print $4}' 2>/dev/null || echo "ERROR")
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
        
        echo "$timestamp,$temp,SENSOR,$x_val,$y_val,$z_val,$yc_val,$x_chr,$y_chr,$voltage,$current,$BRLEVEL"
        return 0
    else
        log_error "Sensor measurement failed after $MAX_RETRIES attempts"
        echo "$timestamp,$temp,SENSOR,ERROR,ERROR,ERROR,ERROR,ERROR,ERROR,$voltage,$current,$BRLEVEL"
        return 1
    fi
}

# Display pattern using mplayclt or log message
display_pattern() {
    local pattern_file="$1"
    local mplayclt_path="$2"
    
    if [ "$pattern_file" = "none" ] || [ ! -f "$pattern_file" ]; then
        return 0
    fi
    
    if [ -x "$mplayclt_path" ]; then
        log_debug "Displaying pattern: $pattern_file"
        if "$mplayclt_path" --showimg=none --showimg="$pattern_file" >/dev/null 2>&1; then
            return 0
        else
            log_error "Failed to display pattern with mplayclt: $pattern_file"
            return 1
        fi
    else
        log_info "Pattern: $(basename "$pattern_file") (mplayclt not available)"
        return 0
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
        if ! display_pattern "$pattern_file" "$mplayclt_path"; then
            log_error "Failed to display pattern for $color_prefix"
        fi
        # Wait for pattern to stabilize
        sleep "$MEASUREMENT_DELAY"
    fi
    
    # Get timestamp
    local timestamp=$(date "+%m/%d/%y,%H:%M:%S")
    
    # Measure temperature if sensor available
    local temp="N/A"
    if [ -x "$tempered_path" ]; then
        temp=$("$tempered_path" 2>/dev/null | awk '{print $4}' 2>/dev/null || echo "ERROR")
        log_debug "Temperature: $temp"
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
        
        echo "$timestamp,$temp,$color_prefix,$x_val,$y_val,$z_val,$yc_val,$x_chr,$y_chr,$voltage,$current,$br_level"
        return 0
    else
        log_error "All measurement attempts failed for $color_prefix"
        echo "$timestamp,$temp,$color_prefix,ERROR,ERROR,ERROR,ERROR,ERROR,ERROR,$voltage,$current,$br_level"
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
            --help|-h) echo "$USAGE"; exit 0 ;;
            *) log_error "Unknown option: $arg"; echo "$USAGE"; exit 1 ;;
        esac
    done
    
    if [ "$noargs" = "yes" ]; then
        echo "$USAGE"
        exit 0
    fi
}

# Validate configuration and setup paths
setup_environment() {
    # Setup paths
    local temperedpath="${MYPATH}/Output/usb-tempered/utils/${TEMPERED}"
    local powerpath="${MYPATH}/binaries/${POWER}"
    local mplayclt="${MYPATH}/brbox/output/bin/mplayclt"
    
    # Validate executables
    [ ! -x "$temperedpath" ] && temperedpath="none"
    [ ! -x "$powerpath" ] && powerpath="none"
    [ ! -x "$mplayclt" ] && mplayclt="none"
    
    # Skip pattern validation in sensor-only mode
    if [ "$SENSORONLY" = "yes" ]; then
        TEMPEREDPATH="$temperedpath"
        POWERPATH="$powerpath"
        MPLAYCLT="$mplayclt"
        return 0
    fi
    
    # Setup pattern file paths (only when needed)
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
    
    # Export paths for use in measurement function
    TEMPEREDPATH="$temperedpath"
    POWERPATH="$powerpath"
    MPLAYCLT="$mplayclt"
    WFILEPATH="$wfilepath"
    RFILEPATH="$rfilepath"
    GFILEPATH="$gfilepath"
    BFILEPATH="$bfilepath"
    CFILEPATH="$cfilepath"
    MFILEPATH="$mfilepath"
    YFILEPATH="$yfilepath"
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
    
    # Check for required commands
    if ! check_command "$SPOTREAD_CMD"; then
        log_error "spotread command not found: $SPOTREAD_CMD"
        log_info "Please ensure ArgyllCMS is installed and spotread is in PATH"
        exit 1
    fi
    
    # Check for i1Display Pro
    check_i1_device || log_error "Proceeding anyway - device may still work"
    
    # Handle sensor-only mode (just take one measurement)
    if [ "$SENSORONLY" = "yes" ]; then
        log_info "Sensor-only mode - taking single measurement"
        if [ "$NOHEADER" = "no" ]; then
            echo "DATE,TIME,temp,Sampled-Color,X,Y,Z,Y,x,y,voltage,current,brightnesslevel"
        fi
        measure_sensor_only
        exit $?
    fi
    
    # Initialize display if needed
    if [ "$MEASUREONLY" = "no" ] && [ "$STARTUPIMG" != "none" ]; then
        display_pattern "$WFILEPATH" "$MPLAYCLT"
        sleep 2
    fi
    
    # Print CSV header
    if [ "$NOHEADER" = "no" ]; then
        echo "DATE,TIME,temp,Sampled-Color,X,Y,Z,Y,x,y,voltage,current,brightnesslevel"
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
            
            if [ "$file_path" != "none" ]; then
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
    
    # Cleanup display
    if [ "$MEASUREONLY" = "no" ] && [ -x "$MPLAYCLT" ]; then
        "$MPLAYCLT" --showimg=none >/dev/null 2>&1
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
