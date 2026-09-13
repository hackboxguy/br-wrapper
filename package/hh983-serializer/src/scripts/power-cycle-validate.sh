#!/bin/bash
#
# power-cycle-validate.sh - repeated cold power cycles of the 983/984 bench rig,
# with the panel checked by colorimeter after every boot.
#
# Written for the 15.6" 2K5 (0OD) intermittent black screen (analysis
# tmp-docs/fable-prompt-v1.md, 2026-09-13): a torn read of the 984's measured
# H total made the driver's DP guard declare a wedge and pulse the 984 DTG on a
# healthy pipeline, and that pulse drops this panel into its own BIST about four
# times out of five.  The failure is per-boot and probabilistic, so the only
# honest proof of a fix is a run of cold cycles.
#
# Two things this deliberately does NOT do:
#
#   - it never decides "the screen is on" from 983/984 registers.  While the
#     panel was black, fpdlink-tool.sh --diagnose said "Pipeline healthy" and
#     every timing register was correct; only the i1Display Pro on the glass
#     knows.  Pass = all samples at or above --pass-nits with a white pattern up.
#   - it does not power-cycle past a failure.  On the first black screen it
#     dumps the evidence and exits non-zero with the rig left exactly as it is,
#     so the bad state can be inspected live.
#
# Usage:
#   RIG_PASS=<device password> ./power-cycle-validate.sh [options]
#
#   --cycles=N        power cycles to run (default 10)
#   --soak-min=M      minutes to keep sampling the white pattern after the
#                     five-sample verdict, once every --soak-gap s (default 0)
#   --pi=user@host    head-unit Pi (default pi@192.168.1.243)
#   --tasmota=URL     Tasmota switch feeding display + 983 + Pi
#                     (default http://192.168.1.186)
#   --log-dir=DIR     per-cycle log and failure dumps (default ./power-cycle-validate-logs;
#                     the 2026-09-13 runs used tmp-docs/fable-prompt-v1-data/)
#   --pass-nits=N     luminance a white sample must reach (default 800; this
#                     panel measures 995..1034 healthy, and the BIST it falls
#                     into cycles 0.8 / 194 / 215 / 233 / 300 / 455 / 580 / 1020)
#   --samples=N       samples per verdict (default 5)
#   --sample-gap=S    seconds between them (default 3)
#   --soak-gap=S      seconds between soak samples (default 30)
#   --off-secs=S      seconds the socket stays off (default 12)
#   --boot-timeout=S  seconds to wait for ssh after power on (default 180)
#   --min-uptime=S    seconds of uptime before measuring, so the desktop and the
#                     guard's boot-time restore have happened (default 120)
#   --no-histogram    skip the 150-read H total histogram
#
# RIG_PASS is read from the environment so that no password reaches the repo.
# With it unset the script uses plain ssh/scp (key auth).
#
set -u

CYCLES=10
SOAK_MIN=0
PI=pi@192.168.1.243
TASMOTA=http://192.168.1.186
LOG_DIR=$PWD/power-cycle-validate-logs
PASS_NITS=800
SAMPLES=5
SAMPLE_GAP=3
SOAK_GAP=30
OFF_SECS=12
BOOT_TIMEOUT=180
MIN_UPTIME=120
HISTOGRAM=1
FPDTOOL=/home/pi/micropanel/bin/fpdlink-tool.sh
MICROBIN=/home/pi/micropanel/usr/bin
MEASDIR=/home/pi/micropanel/share/disptool/display-test-framework

for arg in "$@"; do
    case "$arg" in
        --cycles=*)       CYCLES="${arg#*=}" ;;
        --soak-min=*)     SOAK_MIN="${arg#*=}" ;;
        --pi=*)           PI="${arg#*=}" ;;
        --tasmota=*)      TASMOTA="${arg#*=}" ;;
        --log-dir=*)      LOG_DIR="${arg#*=}" ;;
        --pass-nits=*)    PASS_NITS="${arg#*=}" ;;
        --samples=*)      SAMPLES="${arg#*=}" ;;
        --sample-gap=*)   SAMPLE_GAP="${arg#*=}" ;;
        --soak-gap=*)     SOAK_GAP="${arg#*=}" ;;
        --off-secs=*)     OFF_SECS="${arg#*=}" ;;
        --boot-timeout=*) BOOT_TIMEOUT="${arg#*=}" ;;
        --min-uptime=*)   MIN_UPTIME="${arg#*=}" ;;
        --no-histogram)   HISTOGRAM=0 ;;
        --help|-h)        sed -n '/^# Usage:/,/^set -u/p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=8"
if [ -n "${RIG_PASS:-}" ]; then
    SSH_BIN="sshpass -p $RIG_PASS ssh"
else
    SSH_BIN="ssh"
fi

RUN_ID=$(date +%Y%m%d-%H%M%S)
mkdir -p "$LOG_DIR" || exit 2
LOG="$LOG_DIR/power-cycle-validate-$RUN_ID.log"

say()  { echo "$(date +%H:%M:%S) $*" | tee -a "$LOG"; }
logf() { echo "$*" >> "$LOG"; }

# Remote command.  Everything goes through here so the /usr/sbin PATH fix (i2cget
# and i2cset are not on a non-login ssh shell's PATH, and fpdlink-tool.sh just
# reports FAIL without them) is applied exactly once, in one place.
rsh() {
    local t=$1; shift
    timeout "$t" $SSH_BIN $SSH_OPTS "$PI" "export PATH=/usr/sbin:/sbin:\$PATH; $*" 2>&1
}

tasmota() { timeout 15 curl -s "$TASMOTA/cm?cmnd=$1"; }

# ---------------------------------------------------------------- rig control

power_cycle() {
    say "  sync on the Pi (a write seconds before the cut is still in page cache)"
    rsh 60 'sync' >/dev/null
    say "  Tasmota OFF ($(tasmota Power%20OFF))"
    sleep "$OFF_SECS"
    say "  Tasmota ON ($(tasmota Power%20ON))"
}

wait_for_ssh() {
    local waited=0
    while [ "$waited" -lt "$BOOT_TIMEOUT" ]; do
        sleep 5
        waited=$((waited + 5))
        if [ "$(rsh 20 'echo alive')" = "alive" ]; then
            say "  ssh answered after ${waited}s"
            return 0
        fi
    done
    say "  ssh did NOT answer within ${BOOT_TIMEOUT}s"
    return 1
}

wait_for_uptime() {
    local up
    while :; do
        up=$(rsh 30 "cut -d. -f1 /proc/uptime")
        case "$up" in ''|*[!0-9]*) sleep 5; continue ;; esac
        [ "$up" -ge "$MIN_UPTIME" ] && { say "  uptime ${up}s"; return 0; }
        sleep 5
    done
}

white_pattern() {
    rsh 90 "$MICROBIN/launcher-client --command=start-app --command-arg=pattern-generator; sleep 4; \
            $MICROBIN/launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg=white" >/dev/null
    sleep 3
}

stop_pattern() { rsh 60 "$MICROBIN/launcher-client --command=stop-app" >/dev/null; }

# N luminance samples, --sample-gap apart, one line of nits each.  Taken in a
# single ssh call: the sensor read is the slow part and per-sample ssh setup
# would stretch the window the samples are meant to cover.
take_samples() {
    local n=$1 gap=$2
    rsh $((n * 90 + 60)) "cd $MEASDIR; for i in \$(seq $n); do \
        timeout 60 ./measure-display.sh --sensor-only=yes --quiet=yes | grep ',SENSOR,' | tail -1 | cut -d, -f6; \
        [ \$i -lt $n ] && sleep $gap; done"
}

wedge_count()  { rsh 30 'cat /sys/module/hh983_serializer/parameters/dtg_wedge_count 2>/dev/null || echo NA'; }
wedge_lines()  { rsh 30 'dmesg | grep -c "DTG wedge"'; }

# Section 8 of the analysis: 150 raw MSB+LSB reads of MEAS_HTOTAL, counted.
# After the fix the torn outliers (2560, 3066..3071 against a programmed 2816)
# are still there - the hardware has not changed - and that is the point: the
# driver is expected to stop acting on them, not to stop seeing them.
histogram() {
    rsh 180 'i2cset -f -y 1 0x2c 0x40 0x50; for i in $(seq 150); do \
        i2cset -f -y 1 0x2c 0x41 0x40; h=$(i2cget -f -y 1 0x2c 0x42); \
        i2cset -f -y 1 0x2c 0x41 0x41; l=$(i2cget -f -y 1 0x2c 0x42); \
        echo $(( ((h&0x7f)<<8)|l )); done | sort -n | uniq -c | sort -rn'
}

# ------------------------------------------------------------------- verdicts

# Pass only if every sample reached PASS_NITS.  A non-numeric sample (the sensor
# read failed) counts as a failure: it is not evidence that the panel is lit.
samples_ok() {
    local s ok=0 n=0
    for s in $1; do
        n=$((n + 1))
        awk -v v="$s" -v t="$PASS_NITS" 'BEGIN{exit !(v+0 >= t)}' 2>/dev/null && ok=$((ok + 1))
    done
    [ "$n" -gt 0 ] && [ "$ok" -eq "$n" ]
}

fmt_samples() { echo "$1" | tr '\n' ' ' | sed 's/  */ /g; s/ $//'; }

minmax() {
    echo "$1" | awk 'BEGIN{mn="";mx=""} {v=$1+0; if(mn==""||v<mn)mn=v; if(mx==""||v>mx)mx=v}
                     END{if(mn=="")print "NA/NA"; else printf "%.1f/%.1f", mn, mx}'
}

fail_dump() {
    local cycle=$1 samples=$2 reason=$3
    local dump="$LOG_DIR/power-cycle-validate-$RUN_ID-FAIL-cycle$cycle.txt"
    say ""
    say "!!! BLACK SCREEN on cycle $cycle: $reason"
    say "!!! not power-cycling again; the rig is left in the failed state"
    say "!!! evidence -> $dump"
    {
        echo "=== power-cycle-validate failure, cycle $cycle, $(date -Is)"
        echo "reason: $reason"
        echo "pass threshold: $PASS_NITS nits"
        echo ""
        echo "=== luminance samples (nits)"
        echo "$samples"
        echo ""
        echo "=== dtg_wedge_count / module parameters"
        rsh 30 'grep -H . /sys/module/hh983_serializer/parameters/* 2>/dev/null'
        echo ""
        echo "=== uptime"
        rsh 30 'uptime; head -1 /proc/uptime'
        echo ""
        echo "=== dmesg | grep hh983"
        rsh 60 'dmesg | grep -i hh983'
        echo ""
        echo "=== fpdlink-tool.sh --target=984 --diagnose"
        rsh 120 "NO_COLOR=1 $FPDTOOL --target=984 --diagnose"
        echo ""
        echo "=== 150-read MEAS_HTOTAL histogram"
        histogram
    } > "$dump" 2>&1
    sed -n '1,40p' "$dump" | tee -a "$LOG"
}

# ----------------------------------------------------------------------- main

say "power-cycle-validate: $CYCLES cycles, soak ${SOAK_MIN} min, pi=$PI, tasmota=$TASMOTA"
say "pass = $SAMPLES samples >= $PASS_NITS nits on a white pattern, colorimeter only"
say "log: $LOG"
logf "# cycle,timestamp,uptime_s,wedge_count,dmesg_wedge_lines,samples_nits,min/max,result"

if [ "$(tasmota Power)" = "" ]; then
    say "Tasmota at $TASMOTA does not answer - aborting before touching anything"
    exit 2
fi

cycle=1
while [ "$cycle" -le "$CYCLES" ]; do
    say ""
    say "=== cycle $cycle/$CYCLES"
    power_cycle
    if ! wait_for_ssh; then
        fail_dump "$cycle" "" "no ssh within ${BOOT_TIMEOUT}s after power on"
        exit 1
    fi
    wait_for_uptime

    white_pattern
    samples=$(take_samples "$SAMPLES" "$SAMPLE_GAP")
    wc_now=$(wedge_count)
    wl_now=$(wedge_lines)
    up_now=$(rsh 30 "cut -d. -f1 /proc/uptime")
    mm=$(minmax "$samples")
    flat=$(fmt_samples "$samples")

    if ! samples_ok "$samples"; then
        logf "$cycle,$(date -Is),$up_now,$wc_now,$wl_now,\"$flat\",$mm,FAIL"
        fail_dump "$cycle" "$samples" "samples below $PASS_NITS nits: $flat"
        exit 1
    fi
    if [ "$wc_now" != "0" ]; then
        logf "$cycle,$(date -Is),$up_now,$wc_now,$wl_now,\"$flat\",$mm,FAIL-WEDGE"
        fail_dump "$cycle" "$samples" "panel lit but dtg_wedge_count=$wc_now (the guard still fired)"
        exit 1
    fi
    say "  cycle $cycle verdict: PASS  samples=$flat  min/max=$mm  wedges=$wc_now"

    # Soak: the false wedges were 30..120 s apart, so the five-sample verdict
    # alone can walk straight past one.  Same pass rule, once every --soak-gap.
    if [ "$SOAK_MIN" -gt 0 ]; then
        soak_end=$(( $(date +%s) + SOAK_MIN * 60 ))
        say "  soak ${SOAK_MIN} min, sampling every ${SOAK_GAP}s"
        while [ "$(date +%s)" -lt "$soak_end" ]; do
            sleep "$SOAK_GAP"
            s=$(take_samples 1 1)
            if ! samples_ok "$s"; then
                wc_now=$(wedge_count); wl_now=$(wedge_lines)
                logf "$cycle,$(date -Is),soak,$wc_now,$wl_now,\"$s\",$s,FAIL-SOAK"
                fail_dump "$cycle" "$s" "soak sample below $PASS_NITS nits: $s"
                exit 1
            fi
            logf "  soak $cycle $(date +%H:%M:%S) $s nits wedges=$(wedge_count)"
        done
        wc_now=$(wedge_count)
        wl_now=$(wedge_lines)
        if [ "$wc_now" != "0" ]; then
            logf "$cycle,$(date -Is),soak-end,$wc_now,$wl_now,\"\",,FAIL-WEDGE"
            fail_dump "$cycle" "" "soak ended with dtg_wedge_count=$wc_now"
            exit 1
        fi
        say "  soak done, wedges=$wc_now"
    fi

    if [ "$HISTOGRAM" = "1" ]; then
        hist=$(histogram | tr '\n' ';' | sed 's/  */ /g')
        logf "  hist $cycle: $hist"
        say "  MEAS_HTOTAL histogram: $(echo "$hist" | cut -c1-110)"
    fi

    logf "$cycle,$(date -Is),$up_now,$wc_now,$wl_now,\"$flat\",$mm,PASS"
    stop_pattern
    cycle=$((cycle + 1))
done

say ""
say "=== $CYCLES/$CYCLES cycles passed, dtg_wedge_count 0 at the end of every one"
say "rig left on the desktop with the pattern generator stopped"
exit 0
