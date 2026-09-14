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
#   --pass-nits=N     floor the white reference must clear for the panel to
#                     count as lit at all (default 50).  It is NOT a health
#                     threshold: everything else is judged as a ratio to that
#                     reference, because the image ships als-dimmer enabled and
#                     the backlight can sit anywhere.  15.6-2k5 white measures
#                     ~1110 nits at full brightness and ~613 at 55 %.
#   --panel=NAME      display type whose colour references to use (e.g.
#                     ots-oled-17, 15.6-2k5).  Detected from the Pi when not
#                     given; see pat_ref() for why it matters.
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
PASS_NITS=50
VERDICT_MODE=sequence
PANEL=""
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
MICROSHARE=/home/pi/micropanel/usr/share/micropanel

for arg in "$@"; do
    case "$arg" in
        --cycles=*)       CYCLES="${arg#*=}" ;;
        --soak-min=*)     SOAK_MIN="${arg#*=}" ;;
        --pi=*)           PI="${arg#*=}" ;;
        --tasmota=*)      TASMOTA="${arg#*=}" ;;
        --log-dir=*)      LOG_DIR="${arg#*=}" ;;
        --pass-nits=*)    PASS_NITS="${arg#*=}" ;;
        --verdict=*)      VERDICT_MODE="${arg#*=}" ;;
        --panel=*)        PANEL="${arg#*=}" ;;
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

# Switch the socket and insist the switch actually answered.
#
# curl returns an empty body when the Tasmota does not reply, and this used to
# print that as "Tasmota ON ()" and carry on into a 180 s ssh wait that could
# only fail. On 2026-09-14 the OLED rig's socket dropped off WiFi between the
# OFF and the ON: the rig was switched off and never switched back on, sat
# unpowered, and the run reported a black screen -- which was true and
# completely misleading. An unpowered rig is not a display fault.
#
# Retries a few times, and the caller stops the run if the switch never answers,
# so the failure is named for what it is.
tasmota_set() {
    local want=$1 try=1 out=""
    while [ "$try" -le 5 ]; do
        out=$(tasmota "Power%20$want")
        case "$out" in
            *"\"POWER\":\"$want\""*) echo "$out"; return 0 ;;
        esac
        sleep 3
        try=$((try + 1))
    done
    echo "${out:-no answer}"
    return 1
}

power_cycle() {
    say "  sync on the Pi (a write seconds before the cut is still in page cache)"
    rsh 60 'sync' >/dev/null
    if ! out=$(tasmota_set OFF); then
        say "  Tasmota did not confirm OFF ($out)"
        return 1
    fi
    say "  Tasmota OFF ($out)"
    sleep "$OFF_SECS"
    if ! out=$(tasmota_set ON); then
        say "  Tasmota did not confirm ON ($out) -- the rig may be sitting unpowered"
        return 1
    fi
    say "  Tasmota ON ($out)"
    return 0
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

# Start the pattern generator, stopping whatever the launcher already has up.
#
# start-app answers "ERROR: app-already-running" and changes nothing if another
# app is running -- disp-settings is up by default on the OLED rig -- and the
# harness then measured the desktop while believing it was measuring patterns.
# Every reading came back around 5 nits and looked exactly like a black panel.
# Stop first, then start, and confirm what is actually running.
start_pattern_app() {
    local running
    running=$(rsh 60 "$MICROBIN/launcher-client --command=get-running-app" 2>/dev/null)
    if [ "$running" != "pattern-generator" ]; then
        rsh 60 "$MICROBIN/launcher-client --command=stop-app" >/dev/null 2>&1
        sleep 5
        rsh 90 "$MICROBIN/launcher-client --command=start-app --command-arg=pattern-generator" >/dev/null 2>&1
        sleep 7
        running=$(rsh 60 "$MICROBIN/launcher-client --command=get-running-app" 2>/dev/null)
    fi
    if [ "$running" != "pattern-generator" ]; then
        say "  WARNING: pattern generator is not running (launcher says '${running:-nothing}')"
        return 1
    fi
    return 0
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
# Luminance is checked as a RATIO to a white reference measured at the start of
# the same verdict, not as an absolute number of nits.  The image ships
# als-dimmer enabled, so the backlight is wherever ambient light and the user
# have left it -- on 2026-09-14 image 01.27 booted at manual_brightness 55 and
# every reading came in at 0.55 of the 2026-09-13 reference, which failed an
# absolute band while the panel was displaying perfectly.  The dimmer also
# restarts on every boot, so a power-cycle test cannot simply turn it off.
#
# Ratios are immune to that, because the backlight scales everything equally.
# Measured at two very different brightnesses: blue/white = 129.4/1110.4 =
# 0.1166 at full brightness, 71.52/612.68 = 0.1167 at 55 %.  Bands are +-20 %
# of nominal, far wider than the panel drifts and far narrower than the gaps
# between the colours (blue 0.117, red 0.243, green 0.640).
#
# Chromaticity stays absolute: it does not move with the backlight at all.
# Black stays absolute too -- the Pi's black reads 0.000 at any brightness,
# where the BIST's darkest steps read 0.93 and 1.70 -- and its chromaticity is
# meaningless at zero, so only the level is checked.
#
# Both the chromaticities and the colour/white ratios are properties of the
# PANEL, so they are per-profile.  Measured 2026-09-14 on the OLED rig, the
# 15.6" LCD's numbers would have failed a perfectly healthy OTS-OLED on three of
# the four colours: green is 0.3014/0.6675 there against the LCD's 0.2229/0.7175
# (0.079 out in x, against a 0.03 tolerance), and the red and blue ratios are
# 0.169 and 0.062 against the LCD's 0.243 and 0.117.  An OLED's primaries are
# simply not an LCD's.
#
# $PANEL is the display type, detected once at startup from pi-config-txt.sh the
# way sync-video.sh does it, or forced with --panel=.  An unknown panel is a hard
# error rather than a silent fall-through to the wrong numbers.
#
# Fields: <kind> <lo> <hi> <x> <y>, kind = abs (nits) or ratio (of white).
pat_ref() {
    case "$PANEL" in
    ots-oled-17)
        # OTS-OLED 17", white 219.5 nits at the dimmer setting of 2026-09-14.
        case "$1" in
            white) echo "abs $PASS_NITS 100000 0.3004 0.3241" ;;
            red)   echo "ratio 0.135 0.202 0.6788 0.3212" ;;
            green) echo "ratio 0.596 0.895 0.3014 0.6675" ;;
            blue)  echo "ratio 0.050 0.074 0.1362 0.0509" ;;
            black) echo "abs 0 0.3 - -" ;;
            *)     echo "" ;;
        esac ;;
    *)
        # 15.6" 2K5 and anything else that measures like it.
        case "$1" in
            white) echo "abs $PASS_NITS 100000 0.3050 0.3301" ;;
            red)   echo "ratio 0.194 0.292 0.6852 0.3134" ;;
            green) echo "ratio 0.512 0.768 0.2229 0.7175" ;;
            blue)  echo "ratio 0.094 0.140 0.1428 0.0856" ;;
            black) echo "abs 0 0.3 - -" ;;
            *)     echo "" ;;
        esac ;;
    esac
}

# Ask the Pi which display it is configured for, the same call sync-video.sh
# makes.  Falls back to the 15.6 set with a warning rather than guessing.
detect_panel() {
    local t
    t=$(rsh 40 "$MICROBIN/pi-config-txt.sh --configspath=$MICROSHARE/configs/ --input=/boot/firmware/config.txt 2>/dev/null | tr -d '[:space:]'")
    case "$t" in
        ots-oled-17|15.6-2k5|12.3-nq1|14.6-2k5|17.3-3k|27|12.3|14.6-fhd|3x-qvue)
            PANEL=$t ;;
        *)
            say "  WARNING: could not read the display type (got '${t:-empty}'), using the 15.6-2k5 reference set"
            PANEL=15.6-2k5 ;;
    esac
}

# White luminance of the current verdict, the denominator for every ratio.
WHITE_REF=""

# Command white, measure it, and keep it as this verdict's reference.
#
# Doubles as the first real check: a panel that is black, or showing something
# that is not the Pi's white, fails here before any ratio is computed.
establish_white_ref() {
    local out y cx cy
    WHITE_REF=""
    out=$(measure_pattern white)
    y=$(echo  "$out" | awk '{print $1}')
    cx=$(echo "$out" | awk '{print $2}')
    cy=$(echo "$out" | awk '{print $3}')
    if is_number "$y" && pattern_ok white "$y" "$cx" "$cy"; then
        WHITE_REF=$y
        SEQ_LOG="$SEQ_LOG white-ref=ok(${y})"
        return 0
    fi
    if [ "$RETRY_SETTLE" -gt 0 ]; then
        SEQ_LOG="$SEQ_LOG white-ref=retry(Y=${y:-?} x=${cx:-?} y=${cy:-?})"
        sleep "$RETRY_SETTLE"
        out=$(measure_only)
        y=$(echo  "$out" | awk '{print $1}')
        cx=$(echo "$out" | awk '{print $2}')
        cy=$(echo "$out" | awk '{print $3}')
        if is_number "$y" && pattern_ok white "$y" "$cx" "$cy"; then
            WHITE_REF=$y
            SEQ_LOG="$SEQ_LOG white-ref=ok-after-${RETRY_SETTLE}s(${y})"
            return 0
        fi
    fi
    if ! is_number "$y"; then
        SEQ_LOG="$SEQ_LOG white-ref=SENSOR-ERROR(${y:-empty})"
        SEQ_SENSOR_ERR=1
    else
        SEQ_LOG="$SEQ_LOG white-ref=MISMATCH(Y=${y:-?} x=${cx:-?} y=${cy:-?})"
    fi
    return 1
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
    local name=$1 y=$2 cx=$3 cy=$4 ref kind ymin ymax rx ry
    ref=$(pat_ref "$name")
    [ -z "$ref" ] && return 1
    kind=$(echo "$ref" | cut -d\  -f1)
    ymin=$(echo "$ref" | cut -d\  -f2); ymax=$(echo "$ref" | cut -d\  -f3)
    rx=$(echo "$ref" | cut -d\  -f4);   ry=$(echo "$ref" | cut -d\  -f5)
    if [ "$kind" = "ratio" ]; then
        # No white reference yet means the verdict has nothing to compare
        # against; treat that as a failure rather than silently passing.
        [ -z "$WHITE_REF" ] && return 1
        awk -v v="$y" -v w="$WHITE_REF" -v a="$ymin" -v b="$ymax" \
            'BEGIN{if (w+0 <= 0) exit 1; r=(v+0)/(w+0); exit !(r>=a+0 && r<=b+0)}' || return 1
    else
        awk -v v="$y" -v a="$ymin" -v b="$ymax" 'BEGIN{exit !(v+0>=a+0 && v+0<=b+0)}' || return 1
    fi
    [ "$rx" = "-" ] && return 0
    awk -v v="$cx" -v r="$rx" -v t="$XY_TOL" 'BEGIN{d=v-r; if(d<0)d=-d; exit !(d<=t+0)}' || return 1
    awk -v v="$cy" -v r="$ry" -v t="$XY_TOL" 'BEGIN{d=v-r; if(d<0)d=-d; exit !(d<=t+0)}' || return 1
    return 0
}

# White first and last - the brightness criterion lives on white - with the
# rest shuffled in between.  The BIST cycle runs white -> red -> green -> blue,
# which is exactly the fixed order it could imitate by accident, so the order
# is drawn fresh every time.
# The leading white is establish_white_ref()'s measurement, so the sequence
# itself is the four others in a fresh order plus a closing white.
verdict_sequence() { echo "$(printf '%s\n' black red green blue | shuf | tr '\n' ' ')white"; }
soak_sequence()    { echo "$(printf '%s\n' black red green blue | shuf -n 1)"; }

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
    establish_white_ref || return 1
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
    establish_white_ref || return 1
    for p in $1; do
        check_one "$p" "$p" "$(measure_pattern "$p")" || rc=1
    done
    return $rc
}

wedge_count()  { rsh 30 'cat /sys/module/hh983_serializer/parameters/dtg_wedge_count 2>/dev/null || echo NA'; }

# Wedges the boot/resync restore path found -- the ones dtg_wedge_count never
# counted.  "NA" on a module that predates the counter.
boot_wedge_count() { rsh 30 'cat /sys/module/hh983_serializer/parameters/dtg_boot_wedge_count 2>/dev/null || echo NA'; }

# OTS-OLED IOC status at 0x66.  0x1008 bit 5 is TCON_INT: set and staying set is
# the panel's latched-black signature, the thing a healthy recovery must leave
# clear.  0x1009 counts TCON_INT rising edges since the MCU booted and is
# informational only -- a 984 digital reset produces an edge that self-clears
# well inside a second (measured 2026-09-14: five resets, four edges, bit 5
# never observed set), so this number climbing is normal after a recovery.
# Both print n/a on a rig with no IOC there, which is every non-OLED rig.
ioc_1008() { rsh 30 'sudo i2ctransfer -y -f 1 w2@0x66 0x10 0x08 r1@0x66 2>/dev/null || echo n/a'; }
ioc_1009() { rsh 30 'sudo i2ctransfer -y -f 1 w2@0x66 0x10 0x09 r1@0x66 2>/dev/null || echo n/a'; }
tcon_latched() {
    case "$1" in
        n/a|"") return 1 ;;
        *) [ $(( $1 & 0x20 )) -ne 0 ] ;;
    esac
}

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
        echo "=== dtg_boot_wedge_count"
        boot_wedge_count
        echo ""
        echo "=== wedge_recovery"
        rsh 30 'cat /sys/module/hh983_serializer/parameters/wedge_recovery 2>/dev/null'
        echo ""
        echo "=== IOC 0x1008 (bit 5 TCON_INT = latched black) and 0x1009 (rising edges)"
        echo "0x1008=$(ioc_1008)  0x1009=$(ioc_1009)"
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
logf "# cycle,timestamp,uptime_s,wedge_count,boot_wedge_count,dmesg_wedge_lines,boot_htotal,wedged_at_boot,recovery,ioc_1008,ioc_1009,measured_sequence,result"

if [ -z "$PANEL" ]; then
    detect_panel
fi
say "panel reference set: $PANEL"

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
    elif ! power_cycle; then
        fail_dump "$cycle" "" "the Tasmota switch at $TASMOTA did not answer -- the rig is probably unpowered, this says nothing about the display"
        exit 1
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
    bwc=$(boot_wedge_count)
    i8=$(ioc_1008)
    i9=$(ioc_1009)
    say "  boot: DTG measured $bh, wedged=$bw, recovery=$rec, boot-wedges=$bwc, IOC 0x1008=$i8 0x1009=$i9"
    if tcon_latched "$i8"; then
        logf "$cycle,$(date -Is),$up_now,$wc_now,$bwc,$wl_now,$bh,$bw,$rec,$i8,$i9,\"$flat\",FAIL-TCON"
        fail_dump "$cycle" "commanded: $seq_list${NL}measured:$flat" \
                  "IOC 0x1008=$i8 -- TCON_INT is set, the OLED latched-black signature"
        exit 1
    fi

    if [ "$seq_rc" != "0" ] && [ "$SEQ_SENSOR_ERR" = "1" ]; then
        say "  colorimeter did not answer; re-running the verdict once"
        if [ "$VERDICT_MODE" = "hold" ]; then check_hold; seq_rc=$?
        else check_sequence "$seq_list"; seq_rc=$?; fi
        flat=$SEQ_LOG
    fi
    if [ "$seq_rc" != "0" ]; then
        if [ "$SEQ_SENSOR_ERR" = "1" ]; then
            logf "$cycle,$(date -Is),$up_now,$wc_now,$bwc,$wl_now,$bh,$bw,$rec,$i8,$i9,\"$flat\",FAIL-SENSOR"
            fail_dump "$cycle" "commanded: $seq_list${NL}measured:$flat" \
                      "the colorimeter would not return a reading - this says nothing about the panel, re-run"
        else
            logf "$cycle,$(date -Is),$up_now,$wc_now,$bwc,$wl_now,$bh,$bw,$rec,$i8,$i9,\"$flat\",FAIL"
            fail_dump "$cycle" "commanded: $seq_list${NL}measured:$flat" \
                      "the panel did not follow the commanded patterns:$flat"
        fi
        exit 1
    fi
    if [ "$wc_now" != "0" ]; then
        logf "$cycle,$(date -Is),$up_now,$wc_now,$bwc,$wl_now,$bh,$bw,$rec,$i8,$i9,\"$flat\",FAIL-WEDGE"
        fail_dump "$cycle" "$flat" "panel followed every pattern but dtg_wedge_count=$wc_now (the guard still fired)"
        exit 1
    fi
    say "  cycle $cycle verdict: PASS $flat  wedges=$wc_now boot-wedges=$bwc recovery=$rec IOC=$i8"

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
            s_i8=$(ioc_1008)
            logf "  soak $cycle $(date +%H:%M:%S)$SEQ_LOG wedges=$(wedge_count) boot-wedges=$(boot_wedge_count) IOC 0x1008=$s_i8 0x1009=$(ioc_1009)"
            if tcon_latched "$s_i8"; then
                fail_dump "$cycle" "soak${NL}measured:$SEQ_LOG" \
                          "soak: IOC 0x1008=$s_i8 -- TCON_INT is set, the OLED latched-black signature"
                exit 1
            fi
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

    logf "$cycle,$(date -Is),$up_now,$wc_now,$bwc,$wl_now,$bh,$bw,$rec,$i8,$i9,\"$flat\",PASS"
    stop_pattern
    cycle=$((cycle + 1))
done

say ""
say "=== $CYCLES/$CYCLES cycles passed, dtg_wedge_count 0 at the end of every one"
say "rig left on the desktop with the pattern generator stopped"
exit 0
