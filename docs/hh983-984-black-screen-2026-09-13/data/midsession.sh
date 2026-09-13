#!/bin/bash
# Mid-session wedge recovery, both wedge_recovery settings, panel LIVE.
#
# The injected boot-time wedge cannot tell the two paths apart: there the 984
# main stream is already cut, so the DTG pulse lands on a panel that is not
# being fed anyway.  The case that hurt this panel is a recovery that interrupts
# a running stream.  Force exactly that by dropping dtg_tolerance to 1 while the
# panel is happily displaying: the healthy 2813..2815 against a programmed 2816
# is then "out of tolerance", consistently on one side, so the guard confirms a
# wedge and runs its recovery with the stream up.
export PATH=/usr/sbin:/sbin:$PATH
B=/home/pi/micropanel/usr/bin
M=/home/pi/micropanel/share/disptool/display-test-framework/measure-display.sh
T=/home/pi/fpdlink-tool-torn-fix.sh
P=/sys/module/hh983_serializer/parameters
meas() { timeout 60 $M --sensor-only=yes --quiet=yes | grep ',SENSOR,' | tail -1 | awk -F, -v l="$1" '{printf "  %-24s Y=%9.3f x=%.4f y=%.4f\n", l, $6, $9, $10}'; }
setp() { $B/launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg=$1 >/dev/null; sleep 3; }

trial() {
  WR=$1
  echo ""
  echo "############ mid-session recovery with wedge_recovery=$WR  $(date +%T)"
  echo "$WR" | sudo tee $P/wedge_recovery >/dev/null
  echo 32 | sudo tee $P/dtg_tolerance >/dev/null
  setp white; meas "before (white)"
  sudo dmesg -C >/dev/null
  echo "--- forcing a wedge verdict on a live stream: dtg_tolerance=0 ---"
  echo 0 | sudo tee $P/dtg_tolerance >/dev/null
  sleep 14
  echo 32 | sudo tee $P/dtg_tolerance >/dev/null
  echo "--- what the guard did ---"
  dmesg | grep -i hh983 | sed 's/^/  /'
  echo "  wedges=$(cat $P/dtg_wedge_count)  H total now = $(NO_COLOR=1 $T --target=984 --timings 2>/dev/null | awk '/^  H Total/{print $3; exit}')"
  echo "--- is the Pi picture still arriving? ---"
  meas "after (white held)"
  setp red;   meas "after cmd=red"
  setp green; meas "after cmd=green"
  setp white; meas "after cmd=white"
}

$B/launcher-client --command=start-app --command-arg=pattern-generator >/dev/null; sleep 5
trial 1
echo ""
echo "=== restoring a known-good picture before the second trial ==="
i2cset -f -y 1 0x2c 0x01 0x01; sleep 4; setp white; meas "recovered"
trial 0
echo ""
echo "=== final recovery by hand if needed (fpdlink-tool --recover --target=984) ==="
$T --recover --target=984 >/dev/null 2>&1; sleep 4
setp white; meas "final white"
setp red;   meas "final red"
echo 32 | sudo tee $P/dtg_tolerance >/dev/null
echo 1  | sudo tee $P/wedge_recovery >/dev/null
echo "restored: dtg_tolerance=$(cat $P/dtg_tolerance) wedge_recovery=$(cat $P/wedge_recovery) wedges=$(cat $P/dtg_wedge_count)"
