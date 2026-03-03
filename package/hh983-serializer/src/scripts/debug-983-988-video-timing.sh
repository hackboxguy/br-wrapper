#!/bin/sh
#
# Debug script: Read video timing from DS90UH983 (serializer) and DS90UH988 (deserializer)
#
# Reads the actual DP input timing received by the 983 from the Pi4,
# and the DTG/output timing configured on the 988.
#
# Usage: sudo ./debug-video-timing.sh [i2c_bus]
#

I2C_BUS=${1:-1}
SER=0x18
DES=0x2c

ser_write() { i2cset -f -y $I2C_BUS $SER "$1" "$2"; }
ser_read()  { i2cget -f -y $I2C_BUS $SER "$1"; }
des_write() { i2cset -f -y $I2C_BUS $DES "$1" "$2"; }
des_read()  { i2cget -f -y $I2C_BUS $DES "$1"; }

# Read 16-bit value from SER indirect register (low byte at offset, high byte at offset+1)
ser_ind_read16() {
    _page=$1
    _offset=$2
    _next=$(printf "0x%02x" $((_offset + 1)))
    ser_write 0x40 "$_page"
    ser_write 0x41 "$_offset"
    _lo=$(ser_read 0x42)
    ser_write 0x41 "$_next"
    _hi=$(ser_read 0x42)
    echo $(( (_hi << 8) | _lo ))
}

# Read 8-bit value from SER indirect register
ser_ind_read8() {
    _page=$1
    _offset=$2
    ser_write 0x40 "$_page"
    ser_write 0x41 "$_offset"
    ser_read 0x42
}

# Read 16-bit from DES indirect register
des_ind_read16() {
    _page=$1
    _offset=$2
    _next=$(printf "0x%02x" $((_offset + 1)))
    des_write 0x40 "$_page"
    des_write 0x41 "$_offset"
    _lo=$(des_read 0x42)
    des_write 0x41 "$_next"
    _hi=$(des_read 0x42)
    echo $(( (_hi << 8) | _lo ))
}

des_ind_read8() {
    _page=$1
    _offset=$2
    des_write 0x40 "$_page"
    des_write 0x41 "$_offset"
    des_read 0x42
}

echo "=============================================="
echo " DS90UH983 + DS90UH988 Video Timing Debug"
echo " I2C Bus: $I2C_BUS"
echo "=============================================="
echo ""

# -----------------------------------------------
# 983 Serializer: DP Input Status
# -----------------------------------------------
echo "=== DS90UH983 Serializer ==="
echo ""

echo "--- General Link Status ---"
sts=$(ser_read 0x0C)
echo "  GENERAL_STS (0x0C): $sts"
val=$((sts))
if [ $((val & 0x40)) -ne 0 ]; then echo "    [6] RX_LOCK: YES"; else echo "    [6] RX_LOCK: NO"; fi
if [ $((val & 0x10)) -ne 0 ]; then echo "    [4] LINK_LOST: YES"; else echo "    [4] LINK_LOST: NO"; fi
if [ $((val & 0x01)) -ne 0 ]; then echo "    [0] LINK_DET: YES"; else echo "    [0] LINK_DET: NO"; fi
echo ""

echo "--- DP Link Status ---"
echo "  DP_STS  (0x54): $(ser_read 0x54)"
echo "  DP_STS2 (0x55): $(ser_read 0x55)"
echo ""

echo "--- VP0 Status (Indirect Page 0x31) ---"
vp_sts=$(ser_ind_read8 0x31 0x28)
echo "  VP0_STATUS  (0x31:0x28): $vp_sts"
vp_sts2=$(ser_ind_read8 0x31 0x30)
echo "  VP0_STATUS2 (0x31:0x30): $vp_sts2"
echo ""

echo "--- 983 VP0 Video Timing (Indirect Page 0x32) ---"
echo "  (Timing values programmed/detected by the Video Processor)"
h_active=$(ser_ind_read16 0x32 0x10)
h_bp=$(ser_ind_read16 0x32 0x12)
h_sync=$(ser_ind_read16 0x32 0x14)
h_total=$(ser_ind_read16 0x32 0x16)
v_active=$(ser_ind_read16 0x32 0x18)
v_bp=$(ser_ind_read16 0x32 0x1a)
v_sync=$(ser_ind_read16 0x32 0x1c)
v_fp=$(ser_ind_read16 0x32 0x1e)

echo "  H Active:      $h_active"
echo "  H Back Porch:  $h_bp"
echo "  H Sync:        $h_sync"
echo "  H Total:       $h_total"
echo "  V Active:      $v_active"
echo "  V Back Porch:  $v_bp"
echo "  V Sync:        $v_sync"
echo "  V Front Porch: $v_fp"

sync_pol=$(ser_ind_read8 0x32 0x27)
echo "  Sync Polarity (0x32:0x27): $sync_pol"

# Calculate derived values
v_total=$((v_active + v_bp + v_sync + v_fp))
h_fp=$((h_total - h_active - h_bp - h_sync))
echo ""
echo "  --- Derived ---"
echo "  V Total:       $v_total  (active + bp + sync + fp)"
echo "  H Front Porch: $h_fp  (total - active - bp - sync)"
if [ "$h_total" -gt 0 ] && [ "$v_total" -gt 0 ]; then
    echo "  Resolution:    ${h_active}x${v_active}"
    echo "  H/V Total:     ${h_total}x${v_total}"
fi
echo ""

echo "--- DP APB Status ---"
# Read APB registers for DP config
# LINK_ENABLE (HPD)
ser_write 0x49 0x00
ser_write 0x4a 0x00
ser_write 0x48 0x03
hpd=$(ser_read 0x4b)
echo "  APB 0x000 LINK_ENABLE (HPD): $hpd"

# MAX_LINK_RATE
ser_write 0x49 0x74
ser_write 0x4a 0x00
ser_write 0x48 0x03
link_rate=$(ser_read 0x4b)
lr_val=$((link_rate))
case $lr_val in
    6)  lr_desc=" (1.62 Gbps / RBR)" ;;
    10) lr_desc=" (2.7 Gbps / HBR)" ;;
    20) lr_desc=" (5.4 Gbps / HBR2)" ;;
    30) lr_desc=" (8.1 Gbps / HBR3)" ;;
    *)  lr_desc="" ;;
esac
echo "  APB 0x074 MAX_LINK_RATE: $link_rate$lr_desc"

# MAX_LANE_COUNT
ser_write 0x49 0x70
ser_write 0x4a 0x00
ser_write 0x48 0x03
lane_count=$(ser_read 0x4b)
echo "  APB 0x070 MAX_LANE_COUNT: $lane_count"
echo ""

# -----------------------------------------------
# 988 Deserializer: Output Status
# -----------------------------------------------
echo "=== DS90UH988 Deserializer ==="
echo ""

echo "--- FPDLink Lock Status ---"
sts0=$(des_read 0x53)
sts1=$(des_read 0x54)
echo "  GP_STATUS_0 (0x53): $sts0"
s0=$((sts0))
if [ $((s0 & 0x01)) -ne 0 ]; then echo "    [0] FPD4_LOCK: YES"; else echo "    [0] FPD4_LOCK: NO"; fi
if [ $((s0 & 0x04)) -ne 0 ]; then echo "    [2] FPDTX_PLL_LOCK: YES"; else echo "    [2] FPDTX_PLL_LOCK: NO"; fi
echo "  GP_STATUS_1 (0x54): $sts1"
s1=$((sts1))
if [ $((s1 & 0x01)) -ne 0 ]; then echo "    [0] LOCK: YES"; else echo "    [0] LOCK: NO"; fi
if [ $((s1 & 0x02)) -ne 0 ]; then echo "    [1] SIG_DET: YES"; else echo "    [1] SIG_DET: NO"; fi
if [ $((s1 & 0x40)) -ne 0 ]; then echo "    [6] FPD_PLL_LOCK: YES"; else echo "    [6] FPD_PLL_LOCK: NO"; fi
echo ""

echo "--- 988 DTG Status (Indirect Page 0x50) ---"
echo "  (Display Timing Generator - what the 988 outputs to the panel)"
dtg_cfg=$(des_ind_read8 0x50 0x20)
echo "  DTG_CONFIG (0x50:0x20): $dtg_cfg"
dtg_sts=$(des_ind_read8 0x50 0x32)
echo "  DTG0_STS   (0x50:0x32): $dtg_sts"
dtg1_sts=$(des_ind_read8 0x50 0x62)
echo "  DTG1_STS   (0x50:0x62): $dtg1_sts"
echo ""

echo "--- 988 Video Mapping ---"
des_write 0x0e 0x01  # Select Port 0
echo "  FPD4_VIDEO_MAP (0xD0): $(des_read 0xD0)"
echo "  FPD4_STRM_FWD  (0xD1): $(des_read 0xD1)"
echo "  FPD4_STRM_MAP0 (0xD6): $(des_read 0xD6)"
echo "  FPD3_VIDEO_MAP (0xD7): $(des_read 0xD7)"
echo ""

echo "--- 988 OLDI/RGB Config (Indirect Page 0x2C) ---"
oldi0=$(des_ind_read8 0x2c 0x00)
oldi1=$(des_ind_read8 0x2c 0x01)
oldi_en=$(des_ind_read8 0x2c 0x02)
echo "  OLDI_CFG0 (0x2C:0x00): $oldi0"
echo "  OLDI_CFG1 (0x2C:0x01): $oldi1"
echo "  OLDI_EN   (0x2C:0x02): $oldi_en"
echo ""

echo "=============================================="
echo "Done."
echo "=============================================="
