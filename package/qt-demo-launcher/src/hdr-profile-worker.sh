#!/bin/sh
# The launcher's "HDR Display Profile" measurement: one display profile (sdr-hdr-shootout-demo's
# display-profile/1) of the connected display, at 100 % backlight (or the gate's Current-Brightness)
# in the local-dimming state the display is in. Started by hdr-profile-start-child.sh once the sensor
# locator has put the meter on the centre (SENSOR_LOCATION); detached from disp-tester.
#
#  1. detect the display (hdr-profile-common.sh: model, type, serial, local dimming), take the lock;
#  2. run display-test-framework's run-display-profile.sh as root (systemd-run --unit=display-profile,
#     as plan K.2 / section "17" OLED-IB" made the two real profiles), with the options of the type:
#       oled                  --display-type=oled --backlight=BL --warmup-min=20
#       localdimming, dim on  --backlight=BL --local-dimming=on --warmup-min=20 --warmup-pattern=grey:124
#                             (tests: gamut gamma-window gamma-window-small abl gamma apl in-scene-black)
#       localdimming, dim off --backlight=BL --local-dimming=off --warmup-min=20
#     progress (warm-up minute, test i of n) on the disp-tester overlay; the gate's stop button cancels
#     (systemctl stop: the runner's trap restores backlight, local dimming, adaptive LD, als-dimmer);
#  3. make-display-profile.py on the session: <model>-<serial>-b<BL>[-ld<on|off>]-<date>.json with the
#     image's player code offset (hdr-profile-common.sh, measured with tools/player-offset.py: a new
#     dual-video-player build needs a new value), the oled subpixel layout from the framework config;
#  4. a report PNG (disp-report-card --display-profile) and the gallery on it.
#
# Files (serial first, as the reports):  PROFILE_ROOT (/home/pi/display-profiles)/
#   <serial>_profile-<date>-<time>.png            the card touch-gallery shows
#   <serial>_profile-<date>-<time>-data/          what View Profiles' USB copy takes with the PNG:
#       <id>.json  session/  make-display-profile.txt  validate.txt  hdr-profile.log
# Exit 0 = profile written; 1 = failed or cancelled (state restored by the runner); 0 also when another
# measurement holds the lock.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/hdr-profile-common.sh"

LAUNCHER_ADDR="${LAUNCHER_ADDR:-127.0.0.1:8081}"
GALLERY_ADDR="${GALLERY_ADDR:-127.0.0.1:8086}"
GALLERY_APP_ID="${HDR_PROFILE_GALLERY_APP_ID:-hdr-profile-gallery}"
TIMEOUT="${HDR_PROFILE_TIMEOUT:-10}"
DA_DISP_ADDR="${PATTERN_GEN_ADDR:-127.0.0.1:8082}" DA_CMD_TIMEOUT=2 DA_CMD_RETRIES=2 DA_CMD_GAP=0.15
UNIT="${HDR_PROFILE_UNIT:-display-profile}"
REPORT_CARD="${DISPLAY_REPORT_CARD:-$MICROPANEL_HOME/lib/disp-report-card/display_report_card.py}"
TITLE="HDR Display Profile"
# make-display-profile.py (sdr-hdr-shootout-demo tools/): the image does not ship it yet; first found wins
find_creator() {
    for c in "${HDR_PROFILE_CREATOR:-}" "$MICROPANEL_HOME/share/display-profile/make-display-profile.py" \
             /data/display-profile-tools/make-display-profile.py /home/pi/sdr-hdr-demo/tools/make-display-profile.py; do
        [ -n "$c" ] && [ -f "$c" ] && echo "$c" && return 0
    done
    return 1
}

LAUNCHER_CLIENT_BIN="$(detect_launcher_client)"
mkdir -p "$HP_STATE_DIR" "$PROFILE_ROOT" 2>/dev/null
if ! mkdir "$HP_LOCK_DIR" 2>/dev/null; then
    if hp_running; then
        echo "$(date '+%F %T') hdr-profile: another measurement is running" >&2
        exit 0
    fi
    rm -rf "$HP_LOCK_DIR"; mkdir "$HP_LOCK_DIR" || exit 1   # a stale lock (the worker died)
fi
echo "$$" > "$HP_LOCK_DIR/pid"

hp_detect
BL="$(hp_backlight)"
STAMP="$(date +%Y%m%d-%H%M%S)"
STEM="${HP_SERIAL:+${HP_SERIAL}_}profile-$STAMP"
DATA="$PROFILE_ROOT/$STEM-data"
PNG="$PROFILE_ROOT/$STEM.png"
SESSION="$DATA/session"
LD_PART=""
[ "$HP_TYPE" = localdimming ] && LD_PART="-ld$HP_LD"
ID="$(echo "$HP_MODEL-${HP_SERIAL:-noserial}-b$BL$LD_PART-$(date +%Y%m%d)" | tr 'A-Z' 'a-z')"
PROFILE="$DATA/$ID.json"
mkdir -p "$DATA"
LOG_FILE="$DATA/hdr-profile.log"
DONE=0 CANCELLED=0 FAIL_TEXT=""

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG_FILE"
}

say() {   # say <colour> <text>: the gate's disp-tester overlay (bottom right, away from the sensor)
    disp_cmd set-metadata-status enable
    disp_cmd set-metadata-fontsize 24
    disp_cmd set-metadata-color "$1"
    disp_cmd set-metadata-text "$2"
}

cleanup() {
    status=$?
    if [ "$DONE" -ne 1 ]; then
        log "ended without a profile: ${FAIL_TEXT:-exit $status}"
        say red "$TITLE\\n${FAIL_TEXT:-Failed (exit $status)}\\nlog: $LOG_FILE" || true
        disp_cmd set-child-action-active disable || true
        status=1
    fi
    rm -f "$HP_LOCK_DIR/pid" 2>/dev/null
    rmdir "$HP_LOCK_DIR" 2>/dev/null
    exit "$status"
}
trap cleanup EXIT
trap 'FAIL_TEXT="Stopped (signal)"; exit 1' INT TERM

fail() {
    FAIL_TEXT="$1"
    exit 1
}

log "HDR display profile: model=$HP_MODEL type=$HP_TYPE serial=${HP_SERIAL:-none} local_dimming=$HP_LD backlight=$BL% id=$ID"
log "sensor location: ${SENSOR_LOCATION:-none} $(jq -c '{sensor_px, offset_mm, footprint_px}' "${SENSOR_LOCATION:-/dev/null}" 2>/dev/null)"
[ -z "$HP_UNSUPPORTED" ] || fail "$HP_UNSUPPORTED"
CREATOR="$(find_creator)" || fail "make-display-profile.py not found (HDR_PROFILE_CREATOR)"
[ -x "$FRAMEWORK_DIR/runners/run-display-profile.sh" ] || fail "run-display-profile.sh not found in $FRAMEWORK_DIR"

case "$HP_TYPE" in
    oled)         set -- --display-type=oled --backlight="$BL" --warmup-min=20 ;;
    localdimming) set -- --backlight="$BL" --local-dimming="$HP_LD" --warmup-min=20
                  [ "$HP_LD" = on ] && set -- "$@" --warmup-pattern=grey:124 ;;
    *)            fail "type $HP_TYPE: not yet" ;;
esac
[ -n "${SENSOR_LOCATION:-}" ] && [ -f "$SENSOR_LOCATION" ] && set -- "$@" --sensor-location="$SENSOR_LOCATION"
set -- "$@" --output="$SESSION"
TESTS_ALL="gamut gamma apl"
[ "$HP_TYPE" = oled ] && TESTS_ALL="gamut gamma apl abl"
[ "$HP_LD" = on ] && TESTS_ALL="gamut gamma-window gamma-window-small abl gamma apl in-scene-black"
N_TESTS=$(echo "$TESTS_ALL" | wc -w)

# ---------------------------------------------------------------- the session (root, own unit)
log "runner: $FRAMEWORK_DIR/runners/run-display-profile.sh $*"
sudo -n systemctl reset-failed "$UNIT" >/dev/null 2>&1
sudo -n systemctl stop "$UNIT" >/dev/null 2>&1
sudo -n systemd-run --wait --collect --quiet --unit="$UNIT" --working-directory="$FRAMEWORK_DIR" \
    -E DISPLAY_MODEL="$HP_MODEL" -E DISPLAY_SERIAL="$HP_SERIAL" -E DISPLAY_ANALYSIS_KEEP_PATTERN_APP=1 \
    -E LAUNCHER_CLIENT="$LAUNCHER_CLIENT_BIN" -E FRAMEWORK_GIT="${FRAMEWORK_GIT:-installed}" \
    "$FRAMEWORK_DIR/runners/run-display-profile.sh" "$@" >> "$LOG_FILE" 2>&1 &
RUN_PID=$!
T0=$(date +%s)
last=""
while kill -0 "$RUN_PID" 2>/dev/null; do
    if [ -f "$HP_STATE_DIR/cancel" ] && [ "$CANCELLED" -eq 0 ]; then
        CANCELLED=1
        log "cancel: stopping $UNIT (the runner restores the display state)"
        say yellow "$TITLE\\nCancelling - restoring the display state..."
        sudo -n systemctl stop "$UNIT" >> "$LOG_FILE" 2>&1
    fi
    line="$(grep -E 'warm-up minute|test [a-z-]+ \(attempt|ERROR' "$SESSION/session.log" 2>/dev/null | tail -1)"
    if [ -n "$line" ] && [ "$line" != "$last" ] && [ "$CANCELLED" -eq 0 ]; then
        last="$line"
        el=$(( ($(date +%s) - T0) / 60 ))
        case "$line" in
            *"warm-up minute"*)
                m=$(echo "$line" | sed -n -E 's/.*warm-up minute ([0-9]+):(.*)/\1/p')
                detail=$(echo "$line" | sed -n -E 's/.*warm-up minute [0-9]+:[[:space:]]*(.*)/\1/p')
                say white "$TITLE  ($HP_MODEL, ${HP_SERIAL:-no serial})\\nWarm-up minute $m: $detail\\n(20 min, then until the panel is stable)\\nthen $N_TESTS tests - ${el} min so far" ;;
            *"test "*)
                t=$(echo "$line" | sed -n -E 's/.*test ([a-z-]+) \(attempt.*/\1/p')
                i=0 n=0
                for x in $TESTS_ALL; do n=$((n + 1)); [ "$x" = "$t" ] && i=$n; done
                say white "$TITLE  ($HP_MODEL, ${HP_SERIAL:-no serial})\\nTest $i of $N_TESTS: $t\\n${el} min so far" ;;
            *) log "runner: $line" ;;
        esac
    fi
    sleep 10
done
wait "$RUN_PID"
RUN_RC=$?
# the runner ran as root: hand its folder back, so the profile folder can be moved and deleted
sudo -n chown -R "$(id -u):$(id -g)" "$SESSION" 2>/dev/null
log "runner exited $RUN_RC after $(( ($(date +%s) - T0) / 60 )) min"
if [ "$CANCELLED" -eq 1 ]; then
    rs="$(jq -r '.restore | "local dimming \(.local_dimming // "-"), backlight \(.backlight // "-")"' "$SESSION/session.json" 2>/dev/null)"
    fail "Cancelled; display state restored (${rs:-see the session log})"
fi
[ -f "$SESSION/session.json" ] || fail "the session wrote no session.json (runner exit $RUN_RC)"
# 1 = a test failed or the state drifted (data kept): the creator decides; 2 / 3 = usage / state not set
case "$RUN_RC" in 0|1) ;; *) fail "the runner stopped (exit $RUN_RC): $(grep -m1 ERROR "$SESSION/session.log" 2>/dev/null)" ;; esac

# runner exit 1: which test, and why (a test exits non-zero also when it wrote its data: the gamma test's
# LCD fit criterion on an OLED, one failed i1 read in the abl table) - one line each for the card
runner_problems() {
    python3 - "$SESSION" <<'PY'
import glob, json, os, sys
d = sys.argv[1]
s = json.load(open(os.path.join(d, "session.json")))
for t in s.get("tests", []):
    if t.get("exit_code") in (0, None):
        continue
    name, folder, why = t["name"], os.path.join(d, t.get("folder") or t["name"]), []
    try:
        if name == "abl":
            rows = json.load(open(os.path.join(folder, "abl-gain.json")))["rows"]
            bad = [f"code {r['window_code']} on {r['surround_code']}" for r in rows if r.get("Y") is None]
            why.append(f"{len(bad)} of {len(rows)} reads failed ({', '.join(bad[:3])}); the table uses the rest")
        for f in glob.glob(os.path.join(folder, "raw", "test-*.json")):
            r = json.load(open(f))
            why += [f"{c['name']} {c['actual']} (wants {c['expected']})" for c in
                    (r.get("validation") or {}).get("checks", []) if c.get("result") == "FAIL"]
            why += [e for e in r.get("errors") or [] if e]
    except (OSError, ValueError, KeyError) as ex:
        why.append(f"no detail ({ex.__class__.__name__})")
    print(f"test {name} exited {t['exit_code']}: " + ("; ".join(why)[:200] or "see session/session.log"))
if s.get("state_verified") is False or any(t.get("state_after") == "changed" for t in s.get("tests", [])):
    print("the display state changed during the session: see session/session.log")
PY
}

# ---------------------------------------------------------------- the profile
say white "$TITLE\\nCreating the profile..."
set -- --session="$SESSION" --type="$HP_TYPE" --id="$ID" --backlight="$BL" --player-code-offset="$HP_OFFSET" \
    --signal-path="$HP_SIGNAL_PATH" --meter="i1 Display Pro" --output="$PROFILE"
[ -n "$HP_SERIAL" ] && set -- "$@" --serial="$HP_SERIAL"
[ "$HP_TYPE" = localdimming ] && set -- "$@" --local-dimming="$HP_LD"
[ "$HP_TYPE" = oled ] && set -- "$@" --subpixel="$HP_SUBPIXEL"
log "creator: $CREATOR $*"
python3 "$CREATOR" "$@" > "$DATA/make-display-profile.txt" 2>&1
CRC=$?
[ "$CRC" -eq 0 ] && [ -f "$PROFILE" ] \
    || fail "make-display-profile.py failed: $(grep -m1 -i error "$DATA/make-display-profile.txt" | cut -c1-120)"
python3 "$CREATOR" --validate="$PROFILE" > "$DATA/validate.txt" 2>&1
grep -E '^[[:space:]]*assumed:' "$DATA/validate.txt" | sed 's/^[[:space:]]*//' > "$HP_STATE_DIR/card-notes.txt"
[ "$RUN_RC" -eq 0 ] || runner_problems >> "$HP_STATE_DIR/card-notes.txt"
log "profile: $PROFILE"

python3 "$REPORT_CARD" --display-profile="$PROFILE" --output="$PNG" --notes-file="$HP_STATE_DIR/card-notes.txt" >> "$LOG_FILE" 2>&1 \
    || fail "the report card failed (disp-report-card with --display-profile?)"
DONE=1
log "card: $PNG"

# ---------------------------------------------------------------- the gallery on the new card
"$LAUNCHER_CLIENT_BIN" --srv="$LAUNCHER_ADDR" --command=stop-app --timeoutsec="$TIMEOUT" >/dev/null 2>&1
sleep 0.5
r="$("$LAUNCHER_CLIENT_BIN" --srv="$LAUNCHER_ADDR" --command=start-app --command-arg="$GALLERY_APP_ID" --timeoutsec="$TIMEOUT" 2>&1)"
echo "$r" | grep -q OK || { log "gallery app '$GALLERY_APP_ID' did not start: $r"; exit 0; }
i=0
while [ "$i" -lt "$TIMEOUT" ] && ! "$LAUNCHER_CLIENT_BIN" --srv="$GALLERY_ADDR" --command=get-count --timeoutsec=2 >/dev/null 2>&1; do
    i=$((i + 1)); sleep 1
done
sleep 1
"$LAUNCHER_CLIENT_BIN" --srv="$GALLERY_ADDR" --command=display --command-arg="$PNG" --timeoutsec="$TIMEOUT" >> "$LOG_FILE" 2>&1
log "done"
exit 0
