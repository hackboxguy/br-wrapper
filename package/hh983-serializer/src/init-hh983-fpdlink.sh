#!/bin/sh
#
# HH983 FPDLink Serializer Initialization Script
# Equivalent to hh983-serializer kernel module
#
# This script initializes the TI DS90UH983 FPDLink-III serializer
# and configures REM_INTB (remote interrupt) functionality
#
# Hardware setup:
#   - Serializer: DS90UH983 at I2C address 0x18
#   - Deserializer: DS90UH984 at I2C address 0x2C
#   - I2C Bus: /dev/i2c-1 (Pi4 default)

# Configuration
I2C_BUS=1
SERIALIZER_ADDR=18
DESERIALIZER_ADDR=2c

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Exit on error
set -e

# Check if running as root or with i2c permissions
if ! i2cdetect -y ${I2C_BUS} >/dev/null 2>&1; then
    printf "${RED}Error: Cannot access I2C bus ${I2C_BUS}. Run as root or add user to i2c group.${NC}\n"
    exit 1
fi

echo "========================================"
echo "HH983 FPDLink Serializer Initialization"
echo "========================================"
echo "I2C Bus: ${I2C_BUS}"
echo "Serializer: 0x${SERIALIZER_ADDR}"
echo "Deserializer: 0x${DESERIALIZER_ADDR}"
echo ""

# Function to write to serializer register
write_ser_reg() {
    reg=$1
    value=$2
    desc=$3

    printf "Writing 0x${value} to serializer reg 0x${reg} (${desc})... "
    if i2cset -y ${I2C_BUS} 0x${SERIALIZER_ADDR} 0x${reg} 0x${value} 2>/dev/null; then
        printf "${GREEN}OK${NC}\n"
        return 0
    else
        printf "${RED}FAILED${NC}\n"
        return 1
    fi
}

# Function to read from serializer register
read_ser_reg() {
    reg=$1
    desc=$2

    value=$(i2cget -y ${I2C_BUS} 0x${SERIALIZER_ADDR} 0x${reg} 2>/dev/null)
    if [ $? -eq 0 ]; then
        printf "Read serializer reg 0x${reg} (${desc}): ${GREEN}${value}${NC}\n"
        return 0
    else
        printf "${RED}Failed to read reg 0x${reg}${NC}\n"
        return 1
    fi
}

# Function to write to deserializer register
write_deser_reg() {
    reg=$1
    value=$2
    desc=$3

    printf "Writing 0x${value} to deserializer reg 0x${reg} (${desc})... "
    if i2cset -y ${I2C_BUS} 0x${DESERIALIZER_ADDR} 0x${reg} 0x${value} 2>/dev/null; then
        printf "${GREEN}OK${NC}\n"
        return 0
    else
        printf "${RED}FAILED${NC}\n"
        return 1
    fi
}

# Function to read from deserializer register
read_deser_reg() {
    reg=$1
    desc=$2

    value=$(i2cget -y ${I2C_BUS} 0x${DESERIALIZER_ADDR} 0x${reg} 2>/dev/null)
    if [ $? -eq 0 ]; then
        printf "Read deserializer reg 0x${reg} (${desc}): ${GREEN}${value}${NC}\n"
        return 0
    else
        printf "${RED}Failed to read reg 0x${reg}${NC}\n"
        return 1
    fi
}

echo "=== Step 1: Enable I2C Passthrough ==="
write_ser_reg "07" "d8" "I2C Passthrough"
echo "Waiting for passthrough to stabilize..."
sleep 1
echo ""

echo "=== Step 2: Configure REM_INTB on Serializer ==="

# Enable REM_INT in interrupt control register (0xC6 = 0x21)
# Bits [5] and [0] = 1
write_ser_reg "c6" "21" "Enable REM_INT"

# Configure GPIO4 for Port 0 REM_INT forwarding (0x1B = 0x88)
# Use 0x98 for Port 1 if needed in dual-port configurations
write_ser_reg "1b" "88" "GPIO4 Port 0 REM_INT"

# Enable global INTB and FPD_TX interrupts (0x51 = 0x83)
# Bit [7] = 1, Bits [1:0] = 0b11
write_ser_reg "51" "83" "Global interrupts"
echo ""

echo "=== Step 3: Configure Deserializer INTB ==="
# Enable INTB on DS90UH984 deserializer (0x44 = 0x81)
write_deser_reg "44" "81" "Enable INTB"
echo ""

echo "=== Step 4: Verify Configuration ==="
read_ser_reg "c6" "Interrupt Control"
read_ser_reg "1b" "GPIO4 Config"
read_ser_reg "51" "Global Interrupt"
read_deser_reg "44" "Deserializer INTB"
echo ""

printf "${GREEN}========================================\n"
echo "HH983 Initialization Completed!"
printf "========================================${NC}\n"
echo ""
echo "REM_INTB pin is ready to mirror deserializer INTB_IN"
echo ""
echo "Expected register values:"
echo "  Serializer 0xC6 (Interrupt Control): 0x21"
echo "  Serializer 0x1B (GPIO4 Config):      0x88"
echo "  Serializer 0x51 (Global Interrupt):  0x83"
echo "  Deserializer 0x44 (INTB Enable):     0x81"

exit 0
