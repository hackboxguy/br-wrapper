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

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLAYER="$SCRIPT_DIR/../../bin/dual-video-player"
[ -x "$PLAYER" ] || PLAYER="$(command -v dual-video-player)"

. "$SCRIPT_DIR/kodi-usb-common.sh"

# Print the path of $2 in directory $1 (case-insensitive match)
find_file() {
    find "$1" -maxdepth 1 -type f -iname "$2" 2>/dev/null | head -1
}

usb_root=$(detect_usb_media_path ".") || {
    echo "dual-video: no USB stick found" >&2
    exit 1
}

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

if [ -z "$V1" ]; then
    echo "dual-video: dual-player-video-1.mp4 / -2.mp4 not found in USB root or Videos/" >&2
    exit 1
fi

if [ -z "$PLAYER" ]; then
    echo "dual-video: dual-video-player not installed" >&2
    exit 1
fi

exec "$PLAYER" "$V1" "$V2"
