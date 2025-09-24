#!/bin/sh

# Automotive touch display non-responsive detection script
# Usage: ./detect-non-touch.sh --loop=1000 --waitsec=30 --ythreshold=15 --retry=2 --toolspath=/path/to/tools/

# Default values
LOOP_COUNT=100
WAIT_SEC=30
Y_THRESHOLD=15
CHECK_DISPLAY_STATUS="no"
TOOLS_PATH=""
RETRY_COUNT=2

# Check if no arguments provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 [OPTIONS]"
    echo "Options:"
    echo "  --loop=N                    Number of power cycles (default: 100)"
    echo "  --waitsec=N                Wait seconds for off/on cycles (default: 30)"
    echo "  --ythreshold=N             Minimum Y luminance delta for responsive touch (default: 15)"
    echo "  --retry=N                  Number of retry attempts for touch (default: 2)"
    echo "  --toolspath=PATH           Path prefix for tools (default: current script directory)"
    echo "  --check-display-on-status=yes/no  Check display power status (placeholder, default: no)"
    echo "  --help, -h                 Show this help message"
    echo ""
    echo "Example: $0 --loop=1000 --waitsec=30 --ythreshold=15"
    exit 0
fi

# Parse command line arguments
for arg in "$@"; do
    case $arg in
        --loop=*)
            LOOP_COUNT="${arg#*=}"
            ;;
        --waitsec=*)
            WAIT_SEC="${arg#*=}"
            ;;
        --ythreshold=*)
            Y_THRESHOLD="${arg#*=}"
            ;;
        --retry=*)
            RETRY_COUNT="${arg#*=}"
            ;;
        --toolspath=*)
            TOOLS_PATH="${arg#*=}"
            ;;
        --check-display-on-status=*)
            CHECK_DISPLAY_STATUS="${arg#*=}"
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  --loop=N                    Number of power cycles (default: 100)"
            echo "  --waitsec=N                Wait seconds for off/on cycles (default: 30)"
            echo "  --ythreshold=N             Minimum Y luminance delta for responsive touch (default: 15)"
            echo "  --retry=N                  Number of retry attempts for touch (default: 2)"
            echo "  --toolspath=PATH           Path prefix for tools (default: current script directory)"
            echo "  --check-display-on-status=yes/no  Check display power status (placeholder, default: no)"
            echo "  --help, -h                 Show this help message"
            exit 0
            ;;
        *)
            echo "Error: Unknown argument $arg"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Validate arguments
if ! echo "$LOOP_COUNT" | grep -q '^[0-9]\+$' || [ "$LOOP_COUNT" -le 0 ]; then
    echo "Error: loop count must be a positive integer"
    exit 1
fi

if ! echo "$WAIT_SEC" | grep -q '^[0-9]\+$' || [ "$WAIT_SEC" -le 0 ]; then
    echo "Error: wait seconds must be a positive integer"
    exit 1
fi

if ! echo "$Y_THRESHOLD" | grep -q '^[0-9]\+\(\.[0-9]\+\)\?$'; then
    echo "Error: Y threshold must be a positive number"
    exit 1
fi

if ! echo "$RETRY_COUNT" | grep -q '^[0-9]\+$' || [ "$RETRY_COUNT" -lt 0 ]; then
    echo "Error: retry count must be a non-negative integer"
    exit 1
fi

# Set up tool paths
if [ -n "$TOOLS_PATH" ]; then
    # Remove trailing slash if present and add one
    TOOLS_PATH=$(echo "$TOOLS_PATH" | sed 's/\/*$/\//')
    USB_RELAY_CMD="${TOOLS_PATH}usb-relay.sh"
    MEASURE_DISPLAY_CMD="${TOOLS_PATH}measure-display.sh"
else
    # Use current directory (relative to script location)
    SCRIPT_DIR=$(dirname "$0")
    USB_RELAY_CMD="${SCRIPT_DIR}/usb-relay.sh"
    MEASURE_DISPLAY_CMD="${SCRIPT_DIR}/measure-display.sh"
fi

# Function to extract Y value from measure-display.sh output
extract_y_value() {
    # Parse CSV output and extract Y value (5th column)
    echo "$1" | tail -1 | cut -d',' -f5
}

# Function to measure display without touch with retry logic
measure_display_only() {
    local attempt=0
    while [ "$attempt" -le "$RETRY_COUNT" ]; do
        if [ "$attempt" -gt 0 ]; then
            echo "    Measurement retry attempt $attempt/$RETRY_COUNT"
        fi
        
        MEASUREMENT=$($MEASURE_DISPLAY_CMD --measureonly=yes --sensor-only=yes --noheader=yes 2>/dev/null)
        if [ $? -eq 0 ] && [ -n "$MEASUREMENT" ]; then
            echo "$MEASUREMENT"
            return 0
        fi
        
        if [ "$attempt" -lt "$RETRY_COUNT" ]; then
            echo "    Measurement failed - retrying..."
            sleep 1
        fi
        
        attempt=$((attempt + 1))
    done
    
    echo "Error: Failed to measure display after $RETRY_COUNT retries"
    return 1
}

# Function to perform touch and then measure with retry logic
measure_after_touch() {
    # Wait 2 seconds before touch (as in your working sequence)
    sleep 2
    
    # Generate touch event with proper timing
    gpioset 0 27=1
    sleep 1
    gpioset 0 27=0
    
    # Wait 3 seconds after touch before measuring
    sleep 3
    
    # Measure display with retry logic
    local attempt=0
    while [ "$attempt" -le "$RETRY_COUNT" ]; do
        if [ "$attempt" -gt 0 ]; then
            echo "    Measurement retry attempt $attempt/$RETRY_COUNT"
        fi
        
        MEASUREMENT=$($MEASURE_DISPLAY_CMD --measureonly=yes --sensor-only=yes --noheader=yes 2>/dev/null)
        if [ $? -eq 0 ] && [ -n "$MEASUREMENT" ]; then
            echo "$MEASUREMENT"
            return 0
        fi
        
        if [ "$attempt" -lt "$RETRY_COUNT" ]; then
            echo "    Measurement failed - retrying..."
            sleep 1
        fi
        
        attempt=$((attempt + 1))
    done
    
    echo "Error: Failed to measure display after $RETRY_COUNT retries"
    return 1
}

# Function to check display power status (placeholder)
check_display_power() {
    if [ "$CHECK_DISPLAY_STATUS" = "yes" ]; then
        # Placeholder for future implementation
        echo "Display power check: not implemented yet"
    fi
}

echo "Starting touch responsiveness detection..."
echo "Parameters: loop=$LOOP_COUNT, wait=${WAIT_SEC}s, threshold=$Y_THRESHOLD, retry=$RETRY_COUNT"
if [ -n "$TOOLS_PATH" ]; then
    echo "Tools path: $TOOLS_PATH"
fi
echo "========================================="

# Main detection loop
CURRENT_CYCLE=1
while [ "$CURRENT_CYCLE" -le "$LOOP_COUNT" ]; do
    echo "Cycle $CURRENT_CYCLE/$LOOP_COUNT:"
    
    # Power off the display
    echo "  Powering off display..."
    if ! $USB_RELAY_CMD /dev/ttyUSB0 off; then
        echo "Error: Failed to power off display"
        exit 1
    fi
    
    # Wait for power off stabilization
    sleep "$WAIT_SEC"
    
    # Power on the display
    echo "  Powering on display..."
    if ! $USB_RELAY_CMD /dev/ttyUSB0 on; then
        echo "Error: Failed to power on display"
        exit 1
    fi
    
    # Wait for power on stabilization
    sleep "$WAIT_SEC"
    
    # Check display power status (placeholder)
    check_display_power
    
    # First measurement (before touch - should be bright initially)
    echo "  Taking measurement before touch..."
    MEASUREMENT1=$(measure_display_only)
    if [ $? -ne 0 ]; then
        exit 1
    fi
    Y1=$(extract_y_value "$MEASUREMENT1")
    
    # Second measurement with retry logic (after touch - should be dimmer if responsive)
    echo "  Taking measurement after touch..."
    RETRY_ATTEMPT=0
    IS_RESPONSIVE="no"
    
    while [ "$RETRY_ATTEMPT" -le "$RETRY_COUNT" ] && [ "$IS_RESPONSIVE" = "no" ]; do
        if [ "$RETRY_ATTEMPT" -gt 0 ]; then
            echo "    Retry attempt $RETRY_ATTEMPT/$RETRY_COUNT"
        fi
        
        MEASUREMENT2=$(measure_after_touch)
        if [ $? -ne 0 ]; then
            exit 1
        fi
        Y2=$(extract_y_value "$MEASUREMENT2")
        
        # Calculate delta using awk for floating point arithmetic
        DELTA=$(echo "$Y1 $Y2" | awk '{printf "%.6f", $1 - $2}')
        
        # Check if touch is responsive using awk for floating point comparison  
        IS_RESPONSIVE=$(echo "$DELTA $Y_THRESHOLD" | awk '{if ($1 >= $2) print "yes"; else print "no"}')
        
        if [ "$IS_RESPONSIVE" = "no" ]; then
            echo "    Delta=$DELTA (< $Y_THRESHOLD) - attempting retry..."
        fi
        
        RETRY_ATTEMPT=$((RETRY_ATTEMPT + 1))
    done
    
    if [ "$IS_RESPONSIVE" = "yes" ]; then
        echo "  Result: Y1=$Y1, Y2=$Y2, Delta=$DELTA - RESPONSIVE"
    else
        echo "  Result: Y1=$Y1, Y2=$Y2, Delta=$DELTA - NON-RESPONSIVE (delta < $Y_THRESHOLD)"
        echo ""
        echo "NON-RESPONSIVE TOUCH DETECTED on cycle $CURRENT_CYCLE!"
        echo "Touch display failed to respond properly after $RETRY_COUNT retries."
        exit 1
    fi
    
    # Wait 3 seconds at end of cycle (matching your sequence)
    sleep 3
    
    echo ""
    CURRENT_CYCLE=$((CURRENT_CYCLE + 1))
done

echo "All $LOOP_COUNT cycles completed successfully - no touch issues detected"
exit 0
