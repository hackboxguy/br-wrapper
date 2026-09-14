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
#     knows.
#   - it never decides from a measurement of one pattern either, however
#     thorough.  See pat_ref() below: the panel's own BIST reproduces the Pi's
#     white, red, green and blue to within the sensor's repeatability, so no
#     single reading of luminance or chromaticity says which source is on the
#     glass.  The verdict is a command-response test instead - a randomised
#     sequence of patterns, each of which has to arrive.
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
#   --pass-nits=N     luminance the white step must reach (default 800; this
#                     panel measures 1102..1110 healthy)
#   --verdict=MODE    "sequence" (default): walk white + the four others in a
#                     random order + white.  "hold": show one randomly chosen
#                     colour, read it twice --hold-secs apart, then command
#                     white.  See check_hold() for why the cheap one is sound.
#   --hold-secs=S     gap between the two reads of the held colour (default 10)
#   --warm            reboot over ssh ("sync; sudo reboot") instead of cutting
#                     the Tasmota socket.  Cold cycles never wedge the 984 DTG
#                     on this rig; warm reboots do, which is the only way to
#                     exercise the wedge detection and recovery end to end.
#   --retry-settle=S  on a pattern mismatch, wait S seconds and measure that
#                     same pattern once more before failing (default 8).  A
#                     panel that has just recovered from a wedge can still be
#                     converging its local dimming -- 2026-09-13 saw red read
#                     571 nits against a steady-state 268, with the chromaticity
#                     already correct.  A BIST does not survive the re-measure:
#                     it keeps stepping, so it has to coincide twice.
#   --settle=S        seconds between setting a pattern and measuring (default 3)
#   --xy-tol=T        chromaticity tolerance per axis (default 0.03; the sensor
#                     repeated to +-0.0007 across a whole session)
#   --soak-gap=S      seconds between soak checks (default 45)
#   --off-secs=S      seconds the socket stays off (default 12)
#   --boot-timeout=S  seconds to wait for ssh after power on (default 180)
#   --min-uptime=S    seconds of uptime before measuring, so the desktop and the
#                     guard's boot-time restore have happened (default 120)
#   --hist-every=N    take the 150-read H total histogram every Nth cycle
#                     (default 1).  It is diagnostic, not pass/fail, and it
#                     costs ~40 s with the driver poll stopped.
#   --no-histogram    skip the histogram entirely
#
# On soak length: the failure this was built for latches.  A false wedge pulses
# the DTG, the panel drops into its BIST and stays there until a 984 digital
# reset, so a wedge at t=36 s is still plainly visible at t=120 s -- the
# observation does not have to be concurrent with the event.  dtg_wedge_count
# is better still: the pulse only costs the picture about four times in five,
# so the counter catches wedges the colorimeter cannot see, and it is
# cumulative and instant.  And the variable that actually varies is per-boot,
# not per-minute (whether the measured H total straddles a byte boundary
# depends on the pixel-clock relationship established at each power-up), so
# cycles buy more confidence per minute than soak does.  Prefer many cycles
# with a short soak over few cycles with a long one.
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
VERDICT_MODE=sequence
HOLD_SECS=10
WARM=0
RETRY_SETTLE=8
SETTLE=3
XY_TOL=0.03
SOAK_GAP=45
OFF_SECS=12
BOOT_TIMEOUT=180
MIN_UPTIME=120
HISTOGRAM=1
HIST_EVERY=1
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
        --verdict=*)      VERDICT_MODE="${arg#*=}" ;;
        --hold-secs=*)    HOLD_SECS="${arg#*=}" ;;
        --warm)           WARM=1 ;;
        --retry-settle=*) RETRY_SETTLE="${arg#*=}" ;;
        --settle=*)       SETTLE="${arg#*=}" ;;
        --xy-tol=*)       XY_TOL="${arg#*=}" ;;
        --soak-gap=*)     SOAK_GAP="${arg#*=}" ;;
        --off-secs=*)     OFF_SECS="${arg#*=}" ;;
        --boot-timeout=*) BOOT_TIMEOUT="${arg#*=}" ;;
        --min-uptime=*)   MIN_UPTIME="${arg#*=}" ;;
        --hist-every=*)   HIST_EVERY="${arg#*=}" ;;
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

NL='\n'
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

# Warm reboot over ssh.
#
# "reboot" returns immediately and the host keeps answering ssh for a few
# seconds afterwards, so polling for uptime straight away matches the *old*
# uptime and sails past the shutdown entirely.  Wait for the host to actually go
# away first, then let wait_for_ssh() find it again.
warm_reboot() {
    local waited=0
    say "  sync + warm reboot over ssh"
    rsh 60 'sync; (sudo reboot &) >/dev/null 2>&1' >/dev/null 2>&1
    while [ "$waited" -lt 90 ]; do
        sleep 3
        waited=$((waited + 3))
        if ! rsh 8 'true' >/dev/null 2>&1; then
            say "  host went down after ${waited}s"
            return 0
        fi
    done
    say "  host never went down after the reboot request"
    return 1
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

start_pattern_app() {
    rsh 90 "$MICROBIN/launcher-client --command=start-app --command-arg=pattern-generator" >/dev/null
    sleep 4
}

stop_pattern() { rsh 60 "$MICROBIN/launcher-client --command=stop-app" >/dev/null; }

# What a Pi-generated pattern must measure on this panel.
#
# Measured with the i1Display Pro on the glass, 2026-09-13, and kept in
# tmp-docs/fable-prompt-v1-data/bist-vs-pi-content.txt:
#
#   white Y=1109 x=0.3050 y=0.3301     red   Y=268 x=0.6852 y=0.3134
#   green Y=710  x=0.2229 y=0.7175     blue  Y=129 x=0.1428 y=0.0856
#   black Y=0.000
#
# The reason the verdict is a sequence and not a threshold is in the same file.
# Sampled 18 times while the Pi sent nothing but solid white, the panel's TDDI
# BIST walked a 12-step cycle that contained white at Y=1105 x=0.3044 y=0.3300,
# red at 267/0.6853/0.3133, green at 709/0.2235/0.7171, blue at
# 129/0.1427/0.0854, two blacks and five greys.  Every one of those is inside
# any tolerance worth setting, so a single reading - of luminance, of
# chromaticity, of both - cannot say which source is on the glass.  What the
# BIST cannot do is follow the Pi: commanded red and green while it was running
# it answered 0.93/186/1103 and 129/251/251.
#
# Y bands are +-30 % of nominal, which is far wider than the panel drifts and
# far narrower than the gaps between these colours.  Black is checked on
# luminance alone (its chromaticity is meaningless at zero) and separates
# cleanly anyway: the Pi's black reads 0.000 where the BIST's darkest steps
# read 0.93 and 1.70.
pat_ref() {
    case "$1" in
        white) echo "$PASS_NITS 1400 0.3050 0.3301" ;;
        red)   echo "188 348 0.6852 0.3134" ;;
        green) echo "496 922 0.2229 0.7175" ;;
        blue)  echo "90 168 0.1428 0.0856" ;;
        black) echo "0 0.3 - -" ;;
        *)     echo "" ;;
    esac
}

# Measure whatever is on the glass right now.  Echoes "<Y> <x> <y>".
measure_only() {
    rsh 150 "cd $MEASDIR; \
             timeout 60 ./measure-display.sh --sensor-only=yes --quiet=yes | grep ',SENSOR,' | tail -1 | \
             awk -F, '{print \$6, \$9, \$10}'"
}

# Set one pattern, let it settle, measure it.  Echoes "<Y> <x> <y>".
measure_pattern() {
    rsh 150 "$MICROBIN/launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg=$1 >/dev/null; \
             sleep $SETTLE; cd $MEASDIR; \
             timeout 60 ./measure-display.sh --sensor-only=yes --quiet=yes | grep ',SENSOR,' | tail -1 | \
             awk -F, '{print \$6, \$9, \$10}'"
}

pattern_ok() {
    local name=$1 y=$2 cx=$3 cy=$4 ref ymin ymax rx ry
    ref=$(pat_ref "$name")
    [ -z "$ref" ] && return 1
    ymin=$(echo "$ref" | cut -d\  -f1); ymax=$(echo "$ref" | cut -d\  -f2)
    rx=$(echo "$ref" | cut -d\  -f3);   ry=$(echo "$ref" | cut -d\  -f4)
    awk -v v="$y" -v a="$ymin" -v b="$ymax" 'BEGIN{exit !(v+0>=a+0 && v+0<=b+0)}' || return 1
    [ "$rx" = "-" ] && return 0
    awk -v v="$cx" -v r="$rx" -v t="$XY_TOL" 'BEGIN{d=v-r; if(d<0)d=-d; exit !(d<=t+0)}' || return 1
    awk -v v="$cy" -v r="$ry" -v t="$XY_TOL" 'BEGIN{d=v-r; if(d<0)d=-d; exit !(d<=t+0)}' || return 1
    return 0
}

# White first and last - the brightness criterion lives on white - with the
# rest shuffled in between.  The BIST cycle runs white -> red -> green -> blue,
# which is exactly the fixed order it could imitate by accident, so the order
# is drawn fresh every time.
verdict_sequence() { echo "white $(printf '%s\n' black red green blue | shuf | tr '\n' ' ')white"; }
soak_sequence()    { echo "white $(printf '%s\n' black red green blue | shuf -n 1)"; }

# Walk a sequence, measuring every step.  Sets SEQ_LOG to a one-line record of
# what each pattern actually measured and returns non-zero if any of them did
# not arrive.  Every step is measured even after a mismatch: when this fails it
# is the only evidence of what the panel was doing.
# One reading, matched against what the named pattern should measure.  Appends
# to SEQ_LOG and returns non-zero on a mismatch.
is_number() { case "$1" in ''|*[!0-9.]*) return 1 ;; *) return 0 ;; esac; }

# Did any step of the last sequence fail only because the sensor would not
# answer?  Then the verdict says nothing about the panel and the caller must not
# call it a black screen.
SEQ_SENSOR_ERR=0

check_one() {
    local label=$1 ref=$2 out=$3 y cx cy
    y=$(echo  "$out" | awk '{print $1}')
    cx=$(echo "$out" | awk '{print $2}')
    cy=$(echo "$out" | awk '{print $3}')
    # A reading that is not a number is not a measurement, and it has to be
    # rejected here rather than left to pattern_ok(): awk coerces "ERROR" to 0,
    # and black's band includes 0, so a failed sensor read would otherwise be
    # accepted as a perfect black.  Seen on 2026-09-13 as "black=ok(ERROR)".
    if is_number "$y" && pattern_ok "$ref" "$y" "$cx" "$cy"; then
        SEQ_LOG="$SEQ_LOG ${label}=ok(${y})"
        return 0
    fi
    if [ "$RETRY_SETTLE" -gt 0 ]; then
        SEQ_LOG="$SEQ_LOG ${label}=retry(Y=${y:-?} x=${cx:-?} y=${cy:-?})"
        sleep "$RETRY_SETTLE"
        out=$(measure_only)
        y=$(echo  "$out" | awk '{print $1}')
        cx=$(echo "$out" | awk '{print $2}')
        cy=$(echo "$out" | awk '{print $3}')
        if is_number "$y" && pattern_ok "$ref" "$y" "$cx" "$cy"; then
            SEQ_LOG="$SEQ_LOG ${label}=ok-after-${RETRY_SETTLE}s(${y})"
            return 0
        fi
    fi
    if ! is_number "$y"; then
        SEQ_LOG="$SEQ_LOG ${label}=SENSOR-ERROR(${y:-empty})"
        SEQ_SENSOR_ERR=1
        return 1
    fi
    SEQ_LOG="$SEQ_LOG ${label}=MISMATCH(Y=${y:-?} x=${cx:-?} y=${cy:-?})"
    return 1
}

# The cheap verdict: hold one colour, look at it twice, then command a change.
#
# The BIST never rests.  It is a 12-step cycle whose steps last a few seconds,
# so it can be showing red at one instant but it cannot still be showing red
# ten seconds later -- and red, green and blue each appear exactly once in that
# cycle, so the colour is drawn fresh to keep the test from ever lining up with
# the cycle systematically.  Finding the held colour still there on the second
# look is therefore proof the Pi is driving the glass, at about a third of the
# cost of walking the whole set.
#
# The closing white is what a held pattern alone cannot give: it rules out a
# panel frozen on a still frame, which would sit through any number of reads of
# the same colour.  Together they are a stability check and a command-response
# check for roughly 30 s.
check_hold() {
    local colour rc=0
    colour=$(printf '%s\n' red green blue | shuf -n 1)
    SEQ_LOG=""
    SEQ_SENSOR_ERR=0
    check_one "$colour" "$colour" "$(measure_pattern "$colour")" || rc=1
    sleep "$HOLD_SECS"
    check_one "${colour}+${HOLD_SECS}s" "$colour" "$(measure_only)" || rc=1
    check_one white white "$(measure_pattern white)" || rc=1
    return $rc
}

SEQ_LOG=""
check_sequence() {
    local p rc=0
    SEQ_LOG=""
    SEQ_SENSOR_ERR=0
    for p in $1; do
        check_one "$p" "$p" "$(measure_pattern "$p")" || rc=1
    done
    return $rc
}

wedge_count()  { rsh 30 'cat /sys/module/hh983_serializer/parameters/dtg_wedge_count 2>/dev/null || echo NA'; }

# What the driver itself saw at boot, taken from dmesg so it costs no I2C and
# cannot race the guard's own poll the way an i2cget loop does.
boot_htotal()  { rsh 30 'dmesg | grep -m1 -o "DTG measured Htotal=-\?[0-9]*" | grep -o -- "-\?[0-9]*$" || echo NA'; }
boot_wedged()  { rsh 30 'dmesg | grep -qE "\(DTG wedged\)|DTG wedge without video loss" && echo yes || echo no'; }
recovery_ran() {
    rsh 30 'if dmesg | grep -q "984 digital reset after"; then echo digital-reset;
            elif dmesg | grep -qE "\(DTG wedged\)|DTG wedge without video loss"; then echo dtg-pulse;
            else echo none; fi'
}
wedge_lines()  { rsh 30 'dmesg | grep -c "DTG wedge"'; }

# Section 8 of the analysis: 150 raw MSB+LSB reads of MEAS_HTOTAL, counted.
# After the fix the torn outliers (2560, 3066..3071 against a programmed 2816)
# are still there - the hardware has not changed - and that is the point: the
# driver is expected to stop acting on them, not to stop seeing them.
#
# The driver poll is stopped for the duration and restarted afterwards.  Both
# it and this loop reach the 984 through the 983's one set of indirect-access
# registers, so leaving it running lets the two interleave: the driver's own
# reads come back inconsistent and its wedge check is effectively blind while
# this runs.  That cost about 40 s of detection latency on 2026-09-13 before
# the pause was added, and it is why the analysis session paused the poll
# before every dump it took.
histogram() {
    rsh 200 'echo 0 | sudo tee /sys/module/hh983_serializer/parameters/poll_interval_ms >/dev/null; sleep 1; \
        i2cset -f -y 1 0x2c 0x40 0x50; for i in $(seq 150); do \
        i2cset -f -y 1 0x2c 0x41 0x40; h=$(i2cget -f -y 1 0x2c 0x42); \
        i2cset -f -y 1 0x2c 0x41 0x41; l=$(i2cget -f -y 1 0x2c 0x42); \
        echo $(( ((h&0x7f)<<8)|l )); done | sort -n | uniq -c | sort -rn; \
        echo 1000 | sudo tee /sys/module/hh983_serializer/parameters/poll_interval_ms >/dev/null'
}

# ------------------------------------------------------------------- verdicts

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
        echo "=== measured sequence"
        echo "$samples"
        echo ""
        # Trustworthy facts first: these are sysfs and dmesg reads, which cannot
        # be corrupted by anything else touching the I2C bus.  The tool output
        # further down can be, so it comes last and with the poll stopped.
        echo "=== dtg_wedge_count"
        rsh 30 'cat /sys/module/hh983_serializer/parameters/dtg_wedge_count 2>/dev/null'
        echo ""
        echo "=== wedge_recovery"
        rsh 30 'cat /sys/module/hh983_serializer/parameters/wedge_recovery 2>/dev/null'
        echo ""
        echo "=== last 30 hh983 dmesg lines"
        rsh 60 'dmesg | grep -i hh983 | tail -30'
        echo ""
        echo "=== all module parameters"
        rsh 30 'grep -H . /sys/module/hh983_serializer/parameters/* 2>/dev/null'
        echo ""
        echo "=== uptime"
        rsh 30 'uptime; head -1 /proc/uptime'
        echo ""
        # --diagnose walks the 983/984 indirect-access registers (page, offset,
        # data) one at a time, and so does the driver's poll.  Run together they
        # interleave and the tool reports whatever the driver left in the address
        # register: on 2026-09-14 a live-poll --diagnose called the 983's static
        # V total 1463 when it is 1492, and read a measured H total of 2560.  A
        # failure dump full of invented register values is worse than no dump, so
        # stop the poll around it exactly as histogram() does.
        echo "=== fpdlink-tool.sh --target=984 --diagnose (driver poll stopped for this)"
        rsh 30 'echo 0 | sudo tee /sys/module/hh983_serializer/parameters/poll_interval_ms >/dev/null; sleep 1'
        rsh 120 "NO_COLOR=1 $FPDTOOL --target=984 --diagnose"
        rsh 30 'echo 1000 | sudo tee /sys/module/hh983_serializer/parameters/poll_interval_ms >/dev/null'
        echo ""
        echo "=== 150-read MEAS_HTOTAL histogram"
        histogram
    } > "$dump" 2>&1
    sed -n '1,40p' "$dump" | tee -a "$LOG"
}

# ----------------------------------------------------------------------- main

say "power-cycle-validate: $CYCLES cycles, soak ${SOAK_MIN} min, pi=$PI, tasmota=$TASMOTA"
say "pass = verdict mode '$VERDICT_MODE' arrives on the glass (colorimeter only) and dtg_wedge_count == 0"
say "log: $LOG"
logf "# cycle,timestamp,uptime_s,wedge_count,dmesg_wedge_lines,boot_htotal,wedged_at_boot,recovery,measured_sequence,result"

if [ "$(tasmota Power)" = "" ]; then
    say "Tasmota at $TASMOTA does not answer - aborting before touching anything"
    exit 2
fi

cycle=1
while [ "$cycle" -le "$CYCLES" ]; do
    say ""
    say "=== cycle $cycle/$CYCLES"
    if [ "$WARM" = "1" ]; then
        if ! warm_reboot; then
            fail_dump "$cycle" "" "warm reboot did not bring the host down"
            exit 1
        fi
    else
        power_cycle
    fi
    if ! wait_for_ssh; then
        fail_dump "$cycle" "" "no ssh within ${BOOT_TIMEOUT}s after power on"
        exit 1
    fi
    wait_for_uptime

    start_pattern_app
    if [ "$VERDICT_MODE" = "hold" ]; then
        seq_list="hold-one-colour"
        check_hold; seq_rc=$?
    else
        seq_list=$(verdict_sequence)
        check_sequence "$seq_list"; seq_rc=$?
    fi
    flat=$SEQ_LOG
    wc_now=$(wedge_count)
    wl_now=$(wedge_lines)
    up_now=$(rsh 30 "cut -d. -f1 /proc/uptime")
    bh=$(boot_htotal)
    bw=$(boot_wedged)
    rec=$(recovery_ran)
    say "  boot: DTG measured $bh, wedged=$bw, recovery=$rec"

    if [ "$seq_rc" != "0" ] && [ "$SEQ_SENSOR_ERR" = "1" ]; then
        say "  colorimeter did not answer; re-running the verdict once"
        if [ "$VERDICT_MODE" = "hold" ]; then check_hold; seq_rc=$?
        else check_sequence "$seq_list"; seq_rc=$?; fi
        flat=$SEQ_LOG
    fi
    if [ "$seq_rc" != "0" ]; then
        if [ "$SEQ_SENSOR_ERR" = "1" ]; then
            logf "$cycle,$(date -Is),$up_now,$wc_now,$wl_now,$bh,$bw,$rec,\"$flat\",FAIL-SENSOR"
            fail_dump "$cycle" "commanded: $seq_list${NL}measured:$flat" \
                      "the colorimeter would not return a reading - this says nothing about the panel, re-run"
        else
            logf "$cycle,$(date -Is),$up_now,$wc_now,$wl_now,$bh,$bw,$rec,\"$flat\",FAIL"
            fail_dump "$cycle" "commanded: $seq_list${NL}measured:$flat" \
                      "the panel did not follow the commanded patterns:$flat"
        fi
        exit 1
    fi
    if [ "$wc_now" != "0" ]; then
        logf "$cycle,$(date -Is),$up_now,$wc_now,$wl_now,$bh,$bw,$rec,\"$flat\",FAIL-WEDGE"
        fail_dump "$cycle" "$flat" "panel followed every pattern but dtg_wedge_count=$wc_now (the guard still fired)"
        exit 1
    fi
    say "  cycle $cycle verdict: PASS $flat  wedges=$wc_now recovery=$rec"

    # Soak: the false wedges were 30..120 s apart, so the five-sample verdict
    # alone can walk straight past one.  Same pass rule, once every --soak-gap.
    if [ "$SOAK_MIN" -gt 0 ]; then
        soak_end=$(( $(date +%s) + SOAK_MIN * 60 ))
        say "  soak ${SOAK_MIN} min, sampling every ${SOAK_GAP}s"
        while [ "$(date +%s)" -lt "$soak_end" ]; do
            sleep "$SOAK_GAP"
            s_list=$(soak_sequence)
            if ! check_sequence "$s_list"; then
                wc_now=$(wedge_count); wl_now=$(wedge_lines)
                logf "$cycle,$(date -Is),soak,$wc_now,$wl_now,\"$SEQ_LOG\",,FAIL-SOAK"
                fail_dump "$cycle" "commanded: $s_list${NL}measured:$SEQ_LOG" \
                          "soak: the panel did not follow the commanded patterns:$SEQ_LOG"
                exit 1
            fi
            logf "  soak $cycle $(date +%H:%M:%S)$SEQ_LOG wedges=$(wedge_count)"
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

    if [ "$HISTOGRAM" = "1" ] && [ $(( (cycle - 1) % HIST_EVERY )) -eq 0 ]; then
        hist=$(histogram | tr '\n' ';' | sed 's/  */ /g')
        logf "  hist $cycle: $hist"
        say "  MEAS_HTOTAL histogram: $(echo "$hist" | cut -c1-110)"
    fi

    logf "$cycle,$(date -Is),$up_now,$wc_now,$wl_now,$bh,$bw,$rec,\"$flat\",PASS"
    stop_pattern
    cycle=$((cycle + 1))
done

say ""
say "=== $CYCLES/$CYCLES cycles passed, dtg_wedge_count 0 at the end of every one"
say "rig left on the desktop with the pattern generator stopped"
exit 0
