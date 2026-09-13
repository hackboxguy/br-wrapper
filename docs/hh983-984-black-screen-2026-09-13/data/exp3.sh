#!/bin/bash
export PATH=/usr/sbin:/sbin:$PATH
M=/home/pi/micropanel/share/disptool/display-test-framework/measure-display.sh
P=/sys/module/hh983_serializer/parameters
LC=/home/pi/micropanel/usr/bin/launcher-client
D=0x2c
indw() { i2cset -f -y 1 $D 0x40 $1; i2cset -f -y 1 $D 0x41 $2; i2cset -f -y 1 $D 0x42 $3; }
apbw32() { i2cset -f -y 1 $D 0x48 0x01; i2cset -f -y 1 $D 0x49 $(printf 0x%02x $(($1&0xff))); i2cset -f -y 1 $D 0x4a $(printf 0x%02x $((($1>>8)&0xff))); i2cset -f -y 1 $D 0x4b $2; i2cset -f -y 1 $D 0x4c 0; i2cset -f -y 1 $D 0x4d 0; i2cset -f -y 1 $D 0x4e 0; }
meas() { timeout 60 $M --sensor-only=yes 2>&1 | tail -1 | awk -F, '{printf "%s Y=%.1f", $2, $5}'; }
samples() { local s=""; for i in 1 2 3 4 5; do s="$s | $(meas)"; done; echo "$s"; }
# classify: steady ~1000+ on all 5 = PI-WHITE, else BIST/other
verdict() { local n=$(echo "$1" | grep -o "Y=[0-9.]*" | awk -F= '$2>900{c++} END{print c+0}'); [ "$n" -eq 5 ] && echo "STEADY-WHITE" || echo "NOT-STEADY(BIST?)"; }
reset984() { i2cset -f -y 1 $D 0x01 0x01; sleep 4; }
echo "=== EXP3 start $(date +%T) poll=$(cat $P/poll_interval_ms) tol=$(cat $P/dtg_tolerance) wedges=$(cat $P/dtg_wedge_count) app=$($LC --command=get-running-app)"
$LC --srv=127.0.0.1:8082 --command=pattern --command-arg=white >/dev/null; sleep 1
S=$(samples); echo "pre-reset      : $S -> $(verdict "$S")"
reset984; S=$(samples); echo "after 984 reset: $S -> $(verdict "$S")"
for k in 1 2 3; do
  apbw32 0x084 0x00; sleep 0.7; apbw32 0x084 0x01; sleep 1
  S=$(samples); V=$(verdict "$S"); echo "A#$k cut/enable  : $S -> $V"
  [ "$V" != "STEADY-WHITE" ] && { reset984; S=$(samples); echo "   re-reset     : $S -> $(verdict "$S")"; }
done
for k in 1 2 3 4 5; do
  apbw32 0x084 0x00; indw 0x50 0x32 0x06; indw 0x50 0x62 0x06; sleep 0.2; indw 0x50 0x32 0x04; indw 0x50 0x62 0x04; sleep 0.5; apbw32 0x084 0x01; sleep 1
  S=$(samples); V=$(verdict "$S"); echo "B#$k full restore: $S -> $V"
  [ "$V" != "STEADY-WHITE" ] && { reset984; S=$(samples); echo "   re-reset     : $S -> $(verdict "$S")"; }
done
echo "--- final: stop pattern app, back to desktop"
$LC --command=stop-app; sleep 3; echo "app=$($LC --command=get-running-app) desktop: $(meas)"
echo "=== EXP3 end $(date +%T) wedges=$(cat $P/dtg_wedge_count) tol=$(cat $P/dtg_tolerance)"
dmesg -T | grep hh983 | tail -3
