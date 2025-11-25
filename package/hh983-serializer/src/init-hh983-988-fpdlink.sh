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
#   - Remote targets: HIMAX TDDI at 0x48, 0x49 (on deserializer I2C Port 1)
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
    if i2cset -y ${I2C_BUS} 0x${SERIALIZER_ADDR} 0x$1 0x$2 2>/dev/null; then
        printf "${GREEN}OK${NC}\n"
    else
        printf "${RED}FAIL${NC}\n"
        return 1
    fi
}

# Write to deserializer register
write_des() {
    printf "DES  0x$1 <- 0x$2 ($3)... "
    if i2cset -y ${I2C_BUS} 0x${DESERIALIZER_ADDR} 0x$1 0x$2 2>/dev/null; then
        printf "${GREEN}OK${NC}\n"
    else
        printf "${RED}FAIL${NC}\n"
        return 1
    fi
}

# Read serializer register
read_ser() {
    value=$(i2cget -y ${I2C_BUS} 0x${SERIALIZER_ADDR} 0x$1 2>/dev/null)
    printf "SER  0x$1 = ${GREEN}${value}${NC} ($2)\n"
}

# Read deserializer register
read_des() {
    value=$(i2cget -y ${I2C_BUS} 0x${DESERIALIZER_ADDR} 0x$1 2>/dev/null)
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
# TARGET_DEST: [7:5] = dest port (001=Port1), [1:0] = depth (00=direct)

# TDDI 0x48 -> route to deserializer I2C Port 1
write_ser "70" "90" "TARGET_ID0: TDDI 0x48"
write_ser "78" "90" "TARGET_ALIAS0: alias 0x48"
write_ser "88" "20" "TARGET_DEST0: DES I2C Port 1"

# TDDI 0x49 -> route to deserializer I2C Port 1
write_ser "71" "92" "TARGET_ID1: TDDI 0x49"
write_ser "79" "92" "TARGET_ALIAS1: alias 0x49"
write_ser "89" "20" "TARGET_DEST1: DES I2C Port 1"
echo ""

echo "=== Step 4: Verify Configuration ==="
read_ser "07" "I2C Control"
read_ser "70" "TARGET_ID0"
read_ser "78" "TARGET_ALIAS0"
read_ser "88" "TARGET_DEST0"
read_ser "71" "TARGET_ID1"
read_ser "79" "TARGET_ALIAS1"
read_ser "89" "TARGET_DEST1"
echo ""

printf "${GREEN}================================================${NC}\n"
printf "${GREEN}Initialization Complete${NC}\n"
printf "${GREEN}================================================${NC}\n"
echo ""
echo "Expected: 0x48 and 0x49 visible in 'i2cdetect -r -y ${I2C_BUS}'"
echo ""

exit 0
