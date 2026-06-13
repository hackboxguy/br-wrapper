#!/bin/sh

# rigol_setup.sh - Configure Rigol oscilloscope for automated touch-testing
# this setup includes 3 channels(TP_INT/TP_RST/TP_SCL)
# 3 channels are configured levels/timing/trigger and ready to capture touch-interrupt signal
# POSIX compliant version for maximum portability
# Usage: ./rigol_setup.sh [IP_ADDRESS]

set -eu

IP="${1:-192.168.1.7}"
SCRIPT_DIR=$(dirname "$0")
RIGOL_CMD="$SCRIPT_DIR/rigol_scpi.sh"

# Color codes for better output (if terminal supports it)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m' 
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
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

# Check if rigol_scpi.sh exists and is executable
if [ ! -f "$RIGOL_CMD" ]; then
    log_error "rigol_scpi.sh not found in $SCRIPT_DIR"
    printf "Make sure rigol_scpi.sh is in the same directory as this script\n"
    exit 1
fi

if [ ! -x "$RIGOL_CMD" ]; then
    log_warning "Making rigol_scpi.sh executable..."
    chmod +x "$RIGOL_CMD"
fi

# Test connection first
log_info "Testing connection to oscilloscope at $IP..."

# Try to get device ID - capture both stdout and stderr
TEMP_OUTPUT=$(mktemp)
if "$RIGOL_CMD" -i "$IP" -q "*IDN?" > "$TEMP_OUTPUT" 2>&1; then
    DEVICE_ID=$(cat "$TEMP_OUTPUT" | grep "Response:" | sed 's/Response: //' || echo "")
    if [ -n "$DEVICE_ID" ]; then
        log_success "Connected to: $DEVICE_ID"
    else
        # Show what we actually got for debugging
        log_warning "Got response but couldn't parse device ID:"
        cat "$TEMP_OUTPUT"
        DEVICE_ID="Unknown Device"
    fi
else
    log_error "Cannot connect to oscilloscope at $IP"
    printf "Command output:\n"
    cat "$TEMP_OUTPUT"
    printf "Please check:\n"
    printf "  - IP address is correct\n"
    printf "  - Oscilloscope is powered on\n" 
    printf "  - Network connection is working\n"
    printf "  - SCPI port 5555 is not blocked\n"
    rm -f "$TEMP_OUTPUT"
    exit 1
fi
rm -f "$TEMP_OUTPUT"

printf "\n"
log_info "Configuring Rigol oscilloscope at $IP..."
printf "\n"

# Function to send command with error checking
send_cmd() {
    if "$RIGOL_CMD" -i "$IP" "$1" >/dev/null 2>&1; then
        return 0
    else
        log_error "Failed to send: $1"
        return 1
    fi
}

# Setup Channel 1
log_info "Setting up Channel 1 (2V/div, +4V offset)..."
send_cmd ":CHANnel1:DISPlay on"
send_cmd ":CHANnel1:SCALe 2"
send_cmd ":CHANnel1:OFFSet 4"

# Setup Channel 2  
log_info "Setting up Channel 2 (2V/div, -0.8V offset)..."
send_cmd ":CHANnel2:DISPlay on"
send_cmd ":CHANnel2:SCALe 2"
send_cmd ":CHANnel2:OFFSet -0.8"

# Setup Channel 3
log_info "Setting up Channel 3 (1V/div, -3.7V offset)..."
send_cmd ":CHANnel3:DISPlay on"
send_cmd ":CHANnel3:SCALe 1"  
send_cmd ":CHANnel3:OFFSet -3.7"

# Turn off Channel 4
log_info "Turning off Channel 4..."
send_cmd ":CHANnel4:DISPlay off"

# Setup timebase
log_info "Setting timebase to 500μs/div..."
send_cmd ":TIMebase:MAIN:SCALe 0.0005"

# Setup trigger
log_info "Configuring trigger (CH1, 1.52V, falling edge, normal)..."
send_cmd ":TRIGger:EDGE:SOURce CHANnel1"
send_cmd ":TRIGger:EDGE:LEVel 1.52"
send_cmd ":TRIGger:EDGE:SLOPe NEG"
send_cmd ":TRIGger:SWEep NORM"
send_cmd ":TIMebase:MAIN:OFFSet 0.002"

printf "\n"
log_success "Oscilloscope configuration complete!"
printf "\n"
sleep 3
# Hide measurement result window
log_info "Hiding measurement result window..."
send_cmd ":MEASure:CLEar" >/dev/null 2>&1 #note: running this command after rigol_setup will help to remove the measurement screen
send_cmd ":MEASure:STATistic:DISPlay OFF" >/dev/null 2>&1
send_cmd ":MEASure:INDicator OFF" >/dev/null 2>&1

# Test measurement functions
log_info "Testing measurement functions..."
printf "\n"

# Function to query with error checking
query_cmd() {
    result=$("$RIGOL_CMD" -i "$IP" -q "$1" 2>/dev/null | sed 's/Response: //' || echo "ERROR")
    printf "%s\n" "$result"
}

printf "📊 Testing measurements:\n"

# Test with normal trigger mode
printf "   Normal trigger mode: "
send_cmd ":TRIGger:SWEep NORM" >/dev/null 2>&1
sleep 1
nwidth_norm=$(query_cmd ":MEASure:NWIDth? CHAN1")
printf "NWidth = %s\n" "$nwidth_norm"

# Test with auto trigger mode  
printf "   Auto trigger mode:   "
send_cmd ":TRIGger:SWEep AUTO" >/dev/null 2>&1
sleep 1  
nwidth_auto=$(query_cmd ":MEASure:NWIDth? CHAN1")
printf "NWidth = %s\n" "$nwidth_auto"

# Return to normal trigger mode
send_cmd ":TRIGger:SWEep NORM" >/dev/null 2>&1

printf "\n"
log_success "Setup and test complete!"

# Show final configuration summary
printf "\n"
log_info "Current Configuration Summary:"
printf "  📍 Device: %s\n" "$DEVICE_ID"
printf "  🔌 Channels: CH1(2V,+4V) CH2(2V,-0.8V) CH3(1V,-3.7V) CH4(OFF)\n"
printf "  ⏱️  Timebase: 500μs/div\n"
printf "  ⚡ Trigger: CH1, 1.52V, falling edge, normal mode\n"

# Check if measurements look reasonable
case "$nwidth_norm" in
    "9.9000E+37"|"ERROR") 
        log_warning "Normal trigger measurement shows no signal detected"
        ;;
    *"E-"*|*"e-"*)
        log_success "Normal trigger measurement successful: ${nwidth_norm}s"
        ;;
    *)
        log_info "Normal trigger measurement: $nwidth_norm"
        ;;
esac

printf "\n"
