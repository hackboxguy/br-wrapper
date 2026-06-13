#!/bin/sh

# Default values
IP=""
PORT=""
WAIT_SEC=""
LOOP_COUNT=""
VERBOSE=false

# Pattern list in order
PATTERNS="grayscale-ramp ansi-checker colorbar white black red green blue cyan magenta yellow"

# Function to show usage
usage() {
    echo "Usage: $0 --ip=<IP> --port=<PORT> --waitsec=<SECONDS> --loopcount=<COUNT> [--verbose]"
    echo "Example: $0 --ip=192.168.1.88 --port=8080 --waitsec=0.5 --loopcount=10"
    exit 1
}

# Function to log messages
log() {
    if [ "$VERBOSE" = true ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1"
    fi
}

# Function to count patterns
count_patterns() {
    set -- $PATTERNS
    echo $#
}

# Parse command line arguments
for arg in "$@"; do
    case $arg in
        --ip=*)
            IP="${arg#*=}"
            ;;
        --port=*)
            PORT="${arg#*=}"
            ;;
        --waitsec=*)
            WAIT_SEC="${arg#*=}"
            ;;
        --loopcount=*)
            LOOP_COUNT="${arg#*=}"
            ;;
        --verbose)
            VERBOSE=true
            ;;
        --help|-h)
            usage
            ;;
        *)
            echo "Error: Unknown argument $arg"
            usage
            ;;
    esac
done

# Validate required arguments
if [ -z "$IP" ] || [ -z "$PORT" ] || [ -z "$WAIT_SEC" ] || [ -z "$LOOP_COUNT" ]; then
    echo "Error: Missing required arguments"
    usage
fi

# Validate port is numeric and in valid range
case "$PORT" in
    ''|*[!0-9]*) 
        echo "Error: Port must be a number"
        exit 1
        ;;
    *)
        if [ "$PORT" -lt 1 ] || [ "$PORT" -gt 65535 ]; then
            echo "Error: Port must be between 1 and 65535"
            exit 1
        fi
        ;;
esac

# Validate wait seconds is numeric
case "$WAIT_SEC" in
    ''|*[!0-9.]*) 
        echo "Error: Wait seconds must be a valid number"
        exit 1
        ;;
esac

# Validate loop count is numeric and positive
case "$LOOP_COUNT" in
    ''|*[!0-9]*) 
        echo "Error: Loop count must be a positive integer"
        exit 1
        ;;
    *)
        if [ "$LOOP_COUNT" -lt 1 ]; then
            echo "Error: Loop count must be at least 1"
            exit 1
        fi
        ;;
esac

# Basic IP validation (four numbers separated by dots)
echo "$IP" | grep -q '^[0-9]\{1,3\}\.[0-9]\{1,3\}\.[0-9]\{1,3\}\.[0-9]\{1,3\}$'
if [ $? -ne 0 ]; then
    echo "Error: Invalid IP address format"
    exit 1
fi

# Check if nc (netcat) is available
NC_CMD=""
if command -v nc >/dev/null 2>&1; then
    NC_CMD="nc"
elif command -v netcat >/dev/null 2>&1; then
    NC_CMD="netcat"
else
    echo "Error: Neither 'nc' nor 'netcat' command found. Please install netcat."
    exit 1
fi

# Calculate totals
PATTERN_COUNT=$(count_patterns)
TOTAL_COMMANDS=$((PATTERN_COUNT * LOOP_COUNT))

log "Starting stress test of pattern-service"
log "Target: $IP:$PORT"
log "Using netcat command: $NC_CMD"
log "Wait time: ${WAIT_SEC}s between commands"
log "Loop count: $LOOP_COUNT"
log "Total patterns per loop: $PATTERN_COUNT"
log "Total commands to send: $TOTAL_COMMANDS"
log "----------------------------------------"

# Main loop
COMMAND_COUNT=0
current_loop=1

while [ $current_loop -le $LOOP_COUNT ]; do
    log "Starting loop $current_loop of $LOOP_COUNT"
    
    # Process each pattern
    for pattern in $PATTERNS; do
        COMMAND_COUNT=$((COMMAND_COUNT + 1))
        COMMAND="pattern $pattern"
        
        log "[$COMMAND_COUNT/$TOTAL_COMMANDS] Sending: $COMMAND"
        
        # Send command via netcat
        echo "$COMMAND" | $NC_CMD -q 0 "$IP" "$PORT" 2>/dev/null
        if [ $? -ne 0 ]; then
            echo "Error: Failed to send command '$COMMAND' to $IP:$PORT"
            echo "Command $COMMAND_COUNT of $TOTAL_COMMANDS failed"
            exit 1
        fi
        
        # Wait between commands (except for the very last command)
        if [ "$COMMAND_COUNT" -lt "$TOTAL_COMMANDS" ]; then
            sleep "$WAIT_SEC"
        fi
    done
    
    log "Completed loop $current_loop of $LOOP_COUNT"
    current_loop=$((current_loop + 1))
done

log "----------------------------------------"
log "Stress test completed successfully!"
log "Total commands sent: $COMMAND_COUNT"
