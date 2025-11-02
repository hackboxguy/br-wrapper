#!/bin/sh

# automated-latency-test.sh - Automated touch latency testing with hardware-synchronized DSO measurement
# Coordinates touch-tester and oscilloscope using DSO's hardware trigger for precise synchronization
#
# Synchronization Strategy:
# 1. Configure DSO with measurement and enable statistics mode
# 2. Reset DSO statistics counter to zero
# 3. DSO waits in NORMAL trigger mode (hardware trigger on each touch event)
# 4. Start touch-tester to generate N touch events
# 5. Each touch event triggers DSO capture automatically (hardware synchronized)
# 6. After touch-tester completes, query accumulated statistics from DSO
#
# Usage: ./automated-latency-test.sh [OPTIONS]

set -eu

# Default configuration
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Try to find rigol-tool.sh in multiple locations
if [ -x "$SCRIPT_DIR/rigol-tool.sh" ]; then
    # Found in same directory (development/source tree)
    RIGOL_TOOL="$SCRIPT_DIR/rigol-tool.sh"
elif [ -x "/usr/share/touch-tester/dso-scripts/rigol-tool.sh" ]; then
    # Found in install location (CMAKE_INSTALL_PREFIX)
    RIGOL_TOOL="/usr/share/touch-tester/dso-scripts/rigol-tool.sh"
elif command -v rigol-tool.sh >/dev/null 2>&1; then
    # Found in PATH
    RIGOL_TOOL="rigol-tool.sh"
else
    echo "Error: rigol-tool.sh not found" >&2
    exit 1
fi

# Touch-tester configuration
TOUCH_TESTER="${TOUCH_TESTER:-touch-tester}"
OUTPUT_GPIO="${OUTPUT_GPIO:-27}"
OUTPUT_PROBE="${OUTPUT_PROBE:-7}"
LOOP_COUNT="${LOOP_COUNT:-100}"
TEST_TYPE="${TEST_TYPE:-latencymeasure}"
WAIT_MS="${WAIT_MS:-50}"  # Wait time between pulses in ms (default matches touch-tester default)

# DSO configuration
DSO_IP="${DSO_IP:-192.168.1.7}"
DSO_CH1="${DSO_CH1:-1}"
DSO_CH2="${DSO_CH2:-2}"
DSO_EDGE1="${DSO_EDGE1:-rising}"
DSO_EDGE2="${DSO_EDGE2:-rising}"
DSO_CONFIG_FILE=""  # Empty means no DSO setup/initialization

# Output configuration
OUTPUT_DIR="${OUTPUT_DIR:-./results}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
TEST_NAME="${TEST_NAME:-latency_test_${TIMESTAMP}}"
VERBOSE="${VERBOSE:-false}"  # Verbose output mode

# Color codes
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    BOLD='\033[1m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    BOLD=''
    NC=''
fi

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
    printf "${RED}❌ %s${NC}\n" "$1"
}

log_info_verbose() {
    if [ "$VERBOSE" = "true" ]; then
        printf "${BLUE}ℹ️  %s${NC}\n" "$1"
    fi
}

log_success_verbose() {
    if [ "$VERBOSE" = "true" ]; then
        printf "${GREEN}✅ %s${NC}\n" "$1"
    fi
}

usage() {
    cat <<EOF
Usage: $0 [OPTIONS]

Automated touch latency testing with hardware-synchronized oscilloscope measurement.

The DSO's hardware trigger provides precise synchronization:
  1. DSO is configured and armed in NORMAL trigger mode
  2. Statistics counter is reset to zero
  3. Touch-tester generates touch events
  4. Each touch event triggers DSO capture automatically
  5. After all events, accumulated statistics are queried from DSO

OPTIONS:
    --touch-tester PATH     Path to touch-tester binary (default: touch-tester)
    --output-gpio NUM       GPIO number for touch simulation (default: 27)
    --output-probe NUM      GPIO number for latency probe pulse (default: 7)
    --loop-count NUM        Number of touch events to generate (default: 100)
    --test-type TYPE        Touch-tester test type (default: latencymeasure)
    --wait-ms NUM           Wait time between touch events in ms (default: 50)

    --dso-ip IP             Oscilloscope IP address (default: 192.168.1.7)
    --dso-ch1 NUM           DSO channel 1 number (default: 1)
    --dso-ch2 NUM           DSO channel 2 number (default: 2)
    --dso-edge1 TYPE        DSO channel 1 edge type (rising/falling, default: rising)
    --dso-edge2 TYPE        DSO channel 2 edge type (rising/falling, default: rising)
    --dso-config FILE       DSO setup config file (if provided, applies setup before test)

    --output-dir DIR        Output directory for results (default: ./results)
    --test-name NAME        Test name for output files (default: latency_test_TIMESTAMP)

    --verbose               Enable verbose output (show all phase details)
    --help, -h              Show this help message

ENVIRONMENT VARIABLES:
    TOUCH_TESTER            Path to touch-tester binary
    OUTPUT_GPIO             GPIO for touch simulation
    OUTPUT_PROBE            GPIO for probe pulse
    DSO_IP                  Oscilloscope IP address
    OUTPUT_DIR              Results directory

EXAMPLES:
    # Basic test with defaults (100 samples)
    $0

    # Initialize DSO with config file before test
    $0 --dso-config=/usr/share/touch-tester/dso-configs/touch-tester/touch-test-setup.json

    # Custom GPIO and sample count with slower pulse rate for DSO
    $0 --output-gpio=17 --output-probe=22 --loop-count=200 --wait-ms=150

    # Different DSO channels and edges
    $0 --dso-ch1=2 --dso-ch2=3 --dso-edge1=falling --dso-edge2=rising

    # Custom output location
    $0 --output-dir=/data/tests --test-name=prod_validation_001

OUTPUT FILES:
    \$OUTPUT_DIR/\$TEST_NAME_touch.json     - Touch-tester measurement results
    \$OUTPUT_DIR/\$TEST_NAME_dso.json       - DSO accumulated statistics
    \$OUTPUT_DIR/\$TEST_NAME_combined.json  - Combined analysis report

EOF
}

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        --touch-tester=*)
            TOUCH_TESTER="${1#*=}"
            ;;
        --output-gpio=*)
            OUTPUT_GPIO="${1#*=}"
            ;;
        --output-probe=*)
            OUTPUT_PROBE="${1#*=}"
            ;;
        --loop-count=*)
            LOOP_COUNT="${1#*=}"
            ;;
        --test-type=*)
            TEST_TYPE="${1#*=}"
            ;;
        --wait-ms=*)
            WAIT_MS="${1#*=}"
            ;;
        --dso-ip=*)
            DSO_IP="${1#*=}"
            ;;
        --dso-ch1=*)
            DSO_CH1="${1#*=}"
            ;;
        --dso-ch2=*)
            DSO_CH2="${1#*=}"
            ;;
        --dso-edge1=*)
            DSO_EDGE1="${1#*=}"
            ;;
        --dso-edge2=*)
            DSO_EDGE2="${1#*=}"
            ;;
        --dso-config=*)
            DSO_CONFIG_FILE="${1#*=}"
            ;;
        --output-dir=*)
            OUTPUT_DIR="${1#*=}"
            ;;
        --test-name=*)
            TEST_NAME="${1#*=}"
            ;;
        --verbose)
            VERBOSE="true"
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            usage >&2
            exit 1
            ;;
    esac
    shift
done

# Map edge types to Rigol delay measurement type
case "${DSO_EDGE1}-${DSO_EDGE2}" in
    rising-rising)   DELAY_TYPE="RRDelay" ;;
    rising-falling)  DELAY_TYPE="RFDelay" ;;
    falling-rising)  DELAY_TYPE="FRDelay" ;;
    falling-falling) DELAY_TYPE="FFDelay" ;;
    *)
        log_error "Invalid edge combination: ${DSO_EDGE1}-${DSO_EDGE2}"
        exit 1
        ;;
esac

# Validation
if [ ! -x "$RIGOL_TOOL" ]; then
    log_error "rigol-tool.sh not found or not executable: $RIGOL_TOOL"
    exit 1
fi

if ! command -v "$TOUCH_TESTER" >/dev/null 2>&1; then
    log_error "touch-tester not found: $TOUCH_TESTER"
    log_info "Make sure touch-tester is installed or set TOUCH_TESTER environment variable"
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Define output files
TOUCH_OUTPUT="$OUTPUT_DIR/${TEST_NAME}_touch.json"
DSO_OUTPUT="$OUTPUT_DIR/${TEST_NAME}_dso.json"
COMBINED_OUTPUT="$OUTPUT_DIR/${TEST_NAME}_combined.json"

# Print configuration
printf "\n${BOLD}=== Automated Touch Latency Test (Hardware Synchronized) ===${NC}\n\n"
log_info "Test Configuration:"
printf "  Test Name:       %s\n" "$TEST_NAME"
printf "  Output Dir:      %s\n" "$OUTPUT_DIR"
printf "\n"
printf "  ${BOLD}Touch Configuration:${NC}\n"
printf "  Touch Tester:    %s\n" "$TOUCH_TESTER"
printf "  Test Type:       %s\n" "$TEST_TYPE"
printf "  Output GPIO:     %s\n" "$OUTPUT_GPIO"
printf "  Probe GPIO:      %s\n" "$OUTPUT_PROBE"
printf "  Loop Count:      %s\n" "$LOOP_COUNT"
printf "\n"
printf "  ${BOLD}DSO Configuration:${NC}\n"
printf "  DSO IP:          %s\n" "$DSO_IP"
printf "  Channel 1:       %s (%s edge)\n" "$DSO_CH1" "$DSO_EDGE1"
printf "  Channel 2:       %s (%s edge)\n" "$DSO_CH2" "$DSO_EDGE2"
printf "  Delay Type:      %s\n" "$DELAY_TYPE"
if [ -n "$DSO_CONFIG_FILE" ]; then
    printf "  Config File:     %s\n" "$DSO_CONFIG_FILE"
fi
printf "\n"

# Test DSO connection first
log_info_verbose "Testing DSO connection..."
if ! "$RIGOL_TOOL" --command=check-connection --dso-ip="$DSO_IP" >/dev/null 2>&1; then
    log_error "Cannot connect to oscilloscope at $DSO_IP"
    exit 1
fi
log_success_verbose "DSO connection OK"

# Phase 0 (Optional): Initialize DSO with config file
if [ -n "$DSO_CONFIG_FILE" ]; then
    [ "$VERBOSE" = "true" ] && printf "\n"
    log_info_verbose "Phase 0: Initializing DSO with configuration file..."

    # Check if config file exists
    if [ ! -f "$DSO_CONFIG_FILE" ]; then
        log_error "DSO config file not found: $DSO_CONFIG_FILE"
        exit 1
    fi

    log_info_verbose "  Applying setup from: $DSO_CONFIG_FILE"
    if "$RIGOL_TOOL" --command=apply-setup --dso-ip="$DSO_IP" --input="$DSO_CONFIG_FILE"; then
        log_success_verbose "DSO initialization complete"
    else
        log_error "Failed to apply DSO configuration"
        exit 1
    fi

    # Give DSO time to settle after configuration
    sleep 2
fi

# Phase 1: Configure DSO for hardware-triggered measurements
log_info_verbose "Phase 1: Configuring DSO for hardware-triggered measurement..."

# Enable delay measurement with statistics
log_info_verbose "  - Enabling statistics mode..."
"$RIGOL_TOOL" --command=write --dso-ip="$DSO_IP" --scpi=":MEASure:STATistic:DISPlay ON" >/dev/null

# Setup the delay measurement
log_info_verbose "  - Setting up ${DELAY_TYPE} measurement (CH${DSO_CH1} → CH${DSO_CH2})..."
"$RIGOL_TOOL" --command=write --dso-ip="$DSO_IP" --scpi=":MEASure:CLEar" >/dev/null
"$RIGOL_TOOL" --command=write --dso-ip="$DSO_IP" --scpi=":MEASure:STATistic:ITEM ${DELAY_TYPE},CHANnel${DSO_CH1},CHANnel${DSO_CH2}" >/dev/null

# Reset statistics counter
log_info_verbose "  - Resetting statistics counter to zero..."
"$RIGOL_TOOL" --command=write --dso-ip="$DSO_IP" --scpi=":MEASure:STATistic:RESet" >/dev/null

# Ensure trigger is in NORMAL mode (waits for signal)
log_info_verbose "  - Setting trigger to NORMAL mode (hardware synchronized)..."
"$RIGOL_TOOL" --command=write --dso-ip="$DSO_IP" --scpi=":TRIGger:SWEep NORM" >/dev/null

sleep 1
log_success_verbose "DSO configured and armed, waiting for triggers"

# Phase 2: Run touch-tester to generate events
[ "$VERBOSE" = "true" ] && printf "\n"
log_info "Generating ${LOOP_COUNT} touch events..."
[ "$VERBOSE" = "true" ] && printf "\n"

TOUCH_CMD="$TOUCH_TESTER --testtype=$TEST_TYPE --output-gpio=$OUTPUT_GPIO --output-probe=$OUTPUT_PROBE --loopcount=$LOOP_COUNT --wait-ms=$WAIT_MS"

log_info_verbose "Command: $TOUCH_CMD"
[ "$VERBOSE" = "true" ] && printf "\n"

# Run touch-tester (DSO captures automatically on each trigger)
# Note: touch-tester doesn't support --output parameter, so redirect stdout/stderr to file if needed
if [ -n "$TOUCH_OUTPUT" ]; then
    if $TOUCH_CMD > "$TOUCH_OUTPUT" 2>&1; then
        TOUCH_EXIT=0
        log_success_verbose "Touch-tester completed successfully (output saved to $TOUCH_OUTPUT)"
    else
        TOUCH_EXIT=$?
        log_error "Touch-tester failed with exit code $TOUCH_EXIT"
        cat "$TOUCH_OUTPUT"
        exit 1
    fi
else
    if $TOUCH_CMD; then
        TOUCH_EXIT=0
        log_success_verbose "Touch-tester completed successfully"
    else
        TOUCH_EXIT=$?
        log_error "Touch-tester failed with exit code $TOUCH_EXIT"
        exit 1
    fi
fi

# Phase 3: Query accumulated statistics from DSO
[ "$VERBOSE" = "true" ] && printf "\n"
log_info_verbose "Phase 3: Querying accumulated statistics from DSO..."

# Give DSO time to finish processing final measurements
log_info_verbose "  Waiting for DSO to complete final measurement processing..."
sleep 2

# Query statistics count
CNT_RESULT=$("$RIGOL_TOOL" --command=query --dso-ip="$DSO_IP" --scpi=":MEASure:STATistic:ITEM? CNT,${DELAY_TYPE},CHANnel${DSO_CH1},CHANnel${DSO_CH2}" | grep "Response:" | sed 's/Response: //' || echo "0")

# Convert scientific notation to integer
CNT_INTEGER=$(awk "BEGIN {printf \"%.0f\", $CNT_RESULT}")

log_info_verbose "  Statistics count: $CNT_INTEGER triggers captured"

# Query all statistics
MIN_RESULT=$("$RIGOL_TOOL" --command=query --dso-ip="$DSO_IP" --scpi=":MEASure:STATistic:ITEM? MINimum,${DELAY_TYPE},CHANnel${DSO_CH1},CHANnel${DSO_CH2}" | grep "Response:" | sed 's/Response: //' || echo "0")
MAX_RESULT=$("$RIGOL_TOOL" --command=query --dso-ip="$DSO_IP" --scpi=":MEASure:STATistic:ITEM? MAXimum,${DELAY_TYPE},CHANnel${DSO_CH1},CHANnel${DSO_CH2}" | grep "Response:" | sed 's/Response: //' || echo "0")
AVG_RESULT=$("$RIGOL_TOOL" --command=query --dso-ip="$DSO_IP" --scpi=":MEASure:STATistic:ITEM? AVERages,${DELAY_TYPE},CHANnel${DSO_CH1},CHANnel${DSO_CH2}" | grep "Response:" | sed 's/Response: //' || echo "0")
DEV_RESULT=$("$RIGOL_TOOL" --command=query --dso-ip="$DSO_IP" --scpi=":MEASure:STATistic:ITEM? DEViation,${DELAY_TYPE},CHANnel${DSO_CH1},CHANnel${DSO_CH2}" | grep "Response:" | sed 's/Response: //' || echo "0")

# Create JSON output with DSO statistics
cat > "$DSO_OUTPUT" <<EOF
{
  "measurement": "hardware_triggered_stats",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "oscilloscope_ip": "$DSO_IP",
  "channel1": $DSO_CH1,
  "channel2": $DSO_CH2,
  "edge1": "$DSO_EDGE1",
  "edge2": "$DSO_EDGE2",
  "delay_type": "$DELAY_TYPE",
  "samples_requested": $LOOP_COUNT,
  "samples_captured": $CNT_RESULT,
  "statistics": {
    "min_seconds": $MIN_RESULT,
    "max_seconds": $MAX_RESULT,
    "mean_seconds": $AVG_RESULT,
    "stddev_seconds": $DEV_RESULT,
    "min_ms": $(awk "BEGIN {printf \"%.6f\", $MIN_RESULT * 1000}"),
    "max_ms": $(awk "BEGIN {printf \"%.6f\", $MAX_RESULT * 1000}"),
    "mean_ms": $(awk "BEGIN {printf \"%.6f\", $AVG_RESULT * 1000}"),
    "stddev_ms": $(awk "BEGIN {printf \"%.6f\", $DEV_RESULT * 1000}")
  },
  "synchronization": "hardware_trigger",
  "status": "success"
}
EOF

log_success_verbose "DSO statistics saved to: $DSO_OUTPUT"

# Display summary
printf "\n${BOLD}Test Results:${NC}\n"

# Compare captured count with requested count and color accordingly
if [ "$CNT_INTEGER" -eq "$LOOP_COUNT" ]; then
    # Match - use green
    printf "  Triggers Captured: ${GREEN}%d / %d requested${NC}\n" "$CNT_INTEGER" "$LOOP_COUNT"
else
    # Mismatch - use red
    printf "  Triggers Captured: ${RED}%d / %d requested${NC}\n" "$CNT_INTEGER" "$LOOP_COUNT"
fi

printf "  Min Delay:         %.3f ms\n" $(awk "BEGIN {printf \"%.3f\", $MIN_RESULT * 1000}")
printf "  Max Delay:         %.3f ms\n" $(awk "BEGIN {printf \"%.3f\", $MAX_RESULT * 1000}")
printf "  Mean Delay:        %.3f ms\n" $(awk "BEGIN {printf \"%.3f\", $AVG_RESULT * 1000}")
printf "  Std Deviation:     %.3f ms\n" $(awk "BEGIN {printf \"%.3f\", $DEV_RESULT * 1000}")

# Combine results if both exist
if [ -f "$TOUCH_OUTPUT" ] && [ -f "$DSO_OUTPUT" ]; then
    [ "$VERBOSE" = "true" ] && printf "\n"
    log_info_verbose "Generating combined analysis report..."

    if command -v jq >/dev/null 2>&1; then
        # Check if touch output is valid JSON
        if jq empty "$TOUCH_OUTPUT" 2>/dev/null; then
            # Both files are valid JSON - create structured combined report
            jq -n \
                --slurpfile touch "$TOUCH_OUTPUT" \
                --slurpfile dso "$DSO_OUTPUT" \
                --arg test_name "$TEST_NAME" \
                --arg timestamp "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
                '{
                    test_name: $test_name,
                    timestamp: $timestamp,
                    synchronization_method: "hardware_trigger",
                    touch_tester: $touch[0],
                    oscilloscope: $dso[0],
                    summary: {
                        samples_touch: ($touch[0].loop_count // 0),
                        samples_dso: ($dso[0].samples_captured // 0),
                        touch_latency_mean_ms: ($touch[0].statistics.mean_ms // 0),
                        dso_delay_mean_ms: ($dso[0].statistics.mean_ms // 0),
                        correlation: "Hardware synchronized via DSO trigger"
                    }
                }' > "$COMBINED_OUTPUT"
            log_success_verbose "Combined report saved to: $COMBINED_OUTPUT"
        else
            # Touch output is not JSON - include as raw text
            jq -n \
                --rawfile touch_raw "$TOUCH_OUTPUT" \
                --slurpfile dso "$DSO_OUTPUT" \
                --arg test_name "$TEST_NAME" \
                --arg timestamp "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
                '{
                    test_name: $test_name,
                    timestamp: $timestamp,
                    synchronization_method: "hardware_trigger",
                    touch_tester_output: $touch_raw,
                    oscilloscope: $dso[0],
                    note: "Touch-tester output is human-readable text, not JSON"
                }' > "$COMBINED_OUTPUT"
            log_success_verbose "Combined report saved to: $COMBINED_OUTPUT (touch output as text)"
        fi
    else
        cat > "$COMBINED_OUTPUT" <<EOF
{
  "test_name": "$TEST_NAME",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "synchronization_method": "hardware_trigger",
  "touch_tester_results": "$TOUCH_OUTPUT",
  "oscilloscope_results": "$DSO_OUTPUT",
  "note": "Install jq for automated data merging"
}
EOF
        log_warning "jq not found - basic combined report created"
    fi
fi

# Print final summary
printf "\n${BOLD}=== Test Complete ===${NC}\n\n"
log_success "Results saved to:"
printf "  Touch Data:    %s\n" "$TOUCH_OUTPUT"
printf "  DSO Data:      %s\n" "$DSO_OUTPUT"
[ -f "$COMBINED_OUTPUT" ] && printf "  Combined:      %s\n" "$COMBINED_OUTPUT"
printf "\n"

exit 0
