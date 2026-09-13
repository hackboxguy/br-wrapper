#!/bin/bash
export PATH=/usr/sbin:/sbin:$PATH
M=/home/pi/micropanel/share/disptool/display-test-framework/measure-display.sh
T=/home/pi/micropanel/bin/fpdlink-tool.sh
D=0x2c
apbw() { i2cset -f -y 1 $D 0x49 $(printf 0x%02x $(($1&0xff))); i2cset -f -y 1 $D 0x4a $(printf 0x%02x $((($1>>8)&0xff))); i2cset -f -y 1 $D 0x4b $2; i2cset -f -y 1 $D 0x4c 0; i2cset -f -y 1 $D 0x4d 0; i2cset -f -y 1 $D 0x4e 0; }
apbr() { i2cset -f -y 1 $D 0x49 $(printf 0x%02x $(($1&0xff))); i2cset -f -y 1 $D 0x4a $(printf 0x%02x $((($1>>8)&0xff))); i2cset -f -y 1 $D 0x48 0x03; sleep 0.002; i2cget -f -y 1 $D 0x4b; }
indw() { i2cset -f -y 1 $D 0x40 $1; i2cset -f -y 1 $D 0x41 $2; i2cset -f -y 1 $D 0x42 $3; }
meas() { timeout 90 $M --sensor-only=yes 2>&1 | tail -1 | awk -F, -v l="$1" '{printf "MEASURE %-28s Y=%s nits (x=%s y=%s) at %s\n", l, $5, $9, $10, $2}'; }
sts() { echo "STS $1: 984 0x53=$(i2cget -f -y 1 $D 0x53) 0x54=$(i2cget -f -y 1 $D 0x54) 0x51=$(i2cget -f -y 1 $D 0x51) APB084=$(apbr 0x084) 983 0x0C=$(i2cget -f -y 1 0x18 0x0c) poll=$(cat /sys/module/hh983_serializer/parameters/poll_interval_ms)"; }
# note: APB 0x084 write needs enable ctl: driver writes 0x48=0x01 first then addr/data. replicate driver order.
apbw32() { i2cset -f -y 1 $D 0x48 0x01; i2cset -f -y 1 $D 0x49 $(printf 0x%02x $(($1&0xff))); i2cset -f -y 1 $D 0x4a $(printf 0x%02x $((($1>>8)&0xff))); i2cset -f -y 1 $D 0x4b $2; i2cset -f -y 1 $D 0x4c 0; i2cset -f -y 1 $D 0x4d 0; i2cset -f -y 1 $D 0x4e 0; }
echo "=== EXPERIMENT start $(date +%T)"
sts baseline
meas "baseline (after sync-video)"
echo "--- STEP A: stream cut 0.7s then enable, no DTG pulse  $(date +%T)"
apbw32 0x084 0x00; sleep 0.7; apbw32 0x084 0x01; sleep 0.05; echo "APB084 readback=$(apbr 0x084)"; sleep 2
sts after-A
meas "after A (cut/enable only)"
echo "--- STEP B: driver restore replay: cut, DTG P0/P1=0x06, 200ms, 0x04, 500ms, enable  $(date +%T)"
apbw32 0x084 0x00
indw 0x50 0x32 0x06; indw 0x50 0x62 0x06; sleep 0.2; indw 0x50 0x32 0x04; indw 0x50 0x62 0x04; sleep 0.5
apbw32 0x084 0x01; sleep 0.05; echo "APB084 readback=$(apbr 0x084)"; sleep 2
sts after-B
meas "after B (restore w/ DTG pulse)"
NO_COLOR=1 $T --target=984 --timings 2>&1 | grep -A3 "Measured Timing" 
echo "--- STEP C: sync-video (984 digital reset 0x01=0x01)  $(date +%T)"
i2cset -f -y 1 $D 0x01 0x01; sleep 3
sts after-C
meas "after C (digital reset)"
echo "=== EXPERIMENT end $(date +%T)"
