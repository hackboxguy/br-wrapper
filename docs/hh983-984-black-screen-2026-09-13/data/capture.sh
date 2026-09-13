#!/bin/bash
# Register capture on the Pi. $1 = label
L=$1
T=/home/pi/micropanel/bin/fpdlink-tool.sh
echo "##### CAPTURE $L  $(date '+%F %T')"
echo "##### poll_interval_ms=$(cat /sys/module/hh983_serializer/parameters/poll_interval_ms) dtg_wedge_count=$(cat /sys/module/hh983_serializer/parameters/dtg_wedge_count)"
echo "##### --- fpdlink-tool --target=984 --diagnose"
NO_COLOR=1 $T --target=984 --diagnose 2>&1
echo "##### --- fpdlink-tool --target=983 --timings"
NO_COLOR=1 $T --target=983 --timings 2>&1
echo "##### --- fpdlink-tool --target=984 --timings"
NO_COLOR=1 $T --target=984 --timings 2>&1
echo "##### --- raw 983 main page 0x00-0x5F"
for r in $(seq 0 95); do printf "%02x=%s " $r $(i2cget -f -y 1 0x18 $r 2>/dev/null); [ $((r%16)) -eq 15 ] && echo; done; echo
echo "##### --- raw 984 main page 0x00-0x5F"
for r in $(seq 0 95); do printf "%02x=%s " $r $(i2cget -f -y 1 0x2c $r 2>/dev/null); [ $((r%16)) -eq 15 ] && echo; done; echo
echo "##### --- 984 DTG page 0x50 offsets 0x00-0x7F"
i2cset -f -y 1 0x2c 0x40 0x50
for r in $(seq 0 127); do i2cset -f -y 1 0x2c 0x41 $r; printf "%02x=%s " $r $(i2cget -f -y 1 0x2c 0x42); [ $((r%16)) -eq 15 ] && echo; done; echo
echo "##### --- 984 APB 0x084 (main stream en), 0x080-0x0A0"
for a in 0x080 0x084 0x088 0x08c 0x090 0x094 0x098 0x09c 0x0a0 0x000; do
  lo=$(printf "0x%02x" $((a & 0xff))); hi=$(printf "0x%02x" $(((a>>8)&0xff)))
  i2cset -f -y 1 0x2c 0x49 $lo; i2cset -f -y 1 0x2c 0x4a $hi; i2cset -f -y 1 0x2c 0x48 0x03; sleep 0.002
  printf "APB[%s]=%s %s %s %s\n" $a $(i2cget -f -y 1 0x2c 0x4b) $(i2cget -f -y 1 0x2c 0x4c) $(i2cget -f -y 1 0x2c 0x4d) $(i2cget -f -y 1 0x2c 0x4e)
done
echo "##### --- 983 VP page 0x32 offsets 0x00-0x3F"
i2cset -f -y 1 0x18 0x40 0x32
for r in $(seq 0 63); do i2cset -f -y 1 0x18 0x41 $r; printf "%02x=%s " $r $(i2cget -f -y 1 0x18 0x42); [ $((r%16)) -eq 15 ] && echo; done; echo
echo "##### --- Htotal torn-read sampling: 150 samples hi-then-lo (driver order), then 150 lo-then-hi"
i2cset -f -y 1 0x2c 0x40 0x50
for i in $(seq 1 150); do i2cset -f -y 1 0x2c 0x41 0x40; h=$(i2cget -f -y 1 0x2c 0x42); i2cset -f -y 1 0x2c 0x41 0x41; l=$(i2cget -f -y 1 0x2c 0x42); printf "%d " $(( ((h&0x7f)<<8)|l )); done | tr ' ' '\n' | sort -n | uniq -c | sort -rn | head; 
echo "--- lo-then-hi"
for i in $(seq 1 150); do i2cset -f -y 1 0x2c 0x41 0x41; l=$(i2cget -f -y 1 0x2c 0x42); i2cset -f -y 1 0x2c 0x41 0x40; h=$(i2cget -f -y 1 0x2c 0x42); printf "%d " $(( ((h&0x7f)<<8)|l )); done | tr ' ' '\n' | sort -n | uniq -c | sort -rn | head
echo "--- hi byte only x100"; for i in $(seq 1 100); do i2cset -f -y 1 0x2c 0x41 0x40; i2cget -f -y 1 0x2c 0x42; done | sort | uniq -c
echo "--- lo byte only x100"; for i in $(seq 1 100); do i2cset -f -y 1 0x2c 0x41 0x41; i2cget -f -y 1 0x2c 0x42; done | sort | uniq -c
echo "##### END $L $(date '+%F %T')"
