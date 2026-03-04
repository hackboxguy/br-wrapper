#!/bin/sh
#
# fpdlink-tool.sh - Unified diagnostic tool for FPDLink-4 devices
#
# Supports:
#   DS90UH983 serializer   (DP RX -> FPDLink TX)
#   DS90UH988 deserializer (FPDLink RX -> OLDI/RGB)
#   DS90HH984 deserializer (FPDLink RX -> DP/eDP TX)
#
# Usage:
#   fpdlink-tool.sh --target=983 --timings [--bus=N]
#   fpdlink-tool.sh --target=988 --timings [--bus=N]
#   fpdlink-tool.sh --target=984 --timings [--bus=N] [--stream=N]
#   fpdlink-tool.sh --target=<983|988|984> --pattern [--bus=N]
#   fpdlink-tool.sh --target=<983|988|984> --pattern=<name> [--bus=N]
#

# Default I2C addresses
ADDR_983=0x18
ADDR_988=0x2c
ADDR_984=0x2c

# Defaults
I2C_BUS=1
TARGET=""
TIMINGS=0
PATTERN=""
PATTERN_SET=0
STREAM=0

# -----------------------------------------------
# Argument parsing
# -----------------------------------------------
for arg in "$@"; do
    case "$arg" in
        --target=*)  TARGET="${arg#*=}" ;;
        --bus=*)     I2C_BUS="${arg#*=}" ;;
        --timings)   TIMINGS=1 ;;
        --pattern)   PATTERN_SET=1; PATTERN="" ;;
        --pattern=*) PATTERN_SET=1; PATTERN="${arg#*=}" ;;
        --stream=*)  STREAM="${arg#*=}" ;;
        --help|-h)
            echo "Usage: $0 --target=<983|988|984> <action> [options]"
            echo ""
            echo "Actions:"
            echo "  --timings              Read video timing registers"
            echo "  --pattern              Read current pattern generator state"
            echo "  --pattern=<name>       Set pattern generator"
            echo ""
            echo "Options:"
            echo "  --target=<983|988|984> Target device (required)"
            echo "  --bus=N                I2C bus number (default: 1)"
            echo "  --stream=N             FPDLink stream number for 984 (default: 0)"
            echo ""
            echo "Pattern names (all targets):"
            echo "  off          Disable pattern generator, pass DP video through"
            echo "  checker      Checkerboard (white/black)"
            echo "  white        Solid white"
            echo "  black        Solid black"
            echo "  red          Solid red"
            echo "  green        Solid green"
            echo "  blue         Solid blue"
            echo "  colorbar     Color bars (8 colors)"
            echo "  vcom         VCOM pattern"
            echo "  vcom-alt     Alt VCOM pattern"
            echo "  hgrad-white  Horizontal gradient black to white"
            echo "  hgrad-red    Horizontal gradient black to red"
            echo "  hgrad-green  Horizontal gradient black to green"
            echo "  hgrad-blue   Horizontal gradient black to blue"
            echo "  vgrad-white  Vertical gradient black to white"
            echo "  vgrad-red    Vertical gradient black to red"
            echo "  vgrad-green  Vertical gradient black to green"
            echo "  vgrad-blue   Vertical gradient black to blue"
            echo ""
            echo "Examples:"
            echo "  $0 --target=983 --timings"
            echo "  $0 --target=988 --timings --bus=3"
            echo "  $0 --target=984 --timings --stream=0"
            echo "  $0 --target=983 --pattern"
            echo "  $0 --target=988 --pattern=colorbar"
            echo "  $0 --target=984 --pattern=red"
            echo "  $0 --target=983 --pattern=off"
            exit 0
            ;;
        *)
            echo "Error: Unknown argument '$arg'"
            echo "Use --help for usage"
            exit 1
            ;;
    esac
done

if [ -z "$TARGET" ]; then
    echo "Error: --target is required"
    echo "Use --help for usage"
    exit 1
fi

if [ "$TIMINGS" -eq 0 ] && [ "$PATTERN_SET" -eq 0 ]; then
    echo "Error: No action specified (use --timings or --pattern)"
    echo "Use --help for usage"
    exit 1
fi

# -----------------------------------------------
# I2C helpers
# -----------------------------------------------
i2c_write() { i2cset -f -y "$I2C_BUS" "$1" "$2" "$3"; }
i2c_read()  { i2cget -f -y "$I2C_BUS" "$1" "$2"; }

# Read 8-bit indirect register: ind_read8 <dev_addr> <page> <offset>
ind_read8() {
    i2c_write "$1" 0x40 "$2"
    i2c_write "$1" 0x41 "$3"
    i2c_read  "$1" 0x42
}

# Read 16-bit LE indirect register: ind_read16_le <dev_addr> <page> <offset>
# Reads low byte at offset, high byte at offset+1 -> (hi << 8) | lo
ind_read16_le() {
    _dev=$1; _page=$2; _off=$3
    _next=$(printf "0x%02x" $((_off + 1)))
    i2c_write "$_dev" 0x40 "$_page"
    i2c_write "$_dev" 0x41 "$_off"
    _lo=$(i2c_read "$_dev" 0x42)
    i2c_write "$_dev" 0x41 "$_next"
    _hi=$(i2c_read "$_dev" 0x42)
    echo $(( (_hi << 8) | _lo ))
}

# Read 15-bit BE indirect register: ind_read15_be <dev_addr> <page> <msb_offset> <lsb_offset>
# MSB[6:0] at msb_offset, LSB[7:0] at lsb_offset -> (MSB[6:0] << 8) | LSB
ind_read15_be() {
    _dev=$1; _page=$2; _msb_off=$3; _lsb_off=$4
    i2c_write "$_dev" 0x40 "$_page"
    i2c_write "$_dev" 0x41 "$_msb_off"
    _msb=$(i2c_read "$_dev" 0x42)
    i2c_write "$_dev" 0x41 "$_lsb_off"
    _lsb=$(i2c_read "$_dev" 0x42)
    echo $(( ((_msb & 0x7F) << 8) | _lsb ))
}

# Read 13-bit BE indirect register (for sync widths): ind_read13_be <dev_addr> <page> <msb_off> <lsb_off>
# MSB[5:0] at msb_offset, LSB[7:0] at lsb_offset -> (MSB[5:0] << 8) | LSB
ind_read13_be() {
    _dev=$1; _page=$2; _msb_off=$3; _lsb_off=$4
    i2c_write "$_dev" 0x40 "$_page"
    i2c_write "$_dev" 0x41 "$_msb_off"
    _msb=$(i2c_read "$_dev" 0x42)
    i2c_write "$_dev" 0x41 "$_lsb_off"
    _lsb=$(i2c_read "$_dev" 0x42)
    echo $(( ((_msb & 0x3F) << 8) | _lsb ))
}

# Write indirect register: ind_write8 <dev_addr> <page> <offset> <value>
ind_write8() {
    i2c_write "$1" 0x40 "$2"
    i2c_write "$1" 0x41 "$3"
    i2c_write "$1" 0x42 "$4"
}

# APB write: apb_write <dev_addr> <apb_addr_16bit> <data_8bit>
# APB indirect access: addr low (0x49), addr high (0x4A), data (0x4B), trigger (0x48=0x01)
apb_write() {
    _dev=$1; _addr=$2; _data=$3
    _lo=$(printf "0x%02x" $((_addr & 0xFF)))
    _hi=$(printf "0x%02x" $(((_addr >> 8) & 0xFF)))
    i2c_write "$_dev" 0x49 "$_lo"
    i2c_write "$_dev" 0x4a "$_hi"
    i2c_write "$_dev" 0x4b "$_data"
    i2c_write "$_dev" 0x48 0x01
}

# Recovery: digital reset + HPD toggle to force DP re-training
# Note: After this, also run 'toggle-pi-hdmi.sh cycle' for full recovery
recover_983_dp() {
    echo "Recovering DP link (digital reset + HPD toggle)..."
    # Digital reset (reg 0x01 bit 0, self-clearing)
    i2c_write "$ADDR_983" 0x01 0x01
    sleep 1
    # HPD LOW -> DP source drops link
    apb_write "$ADDR_983" 0x000 0x00
    sleep 1
    # HPD HIGH -> DP source re-trains
    apb_write "$ADDR_983" 0x000 0x01
    sleep 1
    echo "DP link recovery complete"
}

# -----------------------------------------------
# 983 Timings: VP0 Video Timing (Indirect Page 0x32)
# -----------------------------------------------
cmd_983_timings() {
    echo "=== DS90UH983 Serializer - Video Timing ==="
    echo "I2C Bus: $I2C_BUS  Address: $ADDR_983"
    echo ""

    # Link status
    sts=$(i2c_read "$ADDR_983" 0x0C)
    val=$((sts))
    rx_lock="NO"; [ $((val & 0x40)) -ne 0 ] && rx_lock="YES"
    link_det="NO"; [ $((val & 0x01)) -ne 0 ] && link_det="YES"
    echo "Link Status (0x0C): $sts  RX_LOCK=$rx_lock LINK_DET=$link_det"
    echo ""

    # VP0 timing from indirect page 0x32 (16-bit LE)
    echo "--- VP0 Timing (Page 0x32) ---"
    h_active=$(ind_read16_le "$ADDR_983" 0x32 0x10)
    h_bp=$(ind_read16_le "$ADDR_983" 0x32 0x12)
    h_sync=$(ind_read16_le "$ADDR_983" 0x32 0x14)
    h_total=$(ind_read16_le "$ADDR_983" 0x32 0x16)
    v_active=$(ind_read16_le "$ADDR_983" 0x32 0x18)
    v_bp=$(ind_read16_le "$ADDR_983" 0x32 0x1a)
    v_sync=$(ind_read16_le "$ADDR_983" 0x32 0x1c)
    v_fp=$(ind_read16_le "$ADDR_983" 0x32 0x1e)

    echo "  H Active:      $h_active"
    echo "  H Back Porch:  $h_bp"
    echo "  H Sync Width:  $h_sync"
    echo "  H Total:       $h_total"
    echo "  V Active:      $v_active"
    echo "  V Back Porch:  $v_bp"
    echo "  V Sync Width:  $v_sync"
    echo "  V Front Porch: $v_fp"

    # Sync polarity
    sync_pol=$(ind_read8 "$ADDR_983" 0x32 0x27)
    echo "  Sync Polarity: $sync_pol"

    # Derived
    v_total=$((v_active + v_bp + v_sync + v_fp))
    h_fp=$((h_total - h_active - h_bp - h_sync))
    echo ""
    echo "--- Derived ---"
    echo "  V Total:       $v_total"
    echo "  H Front Porch: $h_fp"
    if [ "$h_total" -gt 0 ] && [ "$v_total" -gt 0 ]; then
        echo "  Resolution:    ${h_active}x${v_active}"
        echo "  Total:         ${h_total}x${v_total}"
    fi
}

# -----------------------------------------------
# 988 Timings: DTG Measured Timing (Indirect Page 0x50)
# -----------------------------------------------
cmd_988_timings() {
    echo "=== DS90UH988 Deserializer - Video Timing ==="
    echo "I2C Bus: $I2C_BUS  Address: $ADDR_988"
    echo ""

    # FPDLink lock status
    sts0=$(i2c_read "$ADDR_988" 0x53)
    sts1=$(i2c_read "$ADDR_988" 0x54)
    s0=$((sts0)); s1=$((sts1))
    fpd4_lock="NO"; [ $((s0 & 0x01)) -ne 0 ] && fpd4_lock="YES"
    lock="NO"; [ $((s1 & 0x01)) -ne 0 ] && lock="YES"
    sig_det="NO"; [ $((s1 & 0x02)) -ne 0 ] && sig_det="YES"
    echo "Link Status: FPD4_LOCK=$fpd4_lock LOCK=$lock SIG_DET=$sig_det"
    echo ""

    # DTG control register
    dtg_ctl=$(ind_read8 "$ADDR_988" 0x50 0x20)
    dtg_ctl_val=$((dtg_ctl))
    src_sel=$((dtg_ctl_val & 0x03))
    case $src_sel in
        0) src_desc="register values" ;;
        1) src_desc="measured timing (with overrides)" ;;
        2) src_desc="register values" ;;
        3) src_desc="measured + main-link HACTIVE" ;;
    esac
    echo "DTG_CTL (0x50:0x20): $dtg_ctl  PG_DATA_SOURCE_SEL=$src_sel ($src_desc)"
    echo ""

    # Measured timing from Page 20, Port 0 (offsets 0x40-0x4F)
    # Format: 15-bit BE — MSB[6:0] at even offset, LSB[7:0] at odd offset
    echo "--- Measured Timing (Page 0x50, Port 0) ---"
    h_total=$(ind_read15_be  "$ADDR_988" 0x50 0x40 0x41)
    v_total=$(ind_read15_be  "$ADDR_988" 0x50 0x42 0x43)
    h_active=$(ind_read15_be "$ADDR_988" 0x50 0x44 0x45)
    v_active=$(ind_read15_be "$ADDR_988" 0x50 0x46 0x47)
    h_start=$(ind_read15_be  "$ADDR_988" 0x50 0x48 0x49)
    v_start=$(ind_read15_be  "$ADDR_988" 0x50 0x4a 0x4b)
    h_sync=$(ind_read13_be   "$ADDR_988" 0x50 0x4c 0x4d)
    v_sync=$(ind_read13_be   "$ADDR_988" 0x50 0x4e 0x4f)

    echo "  H Total:       $h_total"
    echo "  V Total:       $v_total"
    echo "  H Active:      $h_active"
    echo "  V Active:      $v_active"
    echo "  H Start:       $h_start"
    echo "  V Start:       $v_start"
    echo "  H Sync Width:  $h_sync"
    echo "  V Sync Width:  $v_sync"

    if [ "$h_total" -gt 0 ] && [ "$v_total" -gt 0 ]; then
        echo ""
        echo "--- Derived ---"
        echo "  Resolution:    ${h_active}x${v_active}"
        echo "  Total:         ${h_total}x${v_total}"
    fi

    # Also show override/configured timing for reference
    echo ""
    echo "--- Override Timing (Page 0x50, Port 0) ---"
    oh_total=$(ind_read15_be  "$ADDR_988" 0x50 0x21 0x22)
    ov_total=$(ind_read15_be  "$ADDR_988" 0x50 0x23 0x24)
    oh_active=$(ind_read15_be "$ADDR_988" 0x50 0x25 0x26)
    ov_active=$(ind_read15_be "$ADDR_988" 0x50 0x27 0x28)
    oh_start=$(ind_read15_be  "$ADDR_988" 0x50 0x29 0x2a)
    ov_start=$(ind_read15_be  "$ADDR_988" 0x50 0x2b 0x2c)
    oh_sync=$(ind_read13_be   "$ADDR_988" 0x50 0x2d 0x2e)
    ov_sync=$(ind_read13_be   "$ADDR_988" 0x50 0x2f 0x30)

    echo "  H Total:       $oh_total"
    echo "  V Total:       $ov_total"
    echo "  H Active:      $oh_active"
    echo "  V Active:      $ov_active"
    echo "  H Start:       $oh_start"
    echo "  V Start:       $ov_start"
    echo "  H Sync Width:  $oh_sync"
    echo "  V Sync Width:  $ov_sync"
}

# -----------------------------------------------
# 984 Timings: FPD RX Link Layer (Indirect Page 0x48)
# -----------------------------------------------
cmd_984_timings() {
    echo "=== DS90HH984 Deserializer - Video Timing ==="
    echo "I2C Bus: $I2C_BUS  Address: $ADDR_984  Stream: $STREAM"
    echo ""

    # FPDLink lock status (same register layout as 988)
    sts0=$(i2c_read "$ADDR_984" 0x53)
    sts1=$(i2c_read "$ADDR_984" 0x54)
    s0=$((sts0)); s1=$((sts1))
    fpd4_lock="NO"; [ $((s0 & 0x01)) -ne 0 ] && fpd4_lock="YES"
    lock="NO"; [ $((s1 & 0x01)) -ne 0 ] && lock="YES"
    sig_det="NO"; [ $((s1 & 0x02)) -ne 0 ] && sig_det="YES"
    echo "Link Status: FPD4_LOCK=$fpd4_lock LOCK=$lock SIG_DET=$sig_det"
    echo ""

    # Per-stream timing from Page_18 (write 0x48 to reg 0x40)
    # Stream offsets: 0=0x00, 1=0x20, 2=0x40, 3=0x60, 4=0x80, 5=0xA0
    stream_off=$((STREAM * 0x20))
    hact_msb=$(printf "0x%02x" $((0x66 + stream_off)))
    hact_lsb=$(printf "0x%02x" $((0x65 + stream_off)))
    vtot_msb=$(printf "0x%02x" $((0x68 + stream_off)))
    vtot_lsb=$(printf "0x%02x" $((0x67 + stream_off)))
    flags_off=$(printf "0x%02x" $((0x69 + stream_off)))

    # Page_18: write 0x48 to reg 0x40 for write, 0x49 for read
    # Read HACTIVE (MSB first: 0x66, then LSB: 0x65)
    echo "--- Stream $STREAM Timing (Page 0x48) ---"
    echo "  (984 provides HACTIVE and VTOTAL per-stream only)"
    echo ""

    i2c_write "$ADDR_984" 0x40 0x48
    i2c_write "$ADDR_984" 0x41 "$hact_msb"
    _hmsb=$(i2c_read "$ADDR_984" 0x42)
    i2c_write "$ADDR_984" 0x41 "$hact_lsb"
    _hlsb=$(i2c_read "$ADDR_984" 0x42)
    h_active=$(( (_hmsb << 8) | _hlsb ))

    i2c_write "$ADDR_984" 0x41 "$vtot_msb"
    _vmsb=$(i2c_read "$ADDR_984" 0x42)
    i2c_write "$ADDR_984" 0x41 "$vtot_lsb"
    _vlsb=$(i2c_read "$ADDR_984" 0x42)
    v_total=$(( (_vmsb << 8) | _vlsb ))

    # Change flags
    i2c_write "$ADDR_984" 0x41 "$flags_off"
    flags=$(i2c_read "$ADDR_984" 0x42)
    flags_val=$((flags))
    hact_chg="NO"; [ $((flags_val & 0x02)) -ne 0 ] && hact_chg="YES"
    vtot_chg="NO"; [ $((flags_val & 0x01)) -ne 0 ] && vtot_chg="YES"

    echo "  H Active:      $h_active"
    echo "  V Total:       $v_total"
    echo "  HACTIVE_CHNG:  $hact_chg"
    echo "  VTOTAL_CHNG:   $vtot_chg"
}

# -----------------------------------------------
# Pattern Generator (shared across 983, 988, 984)
# -----------------------------------------------
# All three devices use the same PGCTL bit encoding:
#   bits[7:3]=PATGEN_SEL, bit[2]=FREERUN, bit[0]=PATGEN_EN
#   Value = (pattern_id << 3) | 0x05  (freerun=1, enable=1)
#
# Register locations differ:
#   983: Page 0x30, PGCTL=0x28, PGCFG=0x29
#   988: Page 0x50, PGCTL=0x00, PGCFG=0x01
#   984: Page 0x50, PGCTL=0x00, PGCFG=0x01

pattern_name_to_val() {
    case "$1" in
        off)         echo "0x00" ;;
        checker)     echo "0x05" ;;
        white)       echo "0x0D" ;;
        black)       echo "0x15" ;;
        red)         echo "0x1D" ;;
        green)       echo "0x25" ;;
        blue)        echo "0x2D" ;;
        hgrad-white) echo "0x35" ;;
        hgrad-red)   echo "0x3D" ;;
        hgrad-green) echo "0x45" ;;
        hgrad-blue)  echo "0x4D" ;;
        vgrad-white) echo "0x55" ;;
        vgrad-red)   echo "0x5D" ;;
        vgrad-green) echo "0x65" ;;
        vgrad-blue)  echo "0x6D" ;;
        vcom)        echo "0x7D" ;;
        vcom-alt)    echo "0x85" ;;
        colorbar)    echo "0x95" ;;
        *)           echo "" ;;
    esac
}

pattern_val_to_name() {
    _v=$(($1))
    _enabled=$((_v & 0x01))
    if [ "$_enabled" -eq 0 ]; then
        echo "off (pattern generator disabled)"
        return
    fi
    _sel=$(( (_v >> 3) & 0x1F ))
    case $_sel in
        0)  echo "checker (Checkerboard)" ;;
        1)  echo "white (Solid White)" ;;
        2)  echo "black (Solid Black)" ;;
        3)  echo "red (Solid Red)" ;;
        4)  echo "green (Solid Green)" ;;
        5)  echo "blue (Solid Blue)" ;;
        6)  echo "hgrad-white (Horiz Gradient White)" ;;
        7)  echo "hgrad-red (Horiz Gradient Red)" ;;
        8)  echo "hgrad-green (Horiz Gradient Green)" ;;
        9)  echo "hgrad-blue (Horiz Gradient Blue)" ;;
        10) echo "vgrad-white (Vert Gradient White)" ;;
        11) echo "vgrad-red (Vert Gradient Red)" ;;
        12) echo "vgrad-green (Vert Gradient Green)" ;;
        13) echo "vgrad-blue (Vert Gradient Blue)" ;;
        15) echo "vcom (VCOM Pattern)" ;;
        16) echo "vcom-alt (Alt VCOM Pattern)" ;;
        18) echo "colorbar (Color Bars)" ;;
        *)  echo "unknown (PATGEN_SEL=$_sel)" ;;
    esac
    _freerun=$(( (_v >> 2) & 0x01 ))
    if [ "$_freerun" -eq 1 ]; then
        printf "  [FREERUN]\n"
    fi
}

# Generic pattern read: cmd_pattern_read <label> <addr> <page> <pgctl_off> <pgcfg_off>
cmd_pattern_read() {
    _label=$1; _addr=$2; _page=$3; _pgctl=$4; _pgcfg=$5

    echo "=== $_label Pattern Generator Status ==="
    echo "I2C Bus: $I2C_BUS  Address: $_addr"
    echo ""

    pgctl=$(ind_read8 "$_addr" "$_page" "$_pgctl")
    pgcfg=$(ind_read8 "$_addr" "$_page" "$_pgcfg")
    echo "PGCTL  ($_page:$_pgctl): $pgctl"
    echo "PGCFG  ($_page:$_pgcfg): $pgcfg"
    echo ""
    echo "Pattern: $(pattern_val_to_name "$pgctl")"
}

# Generic pattern set: cmd_pattern_set <label> <addr> <page> <pgctl_off> <pgcfg_off>
cmd_pattern_set() {
    _label=$1; _addr=$2; _page=$3; _pgctl=$4; _pgcfg=$5

    pat_val=$(pattern_name_to_val "$PATTERN")
    if [ -z "$pat_val" ]; then
        echo "Error: Unknown pattern '$PATTERN'"
        echo "Use --help to see available patterns"
        exit 1
    fi

    echo "=== $_label Pattern Generator ==="
    echo "I2C Bus: $I2C_BUS  Address: $_addr"
    echo ""

    if [ "$PATTERN" = "off" ]; then
        echo "Disabling pattern generator..."
        ind_write8 "$_addr" "$_page" "$_pgctl" 0x00
        echo "Restoring color depth to default..."
        ind_write8 "$_addr" "$_page" "$_pgcfg" 0x00
        echo ""
        echo "Pattern generator DISABLED"
    else
        echo "Setting color depth to 24bpp..."
        ind_write8 "$_addr" "$_page" "$_pgcfg" 0x08

        echo "Enabling pattern: $PATTERN (PGCTL = $pat_val)..."
        ind_write8 "$_addr" "$_page" "$_pgctl" "$pat_val"
        echo ""
        echo "Pattern generator ENABLED: $PATTERN"
    fi
    echo ""
    echo "To disable: $0 --target=$TARGET --pattern=off"
}

# 983-specific wrapper: adds DP recovery on pattern-off
cmd_983_pattern_set() {
    pat_val=$(pattern_name_to_val "$PATTERN")
    if [ -z "$pat_val" ]; then
        echo "Error: Unknown pattern '$PATTERN'"
        echo "Use --help to see available patterns"
        exit 1
    fi

    echo "=== DS90UH983 Pattern Generator ==="
    echo "I2C Bus: $I2C_BUS  Address: $ADDR_983"
    echo ""

    if [ "$PATTERN" = "off" ]; then
        echo "Disabling pattern generator..."
        ind_write8 "$ADDR_983" 0x30 0x28 0x00
        echo "Restoring color depth to default..."
        ind_write8 "$ADDR_983" 0x30 0x29 0x00
        echo ""
        recover_983_dp
        echo ""
        echo "Pattern generator DISABLED — DP input video passes through"
        echo ""
        echo "If image is not stable, also run:"
        echo "  toggle-pi-hdmi.sh cycle"
    else
        echo "Setting color depth to 24bpp..."
        ind_write8 "$ADDR_983" 0x30 0x29 0x08

        echo "Enabling pattern: $PATTERN (PGCTL = $pat_val)..."
        ind_write8 "$ADDR_983" 0x30 0x28 "$pat_val"
        echo ""
        echo "Pattern generator ENABLED: $PATTERN"
        echo ""
        echo "If display shows pattern -> FPDLink path is working"
        echo "If black screen -> check 983 -> FPDLink -> 988/984 chain"
    fi
    echo ""
    echo "To disable: $0 --target=983 --pattern=off"
}

# -----------------------------------------------
# Dispatch
# -----------------------------------------------
case "$TARGET" in
    983)
        if [ "$TIMINGS" -eq 1 ]; then
            cmd_983_timings
        fi
        if [ "$PATTERN_SET" -eq 1 ]; then
            if [ -n "$PATTERN" ]; then
                cmd_983_pattern_set
            else
                cmd_pattern_read "DS90UH983" "$ADDR_983" 0x30 0x28 0x29
            fi
        fi
        ;;
    988)
        if [ "$TIMINGS" -eq 1 ]; then
            cmd_988_timings
        fi
        if [ "$PATTERN_SET" -eq 1 ]; then
            if [ -n "$PATTERN" ]; then
                cmd_pattern_set "DS90UH988" "$ADDR_988" 0x50 0x00 0x01
            else
                cmd_pattern_read "DS90UH988" "$ADDR_988" 0x50 0x00 0x01
            fi
        fi
        ;;
    984)
        if [ "$TIMINGS" -eq 1 ]; then
            cmd_984_timings
        fi
        if [ "$PATTERN_SET" -eq 1 ]; then
            if [ -n "$PATTERN" ]; then
                cmd_pattern_set "DS90HH984" "$ADDR_984" 0x50 0x00 0x01
            else
                cmd_pattern_read "DS90HH984" "$ADDR_984" 0x50 0x00 0x01
            fi
        fi
        ;;
    *)
        echo "Error: Unknown target '$TARGET' (use 983, 988, or 984)"
        exit 1
        ;;
esac
