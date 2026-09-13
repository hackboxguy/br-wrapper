#!/bin/bash
export PATH=/usr/sbin:/sbin:$PATH
M=/home/pi/micropanel/share/disptool/display-test-framework/measure-display.sh
P=/sys/module/hh983_serializer/parameters
meas() { timeout 60 $M --sensor-only=yes 2>&1 | tail -1 | awk -F, '{printf "%s Y=%s nits\n", $2, $5}'; }
echo "=== EXP2 start $(date +%T)  wedges=$(cat $P/dtg_wedge_count) tol=$(cat $P/dtg_tolerance)"
echo "pre: $(meas)"
echo 1000 | sudo -n tee $P/poll_interval_ms >/dev/null
echo "poll re-enabled at $(date +%T)"
for i in $(seq 1 45); do echo "[$i] $(meas) wedges=$(cat $P/dtg_wedge_count)"; sleep 1; done
echo "=== dmesg since re-enable:"; dmesg -T | grep hh983 | awk -v t="$(date -d '-5 min' '+%s')" '{print}' | tail -25
LAST=$(meas | awk '{print $2}')
echo "final: $LAST  wedges=$(cat $P/dtg_wedge_count)"
echo "=== mitigation: dtg_tolerance=600 (runtime only)"
echo 600 | sudo -n tee $P/dtg_tolerance
case "$LAST" in Y=0.0*) echo "panel black -> running --recover"; NO_COLOR=1 /home/pi/micropanel/bin/fpdlink-tool.sh --recover --target=984; sleep 3; echo "after recover: $(meas)";; esac
echo "=== EXP2 end $(date +%T)"
