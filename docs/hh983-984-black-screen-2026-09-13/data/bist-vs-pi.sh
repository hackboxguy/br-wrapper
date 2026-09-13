#!/bin/bash
export PATH=/usr/sbin:/sbin:$PATH
B=/home/pi/micropanel/usr/bin
M=/home/pi/micropanel/share/disptool/display-test-framework/measure-display.sh
D=0x2c
meas() { timeout 60 $M --sensor-only=yes --quiet=yes | grep ',SENSOR,' | tail -1 | awk -F, -v l="$1" '{printf "%-22s Y=%9.3f  x=%.4f  y=%.4f\n", l, $6, $9, $10}'; }
setpat() { $B/launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg=$1 >/dev/null; sleep 3; }
apbw32() { i2cset -f -y 1 $D 0x48 0x01; i2cset -f -y 1 $D 0x49 $(printf 0x%02x $(($1&0xff))); i2cset -f -y 1 $D 0x4a $(printf 0x%02x $((($1>>8)&0xff))); i2cset -f -y 1 $D 0x4b $2; i2cset -f -y 1 $D 0x4c 0; i2cset -f -y 1 $D 0x4d 0; i2cset -f -y 1 $D 0x4e 0; }
indw() { i2cset -f -y 1 $D 0x40 $1; i2cset -f -y 1 $D 0x41 $2; i2cset -f -y 1 $D 0x42 $3; }

echo "=== A. Pi4-generated patterns (what a healthy panel shows) ==="
$B/launcher-client --command=start-app --command-arg=pattern-generator >/dev/null; sleep 5
for p in white red green blue black white; do setpat $p; meas "pi:$p"; done

echo ""
echo "=== B. driver poll paused, DTG pulse replay to induce BIST ==="
echo 0 | sudo tee /sys/module/hh983_serializer/parameters/poll_interval_ms >/dev/null; sleep 1
setpat white
meas "pi:white before"
apbw32 0x084 0x00
indw 0x50 0x32 0x06; indw 0x50 0x62 0x06; sleep 0.2; indw 0x50 0x32 0x04; indw 0x50 0x62 0x04; sleep 0.5
apbw32 0x084 0x01; sleep 2
echo "--- 18 samples, Pi is still sending solid WHITE the whole time ---"
for i in $(seq 18); do meas "bist-$i"; done

echo ""
echo "=== C. does the panel follow a Pi pattern change while in this state? ==="
setpat red;   for i in 1 2 3; do meas "cmd=red-$i"; done
setpat green; for i in 1 2 3; do meas "cmd=green-$i"; done

echo ""
echo "=== D. recover: 984 digital reset (Sync Video) ==="
i2cset -f -y 1 $D 0x01 0x01; sleep 3
setpat white; meas "recovered:white"
setpat red;   meas "recovered:red"
setpat green; meas "recovered:green"
setpat blue;  meas "recovered:blue"
setpat black; meas "recovered:black"
setpat white; meas "recovered:white"
echo 1000 | sudo tee /sys/module/hh983_serializer/parameters/poll_interval_ms >/dev/null
echo "poll restored; wedges=$(cat /sys/module/hh983_serializer/parameters/dtg_wedge_count)"
