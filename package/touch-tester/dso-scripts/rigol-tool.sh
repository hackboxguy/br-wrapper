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
RIGOL_SCPI="$SCRIPT_DIR/rigol-scpi.sh"
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
    --dso-ip=IP             Oscilloscope IP address (default: ${RIGOL_IP:-$DEFAULT_IP})
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
    $0 --command=status --dso-ip=192.168.1.7

    # Save current setup to JSON
    $0 --command=copy-setup --output=touch-latency-2ch.json

    # Restore setup from JSON
    $0 --command=apply-setup --input=touch-latency-2ch.json

    # Send raw SCPI query
    $0 --command=query --scpi=":MEAS:NWID? CHAN1"

    # Reset scope to defaults
    $0 --command=reset --dso-ip=192.168.1.7

${BOLD}ENVIRONMENT VARIABLES:${NC}
    RIGOL_IP               Default oscilloscope IP address
    RIGOL_PORT             Default SCPI port

${BOLD}DEPENDENCIES:${NC}
    - netcat (nc)          For SCPI communication
    - jq                   For JSON processing
    - rigol-scpi.sh        Low-level SCPI interface (must be in same directory)

${BOLD}CONFIGURATION FILES:${NC}
    Saved configurations are stored in: ${CONFIG_DIR}/

EOF
}

# Check dependencies
check_dependencies() {
    local missing=0

    # Check for rigol-scpi.sh
    if [ ! -f "$RIGOL_SCPI" ]; then
        log_error "rigol-scpi.sh not found at: $RIGOL_SCPI"
        missing=1
    elif [ ! -x "$RIGOL_SCPI" ]; then
        log_warning "Making rigol-scpi.sh executable..."
        chmod +x "$RIGOL_SCPI" 2>/dev/null || {
            log_error "Cannot make rigol-scpi.sh executable"
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

# Send SCPI command using rigol-scpi.sh
scpi_write() {
    local cmd="$1"
    log_debug "SCPI WRITE: $cmd"

    # Use echo to pipe command to rigol-scpi.sh in batch mode
    if echo "$cmd" | "$RIGOL_SCPI" --dso-ip "$IP" -p "$PORT" >/dev/null 2>&1; then
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
    # Use echo to pipe command to rigol-scpi.sh in batch mode to avoid terminal check issues
    response=$(echo "$cmd" | "$RIGOL_SCPI" --dso-ip "$IP" -p "$PORT" -q 2>/dev/null | grep "Response:" | sed 's/Response: //' || echo "")

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

# Command: measure-delay
cmd_measure_delay() {
    if [ -z "$CH1_NUM" ] || [ -z "$CH2_NUM" ]; then
        log_error "Both --ch1 and --ch2 parameters are required"
        printf "Example: --ch1=1 --ch2=2\n"
        return 1
    fi

    # Map edge combinations to Rigol delay measurement types
    # RRDelay: rising to rising
    # FFDelay: falling to falling
    # RFDelay: rising to falling
    # FRDelay: falling to rising
    local delay_type
    case "${EDGE1}-${EDGE2}" in
        rising-rising)   delay_type="RRDelay" ;;
        rising-falling)  delay_type="RFDelay" ;;
        falling-rising)  delay_type="FRDelay" ;;
        falling-falling) delay_type="FFDelay" ;;
        *)
            log_error "Invalid edge combination: ${EDGE1}-${EDGE2}"
            printf "Valid values: rising, falling\n"
            return 1
            ;;
    esac

    log_info "Measuring delay between CH${CH1_NUM} (${EDGE1}) and CH${CH2_NUM} (${EDGE2})..."
    log_debug "Using measurement type: ${delay_type}"

    local src1="CHANnel${CH1_NUM}"
    local src2="CHANnel${CH2_NUM}"

    # Method 1: Direct query with :MEASure:ITEM?
    log_debug "Attempting direct query: :MEASure:ITEM? ${delay_type},${src1},${src2}"
    local delay_result
    delay_result=$(scpi_query ":MEASure:ITEM? ${delay_type},${src1},${src2}" 2>/dev/null || echo "")

    if [ -n "$delay_result" ] && [ "$delay_result" != "9.9000E+37" ]; then
        output_delay_result "$delay_result" "$delay_type"
        return 0
    fi

    log_warning "Direct measurement query returned invalid value: ${delay_result}"
    log_info "Trying setup-then-query method..."

    # Method 2: Set up measurement item then query
    scpi_write ":MEASure:CLEar"
    scpi_write ":MEASure:ITEM ${delay_type},${src1},${src2}"
    sleep 0.5

    delay_result=$(scpi_query ":MEASure:ITEM? ${delay_type},${src1},${src2}" 2>/dev/null || echo "")

    if [ -n "$delay_result" ] && [ "$delay_result" != "9.9000E+37" ]; then
        output_delay_result "$delay_result" "$delay_type"
        return 0
    fi

    log_error "Unable to measure delay. Returned: ${delay_result}"
    log_error "Please check:"
    printf "  - Both channels have valid signals\n"
    printf "  - Channels are enabled and displaying waveforms\n"
    printf "  - Trigger is working properly\n"
    printf "  - Oscilloscope is running (not stopped)\n"
    printf "  - Signals have the specified edge types\n"
    return 1
}

# Helper function to output delay measurement results
output_delay_result() {
    local delay_value="$1"
    local delay_type="$2"
    local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

    if [ -n "$OUTPUT_FILE" ]; then
        # JSON output to file
        cat > "$OUTPUT_FILE" <<EOF
{
  "measurement": "delay",
  "timestamp": "$timestamp",
  "oscilloscope_ip": "$IP",
  "channel1": $CH1_NUM,
  "channel2": $CH2_NUM,
  "edge1": "$EDGE1",
  "edge2": "$EDGE2",
  "delay_type": "$delay_type",
  "delay_seconds": $delay_value,
  "delay_milliseconds": $(awk "BEGIN {printf \"%.6f\", $delay_value * 1000}"),
  "status": "success"
}
EOF
        log_success "Measurement saved to $OUTPUT_FILE"

        # Also print to console (brief)
        printf "\n${BOLD}Delay Measurement:${NC} ${delay_value} seconds (%.3f ms)\n" \
            $(awk "BEGIN {printf \"%.3f\", $delay_value * 1000}")
        printf "Results saved to: ${OUTPUT_FILE}\n"
    else
        # Console output only
        printf "\n${BOLD}Delay Measurement Result:${NC}\n"
        printf "  From: CH${CH1_NUM} (${EDGE1} edge)\n"
        printf "  To:   CH${CH2_NUM} (${EDGE2} edge)\n"
        printf "  Type: ${delay_type}\n"
        printf "  ${BOLD}Delay: ${delay_value} seconds${NC}\n"
        printf "        (%.3f milliseconds)\n" \
            $(awk "BEGIN {printf \"%.3f\", $delay_value * 1000}")
    fi
}

# Command: measure-stats
cmd_measure_stats() {
    if [ "$MEAS_TYPE" = "delay" ]; then
        if [ -z "$CH1_NUM" ] || [ -z "$CH2_NUM" ]; then
            log_error "For delay measurements, both --ch1 and --ch2 are required"
            printf "Example: --type=delay --ch1=1 --ch2=2 --samples=100\n"
            return 1
        fi
    else
        log_error "Unsupported measurement type: $MEAS_TYPE"
        printf "Currently supported types: delay\n"
        return 1
    fi

    # Validate samples count
    if ! printf '%s' "$SAMPLES" | grep -qE '^[0-9]+$' || [ "$SAMPLES" -lt 1 ]; then
        log_error "Invalid samples count: $SAMPLES"
        printf "Samples must be a positive integer\n"
        return 1
    fi

    log_info "Collecting $SAMPLES samples of $MEAS_TYPE measurements..."

    # Map edge combinations to Rigol delay measurement types
    local delay_type
    case "${EDGE1}-${EDGE2}" in
        rising-rising)   delay_type="RRDelay" ;;
        rising-falling)  delay_type="RFDelay" ;;
        falling-rising)  delay_type="FRDelay" ;;
        falling-falling) delay_type="FFDelay" ;;
        *)
            log_error "Invalid edge combination: ${EDGE1}-${EDGE2}"
            return 1
            ;;
    esac

    local src1="CHANnel${CH1_NUM}"
    local src2="CHANnel${CH2_NUM}"

    # Setup measurement once
    scpi_write ":MEASure:CLEar"
    scpi_write ":MEASure:ITEM ${delay_type},${src1},${src2}"
    sleep 0.5

    # Collect samples
    local samples_file=$(mktemp)
    local i=1
    local valid_samples=0
    local failed_samples=0

    while [ $i -le "$SAMPLES" ]; do
        # Show progress every 10 samples or on first/last
        if [ $i -eq 1 ] || [ $i -eq "$SAMPLES" ] || [ $((i % 10)) -eq 0 ]; then
            printf "\r  Progress: %d/%d samples collected (valid: %d, failed: %d)" \
                $i "$SAMPLES" $valid_samples $failed_samples >&2
        fi

        # Query measurement
        local result
        result=$(scpi_query ":MEASure:ITEM? ${delay_type},${src1},${src2}" 2>/dev/null || echo "")

        if [ -n "$result" ] && [ "$result" != "9.9000E+37" ]; then
            printf "%s\n" "$result" >> "$samples_file"
            valid_samples=$((valid_samples + 1))
        else
            failed_samples=$((failed_samples + 1))
        fi

        i=$((i + 1))
        sleep 0.1  # Small delay between measurements
    done

    printf "\n" >&2

    # Check if we got enough valid samples
    if [ $valid_samples -lt 1 ]; then
        log_error "No valid samples collected"
        rm -f "$samples_file"
        return 1
    fi

    if [ $failed_samples -gt 0 ]; then
        log_warning "Failed to collect $failed_samples out of $SAMPLES samples"
    fi

    # Calculate statistics
    log_info "Calculating statistics from $valid_samples valid samples..."
    calculate_statistics "$samples_file" "$delay_type"
    local ret=$?

    rm -f "$samples_file"
    return $ret
}

# Helper function to calculate statistics
calculate_statistics() {
    local samples_file="$1"
    local delay_type="$2"
    local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

    # Use awk for all statistical calculations
    local stats
    stats=$(awk '
    BEGIN {
        count = 0
        sum = 0
        sum_sq = 0
        min = ""
        max = ""
    }
    {
        val = $1 + 0  # Convert to number
        samples[count] = val
        count++
        sum += val
        sum_sq += val * val

        if (min == "" || val < min) min = val
        if (max == "" || val > max) max = val
    }
    END {
        if (count == 0) exit 1

        mean = sum / count
        variance = (sum_sq / count) - (mean * mean)
        stddev = sqrt(variance > 0 ? variance : 0)

        # Sort array for percentiles
        for (i = 0; i < count; i++) {
            for (j = i + 1; j < count; j++) {
                if (samples[i] > samples[j]) {
                    tmp = samples[i]
                    samples[i] = samples[j]
                    samples[j] = tmp
                }
            }
        }

        # Calculate percentiles
        p50_idx = int(count * 0.50)
        p95_idx = int(count * 0.95)
        p99_idx = int(count * 0.99)

        p50 = samples[p50_idx]
        p95 = samples[p95_idx]
        p99 = samples[p99_idx]

        # Output in parseable format
        printf "COUNT=%d\n", count
        printf "MIN=%.10e\n", min
        printf "MAX=%.10e\n", max
        printf "MEAN=%.10e\n", mean
        printf "STDDEV=%.10e\n", stddev
        printf "P50=%.10e\n", p50
        printf "P95=%.10e\n", p95
        printf "P99=%.10e\n", p99
    }
    ' "$samples_file")

    if [ $? -ne 0 ]; then
        log_error "Failed to calculate statistics"
        return 1
    fi

    # Parse statistics
    local count=$(echo "$stats" | grep "^COUNT=" | cut -d= -f2)
    local min=$(echo "$stats" | grep "^MIN=" | cut -d= -f2)
    local max=$(echo "$stats" | grep "^MAX=" | cut -d= -f2)
    local mean=$(echo "$stats" | grep "^MEAN=" | cut -d= -f2)
    local stddev=$(echo "$stats" | grep "^STDDEV=" | cut -d= -f2)
    local p50=$(echo "$stats" | grep "^P50=" | cut -d= -f2)
    local p95=$(echo "$stats" | grep "^P95=" | cut -d= -f2)
    local p99=$(echo "$stats" | grep "^P99=" | cut -d= -f2)

    # Output results
    if [ -n "$OUTPUT_FILE" ]; then
        # JSON output
        cat > "$OUTPUT_FILE" <<EOF
{
  "measurement": "stats",
  "type": "$MEAS_TYPE",
  "timestamp": "$timestamp",
  "oscilloscope_ip": "$IP",
  "channel1": $CH1_NUM,
  "channel2": $CH2_NUM,
  "edge1": "$EDGE1",
  "edge2": "$EDGE2",
  "delay_type": "$delay_type",
  "samples_requested": $SAMPLES,
  "samples_valid": $count,
  "statistics": {
    "min_seconds": $min,
    "max_seconds": $max,
    "mean_seconds": $mean,
    "stddev_seconds": $stddev,
    "p50_seconds": $p50,
    "p95_seconds": $p95,
    "p99_seconds": $p99,
    "min_ms": $(awk "BEGIN {printf \"%.6f\", $min * 1000}"),
    "max_ms": $(awk "BEGIN {printf \"%.6f\", $max * 1000}"),
    "mean_ms": $(awk "BEGIN {printf \"%.6f\", $mean * 1000}"),
    "stddev_ms": $(awk "BEGIN {printf \"%.6f\", $stddev * 1000}"),
    "p50_ms": $(awk "BEGIN {printf \"%.6f\", $p50 * 1000}"),
    "p95_ms": $(awk "BEGIN {printf \"%.6f\", $p95 * 1000}"),
    "p99_ms": $(awk "BEGIN {printf \"%.6f\", $p99 * 1000}")
  },
  "status": "success"
}
EOF
        log_success "Statistics saved to $OUTPUT_FILE"
        printf "\n${BOLD}Statistics Summary:${NC}\n"
        printf "  Samples: %d valid out of %d requested\n" $count $SAMPLES
        printf "  Mean:    %.3f ms (± %.3f ms)\n" \
            $(awk "BEGIN {printf \"%.3f\", $mean * 1000}") \
            $(awk "BEGIN {printf \"%.3f\", $stddev * 1000}")
        printf "  Results saved to: ${OUTPUT_FILE}\n"
    else
        # Console output
        printf "\n${BOLD}Statistical Analysis Results:${NC}\n"
        printf "  Measurement: %s (%s)\n" "$MEAS_TYPE" "$delay_type"
        printf "  From: CH${CH1_NUM} (${EDGE1}) → CH${CH2_NUM} (${EDGE2})\n"
        printf "  Samples: %d valid out of %d requested\n\n" $count $SAMPLES

        printf "  ${BOLD}Statistics (milliseconds):${NC}\n"
        printf "    Min:    %.3f ms\n" $(awk "BEGIN {printf \"%.3f\", $min * 1000}")
        printf "    Max:    %.3f ms\n" $(awk "BEGIN {printf \"%.3f\", $max * 1000}")
        printf "    Mean:   %.3f ms\n" $(awk "BEGIN {printf \"%.3f\", $mean * 1000}")
        printf "    StdDev: %.3f ms\n" $(awk "BEGIN {printf \"%.3f\", $stddev * 1000}")
        printf "    P50:    %.3f ms\n" $(awk "BEGIN {printf \"%.3f\", $p50 * 1000}")
        printf "    P95:    %.3f ms\n" $(awk "BEGIN {printf \"%.3f\", $p95 * 1000}")
        printf "    P99:    %.3f ms\n" $(awk "BEGIN {printf \"%.3f\", $p99 * 1000}")
    fi

    return 0
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
    CH1_NUM=""
    CH2_NUM=""
    EDGE1="rising"
    EDGE2="rising"
    MEAS_TYPE="delay"
    SAMPLES="100"
    CHANNEL=""
    POLARITY="positive"

    # Parse arguments
    for arg in "$@"; do
        case "$arg" in
            --command=*)
                COMMAND="${arg#*=}"
                ;;
            --dso-ip=*)
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
            --ch1=*)
                CH1_NUM="${arg#*=}"
                ;;
            --ch2=*)
                CH2_NUM="${arg#*=}"
                ;;
            --edge1=*)
                EDGE1="${arg#*=}"
                ;;
            --edge2=*)
                EDGE2="${arg#*=}"
                ;;
            --type=*)
                MEAS_TYPE="${arg#*=}"
                ;;
            --samples=*)
                SAMPLES="${arg#*=}"
                ;;
            --channel=*)
                CHANNEL="${arg#*=}"
                ;;
            --polarity=*)
                POLARITY="${arg#*=}"
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
        measure-delay)
            cmd_measure_delay
            ;;
        measure-stats)
            cmd_measure_stats
            ;;
        *)
            log_error "Unknown command: $COMMAND"
            printf "\nAvailable commands:\n"
            printf "  check-connection, status, copy-setup, apply-setup, query, reset, measure-delay, measure-stats\n"
            exit 1
            ;;
    esac
}

# Run main function
main "$@"
