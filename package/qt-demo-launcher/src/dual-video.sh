#!/bin/sh
# dual-video.sh - Play two videos in sync, one per HDMI output
#
# Looks for dual-player-video-1.mp4 and dual-player-video-2.mp4 (case-
# insensitive) on the USB stick, first in its root, then in Videos/, and
# plays them with dual-video-player (GStreamer, V4L2 hardware decode,
# both outputs driven from one pipeline clock, seamless loop).
#
# Files must be H.264 MP4, max 1920x1080 (Pi4 hardware decoder limit).
# With one display connected only video 1 is shown.
#
# Launched by qt-demo-launcher; exec's the player so the launcher's
# stop-app (SIGTERM) reaches it directly.
#
# --check: play nothing; exit 0 if it could play, else print the reason on
# one line and exit 1. The launcher runs this as the button's
# available_command and dims the button (reason as its subtitle) while the
# clips are not there.

CHECK=0
[ "$1" = "--check" ] && CHECK=1

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLAYER="$SCRIPT_DIR/../../bin/dual-video-player"
[ -x "$PLAYER" ] || PLAYER="$(command -v dual-video-player)"

. "$SCRIPT_DIR/kodi-usb-common.sh"

# Print the path of $2 in directory $1 (case-insensitive match)
find_file() {
    find "$1" -maxdepth 1 -type f -iname "$2" 2>/dev/null | head -1
}

# unavailable <reason>: --check prints the reason for the launcher tile
unavailable() {
    if [ "$CHECK" = 1 ]; then echo "$1"; else echo "dual-video: $1" >&2; fi
    exit 1
}

usb_root=$(detect_usb_media_path ".") || unavailable "Insert USB stick with the clips"

V1="" V2=""
for dir in "$usb_root" "$usb_root/Videos" "$usb_root/videos"; do
    [ -d "$dir" ] || continue
    v1=$(find_file "$dir" "dual-player-video-1.mp4")
    v2=$(find_file "$dir" "dual-player-video-2.mp4")
    if [ -n "$v1" ] && [ -n "$v2" ]; then
        V1="$v1" V2="$v2"
        break
    fi
done

[ -n "$V1" ] || unavailable "No dual-player-video-1/2.mp4 on USB stick"
[ -n "$PLAYER" ] || unavailable "dual-video-player not installed"
[ "$CHECK" = 1 ] && exit 0

exec "$PLAYER" "$V1" "$V2"
