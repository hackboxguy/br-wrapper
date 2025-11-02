#!/bin/sh

# rigol_scpi.sh - Lightweight SCPI client for Rigol oscilloscopes
# POSIX compliant version for maximum portability

set -eu

# Default values
DEFAULT_IP="192.168.1.7"
DEFAULT_PORT="5555"
DEFAULT_TIMEOUT="2"

usage() {
    cat << EOF
Usage: $0 [OPTIONS] COMMAND

Send SCPI commands to Rigol oscilloscope over TCP/IP

OPTIONS:
    -i IP           IP address of oscilloscope (default: $DEFAULT_IP)
    -p PORT         TCP port (default: $DEFAULT_PORT)  
    -q              Expect response (query mode)
    -t SEC          Connection timeout (default: $DEFAULT_TIMEOUT)
    -h              Show this help

EXAMPLES:
    $0 "*IDN?"                                    # Query instrument ID
    $0 -q ":MEASure:NWIDth? CHAN1"               # Query measurement
    $0 ":CHANnel1:DISPlay on"                    # Send command
    $0 -i 192.168.1.100 ":CHANnel1:SCALe 2"     # Custom IP
    
BATCH MODE:
    Multiple commands can be sent by piping to stdin:
    printf ":CHAN1:DISP on\\n:CHAN1:SCAL 2\\n" | $0 -i 192.168.1.7

EOF
}

send_scpi_command() {
    ip="$1"
    port="$2" 
    timeout="$3"
    query_mode="$4"
    command="$5"
    
    # Add newline terminator if not present
    case "$command" in
        *"
") ;;
        *) command="${command}
" ;;
    esac
    
    # Check if timeout command exists
    if command -v timeout >/dev/null 2>&1; then
        TIMEOUT_CMD="timeout $timeout"
    else
        TIMEOUT_CMD=""
        printf "Warning: 'timeout' command not found, proceeding without timeout\n" >&2
    fi
    
    if [ "$query_mode" = "true" ]; then
        # Query mode - expect response
        # First test if we can connect
        if ! nc -z -w 1 "$ip" "$port" 2>/dev/null; then
            printf "❌ Cannot connect to %s:%s\n" "$ip" "$port" >&2
            return 1
        fi
        
        # Send query and get response
        response=$(printf '%s' "$command" | $TIMEOUT_CMD nc -w "$timeout" "$ip" "$port" 2>/dev/null || true)
        if [ -n "$response" ]; then
            # Remove carriage returns and newlines for clean output
            clean_response=$(printf '%s' "$response" | tr -d '\r\n')
            printf "Response: %s\n" "$clean_response"
            return 0
        else
            printf "❌ No response received\n" >&2
            return 1
        fi
    else
        # Write mode - send command only
        # Test connection first, then send command
        if ! nc -z -w 1 "$ip" "$port" 2>/dev/null; then
            printf "❌ Cannot connect to %s:%s\n" "$ip" "$port" >&2
            return 1
        fi
        
        # Send the command - for write commands, close connection immediately after sending
        printf '%s' "$command" | nc -N -w 1 "$ip" "$port" >/dev/null 2>&1
        
        # Always report success for write commands if we could connect
        clean_command=$(printf '%s' "$command" | tr -d '\r\n')
        printf "Command sent: %s\n" "$clean_command"
        return 0
    fi
}

# Initialize variables
ip="$DEFAULT_IP"
port="$DEFAULT_PORT"
timeout="$DEFAULT_TIMEOUT"
query_mode="false"
command=""

# Parse command line arguments (POSIX compliant)
while [ $# -gt 0 ]; do
    case $1 in
        -i)
            if [ $# -lt 2 ]; then
                printf "Error: -i requires an argument\n" >&2
                exit 1
            fi
            ip="$2"
            shift 2
            ;;
        -p)
            if [ $# -lt 2 ]; then
                printf "Error: -p requires an argument\n" >&2
                exit 1
            fi
            port="$2"
            shift 2
            ;;
        -t)
            if [ $# -lt 2 ]; then
                printf "Error: -t requires an argument\n" >&2
                exit 1
            fi
            timeout="$2"
            shift 2
            ;;
        -q)
            query_mode="true"
            shift
            ;;
        -h)
            usage
            exit 0
            ;;
        -*)
            printf "Unknown option: %s\n" "$1" >&2
            usage >&2
            exit 1
            ;;
        *)
            if [ -z "$command" ]; then
                command="$1"
            else
                printf "Multiple commands specified. Use quotes or batch mode.\n" >&2
                exit 1
            fi
            shift
            ;;
    esac
done

# Check if netcat is available
if ! command -v nc >/dev/null 2>&1; then
    printf "❌ netcat (nc) is required but not installed\n" >&2
    exit 1
fi

# Handle batch mode (stdin) or single command
if [ -t 0 ]; then
    # Standard input is a terminal (not piped)
    if [ -z "$command" ]; then
        printf "❌ No command specified\n" >&2
        usage >&2
        exit 1
    fi
    
    send_scpi_command "$ip" "$port" "$timeout" "$query_mode" "$command"
else
    # Input is piped - batch mode
    if [ -n "$command" ]; then
        printf "Warning: Command argument ignored in batch mode\n" >&2
    fi
    
    printf "Batch mode: Processing commands from stdin...\n"
    while IFS= read -r line; do
        # Skip empty lines and comments
        case "$line" in
            ""|"#"*|" "*"#"*|"	"*"#"*) continue ;;
        esac
        
        printf "Processing: %s\n" "$line"
        send_scpi_command "$ip" "$port" "$timeout" "$query_mode" "$line"
    done
fi
