#!/bin/sh
# shellcheck disable=SC2016 # the checks are eval'd strings, expanded when run
# Host test for quick-video.sh: which video it plays, in what order, what the
# player's probe rejects, the next-video index and the one-item playlist.
# The stick is a temp directory (device detection and mount stubbed), the
# player a stub whose probe rejects files named *bad*.
# Run: sh tests/test-quick-video.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
T="$(mktemp -d)/my stick"; mkdir -p "$T"; trap 'rm -rf "${T%/*}"' EXIT   # a space in the path
fail=0 pass=0
check() { if eval "$2"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAILED: $1 (got '$got')"; fi; }

mkdir -p "$T/app" "$T/stick" "$T/state"
cp "$HERE/../src/quick-video.sh" "$T/app/"
cat "$HERE/../src/kodi-usb-common.sh" > "$T/app/kodi-usb-common.sh"
cat >> "$T/app/kodi-usb-common.sh" <<EOF
_detect_usb_device() { [ -e "$T/plugged" ] && echo /dev/sdz1; }
_get_usb_mount() { echo "$T/stick"; }
EOF
cat > "$T/player" <<'EOF'
#!/bin/sh
if [ "$1" = --probe ]; then
    shift
    for f; do
        case "$f" in *bad*) r="3840x1440 too large (max 2560x1600)" ;; *) r=- ;; esac
        printf 'PROBE\tkind=video\treason=%s\tfile=%s\n' "$r" "$f"
    done
    exit 0
fi
echo "$@" > "$QUICK_VIDEO_STATE_DIR/played"
EOF
chmod +x "$T/player"
export QUICK_VIDEO_PLAYER="$T/player" QUICK_VIDEO_STATE_DIR="$T/state"
QV="$T/app/quick-video.sh"

got=$(sh "$QV" --check); check "no stick: unavailable" '[ "$got" = "Insert a USB stick with a video" ]'
: > "$T/plugged"
got=$(sh "$QV" --check); check "an empty stick: unavailable" '[ "$got" = "No video on the USB stick" ]'
: > "$T/stick/dual-player-video-1.mp4"; : > "$T/stick/DUAL-PLAYER-VIDEO-2.MP4"; : > "$T/stick/notes.txt"
got=$(sh "$QV" --check); check "only the Dual Video pair: unavailable" '[ -n "$got" ]'
: > "$T/stick/b-top.mp4"; : > "$T/stick/a-bad-4k.mp4"; : > "$T/stick/._b-top.mp4"
mkdir -p "$T/stick/Videos"; : > "$T/stick/Videos/z-clip.mkv"; : > "$T/stick/Videos/c-clip.MP4"
sh "$QV" --check >/dev/null; got=$?; check "a video: available" '[ "$got" = 0 ]'
got=$(sh "$QV" --list | sed "s|$T/stick/||" | tr '\n' ' ')
check "Videos/ first, sorted, unplayable/pair/dotfiles left out" '[ "$got" = "Videos/c-clip.MP4 Videos/z-clip.mkv b-top.mp4 " ]'

sh "$QV" 2>/dev/null
got=$(cat "$T/state/played"); check "first press: the first video" '[ "$got" = "--playlist $T/state/quick-video-playlist.json --root=$T/stick/Videos" ]'
got=$(cat "$T/state/quick-video-playlist.json"); check "one-item looping playlist" '[ "$got" = "{ \"version\": 1, \"loop\": true, \"items\": [\"c-clip.MP4\"] }" ]'
got=$(cut -f1,3 "$T/state/quick-video.current" | tr '\t' ' '); check "current: index and count" '[ "$got" = "0 3" ]'
echo 2 > "$T/state/quick-video.next"; sh "$QV" 2>/dev/null
got=$(cut -f2 "$T/state/quick-video.current"); check "next = 2: the top-level clip" '[ "$got" = "$T/stick/b-top.mp4" ]'
check "next is used once" '[ ! -e "$T/state/quick-video.next" ]'
echo 3 > "$T/state/quick-video.next"; sh "$QV" 2>/dev/null
got=$(cut -f1 "$T/state/quick-video.current"); check "past the last: wraps to the first" '[ "$got" = 0 ]'
rm -f "$T/stick/b-top.mp4" "$T/stick/Videos/"*; rm -f "$T/state/played"
sh "$QV" 2>/dev/null; got=$?; check "nothing playable: fails" '[ "$got" = 1 ] && [ ! -e "$T/state/played" ]'

echo "test-quick-video: $pass passed, $fail failed"
[ "$fail" = 0 ]
