#!/bin/bash
# R5.3: warm-reboot into a wedged DTG with the guard's poll stopped, then let the
# guard loose and watch what it does.  $1 = wedge_recovery value to test.
export PATH=/usr/sbin:/sbin:$PATH
WR=$1
B=/home/pi/micropanel/usr/bin
M=/home/pi/micropanel/share/disptool/display-test-framework/measure-display.sh
T=/home/pi/fpdlink-tool-torn-fix.sh
meas() { timeout 60 $M --sensor-only=yes --quiet=yes | grep ',SENSOR,' | tail -1 | awk -F, -v l="$1" '{printf "  %-20s Y=%9.3f x=%.4f y=%.4f\n", l, $6, $9, $10}'; }
setp() { $B/launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg=$1 >/dev/null; sleep 3; }
htot() { NO_COLOR=1 $T --target=984 --timings 2>/dev/null | awk '/^  H Total/{print $3; exit}'; }

echo "=== injected wedge, wedge_recovery=$WR, $(date +%T)"
echo "poll_interval_ms=$(cat /sys/module/hh983_serializer/parameters/poll_interval_ms) (expect 0)"
echo "wedge_recovery=$(cat /sys/module/hh983_serializer/parameters/wedge_recovery)"
echo "$WR" | sudo tee /sys/module/hh983_serializer/parameters/wedge_recovery >/dev/null
echo "wedge_recovery now=$(cat /sys/module/hh983_serializer/parameters/wedge_recovery)"
echo "--- DTG with the guard asleep ---"
for i in 1 2 3; do echo "  H total = $(htot)"; done
echo "--- boot log (guard should have done nothing) ---"
dmesg | grep -i hh983 | tail -4
echo "--- panel before the guard runs ---"
$B/launcher-client --command=start-app --command-arg=pattern-generator >/dev/null; sleep 5
for p in white red; do setp $p; meas "before:$p"; done
echo "--- releasing the guard: poll_interval_ms=1000 at $(date +%T) ---"
dmesg -C >/dev/null 2>&1 || sudo dmesg -C
echo 1000 | sudo tee /sys/module/hh983_serializer/parameters/poll_interval_ms >/dev/null
sleep 6
echo "--- what the guard did (first 6 s) ---"
dmesg | grep -i hh983
echo "  wedges=$(cat /sys/module/hh983_serializer/parameters/dtg_wedge_count)"
echo "  H total after = $(htot)"
echo "--- panel after the guard ran ---"
for p in white red green blue white; do setp $p; meas "after:$p"; done
echo "=== end $(date +%T)"
