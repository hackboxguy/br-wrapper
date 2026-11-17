#!/bin/sh
# quick-video.sh - Quick Video: a video from the USB stick straight into
# dual-video-player (GStreamer, V4L2 hardware decode), no Kodi: it plays about
# half a second after the start instead of Kodi's 7-8 s. The same picture on
# every HDMI output, looping. Video only: the player has no sound.
#
# Candidates: the stick's Videos/ folder, then its top level, each sorted by
# name; the Dual Video pair (dual-player-video-1/2.mp4) is left out. The
# player's own probe decides what plays (H.264 / H.265 up to 2560x1600 on the
# Pi 4; a 4K clip, a damaged file or another codec is skipped).
#
#   quick-video.sh           play candidate N, N from $STATE_DIR/quick-video.next
#                            (read and removed; default 0, wraps around)
#   quick-video.sh --check   exit 0 if the stick holds a video file, else print
#                            the reason on one line and exit 1 (cheap: no
#                            probe; the launcher's available_command and the
#                            Stream Deck key's poll)
#   quick-video.sh --list    the playable candidates, one path per line
#
# While it plays, $STATE_DIR/quick-video.current holds "N<TAB>path<TAB>count";
# the Stream Deck's Quick Video key writes N+1 to quick-video.next and starts
# the entry again for the next video.
#
# A hidden launcher entry (quick-video); exec's the player so the launcher's
# stop-app (SIGTERM) reaches it directly.

MODE=play
case "${1:-}" in
    --check) MODE=check ;;
    --list) MODE=list ;;
esac

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLAYER="${QUICK_VIDEO_PLAYER:-$SCRIPT_DIR/../../bin/dual-video-player}"
[ -x "$PLAYER" ] || PLAYER="$(command -v dual-video-player)"
STATE_DIR="${QUICK_VIDEO_STATE_DIR:-/tmp}"
NEXT="$STATE_DIR/quick-video.next"
CURRENT="$STATE_DIR/quick-video.current"
PLAYLIST="$STATE_DIR/quick-video-playlist.json"

. "$SCRIPT_DIR/kodi-usb-common.sh"

unavailable() {
    if [ "$MODE" = play ]; then echo "quick-video: $1" >&2; else echo "$1"; fi
    exit 1
}

# the video files on the stick in play order (not probed)
videos_in() {
    find "$1" -maxdepth 1 -type f \( -iname '*.mp4' -o -iname '*.m4v' -o -iname '*.mov' \
        -o -iname '*.mkv' -o -iname '*.webm' -o -iname '*.ts' -o -iname '*.avi' \) \
        ! -iname 'dual-player-video-[12].mp4' ! -name '.*' 2>/dev/null | LC_ALL=C sort
}
candidates() {
    videos=$(find "$1" -mindepth 1 -maxdepth 1 -type d -iname videos 2>/dev/null | sort | head -n 1)
    [ -n "$videos" ] && videos_in "$videos"
    videos_in "$1"
}

# the candidates the player can play (its probe: reason "-")
playable() {
    list=$(candidates "$1")
    [ -n "$list" ] || return 0
    # one probe for all; file= is the last field
    echo "$list" | tr '\n' '\0' | (cd /tmp && xargs -0 "$PLAYER" --probe 2>/dev/null) |
        awk -F '\t' '{ ok = 0; f = ""
                       for (i = 2; i <= NF; i++) {
                           if ($i == "reason=-") ok = 1
                           if (substr($i, 1, 5) == "file=") f = substr($i, 6)
                       }
                       if (ok && f != "") print f }'
}

usb_root=$(detect_usb_mount) || unavailable "Insert a USB stick with a video"
[ -n "$PLAYER" ] || unavailable "dual-video-player not installed"

if [ "$MODE" = check ]; then
    [ -n "$(candidates "$usb_root" | head -n 1)" ] || unavailable "No video on the USB stick"
    exit 0
fi

list=$(playable "$usb_root")
if [ "$MODE" = list ]; then
    [ -n "$list" ] && echo "$list"
    exit 0
fi
[ -n "$list" ] || unavailable "no video on the USB stick that the player can play (H.264/H.265, max 2560x1600)"

count=$(echo "$list" | wc -l)
n=$(cat "$NEXT" 2>/dev/null); rm -f "$NEXT"
case "$n" in ''|*[!0-9]*) n=0 ;; esac
n=$((n % count))
file=$(echo "$list" | sed -n "$((n + 1))p")

# a one-item playlist, looping; the item relative to --root
name=$(basename "$file" | sed 's/\\/\\\\/g; s/"/\\"/g')
printf '{ "version": 1, "loop": true, "items": ["%s"] }\n' "$name" > "$PLAYLIST"
printf '%s\t%s\t%s\n' "$n" "$file" "$count" > "$CURRENT"
echo "quick-video: $((n + 1))/$count $file" >&2
exec "$PLAYER" --playlist "$PLAYLIST" --root="$(dirname "$file")"
