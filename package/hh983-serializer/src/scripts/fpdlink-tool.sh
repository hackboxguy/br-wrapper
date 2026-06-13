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
#   fpdlink-tool.sh --touch-diag [--target=988|984] [--bus=N]
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
TOUCH_DIAG=0
DIAGNOSE=0
DIAGNOSE_MODE=""
DIAG_FILE=""
RECOVER=0

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
        --touch-diag) TOUCH_DIAG=1 ;;
        --diagnose)   DIAGNOSE=1; DIAGNOSE_MODE="" ;;
        --diagnose=*) DIAGNOSE=1; DIAGNOSE_MODE="${arg#*=}" ;;
        --file=*)     DIAG_FILE="${arg#*=}" ;;
        --recover)    RECOVER=1 ;;
        --stream=*)  STREAM="${arg#*=}" ;;
        --help|-h)
            echo "Usage: $0 --target=<983|988|984> <action> [options]"
            echo ""
            echo "Actions:"
            echo "  --timings              Read video timing registers"
            echo "  --pattern              Read current pattern generator state"
            echo "  --pattern=<name>       Set pattern generator"
            echo "  --touch-diag           Dump touch interrupt/I2C/BCC registers (983+deser)"
            echo "                         Use with --target=988 (default) or --target=984"
            echo "  --diagnose             End-to-end pipeline health snapshot with verdict."
            echo "                         Use with --target=984 (default) or --target=988."
            echo "  --diagnose=baseline    Save current state to a baseline file"
            echo "  --diagnose=compare     Print current state + diff vs saved baseline"
            echo "  --recover              Recover deser video pipeline (soft digital reset)"
            echo "                         Fixes black-screen after HDMI replug or PATGEN off."
            echo "                         Use with --target=984 (default) or --target=988."
            echo ""
            echo "Options:"
            echo "  --target=<983|988|984> Target device (required)"
            echo "  --bus=N                I2C bus number (default: 1)"
            echo "  --stream=N             FPDLink stream number for 984 (default: 0)"
            echo "  --file=PATH            Baseline file for --diagnose=baseline|compare"
            echo "                         (default: /tmp/fpdlink-diagnose-baseline.txt)"
            echo "  NO_COLOR=1 env         Disable ANSI color in --diagnose output"
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
            echo "  $0 --touch-diag"
            echo "  $0 --touch-diag --target=984"
            echo "  $0 --touch-diag --bus=3"
            echo "  $0 --diagnose                         # snapshot + verdict (target=984 default)"
            echo "  $0 --diagnose --target=988            # same but for 988 mode"
            echo "  $0 --diagnose=baseline                # save /tmp/fpdlink-diagnose-baseline.txt"
            echo "  $0 --diagnose=compare                 # compare against that baseline"
            echo "  $0 --diagnose=baseline --file=/tmp/ok.txt"
            echo "  $0 --recover                          # fix black display after replug/PATGEN"
            echo "  $0 --recover --target=988             # same for 988 deser"
            exit 0
            ;;
        *)
            echo "Error: Unknown argument '$arg'"
            echo "Use --help for usage"
            exit 1
            ;;
    esac
done

if [ "$TOUCH_DIAG" -eq 1 ] || [ "$DIAGNOSE" -eq 1 ] || [ "$RECOVER" -eq 1 ]; then
    # These default --target internally; no requirement here.
    : # proceed
elif [ -z "$TARGET" ]; then
    echo "Error: --target is required"
    echo "Use --help for usage"
    exit 1
elif [ "$TIMINGS" -eq 0 ] && [ "$PATTERN_SET" -eq 0 ]; then
    echo "Error: No action specified (use --timings, --pattern, --touch-diag, --diagnose, or --recover)"
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

# APB read: apb_read <dev_addr> <apb_addr_16bit>
# Sequence (per DS90UH983 datasheet Table 7-119 and driver hh983_apb_read):
#   1. Set APB_ADR0/ADR1
#   2. Write APB_CTL = ENABLE | READ (0x03). READ bit is W1S, self-clears.
#   3. Small settle delay
#   4. Read APB_DATA0 (0x4B)
# Returns the byte as 0xNN string (via i2cget).
apb_read() {
    _dev=$1; _addr=$2
    _lo=$(printf "0x%02x" $((_addr & 0xFF)))
    _hi=$(printf "0x%02x" $(((_addr >> 8) & 0xFF)))
    i2c_write "$_dev" 0x49 "$_lo"
    i2c_write "$_dev" 0x4a "$_hi"
    i2c_write "$_dev" 0x48 0x03
    # ~200us settle for APB transaction
    sleep 0.001 2>/dev/null || :
    i2c_read "$_dev" 0x4b
}

# -----------------------------------------------
# ANSI color helpers (respect NO_COLOR env var and non-tty output)
# -----------------------------------------------
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    C_RED='\033[31m'
    C_GRN='\033[32m'
    C_YEL='\033[33m'
    C_BLD='\033[1m'
    C_RST='\033[0m'
else
    C_RED=''
    C_GRN=''
    C_YEL=''
    C_BLD=''
    C_RST=''
fi

# Colorized status markers (use with printf %b or the helpers below)
mk_ok()   { printf "${C_GRN}[OK]${C_RST}"; }
mk_warn() { printf "${C_YEL}[WARN]${C_RST}"; }
mk_fail() { printf "${C_RED}[FAIL]${C_RST}"; }
mk_info() { printf "[INFO]"; }

# Column layout for diagnose rows (visible widths):
#   2sp indent | 34-char label | " : " | 8-char value | 2sp | 6-char marker | 2sp | bits
# Marker column has fixed visible width 6 ("[OK]  " / "[FAIL]" / 6 spaces).
# Mismatch marker ("[!! expected 0xNN]") overflows by design so failures stand out.
# NOTE: ANSI color codes are invisible to terminals but are counted by printf's
# %-Ns width specifier, so the marker is emitted via %b (no width) after a
# space-padded inner text.
reg_line() {
    _label=$1; _val=$2; _exp=$3; _bits=$4
    if [ -z "$_exp" ]; then
        _mark='      '
    elif [ "$((_val))" -eq "$((_exp))" ]; then
        _mark="${C_GRN}[OK]  ${C_RST}"
    else
        _mark="${C_RED}[!! expected ${_exp}]${C_RST}"
    fi
    printf "  %-34s : %-8s  %b  %s\n" "$_label" "$_val" "$_mark" "$_bits"
}

# Raw value line (no expected check) — same columns as reg_line.
val_line() {
    reg_line "$1" "$2" "" "$3"
}

# Colored "free-form value" line for composite / non-register rows (e.g.
# timings). Uses the same column layout; <val> is shown in its own color.
info_line() {
    _label=$1; _color=$2; _val=$3; _bits=$4
    printf "  %-34s : ${_color}%-8s${C_RST}  %b  %s\n" \
        "$_label" "$_val" "      " "$_bits"
}

# Section header with a color
section() {
    printf "\n${C_BLD}%s${C_RST}\n" "$1"
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

# Recovery for deserializer (984/988) after PATGEN disable.
# Empirically: writing PGCTL/PGCFG = 0x00 on disable corrupts downstream
# eDP/OLDI color-depth pipeline; the device never re-locks to FPDLink video.
# Restoring the device defaults (0x08) and pulsing the main-page digital
# reset (reg 0x01 bit 0, self-clearing, preserves registers) re-latches
# the output pipeline and brings back incoming FPDLink video.
recover_deser_video() {
    _addr=$1
    echo "Recovering deserializer video path (digital reset)..."
    i2c_write "$_addr" 0x01 0x01
    sleep 0.5
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
# 984 Timings: DTG Measured Timing (Indirect Page 0x50)
# -----------------------------------------------
# 984 shares the DTG measured-timing layout with 988 (Page 0x50 offsets
# 0x40-0x4F). The per-stream HACTIVE/VTOTAL registers on Page 0x48 are
# override/configured fields and stay zero in a passthrough setup, so they
# are reported only as secondary change-flag info.
# GP_STATUS_1 bit 1 (SIG_DET) is reserved/unreliable on 984 and not shown.
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
    echo "Link Status: FPD4_LOCK=$fpd4_lock LOCK=$lock"
    echo ""

    # DTG control register (same layout as 988)
    dtg_ctl=$(ind_read8 "$ADDR_984" 0x50 0x20)
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

    # Measured timing from Page 0x50 offsets 0x40-0x4F (15-bit BE)
    echo "--- Measured Timing (Page 0x50, Port 0) ---"
    h_total=$(ind_read15_be  "$ADDR_984" 0x50 0x40 0x41)
    v_total=$(ind_read15_be  "$ADDR_984" 0x50 0x42 0x43)
    h_active=$(ind_read15_be "$ADDR_984" 0x50 0x44 0x45)
    v_active=$(ind_read15_be "$ADDR_984" 0x50 0x46 0x47)
    h_start=$(ind_read15_be  "$ADDR_984" 0x50 0x48 0x49)
    v_start=$(ind_read15_be  "$ADDR_984" 0x50 0x4a 0x4b)
    h_sync=$(ind_read13_be   "$ADDR_984" 0x50 0x4c 0x4d)
    v_sync=$(ind_read13_be   "$ADDR_984" 0x50 0x4e 0x4f)

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

    # Per-stream change flags (Page 0x48) — secondary diagnostic
    stream_off=$((STREAM * 0x20))
    flags_off=$(printf "0x%02x" $((0x69 + stream_off)))
    flags=$(ind_read8 "$ADDR_984" 0x48 "$flags_off")
    flags_val=$((flags))
    hact_chg="NO"; [ $((flags_val & 0x02)) -ne 0 ] && hact_chg="YES"
    vtot_chg="NO"; [ $((flags_val & 0x01)) -ne 0 ] && vtot_chg="YES"
    echo ""
    echo "--- Stream $STREAM Change Flags (Page 0x48:$flags_off) ---"
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
        echo "Disabling pattern generator (restoring defaults 0x08/0x08)..."
        # Writing 0x00 here corrupts the downstream color-depth pipeline on
        # 984 (eDP TX) and 988 (OLDI TX); the device shows BIST/garbage and
        # never re-locks to FPDLink video. 0x08 matches the device defaults
        # and the known-good stable state.
        ind_write8 "$_addr" "$_page" "$_pgctl" 0x08
        ind_write8 "$_addr" "$_page" "$_pgcfg" 0x08
        recover_deser_video "$_addr"
        echo ""
        echo "Pattern generator DISABLED — FPDLink video restored"
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
# Touch Interrupt Diagnostic (983 + 988 or 984 deserializer)
# -----------------------------------------------
# Dumps all registers in the touch interrupt signal path:
#   Touch_int -> deser INTB_IN -> BCC backchannel -> 983 REM_INTB -> GPIO4 -> Host IRQ
# Also dumps I2C passthrough, routing, and BCC health registers.
#
# Usage: run after fresh boot (baseline) and after HDMI switch + re-init (broken).
# Compare the two outputs — any [!!] marker shows a register that lost its config.

# Helper: print [OK] or [!! expected 0xNN] after a register value
check_reg() {
    _actual=$(($1)); _expected=$(($2))
    if [ "$_actual" -eq "$_expected" ]; then
        printf " ${C_GRN}[OK]${C_RST}"
    else
        printf " ${C_RED}[!! expected %s]${C_RST}" "$2"
    fi
}

# -----------------------------------------------
# Pipeline Diagnose (--diagnose)
# -----------------------------------------------
# Collects end-to-end pipeline state (983 DP RX/FPDLink TX, FPDLink PHY,
# 988/984 FPDLink RX/video/output), prints a colorized health report with
# an ASCII pipeline map, per-section register dumps with expected-vs-actual
# checks, and a final VERDICT with suggested remediation. Also supports
# save/compare against a baseline snapshot to catch config drift.
#
# Usage:
#   fpdlink-tool.sh --diagnose                     # live snapshot + verdict
#   fpdlink-tool.sh --diagnose=baseline [--file=PATH]
#   fpdlink-tool.sh --diagnose=compare  [--file=PATH]
#
# All three forms select the deserializer via --target=984 (default) or =988.

DIAG_BASELINE_FILE_DEFAULT=/tmp/fpdlink-diagnose-baseline.txt

# ---- State collection -----------------------------------------------------
# Populates DV_* variables with raw i2c reads so a single collection pass
# can serve diagnose, baseline save, and compare. Reads include clear-on-read
# registers (983 APB 0x194 SINK_0_INT_CAUSE, deser 0x45 LOCAL_INT_STS) —
# each diagnose call therefore clears those events from the device.
diag_collect() {
    _deser_addr=$1

    # --- 983 direct regs ---
    DV_983_01=$(i2c_read "$ADDR_983" 0x01)
    DV_983_02=$(i2c_read "$ADDR_983" 0x02)
    DV_983_07=$(i2c_read "$ADDR_983" 0x07)
    DV_983_0C=$(i2c_read "$ADDR_983" 0x0C)
    DV_983_1B=$(i2c_read "$ADDR_983" 0x1B)
    DV_983_51=$(i2c_read "$ADDR_983" 0x51)
    DV_983_C6=$(i2c_read "$ADDR_983" 0xC6)

    # --- 983 APB regs ---
    DV_983_APB_000=$(apb_read "$ADDR_983" 0x000)
    DV_983_APB_194=$(apb_read "$ADDR_983" 0x194)   # clear-on-read

    # --- 983 VP0 measured timing (Page 0x32, 16-bit LE) ---
    DV_983_HACT=$(ind_read16_le "$ADDR_983" 0x32 0x10)
    DV_983_HBP=$(ind_read16_le  "$ADDR_983" 0x32 0x12)
    DV_983_HSW=$(ind_read16_le  "$ADDR_983" 0x32 0x14)
    DV_983_HTOT=$(ind_read16_le "$ADDR_983" 0x32 0x16)
    DV_983_VACT=$(ind_read16_le "$ADDR_983" 0x32 0x18)
    DV_983_VBP=$(ind_read16_le  "$ADDR_983" 0x32 0x1a)
    DV_983_VSW=$(ind_read16_le  "$ADDR_983" 0x32 0x1c)
    DV_983_VFP=$(ind_read16_le  "$ADDR_983" 0x32 0x1e)

    # --- Deserializer direct regs ---
    DV_DES_01=$(i2c_read "$_deser_addr" 0x01)
    DV_DES_04=$(i2c_read "$_deser_addr" 0x04)
    DV_DES_0C=$(i2c_read "$_deser_addr" 0x0C)
    DV_DES_44=$(i2c_read "$_deser_addr" 0x44)
    DV_DES_45=$(i2c_read "$_deser_addr" 0x45)       # clear-on-read
    DV_DES_51=$(i2c_read "$_deser_addr" 0x51)
    DV_DES_53=$(i2c_read "$_deser_addr" 0x53)
    DV_DES_54=$(i2c_read "$_deser_addr" 0x54)
    # FPD4 CRC error counters (read-to-clear per datasheet page 152).
    DV_DES_6C=$(i2c_read "$_deser_addr" 0x6C)
    DV_DES_6D=$(i2c_read "$_deser_addr" 0x6D)

    # --- Deserializer Page 0x50 (DTG / PATGEN / measured timing) ---
    DV_DES_P50_00=$(ind_read8 "$_deser_addr" 0x50 0x00)
    DV_DES_P50_01=$(ind_read8 "$_deser_addr" 0x50 0x01)
    DV_DES_P50_20=$(ind_read8 "$_deser_addr" 0x50 0x20)
    DV_DES_P50_32=$(ind_read8 "$_deser_addr" 0x50 0x32)
    DV_DES_HTOT=$(ind_read15_be  "$_deser_addr" 0x50 0x40 0x41)
    DV_DES_VTOT=$(ind_read15_be  "$_deser_addr" 0x50 0x42 0x43)
    DV_DES_HACT=$(ind_read15_be  "$_deser_addr" 0x50 0x44 0x45)
    DV_DES_VACT=$(ind_read15_be  "$_deser_addr" 0x50 0x46 0x47)

    # --- Deserializer Page 0x48 stream-0 change flags ---
    DV_DES_P48_69=$(ind_read8 "$_deser_addr" 0x48 0x69)
}

# Ordered list of variables to dump into/read from a baseline file.
DV_VAR_LIST="DV_983_01 DV_983_02 DV_983_07 DV_983_0C DV_983_1B DV_983_51 DV_983_C6 \
DV_983_APB_000 DV_983_APB_194 \
DV_983_HACT DV_983_HBP DV_983_HSW DV_983_HTOT DV_983_VACT DV_983_VBP DV_983_VSW DV_983_VFP \
DV_DES_01 DV_DES_04 DV_DES_0C DV_DES_44 DV_DES_45 DV_DES_51 DV_DES_53 DV_DES_54 DV_DES_6C DV_DES_6D \
DV_DES_P50_00 DV_DES_P50_01 DV_DES_P50_20 DV_DES_P50_32 \
DV_DES_HTOT DV_DES_VTOT DV_DES_HACT DV_DES_VACT \
DV_DES_P48_69"

# ---- Baseline save / compare ---------------------------------------------
diag_save_baseline() {
    _file=$1; _mode=$2
    {
        echo "# fpdlink-tool --diagnose baseline"
        echo "# saved: $(date -Iseconds 2>/dev/null || date)"
        echo "# mode: $_mode  (0=983+984, 1=983+988)"
        for _v in $DV_VAR_LIST; do
            eval "printf '%s=%s\n' \"$_v\" \"\$$_v\""
        done
    } > "$_file"
    printf "Baseline saved to %s\n" "$_file"
}

diag_load_baseline() {
    _file=$1
    if [ ! -f "$_file" ]; then
        printf "${C_RED}Error:${C_RST} baseline file not found: %s\n" "$_file" >&2
        printf "Run '%s --diagnose=baseline' first to capture a snapshot.\n" "$0" >&2
        exit 1
    fi
    # Prefix each DV_ var with B_ so we can compare against current.
    while IFS='=' read -r _k _v; do
        case "$_k" in
            DV_*) eval "B_${_k#DV_}=$_v" ;;
            \#*|"") : ;;
        esac
    done < "$_file"
}

# Live-measured timing fields naturally jitter by a handful of pixels each
# sample (front porch / total counters tracking pixel clock); treat them as
# informational with a tolerance, not hard drift.
DV_JITTER_VARS="DV_DES_HTOT DV_DES_VTOT DV_983_HTOT DV_983_HBP DV_983_VBP DV_983_VFP"
DV_JITTER_TOL=10

_is_jitter_var() {
    for _j in $DV_JITTER_VARS; do
        [ "$_j" = "$1" ] && return 0
    done
    return 1
}

diag_compare_baseline() {
    printf "\n${C_BLD}Baseline Compare${C_RST}  (baseline: %s)\n" "$1"
    printf "Registers that DIFFER from baseline are highlighted in red.\n"
    printf "(live-measured timing fields use ±%d tolerance; shown as JITTER)\n\n" "$DV_JITTER_TOL"
    _ndiff=0
    _njitter=0
    for _v in $DV_VAR_LIST; do
        _cur=$(eval echo \$$_v)
        _base=$(eval echo \$B_${_v#DV_})
        if [ -z "$_base" ]; then
            continue
        fi
        _c=$((_cur)); _b=$((_base))
        [ "$_c" -eq "$_b" ] && continue
        if _is_jitter_var "$_v"; then
            _delta=$((_c - _b)); [ "$_delta" -lt 0 ] && _delta=$((-_delta))
            if [ "$_delta" -le "$DV_JITTER_TOL" ]; then
                printf "  ${C_YEL}JITTER${C_RST} %-20s  baseline=%s  now=%s  (Δ=%d, within tolerance)\n" \
                    "$_v" "$_base" "$_cur" "$_delta"
                _njitter=$((_njitter + 1))
                continue
            fi
        fi
        printf "  ${C_RED}DRIFT${C_RST}  %-20s  baseline=%s  now=%s\n" "$_v" "$_base" "$_cur"
        _ndiff=$((_ndiff + 1))
    done
    if [ "$_ndiff" -eq 0 ] && [ "$_njitter" -eq 0 ]; then
        printf "  ${C_GRN}No register drift versus baseline.${C_RST}\n"
    elif [ "$_ndiff" -eq 0 ]; then
        printf "\n  ${C_GRN}No drift (only %d jittery live-timing field(s)).${C_RST}\n" "$_njitter"
    else
        printf "\n  ${C_RED}%d register(s) drifted from baseline.${C_RST}" "$_ndiff"
        [ "$_njitter" -gt 0 ] && printf "  (%d jitter-tolerant shown above)" "$_njitter"
        printf "\n"
    fi
}

# ---- Pretty printer -------------------------------------------------------
# Derives per-stage health flags and prints the pipeline map + sections.
diag_print() {
    _deser_addr=$1; _deser_label=$2; _deser_mode=$3

    # Expected values based on mode
    # Mode 0 (984): driver leaves 0x04 at default 0xC1; passthrough (0xC9)
    #   is only asserted during hh983_recover_link(), not at init.
    # Mode 1 (988): init sets 0xD9 (default 0xD1 | bit3 I2C_PASSTHROUGH).
    if [ "$_deser_mode" -eq 0 ]; then
        _exp_983_51="0x83"
        _exp_des_04="0xC1"
    else
        _exp_983_51="0x93"
        _exp_des_04="0xD9"
    fi

    # Decode key bits
    _983_rxlock=$(( $((DV_983_0C)) >> 6 & 1 ))
    _983_linkdet=$(( $((DV_983_0C)) & 1 ))
    _983_hpd=$(( $((DV_983_APB_000)) & 1 ))
    _apb194_novid=$(( $((DV_983_APB_194)) >> 2 & 1 ))
    _apb194_viddet=$(( $((DV_983_APB_194)) >> 1 & 1 ))
    _apb194_mode=$((  $((DV_983_APB_194)) & 1 ))

    _des_fpd4=$(( $((DV_DES_53)) & 1 ))
    _des_lock=$(( $((DV_DES_54)) & 1 ))
    _des_plllock=$(( $((DV_DES_54)) >> 6 & 1 ))

    _patgen_en=$(( $((DV_DES_P50_00)) & 1 ))
    _patgen_sel=$(( $((DV_DES_P50_00)) >> 3 & 0x1F ))
    _dtg_src=$((   $((DV_DES_P50_20)) & 0x03 ))
    _dtg_rlock_en=$(( $((DV_DES_P50_32)) >> 2 & 1 ))

    _p48_hact_chg=$(( $((DV_DES_P48_69)) >> 1 & 1 ))
    _p48_vtot_chg=$(( $((DV_DES_P48_69)) & 1 ))

    _dp_has_video=0
    [ "$DV_983_HTOT" -gt 0 ] && [ "$DV_983_VACT" -gt 0 ] && _dp_has_video=1

    _stream_has_video=0
    [ "$DV_DES_HTOT" -gt 0 ] && [ "$DV_DES_VACT" -gt 0 ] && _stream_has_video=1

    # H_TOTAL on 984 should track 983's VP0 H_TOTAL within jitter. A wildly
    # different value (seen after HDMI unplug/replug) means the DTG is stuck
    # on a corrupt measurement even though HACT/VACT happen to look right.
    _htot_mismatch=0
    if [ "$_dp_has_video" -eq 1 ] && [ "$_stream_has_video" -eq 1 ]; then
        _delta=$((DV_DES_HTOT - DV_983_HTOT))
        [ "$_delta" -lt 0 ] && _delta=$((-_delta))
        [ "$_delta" -gt 100 ] && _htot_mismatch=1
    fi

    # ---- Verdict computation ----
    _verdict_tag=""; _verdict_msg=""; _verdict_hint=""
    if [ "$_983_rxlock" -eq 0 ] || [ "$_dp_has_video" -eq 0 ]; then
        _verdict_tag="FAIL"
        _verdict_msg="DP input missing or untrained at 983"
        _verdict_hint="Check Pi HDMI + HDMI-to-DP converter. Try fbdev blank-cycle on Pi (toggle-pi-hdmi.sh cycle) or replug HDMI."
    elif [ "$_des_fpd4" -eq 0 ] || [ "$_des_lock" -eq 0 ]; then
        _verdict_tag="FAIL"
        _verdict_msg="FPDLink PHY down between 983 and $_deser_label"
        _verdict_hint="Check FPDLink cable/connector and deser power. Rerun init-hh983-fpdlink.sh."
    elif [ "$_patgen_en" -eq 1 ]; then
        _verdict_tag="WARN"
        _verdict_msg="Pattern generator is ENABLED on $_deser_label (PATGEN_SEL=$_patgen_sel)"
        _verdict_hint="Run: $0 --target=${TARGET} --pattern=off"
    elif [ "$_stream_has_video" -eq 0 ]; then
        _verdict_tag="FAIL"
        _verdict_msg="FPDLink locked but no video in stream — $_deser_label not consuming video"
        _verdict_hint="Run: $0 --recover"
    elif [ "$_htot_mismatch" -eq 1 ]; then
        _verdict_tag="FAIL"
        _verdict_msg="984 DTG stuck on corrupt H_TOTAL (stream=${DV_DES_HTOT} vs DP=${DV_983_HTOT}) — likely after HDMI unplug/replug"
        _verdict_hint="Run: $0 --recover"
    else
        _verdict_tag="OK"
        _verdict_msg="Pipeline healthy — Pi -> 983 -> FPDLink -> $_deser_label -> panel"
        _verdict_hint=""
    fi

    # ---- Header + verdict (top) ----
    printf "${C_BLD}=== fpdlink-tool --diagnose ===${C_RST}\n"
    printf "I2C Bus: %s  Serializer: %s  Deserializer: %s (%s, mode=%d)\n" \
        "$I2C_BUS" "$ADDR_983" "$_deser_addr" "$_deser_label" "$_deser_mode"
    printf "Note: reading APB 0x194 and deser 0x45 clears latched events in those regs.\n\n"

    case "$_verdict_tag" in
        OK)   printf "VERDICT: ${C_GRN}[OK]${C_RST}   %s\n"   "$_verdict_msg" ;;
        WARN) printf "VERDICT: ${C_YEL}[WARN]${C_RST} %s\n"   "$_verdict_msg" ;;
        FAIL) printf "VERDICT: ${C_RED}[FAIL]${C_RST} %s\n"   "$_verdict_msg" ;;
    esac
    [ -n "$_verdict_hint" ] && printf "         ${C_YEL}Hint:${C_RST} %s\n" "$_verdict_hint"

    # ---- Pipeline map ----
    _m_dp="${C_GRN}OK${C_RST}";         [ "$_dp_has_video" -eq 0 ] && _m_dp="${C_RED}DOWN${C_RST}"
    _m_phy="${C_GRN}OK${C_RST}";        { [ "$_des_fpd4" -eq 0 ] || [ "$_des_lock" -eq 0 ]; } && _m_phy="${C_RED}DOWN${C_RST}"
    _m_vid="${C_GRN}OK${C_RST}";        [ "$_stream_has_video" -eq 0 ] && _m_vid="${C_RED}NO-VIDEO${C_RST}"
    [ "$_htot_mismatch" -eq 1 ]         && _m_vid="${C_RED}HTOT-BAD${C_RST}"
    [ "$_patgen_en" -eq 1 ]             && _m_vid="${C_YEL}PATGEN${C_RST}"

    printf "\n${C_BLD}Pipeline Map:${C_RST}\n"
    printf "  [Pi/DP src] --DP--> [983 DP RX %b] --FPDLink PHY %b--> [%s %b] --eDP/OLDI--> [FPGA/panel ?]\n" \
        "$_m_dp" "$_m_phy" "$_deser_label" "$_m_vid"
    printf "              src unprobed    RX_LOCK=%d  video=%dx%d    FPD4=%d LOCK=%d   stream=%dx%d    panel unprobed\n" \
        "$_983_rxlock" "$DV_983_HACT" "$DV_983_VACT" "$_des_fpd4" "$_des_lock" "$DV_DES_HACT" "$DV_DES_VACT"

    # ---- 983 section ----
    section "DS90UH983 Serializer ($ADDR_983) — DP RX / FPDLink TX"
    reg_line "0x0C GENERAL_STS"       "$DV_983_0C"  ""        "RX_LOCK=$_983_rxlock LINK_DET=$_983_linkdet"
    reg_line "0x02 GENERAL_CFG"       "$DV_983_02"  ""        "(bit7 set = stuck PHY reset)"
    reg_line "0x01 RESET_CTL"         "$DV_983_01"  "0x00"    "(non-zero = reset in progress)"
    reg_line "0x07 I2C_CONTROL"       "$DV_983_07"  "0xD8"    "passthrough"
    reg_line "0x1B GPIO4_CONFIG"      "$DV_983_1B"  "0x88"    "Port 0 REM_INT"
    reg_line "0x51 INTERRUPT_CTL"     "$DV_983_51"  "$_exp_983_51"  ""
    reg_line "0xC6 INTERRUPT_CTRL"    "$DV_983_C6"  "0x21"    "IE_RX_REM_INT | INT_EN"
    val_line "APB 0x000 LINK_ENABLE"         "$DV_983_APB_000"  "HPD=$_983_hpd"
    val_line "APB 0x194 SINK_0_INT_CAUSE"    "$DV_983_APB_194"  "NO_VIDEO=$_apb194_novid VIDEO_DETECT=$_apb194_viddet MODE_CHANGE=$_apb194_mode (cleared on read)"
    if [ "$_dp_has_video" -eq 1 ]; then
        _983_vtot=$((DV_983_VACT + DV_983_VBP + DV_983_VSW + DV_983_VFP))
        _res="${DV_983_HACT}x${DV_983_VACT}"
        info_line "VP0 Timing" "$C_GRN" "$_res" "@ ${DV_983_HTOT}x${_983_vtot} total, sync H/V=${DV_983_HSW}/${DV_983_VSW}"
    else
        info_line "VP0 Timing" "$C_RED" "no video" "HACT=${DV_983_HACT} VACT=${DV_983_VACT} (DP RX has no video)"
    fi

    # ---- FPDLink PHY section ----
    section "FPDLink PHY"
    if [ "$_des_fpd4" -eq 1 ] && [ "$_des_lock" -eq 1 ]; then
        info_line "PHY status" "$C_GRN" "LOCKED" "FPD4_LOCK=1 LOCK=1 PLL_LOCK=$_des_plllock"
    else
        info_line "PHY status" "$C_RED" "DOWN" "FPD4_LOCK=$_des_fpd4 LOCK=$_des_lock"
    fi
    val_line "0x53 GP_STATUS_0"     "$DV_DES_53"  "FPD4_LOCK=$_des_fpd4"
    val_line "0x54 GP_STATUS_1"     "$DV_DES_54"  "LOCK=$_des_lock PLL_LOCK=$_des_plllock"
    _crc_sum=$(( $((DV_DES_6C)) + $((DV_DES_6D)) ))
    if [ "$_crc_sum" -eq 0 ]; then
        info_line "CRC errors (0x6C/0x6D)" "$C_GRN" "0 / 0" "no FPD4 CRC errors (counters read-to-clear)"
    else
        info_line "CRC errors (0x6C/0x6D)" "$C_YEL" "${DV_DES_6C} / ${DV_DES_6D}" "FPD4 CRC errors since last read"
    fi

    # ---- Deserializer section ----
    section "$_deser_label Deserializer ($_deser_addr) — FPDLink RX / video / output"
    reg_line "0x04 GENERAL_CFG"       "$DV_DES_04"  "$_exp_des_04"  "passthrough"
    reg_line "0x01 RESET_CTL"         "$DV_DES_01"  "0x00"    "(non-zero = reset in progress)"
    reg_line "0x0C GENERAL_STS"       "$DV_DES_0C"  ""        ""
    reg_line "0x44 INTB_CTL"          "$DV_DES_44"  "0x81"    "IE_EN | IE_INTB_P0"
    val_line "0x45 LOCAL_INT_STS"           "$DV_DES_45"       "(cleared on read)"
    val_line "0x51 INTERRUPT_STS"           "$DV_DES_51"       ""

    section "Video Pipeline (Page 0x50 / Page 0x48)"
    val_line "0x50:0x00 PGCTL"        "$DV_DES_P50_00"        "PATGEN_EN=$_patgen_en SEL=$_patgen_sel (default 0x08)"
    val_line "0x50:0x01 PGCFG"        "$DV_DES_P50_01"        "(default 0x08)"
    _dtg_desc="register values"
    case "$_dtg_src" in
        1) _dtg_desc="measured timing" ;;
        3) _dtg_desc="measured + main-link HACTIVE" ;;
    esac
    val_line "0x50:0x20 DTG_CTL"      "$DV_DES_P50_20"        "PG_DATA_SOURCE_SEL=$_dtg_src ($_dtg_desc)"
    val_line "0x50:0x32 DTG_RESET_CTL" "$DV_DES_P50_32"       "DTG_RESET_LOCK_EN=$_dtg_rlock_en (auto-retrain on lock)"
    if [ "$_stream_has_video" -eq 0 ]; then
        info_line "Measured Timing" "$C_RED" "no video" "stream carries no video even though PHY is up"
    elif [ "$_htot_mismatch" -eq 1 ]; then
        _res="${DV_DES_HACT}x${DV_DES_VACT}"
        info_line "Measured Timing" "$C_RED" "$_res" "@ ${DV_DES_HTOT}x${DV_DES_VTOT} total — H_TOTAL mismatch vs 983 (${DV_983_HTOT})"
    else
        _res="${DV_DES_HACT}x${DV_DES_VACT}"
        info_line "Measured Timing" "$C_GRN" "$_res" "@ ${DV_DES_HTOT}x${DV_DES_VTOT} total (from FPDLink stream)"
    fi
    val_line "0x48:0x69 stream-0 flags"      "$DV_DES_P48_69"   "HACTIVE_CHNG=$_p48_hact_chg VTOTAL_CHNG=$_p48_vtot_chg"

    # ---- Verdict again (bottom) ----
    printf "\n${C_BLD}Summary:${C_RST}\n"
    case "$_verdict_tag" in
        OK)   printf "  VERDICT: ${C_GRN}[OK]${C_RST}   %s\n" "$_verdict_msg" ;;
        WARN) printf "  VERDICT: ${C_YEL}[WARN]${C_RST} %s\n" "$_verdict_msg" ;;
        FAIL) printf "  VERDICT: ${C_RED}[FAIL]${C_RST} %s\n" "$_verdict_msg" ;;
    esac
    [ -n "$_verdict_hint" ] && printf "  ${C_YEL}Hint:${C_RST} %s\n" "$_verdict_hint"
}

# ---- Entry point for --diagnose[=MODE] ------------------------------------
cmd_diagnose() {
    # Determine deserializer by --target (default: 984)
    case "$TARGET" in
        988)
            _deser_addr="$ADDR_988"
            _deser_label="DS90UH988"
            _deser_mode=1
            ;;
        *)  # default 984
            _deser_addr="$ADDR_984"
            _deser_label="DS90HH984"
            _deser_mode=0
            ;;
    esac

    _file=${DIAG_FILE:-$DIAG_BASELINE_FILE_DEFAULT}

    diag_collect "$_deser_addr"

    case "$DIAGNOSE_MODE" in
        baseline)
            diag_save_baseline "$_file" "$_deser_mode"
            ;;
        compare)
            diag_load_baseline "$_file"
            diag_print "$_deser_addr" "$_deser_label" "$_deser_mode"
            diag_compare_baseline "$_file"
            ;;
        *)
            diag_print "$_deser_addr" "$_deser_label" "$_deser_mode"
            ;;
    esac
}

# -----------------------------------------------
# Pipeline Recovery (--recover)
# -----------------------------------------------
# Fixes the two known black-display failure modes in this 983+984 pipeline:
#
#   1. Post-PATGEN: after --pattern=off on the deserializer, the internal
#      video pipeline can stay latched in the prior (PATGEN-driven) config,
#      the eDP/OLDI TX shows BIST/garbage, and incoming FPDLink video is
#      never consumed.
#
#   2. Post-HDMI-unplug/replug: during the brief DP loss, the 984 DTG
#      latches a corrupt H_TOTAL measurement (e.g., ~4378 instead of ~2070);
#      on replug FPDLink re-locks but the DTG keeps the bad measurement,
#      producing an out-of-spec eDP clock the panel/FPGA can't lock to.
#
# Cure (confirmed empirically): one soft digital reset on the deserializer
# main-page reg 0x01 bit 0 — it's self-clearing and preserves registers, so
# it re-latches the video pipeline from the currently-configured (correct)
# state without disturbing interrupt / I2C-passthrough / routing config.
cmd_recover() {
    case "$TARGET" in
        988) _deser_addr="$ADDR_988"; _deser_label="DS90UH988" ;;
        *)   _deser_addr="$ADDR_984"; _deser_label="DS90HH984" ;;
    esac
    printf "${C_BLD}=== fpdlink-tool --recover ===${C_RST}\n"
    printf "Target: %s at %s\n" "$_deser_label" "$_deser_addr"
    recover_deser_video "$_deser_addr"
    printf "\n${C_GRN}Recovery done.${C_RST} Run '%s --diagnose' to verify pipeline health.\n" "$0"
}

cmd_touch_diag() {
    # Determine deserializer based on --target (default: 988)
    case "$TARGET" in
        984)
            _deser_addr="$ADDR_984"
            _deser_label="DS90HH984"
            _deser_mode=0
            _passthru_expected="0xC9"
            ;;
        *)  # default to 988
            _deser_addr="$ADDR_988"
            _deser_label="DS90UH988"
            _deser_mode=1
            _passthru_expected="0xD9"
            ;;
    esac

    echo "=== FPDLink Touch Interrupt Diagnostic ==="
    echo "I2C Bus: $I2C_BUS  Serializer: $ADDR_983  Deserializer: $_deser_addr ($_deser_label)"
    echo "Mode: $_deser_mode (983+${_deser_label#DS90*})"
    echo ""
    echo "NOTE: Reading HDCP_ISR (0xC7) and LOCAL_INT_STS (0x45) clears"
    echo "      latched interrupt status bits (clear-on-read registers)."

    # =============================================
    # DS90UH983 Serializer
    # =============================================
    echo ""
    echo "==========================================="
    echo " DS90UH983 Serializer ($ADDR_983)"
    echo "==========================================="

    # --- Link Status ---
    echo ""
    echo "Link Status:"
    _v=$(i2c_read "$ADDR_983" 0x0C); _r=$(($_v))
    printf "  0x0C GENERAL_STS:     %-6s RX_LOCK=%d LINK_DET=%d\n" \
        "$_v" "$((_r >> 6 & 1))" "$((_r & 1))"

    # --- Interrupt Configuration ---
    echo ""
    echo "Interrupt Configuration:"

    _v=$(i2c_read "$ADDR_983" 0x2D); _r=$(($_v))
    printf "  0x2D TX_PORT_SEL:     %-6s READ_PORT=%d (per-port regs below use this port)\n" \
        "$_v" "$(( (_r >> 4) & 0x03 ))"

    _v=$(i2c_read "$ADDR_983" 0x51); _r=$(($_v))
    printf "  0x51 INTERRUPT_CTL:   %-6s INTB_PIN_EN=%d IE_DP_RX0=%d IE_FPD_TX0=%d IE_FPD_TX1=%d" \
        "$_v" "$((_r >> 7 & 1))" "$((_r >> 4 & 1))" "$((_r & 1))" "$((_r >> 1 & 1))"
    if [ "$_deser_mode" -eq 0 ]; then
        check_reg "$_v" 0x83  # Mode 0 (984): no IE_DP_RX0 to avoid INTB contention
    else
        check_reg "$_v" 0x93  # Mode 1 (988): IE_DP_RX0 enabled
    fi
    echo ""

    _v=$(i2c_read "$ADDR_983" 0x52); _r=$(($_v))
    printf "  0x52 INTERRUPT_STS:   %-6s GLOBAL=%d IS_DP_RX0=%d IS_FPD_TX0=%d IS_FPD_TX1=%d REMOTE=%d\n" \
        "$_v" "$((_r >> 7 & 1))" "$((_r >> 4 & 1))" "$((_r & 1))" "$((_r >> 1 & 1))" "$((_r >> 2 & 1))"

    _v=$(i2c_read "$ADDR_983" 0xC6); _r=$(($_v))
    printf "  0xC6 HDCP_ICR:        %-6s IE_RX_REM_INT=%d INT_EN=%d" \
        "$_v" "$((_r >> 5 & 1))" "$((_r & 1))"
    check_reg "$_v" 0x21; echo ""

    _v=$(i2c_read "$ADDR_983" 0xC7); _r=$(($_v))
    printf "  0xC7 HDCP_ISR:        %-6s IS_RX_REM_INT=%d INT=%d  (clear-on-read!)\n" \
        "$_v" "$((_r >> 5 & 1))" "$((_r & 1))"

    _v=$(i2c_read "$ADDR_983" 0xC4); _r=$(($_v))
    printf "  0xC4 HDCP_STS:        %-6s RX_INT=%d(live,1=idle) RX_LOCK_DET=%d RX_DETECT=%d\n" \
        "$_v" "$((_r >> 6 & 1))" "$((_r >> 5 & 1))" "$((_r >> 3 & 1))"

    # --- GPIO4 (REM_INTB) ---
    echo ""
    echo "GPIO4 (REM_INTB pin):"

    _v=$(i2c_read "$ADDR_983" 0x1B); _r=$(($_v))
    _g4_sel=$((_r & 0x0F))
    case $_g4_sel in
        0) _fn="LOW" ;; 1) _fn="HIGH" ;; 2) _fn="RemoteGPIO" ;; 8) _fn="RX_INTN(REM_INTB)" ;; *) _fn="sel=$_g4_sel" ;;
    esac
    printf "  0x1B GPIO4_PIN_CTL:   %-6s OUT_EN=%d SRC=Port%d SEL=%s" \
        "$_v" "$((_r >> 7 & 1))" "$(( (_r >> 4) & 0x07 ))" "$_fn"
    check_reg "$_v" 0x88; echo ""

    _v=$(i2c_read "$ADDR_983" 0x25); _r=$(($_v))
    printf "  0x25 GPI_PIN_STS1:    %-6s GPIO4_pin=%d (0=INT asserted, 1=idle)\n" \
        "$_v" "$(( (_r >> 4) & 1 ))"

    # --- I2C Passthrough ---
    echo ""
    echo "I2C Passthrough:"

    _v=$(i2c_read "$ADDR_983" 0x07); _r=$(($_v))
    printf "  0x07 GENERAL_CFG:     %-6s I2C_PASS_ALL=%d I2C_PASS_THROUGH=%d" \
        "$_v" "$((_r >> 4 & 1))" "$((_r >> 3 & 1))"
    check_reg "$_v" 0xD8; echo ""

    # --- I2C Routing (Touch Controller) ---
    echo ""
    if [ "$_deser_mode" -eq 1 ]; then
        echo "I2C Routing (TDDI Touch 0x48/0x49 — configured by driver for mode 1):"
    else
        echo "I2C Routing (raw TARGET_ID/ALIAS/DEST — not configured by driver for mode 0):"
    fi

    _tid0=$(i2c_read "$ADDR_983" 0x70)
    _tid1=$(i2c_read "$ADDR_983" 0x71)
    _ta0=$(i2c_read "$ADDR_983" 0x78)
    _ta1=$(i2c_read "$ADDR_983" 0x79)
    _td0=$(i2c_read "$ADDR_983" 0x88)
    _td1=$(i2c_read "$ADDR_983" 0x89)

    printf "  0x70 TARGET_ID0:      %-6s phys=0x%02x" "$_tid0" "$(( ($(($_tid0)) >> 1) & 0x7F ))"
    [ "$_deser_mode" -eq 1 ] && check_reg "$_tid0" 0x90; echo ""
    printf "  0x71 TARGET_ID1:      %-6s phys=0x%02x" "$_tid1" "$(( ($(($_tid1)) >> 1) & 0x7F ))"
    [ "$_deser_mode" -eq 1 ] && check_reg "$_tid1" 0x92; echo ""
    printf "  0x78 TARGET_ALIAS0:   %-6s alias=0x%02x" "$_ta0" "$(( ($(($_ta0)) >> 1) & 0x7F ))"
    [ "$_deser_mode" -eq 1 ] && check_reg "$_ta0" 0x90; echo ""
    printf "  0x79 TARGET_ALIAS1:   %-6s alias=0x%02x" "$_ta1" "$(( ($(($_ta1)) >> 1) & 0x7F ))"
    [ "$_deser_mode" -eq 1 ] && check_reg "$_ta1" 0x92; echo ""
    printf "  0x88 TARGET_DEST0:    %-6s dest_port=%d" "$_td0" "$(( ($(($_td0)) >> 5) & 0x07 ))"
    [ "$_deser_mode" -eq 1 ] && check_reg "$_td0" 0x20; echo ""
    printf "  0x89 TARGET_DEST1:    %-6s dest_port=%d" "$_td1" "$(( ($(($_td1)) >> 5) & 0x07 ))"
    [ "$_deser_mode" -eq 1 ] && check_reg "$_td1" 0x20; echo ""

    # --- Back Channel ---
    echo ""
    echo "Back Channel:"

    _v=$(i2c_read "$ADDR_983" 0x28); _r=$(($_v))
    _bc_en=$((_r & 0x03))
    case $_bc_en in
        0) _bc="4 GPIOs" ;; 1) _bc="8 GPIOs" ;; 2) _bc="16 GPIOs" ;; 3) _bc="HS single" ;;
    esac
    printf "  0x28 GPIO_EN_BC:      %-6s BC_GPIO_slots=%s\n" "$_v" "$_bc"

    _v=$(i2c_read "$ADDR_983" 0x47); _r=$(($_v))
    printf "  0x47 BCC_CONFIG:      %-6s I2C_CTLR_DIS=%d ENH_ERROR=%d\n" \
        "$_v" "$((_r >> 5 & 1))" "$((_r & 1))"

    # --- GPIO Interrupt (alternative path, driver doesn't use) ---
    echo ""
    echo "GPIO Interrupt Path (alternative, driver uses HDCP_ICR instead):"
    _v=$(i2c_read "$ADDR_983" 0xA4)
    printf "  0xA4 GPIO_INT_CTL0:   %-6s (BC GPIO[7:0] INT enables)\n" "$_v"
    _v=$(i2c_read "$ADDR_983" 0xA5)
    printf "  0xA5 GPIO_INT_CTL1:   %-6s (BC GPIO[13:8] INT enables)\n" "$_v"

    # =============================================
    # Deserializer (988 or 984)
    # =============================================
    echo ""
    echo "==========================================="
    echo " $_deser_label Deserializer ($_deser_addr)"
    echo "==========================================="

    # --- Link Status ---
    echo ""
    echo "Link Status:"
    _s0=$(i2c_read "$_deser_addr" 0x53); _r0=$(($_s0))
    _s1=$(i2c_read "$_deser_addr" 0x54); _r1=$(($_s1))
    printf "  0x53 GP_STATUS_0:     %-6s FPD4_LOCK=%d FPD3_LOCK=%d FPDTX_PLL=%d\n" \
        "$_s0" "$((_r0 & 1))" "$((_r0 >> 1 & 1))" "$((_r0 >> 2 & 1))"
    if [ "$_deser_mode" -eq 1 ]; then
        printf "  0x54 GP_STATUS_1:     %-6s LOCK=%d SIG_DET=%d PLL_LOCK=%d\n" \
            "$_s1" "$((_r1 & 1))" "$((_r1 >> 1 & 1))" "$((_r1 >> 6 & 1))"
    else
        printf "  0x54 GP_STATUS_1:     %-6s LOCK=%d PLL_LOCK=%d  (no SIG_DET on 984)\n" \
            "$_s1" "$((_r1 & 1))" "$((_r1 >> 6 & 1))"
    fi

    # --- Interrupt Configuration ---
    echo ""
    echo "Interrupt Configuration:"

    _v=$(i2c_read "$_deser_addr" 0x44); _r=$(($_v))
    printf "  0x44 RX_INT_CTL:      %-6s IE_EN=%d IE_INTB_P0=%d" \
        "$_v" "$((_r >> 7 & 1))" "$((_r & 1))"
    check_reg "$_v" 0x81; echo ""

    _v=$(i2c_read "$_deser_addr" 0x43); _r=$(($_v))
    printf "  0x43 RX_INT_STS:      %-6s RX_INT=%d IS_DOWNSTRM_INTB=%d\n" \
        "$_v" "$((_r >> 7 & 1))" "$((_r & 1))"

    _v=$(i2c_read "$_deser_addr" 0x45); _r=$(($_v))
    printf "  0x45 LOCAL_INT_STS:   %-6s IS_LCL_INTB=%d  (clear-on-read!)\n" \
        "$_v" "$((_r & 1))"

    _v=$(i2c_read "$_deser_addr" 0x51); _r=$(($_v))
    printf "  0x51 INTERRUPT_STS:   %-6s GLOBAL_INT=%d IS_LOCAL_INT=%d\n" \
        "$_v" "$((_r >> 7 & 1))" "$((_r & 1))"

    _v=$(i2c_read "$_deser_addr" 0x52); _r=$(($_v))
    printf "  0x52 INTERRUPT_CTL:   %-6s IE_GLOBAL=%d IE_INTB_IN=%d" \
        "$_v" "$((_r >> 7 & 1))" "$((_r & 1))"
    check_reg "$_v" 0xFF; echo ""

    # --- I2C Passthrough ---
    echo ""
    echo "I2C Passthrough:"

    _v=$(i2c_read "$_deser_addr" 0x04); _r=$(($_v))
    printf "  0x04 GENERAL_CFG:     %-6s I2C_PASS_ALL=%d I2C_PASS_THROUGH=%d" \
        "$_v" "$((_r >> 4 & 1))" "$((_r >> 3 & 1))"
    check_reg "$_v" "$_passthru_expected"; echo ""

    # --- I2C Timing ---
    echo ""
    echo "I2C Timing:"

    _v=$(i2c_read "$_deser_addr" 0x02); _r=$(($_v))
    _arb_to=$(( (_r >> 5) & 0x03 ))
    case $_arb_to in
        0) _arb="80us" ;; 1) _arb="160us" ;; 2) _arb="2.4ms" ;; 3) _arb="10ms" ;;
    esac
    printf "  0x02 I2C_CTLR_CFG:   %-6s ARB_TIMEOUT=%s BUS_TIMER_DIS=%d\n" \
        "$_v" "$_arb" "$((_r & 1))"

    _v=$(i2c_read "$_deser_addr" 0x2A); _r=$(($_v))
    printf "  0x2A I2C_CONTROL:     %-6s SDA_HOLD=%d FILTER_DEPTH=%d\n" \
        "$_v" "$(( (_r >> 4) & 0x07 ))" "$((_r & 0x0F))"

    # --- BCC Status ---
    echo ""
    echo "BCC Status (clear-on-read error flags):"

    _v=$(i2c_read "$_deser_addr" 0x08); _r=$(($_v))
    printf "  0x08 TX_BCC_STATUS:   %-6s" "$_v"
    if [ "$_r" -eq 0 ]; then printf " (no errors)"; else
        [ $((_r >> 5 & 1)) -eq 1 ] && printf " I2C_DIS"
        [ $((_r >> 4 & 1)) -eq 1 ] && printf " DATA_ERR"
        [ $((_r >> 3 & 1)) -eq 1 ] && printf " SEQ_ERR"
        [ $((_r >> 1 & 1)) -eq 1 ] && printf " CTRL_ERR"
        [ $((_r & 1)) -eq 1 ] && printf " TIMEOUT"
    fi; echo ""

    _v=$(i2c_read "$_deser_addr" 0x09); _r=$(($_v))
    printf "  0x09 RX_BCC_STATUS:   %-6s" "$_v"
    if [ "$_r" -eq 0 ]; then printf " (no errors)"; else
        [ $((_r >> 6 & 1)) -eq 1 ] && printf " CRC_ERR"
        [ $((_r >> 5 & 1)) -eq 1 ] && printf " I2C_DIS"
        [ $((_r >> 4 & 1)) -eq 1 ] && printf " DATA_ERR"
        [ $((_r >> 3 & 1)) -eq 1 ] && printf " SEQ_ERR"
        [ $((_r >> 1 & 1)) -eq 1 ] && printf " CTRL_ERR"
        [ $((_r & 1)) -eq 1 ] && printf " TIMEOUT"
    fi; echo ""

    _v=$(i2c_read "$_deser_addr" 0x0A); _r=$(($_v))
    printf "  0x0A TRGT_BCC_STS:    %-6s" "$_v"
    if [ "$_r" -eq 0 ]; then printf " (no errors)"; else
        [ $((_r >> 2 & 1)) -eq 1 ] && printf " BCC_ERR"
        [ $((_r >> 1 & 1)) -eq 1 ] && printf " TIMEOUT"
        [ $((_r & 1)) -eq 1 ] && printf " DATA_ERR"
    fi; echo ""

    _v=$(i2c_read "$_deser_addr" 0x0B); _r=$(($_v))
    printf "  0x0B BCC_CONFIG:      %-6s RX_ENH_ERROR=%d TERM_ON_ERR=%d\n" \
        "$_v" "$((_r >> 1 & 1))" "$((_r & 1))"

    _v=$(i2c_read "$_deser_addr" 0x29); _r=$(($_v))
    _wdog_ms=$(( ((_r >> 1) & 0x7F) * 2 ))
    printf "  0x29 BCC_WDOG_CTL:    %-6s timeout=%dms disabled=%d\n" \
        "$_v" "$_wdog_ms" "$((_r & 1))"

    # --- GPIO Configuration ---
    echo ""
    echo "GPIO Configuration (display driver board signals):"

    _v=$(i2c_read "$_deser_addr" 0x19); _r=$(($_v))
    printf "  0x19 GPIO4_PIN_CTL:   %-6s OUT_EN=%d SRC=%d SEL=0x%02x" \
        "$_v" "$((_r >> 7 & 1))" "$(( (_r >> 5) & 3 ))" "$((_r & 0x1F))"
    check_reg "$_v" 0x9C; echo ""

    _v=$(i2c_read "$_deser_addr" 0x1A)
    printf "  0x1A GPIO5_PIN_CTL:   %-6s\n" "$_v"

    _v=$(i2c_read "$_deser_addr" 0x1B); _r=$(($_v))
    printf "  0x1B GPIO6_PIN_CTL:   %-6s OUT_EN=%d SRC=%d SEL=0x%02x" \
        "$_v" "$((_r >> 7 & 1))" "$(( (_r >> 5) & 3 ))" "$((_r & 0x1F))"
    check_reg "$_v" 0xC2; echo ""

    echo ""
    echo "GPIO Pin Status:"
    _v=$(i2c_read "$_deser_addr" 0x10)
    printf "  0x10 GPI_PIN_STS1:    %-6s GPIO[7:0] pin levels\n" "$_v"
    _v=$(i2c_read "$_deser_addr" 0x11)
    printf "  0x11 GPI_PIN_STS2:    %-6s GPIO[13:8] pin levels\n" "$_v"
    _v=$(i2c_read "$_deser_addr" 0x13)
    printf "  0x13 GPIO_IN_EN0:     %-6s GPIO[7:0] input enables\n" "$_v"
    _v=$(i2c_read "$_deser_addr" 0x12)
    printf "  0x12 GPIO_IN_EN1:     %-6s GPIO[13:8] input enables\n" "$_v"

    # --- IO Control ---
    echo ""
    echo "IO Control:"
    _v=$(i2c_read "$_deser_addr" 0x2F); _r=$(($_v))
    printf "  0x2F IO_CTL:          %-6s INTB_3V3=%d I2C_3V3=%d\n" \
        "$_v" "$((_r >> 5 & 1))" "$((_r >> 7 & 1))"

    # =============================================
    # Summary
    # =============================================
    echo ""
    echo "==========================================="
    echo " Diagnostic Hints"
    echo "==========================================="
    echo ""
    echo "Any [!!] markers above show registers that differ from expected"
    echo "boot-time values set by the hh983-serializer driver."
    echo ""
    echo "Touch interrupt signal path:"
    echo "  Touch controller -> $_deser_label INTB_IN pin"
    echo "    -> $_deser_label RX_INT_CTL(0x44) forwards over BCC backchannel"
    echo "    -> 983 HDCP_ICR(0xC6) IE_RX_REM_INT cascades to INTERRUPT_STS"
    echo "    -> 983 INTERRUPT_CTL(0x51) INTB_PIN_EN drives INTB pin to host"
    echo "    -> 983 GPIO4(0x1B) mirrors REM_INTB to GPIO4 pin"
    echo ""
    echo "I2C touch data path:"
    echo "  Host -> 983 GENERAL_CFG(0x07) I2C passthrough"
    if [ "$_deser_mode" -eq 1 ]; then
        echo "    -> 983 TARGET_ID/ALIAS(0x70-0x79) routes 0x48/0x49 to Port 1"
    fi
    echo "    -> BCC backchannel -> $_deser_label GENERAL_CFG(0x04) passthrough"
    echo "    -> Touch controller on $_deser_label local I2C bus"
    echo ""
    echo "To find root cause: run --touch-diag after fresh boot (touch OK)"
    echo "and again after HDMI switch + re-init (touch sluggish)."
    echo "Diff the two outputs to find which register(s) changed."
}

# -----------------------------------------------
# Dispatch
# -----------------------------------------------
# Handle recover first (independent of --target; defaults to 984)
if [ "$RECOVER" -eq 1 ]; then
    cmd_recover
    if [ "$DIAGNOSE" -eq 0 ] && [ "$TOUCH_DIAG" -eq 0 ] && [ "$TIMINGS" -eq 0 ] && [ "$PATTERN_SET" -eq 0 ]; then
        exit 0
    fi
fi

# Handle diagnose (doesn't require --target, defaults to 984)
if [ "$DIAGNOSE" -eq 1 ]; then
    cmd_diagnose
    # --diagnose is a terminal action; only chain if user also asked for something else
    if [ "$TOUCH_DIAG" -eq 0 ] && [ "$TIMINGS" -eq 0 ] && [ "$PATTERN_SET" -eq 0 ]; then
        exit 0
    fi
fi

# Handle touch-diag (doesn't require --target)
if [ "$TOUCH_DIAG" -eq 1 ]; then
    cmd_touch_diag
    # If no other actions requested, exit
    if [ -z "$TARGET" ]; then
        exit 0
    fi
fi

case "$TARGET" in
    "") exit 0 ;;  # touch-diag only, already handled above
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
