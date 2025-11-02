#!/bin/sh

# rigol-tool.sh - High-level tool for Rigol oscilloscope automation
# Supports configuration management, measurements, and automated testing
# Model: DHO-924S (250MHz) and compatible Rigol scopes
# POSIX compliant for maximum portability

set -eu

# Version
VERSION="1.0.0"

# Default values
DEFAULT_IP="192.168.1.7"
DEFAULT_PORT="5555"

# Script directory and paths
SCRIPT_DIR=$(dirname "$0")
RIGOL_SCPI="$SCRIPT_DIR/rigol_scpi.sh"
CONFIG_DIR="$SCRIPT_DIR/dso-configs"

# Color codes for terminal output
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    CYAN='\033[0;36m'
    BOLD='\033[1m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    CYAN=''
    BOLD=''
    NC=''
fi

# Logging functions
log_info() {
    printf "${BLUE}ℹ️  %s${NC}\n" "$1"
}

log_success() {
    printf "${GREEN}✅ %s${NC}\n" "$1"
}

log_warning() {
    printf "${YELLOW}⚠️  %s${NC}\n" "$1"
}

log_error() {
    printf "${RED}❌ %s${NC}\n" "$1" >&2
}

log_debug() {
    if [ "$VERBOSE" = "true" ]; then
        printf "${CYAN}🔍 DEBUG: %s${NC}\n" "$1" >&2
    fi
}

# Usage/help function
usage() {
    cat << EOF
${BOLD}rigol-tool.sh${NC} - Rigol Oscilloscope Automation Tool v${VERSION}

${BOLD}USAGE:${NC}
    $0 --command=COMMAND [OPTIONS]

${BOLD}COMMANDS:${NC}
    ${BOLD}check-connection${NC}      Test connectivity to oscilloscope
    ${BOLD}status${NC}                 Show current oscilloscope configuration
    ${BOLD}copy-setup${NC}             Save current scope config to JSON file
    ${BOLD}apply-setup${NC}            Restore scope config from JSON file
    ${BOLD}query${NC}                  Send raw SCPI query command
    ${BOLD}reset${NC}                  Reset oscilloscope to factory defaults

${BOLD}OPTIONS:${NC}
    --ip=IP                 Oscilloscope IP address (default: ${RIGOL_IP:-$DEFAULT_IP})
    --port=PORT             SCPI port (default: $DEFAULT_PORT)
    --output=FILE           Output file for copy-setup (default: scope-config.json)
    --input=FILE            Input file for apply-setup
    --scpi=COMMAND          SCPI command for query mode
    --verbose               Enable verbose debug output
    --help                  Show this help message

${BOLD}EXAMPLES:${NC}
    # Check connection to scope
    $0 --command=check-connection

    # Show current configuration
    $0 --command=status --ip=192.168.1.7

    # Save current setup to JSON
    $0 --command=copy-setup --output=touch-latency-2ch.json

    # Restore setup from JSON
    $0 --command=apply-setup --input=touch-latency-2ch.json

    # Send raw SCPI query
    $0 --command=query --scpi=":MEAS:NWID? CHAN1"

    # Reset scope to defaults
    $0 --command=reset --ip=192.168.1.7

${BOLD}ENVIRONMENT VARIABLES:${NC}
    RIGOL_IP               Default oscilloscope IP address
    RIGOL_PORT             Default SCPI port

${BOLD}DEPENDENCIES:${NC}
    - netcat (nc)          For SCPI communication
    - jq                   For JSON processing
    - rigol_scpi.sh        Low-level SCPI interface (must be in same directory)

${BOLD}CONFIGURATION FILES:${NC}
    Saved configurations are stored in: ${CONFIG_DIR}/

EOF
}

# Check dependencies
check_dependencies() {
    local missing=0

    # Check for rigol_scpi.sh
    if [ ! -f "$RIGOL_SCPI" ]; then
        log_error "rigol_scpi.sh not found at: $RIGOL_SCPI"
        missing=1
    elif [ ! -x "$RIGOL_SCPI" ]; then
        log_warning "Making rigol_scpi.sh executable..."
        chmod +x "$RIGOL_SCPI" 2>/dev/null || {
            log_error "Cannot make rigol_scpi.sh executable"
            missing=1
        }
    fi

    # Check for netcat
    if ! command -v nc >/dev/null 2>&1; then
        log_error "netcat (nc) is required but not installed"
        log_info "Install with: sudo apt-get install netcat-openbsd"
        missing=1
    fi

    # Check for jq
    if ! command -v jq >/dev/null 2>&1; then
        log_error "jq is required but not installed"
        log_info "Install with: sudo apt-get install jq"
        missing=1
    fi

    if [ $missing -eq 1 ]; then
        return 1
    fi

    return 0
}

# Send SCPI command using rigol_scpi.sh
scpi_write() {
    local cmd="$1"
    log_debug "SCPI WRITE: $cmd"

    # Use echo to pipe command to rigol_scpi.sh in batch mode
    if echo "$cmd" | "$RIGOL_SCPI" -i "$IP" -p "$PORT" >/dev/null 2>&1; then
        return 0
    else
        log_error "Failed to send SCPI command: $cmd"
        return 1
    fi
}

# Query SCPI command and return response
scpi_query() {
    local cmd="$1"
    log_debug "SCPI QUERY: $cmd"

    local response
    # Use echo to pipe command to rigol_scpi.sh in batch mode to avoid terminal check issues
    response=$(echo "$cmd" | "$RIGOL_SCPI" -i "$IP" -p "$PORT" -q 2>/dev/null | grep "Response:" | sed 's/Response: //' || echo "")

    if [ -z "$response" ]; then
        log_debug "No response received for: $cmd"
        echo ""
        return 1
    fi

    log_debug "SCPI RESPONSE: $response"
    echo "$response"
    return 0
}

# Command: check-connection
cmd_check_connection() {
    log_info "Testing connection to oscilloscope at ${IP}:${PORT}..."

    local device_id
    device_id=$(scpi_query "*IDN?" || echo "")

    if [ -n "$device_id" ]; then
        log_success "Connected to oscilloscope"
        printf "  ${BOLD}Device:${NC} %s\n" "$device_id"
        return 0
    else
        log_error "Cannot connect to oscilloscope at ${IP}:${PORT}"
        printf "\n${BOLD}Troubleshooting:${NC}\n"
        printf "  1. Check IP address is correct: %s\n" "$IP"
        printf "  2. Ensure oscilloscope is powered on\n"
        printf "  3. Verify network connectivity: ping %s\n" "$IP"
        printf "  4. Check SCPI is enabled in scope settings (port %s)\n" "$PORT"
        printf "  5. Verify no firewall is blocking port %s\n" "$PORT"
        return 1
    fi
}

# Command: status
cmd_status() {
    log_info "Querying current oscilloscope configuration..."
    printf "\n"

    # Get device ID
    local device_id
    device_id=$(scpi_query "*IDN?" || echo "Unknown")
    printf "${BOLD}Device:${NC} %s\n" "$device_id"
    printf "\n"

    # Channel status
    printf "${BOLD}Channels:${NC}\n"
    for ch in 1 2 3 4; do
        local disp scale offset
        disp=$(scpi_query ":CHAN${ch}:DISP?" || echo "?")
        scale=$(scpi_query ":CHAN${ch}:SCAL?" || echo "?")
        offset=$(scpi_query ":CHAN${ch}:OFFS?" || echo "?")

        if [ "$disp" = "1" ] || [ "$disp" = "ON" ]; then
            printf "  ${GREEN}●${NC} CH%d: Scale=%sV/div, Offset=%sV\n" "$ch" "$scale" "$offset"
        else
            printf "  ${RED}○${NC} CH%d: OFF\n" "$ch"
        fi
    done
    printf "\n"

    # Timebase
    printf "${BOLD}Timebase:${NC}\n"
    local tscale toffset
    tscale=$(scpi_query ":TIM:MAIN:SCAL?" || echo "?")
    toffset=$(scpi_query ":TIM:MAIN:OFFS?" || echo "?")
    printf "  Scale: %ss/div\n" "$tscale"
    printf "  Offset: %ss\n" "$toffset"
    printf "\n"

    # Trigger
    printf "${BOLD}Trigger:${NC}\n"
    local trig_source trig_level trig_slope trig_sweep
    trig_source=$(scpi_query ":TRIGger:EDGE:SOURce?" || echo "?")
    trig_level=$(scpi_query ":TRIGger:EDGE:LEVel?" || echo "?")
    trig_slope=$(scpi_query ":TRIGger:EDGE:SLOPe?" || echo "?")
    trig_sweep=$(scpi_query ":TRIGger:SWEep?" || echo "?")
    printf "  Source: %s\n" "$trig_source"
    printf "  Level: %sV\n" "$trig_level"
    printf "  Slope: %s\n" "$trig_slope"
    printf "  Sweep: %s\n" "$trig_sweep"
    printf "\n"
}

# Command: copy-setup
cmd_copy_setup() {
    if [ -z "$OUTPUT_FILE" ]; then
        log_error "Output file not specified. Use --output=filename.json"
        return 1
    fi

    log_info "Reading oscilloscope configuration..."

    # Query all configuration parameters
    local device_id
    device_id=$(scpi_query "*IDN?" || echo "Unknown Device")

    # Create JSON structure
    local timestamp
    timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

    # Start building JSON
    local json="{}"
    json=$(echo "$json" | jq --arg v "1.0" '.version = $v')
    json=$(echo "$json" | jq --arg ts "$timestamp" '.timestamp = $ts')
    json=$(echo "$json" | jq --arg dev "$device_id" '.device = $dev')

    # Query channels
    log_info "Reading channel configurations..."
    json=$(echo "$json" | jq '.channels = {}')

    for ch in 1 2 3 4; do
        local disp scale offset coup probe bwl vpos
        disp=$(scpi_query ":CHAN${ch}:DISP?" || echo "0")
        scale=$(scpi_query ":CHAN${ch}:SCAL?" || echo "1")
        offset=$(scpi_query ":CHAN${ch}:OFFS?" || echo "0")
        vpos=$(scpi_query ":CHAN${ch}:POS?" || echo "0")
        coup=$(scpi_query ":CHAN${ch}:COUP?" || echo "DC")
        probe=$(scpi_query ":CHAN${ch}:PROB?" || echo "1")
        bwl=$(scpi_query ":CHAN${ch}:BWL?" || echo "OFF")

        # Convert display to boolean
        local disp_bool="false"
        if [ "$disp" = "1" ] || [ "$disp" = "ON" ]; then
            disp_bool="true"
        fi

        json=$(echo "$json" | jq \
            --arg ch "$ch" \
            --argjson disp "$disp_bool" \
            --arg scale "$scale" \
            --arg offset "$offset" \
            --arg vpos "$vpos" \
            --arg coup "$coup" \
            --arg probe "$probe" \
            --arg bwl "$bwl" \
            '.channels[$ch] = {
                "display": $disp,
                "scale": ($scale | tonumber),
                "offset": ($offset | tonumber),
                "position": ($vpos | tonumber),
                "coupling": $coup,
                "probe": ($probe | tonumber),
                "bandwidth_limit": $bwl
            }')
    done

    # Query timebase
    log_info "Reading timebase configuration..."
    local tscale toffset
    tscale=$(scpi_query ":TIM:MAIN:SCAL?" || echo "0.001")
    toffset=$(scpi_query ":TIM:MAIN:OFFS?" || echo "0")

    json=$(echo "$json" | jq \
        --arg scale "$tscale" \
        --arg offset "$toffset" \
        '.timebase = {
            "scale": ($scale | tonumber),
            "offset": ($offset | tonumber)
        }')

    # Query trigger
    log_info "Reading trigger configuration..."
    local trig_source trig_level trig_slope trig_sweep trig_coup
    trig_source=$(scpi_query ":TRIGger:EDGE:SOURce?" || echo "CHAN1")
    trig_level=$(scpi_query ":TRIGger:EDGE:LEVel?" || echo "0")
    trig_slope=$(scpi_query ":TRIGger:EDGE:SLOPe?" || echo "POS")
    trig_sweep=$(scpi_query ":TRIGger:SWEep?" || echo "AUTO")
    trig_coup=$(scpi_query ":TRIGger:COUPling?" || echo "DC")

    json=$(echo "$json" | jq \
        --arg source "$trig_source" \
        --arg level "$trig_level" \
        --arg slope "$trig_slope" \
        --arg sweep "$trig_sweep" \
        --arg coup "$trig_coup" \
        '.trigger = {
            "source": $source,
            "level": ($level | tonumber),
            "slope": $slope,
            "sweep": $sweep,
            "coupling": $coup
        }')

    # Save to file
    log_info "Writing configuration to: $OUTPUT_FILE"
    echo "$json" | jq '.' > "$OUTPUT_FILE"

    log_success "Configuration saved successfully"
    printf "  ${BOLD}File:${NC} %s\n" "$OUTPUT_FILE"
    printf "  ${BOLD}Size:${NC} %s bytes\n" "$(wc -c < "$OUTPUT_FILE")"
}

# Command: apply-setup
cmd_apply_setup() {
    if [ -z "$INPUT_FILE" ]; then
        log_error "Input file not specified. Use --input=filename.json"
        return 1
    fi

    if [ ! -f "$INPUT_FILE" ]; then
        log_error "Configuration file not found: $INPUT_FILE"
        return 1
    fi

    log_info "Reading configuration from: $INPUT_FILE"

    # Validate JSON
    if ! jq empty "$INPUT_FILE" 2>/dev/null; then
        log_error "Invalid JSON file: $INPUT_FILE"
        return 1
    fi

    # Read device info from file
    local saved_device
    saved_device=$(jq -r '.device // "Unknown"' "$INPUT_FILE")
    log_info "Configuration from: $saved_device"

    # Apply channel configurations
    log_info "Applying channel configurations..."
    for ch in 1 2 3 4; do
        local disp scale offset vpos coup probe bwl

        disp=$(jq -r ".channels.\"$ch\".display // false" "$INPUT_FILE")
        scale=$(jq -r ".channels.\"$ch\".scale // 1" "$INPUT_FILE")
        offset=$(jq -r ".channels.\"$ch\".offset // 0" "$INPUT_FILE")
        vpos=$(jq -r ".channels.\"$ch\".position // 0" "$INPUT_FILE")
        coup=$(jq -r ".channels.\"$ch\".coupling // \"DC\"" "$INPUT_FILE")
        probe=$(jq -r ".channels.\"$ch\".probe // 1" "$INPUT_FILE")
        bwl=$(jq -r ".channels.\"$ch\".bandwidth_limit // \"OFF\"" "$INPUT_FILE")

        # Convert boolean to ON/OFF
        local disp_cmd="OFF"
        if [ "$disp" = "true" ]; then
            disp_cmd="ON"
        fi

        scpi_write ":CHAN${ch}:DISP $disp_cmd"
        scpi_write ":CHAN${ch}:SCAL $scale"
        scpi_write ":CHAN${ch}:OFFS $offset"
        scpi_write ":CHAN${ch}:POS $vpos"
        scpi_write ":CHAN${ch}:COUP $coup"
        scpi_write ":CHAN${ch}:PROB $probe"
        scpi_write ":CHAN${ch}:BWL $bwl"

        log_debug "CH${ch}: Display=$disp_cmd, Scale=${scale}V, Offset=${offset}V, Position=${vpos}"
    done

    # Apply timebase configuration
    log_info "Applying timebase configuration..."
    local tscale toffset
    tscale=$(jq -r '.timebase.scale // 0.001' "$INPUT_FILE")
    toffset=$(jq -r '.timebase.offset // 0' "$INPUT_FILE")

    scpi_write ":TIM:MAIN:SCAL $tscale"
    scpi_write ":TIM:MAIN:OFFS $toffset"
    log_debug "Timebase: Scale=${tscale}s/div, Offset=${toffset}s"

    # Apply trigger configuration
    log_info "Applying trigger configuration..."
    local trig_source trig_level trig_slope trig_sweep trig_coup
    trig_source=$(jq -r '.trigger.source // "CHAN1"' "$INPUT_FILE")
    trig_level=$(jq -r '.trigger.level // 0' "$INPUT_FILE")
    trig_slope=$(jq -r '.trigger.slope // "POS"' "$INPUT_FILE")
    trig_sweep=$(jq -r '.trigger.sweep // "AUTO"' "$INPUT_FILE")
    trig_coup=$(jq -r '.trigger.coupling // "DC"' "$INPUT_FILE")

    scpi_write ":TRIGger:EDGE:SOURce $trig_source"
    scpi_write ":TRIGger:EDGE:LEVel $trig_level"
    scpi_write ":TRIGger:EDGE:SLOPe $trig_slope"
    scpi_write ":TRIGger:SWEep $trig_sweep"
    scpi_write ":TRIGger:COUPling $trig_coup"
    log_debug "Trigger: Source=$trig_source, Level=${trig_level}V, Slope=$trig_slope"

    log_success "Configuration applied successfully"
}

# Command: query
cmd_query() {
    if [ -z "$SCPI_CMD" ]; then
        log_error "SCPI command not specified. Use --scpi=\"COMMAND\""
        return 1
    fi

    log_info "Sending SCPI query: $SCPI_CMD"
    local response
    response=$(scpi_query "$SCPI_CMD")

    if [ -n "$response" ]; then
        printf "${BOLD}Response:${NC} %s\n" "$response"
        return 0
    else
        log_error "No response received"
        return 1
    fi
}

# Command: reset
cmd_reset() {
    log_warning "Resetting oscilloscope to factory defaults..."
    printf "This will clear all settings. Continue? [y/N] "

    # Only prompt if interactive
    if [ -t 0 ]; then
        read -r confirm
        case "$confirm" in
            [Yy]*)
                scpi_write "*RST"
                sleep 2
                log_success "Oscilloscope reset complete"
                ;;
            *)
                log_info "Reset cancelled"
                return 1
                ;;
        esac
    else
        # Non-interactive mode
        scpi_write "*RST"
        sleep 2
        log_success "Oscilloscope reset complete"
    fi
}

# Main script
main() {
    # Initialize variables
    COMMAND=""
    IP="${RIGOL_IP:-$DEFAULT_IP}"
    PORT="${RIGOL_PORT:-$DEFAULT_PORT}"
    OUTPUT_FILE=""
    INPUT_FILE=""
    SCPI_CMD=""
    VERBOSE="false"

    # Parse arguments
    for arg in "$@"; do
        case "$arg" in
            --command=*)
                COMMAND="${arg#*=}"
                ;;
            --ip=*)
                IP="${arg#*=}"
                ;;
            --port=*)
                PORT="${arg#*=}"
                ;;
            --output=*)
                OUTPUT_FILE="${arg#*=}"
                ;;
            --input=*)
                INPUT_FILE="${arg#*=}"
                ;;
            --scpi=*)
                SCPI_CMD="${arg#*=}"
                ;;
            --verbose)
                VERBOSE="true"
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $arg"
                usage >&2
                exit 1
                ;;
        esac
    done

    # Check for command
    if [ -z "$COMMAND" ]; then
        log_error "No command specified"
        usage >&2
        exit 1
    fi

    # Check dependencies
    if ! check_dependencies; then
        exit 1
    fi

    # Create config directory if needed
    if [ ! -d "$CONFIG_DIR" ]; then
        log_debug "Creating config directory: $CONFIG_DIR"
        mkdir -p "$CONFIG_DIR"
    fi

    # Execute command
    case "$COMMAND" in
        check-connection)
            cmd_check_connection
            ;;
        status)
            cmd_status
            ;;
        copy-setup)
            cmd_copy_setup
            ;;
        apply-setup)
            cmd_apply_setup
            ;;
        query)
            cmd_query
            ;;
        reset)
            cmd_reset
            ;;
        *)
            log_error "Unknown command: $COMMAND"
            printf "\nAvailable commands:\n"
            printf "  check-connection, status, copy-setup, apply-setup, query, reset\n"
            exit 1
            ;;
    esac
}

# Run main function
main "$@"
