#!/bin/sh
#
# HH983-988 FPDLink Serializer/Deserializer Initialization Script
# DS90UH983 (serializer) + DS90UH988 (deserializer) I2C passthrough configuration
#
# Exposes remote I2C targets (TDDI at 0x48, 0x49) on the deserializer
# to the host (Raspberry Pi 4) via FPDLink BCC passthrough.
#
# Hardware setup:
#   - Host: Raspberry Pi 4
#   - Serializer: DS90UH983 at 0x18
#   - Deserializer: DS90UH988 at 0x2C
#   - Remote target: HIMAX TDDI at 0x48, on the deserializer's I2C Port 0 or
#     Port 1 depending on the panel board -- 12.3"-NQ5 has it on Port 1,
#     12.3"-NQ1.1 on Port 0.  This script probes for it, as the kernel driver
#     does.  Pass TDDI_PORT=0 or 1 in the environment to skip the probe.
#   - I2C Bus: /dev/i2c-1

# Configuration
I2C_BUS=1
SERIALIZER_ADDR=18
DESERIALIZER_ADDR=2c

# TDDI target addresses (7-bit)
TDDI_ADDR_1=48
TDDI_ADDR_2=49

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

set -e

# Check I2C access
if ! i2cdetect -y ${I2C_BUS} >/dev/null 2>&1; then
    printf "${RED}Error: Cannot access I2C bus ${I2C_BUS}. Run as root or add user to i2c group.${NC}\n"
    exit 1
fi

# Write to serializer register
write_ser() {
    printf "SER  0x$1 <- 0x$2 ($3)... "
    if i2cset -f -y ${I2C_BUS} 0x${SERIALIZER_ADDR} 0x$1 0x$2 2>/dev/null; then
        printf "${GREEN}OK${NC}\n"
    else
        printf "${RED}FAIL${NC}\n"
        return 1
    fi
}

# Write to deserializer register
write_des() {
    printf "DES  0x$1 <- 0x$2 ($3)... "
    if i2cset -f -y ${I2C_BUS} 0x${DESERIALIZER_ADDR} 0x$1 0x$2 2>/dev/null; then
        printf "${GREEN}OK${NC}\n"
    else
        printf "${RED}FAIL${NC}\n"
        return 1
    fi
}

# Read serializer register
read_ser() {
    value=$(i2cget -f -y ${I2C_BUS} 0x${SERIALIZER_ADDR} 0x$1 2>/dev/null)
    printf "SER  0x$1 = ${GREEN}${value}${NC} ($2)\n"
}

# Read deserializer register
read_des() {
    value=$(i2cget -f -y ${I2C_BUS} 0x${DESERIALIZER_ADDR} 0x$1 2>/dev/null)
    printf "DES  0x$1 = ${GREEN}${value}${NC} ($2)\n"
}

echo "================================================"
echo "HH983-988 FPDLink I2C Passthrough Init"
echo "================================================"
echo "Serializer (983): 0x${SERIALIZER_ADDR}"
echo "Deserializer (988): 0x${DESERIALIZER_ADDR}"
echo "TDDI targets: 0x${TDDI_ADDR_1}, 0x${TDDI_ADDR_2}"
echo ""

echo "=== Step 1: Enable I2C Passthrough ==="
# Serializer reg 0x07: bit[3]=1 enables passthrough
write_ser "07" "d8" "I2C passthrough enable"
sleep 0.5

# Deserializer reg 0x04: bits[4:3]=11 enables passthrough
write_des "04" "d9" "I2C passthrough enable"
sleep 0.5
echo ""

echo "=== Step 2: Check Link Status ==="
read_des "53" "RX Lock Status"
echo ""

echo "=== Step 3: Configure Serializer I2C Routing ==="
# The serializer controls all BCC routing when connected to the host.
# TARGET_ID: [7:1] = 7-bit address << 1
# TARGET_ALIAS: [7:1] = alias << 1, [0] = port select (not used on serializer)
# TARGET_DEST: [7:5] = dest port (000=Port0, 001=Port1), [1:0] = depth (00=direct)
#
# Which deserializer I2C port carries the TDDI differs per panel board, so
# probe rather than assume.  Port 1 is tried first so a board that has it there
# ends with exactly the register values this script used to write.
#
# Getting this wrong is worse than leaving it unset: TARGET_ALIAS0 claims host
# address 0x48 unconditionally, so a wrong TARGET_DEST0 hijacks 0x48 and NACKs
# every touch transaction -- even where plain pass-through would have reached
# the TDDI.  That is how 12.3"-NQ1.1 touch was broken until 2026-09-20.

probe_port() {
    # $1 = dest byte; returns 0 if the TDDI answers at host 0x48 through it
    i2cset -f -y ${I2C_BUS} 0x${SERIALIZER_ADDR} 0x78 0x00 2>/dev/null
    i2cset -f -y ${I2C_BUS} 0x${SERIALIZER_ADDR} 0x70 0x90 2>/dev/null
    i2cset -f -y ${I2C_BUS} 0x${SERIALIZER_ADDR} 0x88 0x$1 2>/dev/null
    i2cset -f -y ${I2C_BUS} 0x${SERIALIZER_ADDR} 0x78 0x90 2>/dev/null
    usleep 3000 2>/dev/null || sleep 0.01
    i2cget -f -y ${I2C_BUS} 0x48 0x00 >/dev/null 2>&1
}

case "${TDDI_PORT}" in
    0) DEST="00"; printf "TDDI port forced to DES I2C Port 0\n" ;;
    1) DEST="20"; printf "TDDI port forced to DES I2C Port 1\n" ;;
    *)
        if probe_port "20"; then
            DEST="20"; printf "TDDI 0x48 found on DES I2C Port 1\n"
        elif probe_port "00"; then
            DEST="00"; printf "TDDI 0x48 found on DES I2C Port 0\n"
        else
            DEST="20"
            printf "${RED}TDDI 0x48 answered on neither port; leaving the route on Port 1${NC}\n"
        fi
        ;;
esac

# TDDI 0x48 -> the port selected above
write_ser "70" "90" "TARGET_ID0: TDDI 0x48"
write_ser "88" "${DEST}" "TARGET_DEST0: DES I2C Port $(( 0x${DEST} >> 5 ))"
write_ser "78" "90" "TARGET_ALIAS0: alias 0x48"

# TDDI 0x49 -> same port
write_ser "71" "92" "TARGET_ID1: TDDI 0x49"
write_ser "89" "${DEST}" "TARGET_DEST1: DES I2C Port $(( 0x${DEST} >> 5 ))"
write_ser "79" "92" "TARGET_ALIAS1: alias 0x49"
echo ""

echo "=== Step 4: Configure REM_INTB (TDDI touch_int forwarding) ==="
# Signal path: TDDI touch_int -> 988 INTB_IN (pin 45) -> BCC -> 983 REM_INTB -> Host GPIO
#
# IMPORTANT: Configure serializer FIRST, then enable 988 INTB_IN forwarding LAST.
# If INTB_IN is enabled before the 983 is ready to handle interrupts,
# the REM_INTB line can latch in a stuck-low state.

# 983 Serializer: Configure REM_INTB (must be before DES INTB_IN enable)
#   0xC6 = 0x21: Enable REM_INT
#   0x1B = 0x88: GPIO4 for Port 0 REM_INT (BCC is always Port 0)
#   0x51 = 0x83: Enable global INTB output
write_ser "c6" "21" "Enable REM_INT"
usleep 2000 2>/dev/null || sleep 0.01
write_ser "1b" "88" "GPIO4 = Port 0 REM_INT (BCC is always Port 0)"
usleep 2000 2>/dev/null || sleep 0.01
write_ser "51" "83" "Global INTB enable"

# 988 Deserializer: Enable INTB_IN forwarding LAST (datasheet 7.3.9)
#   RX_INTN_CTL (0x44) bit 7 = 1 enables INTB_IN -> back channel -> serializer
write_des "44" "81" "INTB_IN enable (0x81 required, not 0x80!)"

# Allow FPDLink interrupt path to stabilize
sleep 0.1
echo ""

echo "=== Step 5: Verify Configuration ==="
read_ser "07" "I2C Control"
read_ser "70" "TARGET_ID0"
read_ser "78" "TARGET_ALIAS0"
read_ser "88" "TARGET_DEST0"
read_ser "71" "TARGET_ID1"
read_ser "79" "TARGET_ALIAS1"
read_ser "89" "TARGET_DEST1"
read_ser "c6" "REM_INT Control"
read_ser "1b" "GPIO4 Config"
read_ser "51" "Global INT"
read_des "44" "RX_INTN_CTL (INTB_IN)"
echo ""

printf "${GREEN}================================================${NC}\n"
printf "${GREEN}Initialization Complete${NC}\n"
printf "${GREEN}================================================${NC}\n"
echo ""
echo "Expected: 0x48 visible in 'i2cdetect -r -y ${I2C_BUS}' (0x49 only on boards"
echo "that have a second TDDI address; neither 12.3\" panel does)"
echo "REM_INTB chain: TDDI touch_int -> 988 INTB_IN -> BCC -> 983 REM_INTB -> Host"
echo ""

exit 0
