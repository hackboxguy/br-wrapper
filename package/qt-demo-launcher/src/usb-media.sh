#!/bin/sh
# usb-media.sh - USB Media: pick images and videos on the USB stick, play
# them as a playlist, mirrored on both HDMI outputs
#
# Loop: usb-media-app (build/edit the playlist, stored on the stick as
# micropanel-playlist.json) -> Play -> dual-video-player --playlist ->
# back to the app with the player's exit reason -> ... until Back.
#
#   app exit 10  play: the stick's playlist, or (read-only stick) the copy
#                the app wrote to $TMP_PLAYLIST, items resolved on the stick
#   app exit 0   leave (back to the launcher)
#   player exit  0 done / EXIT, 1 nothing playable, 3 stick removed
#
# Launched by qt-demo-launcher as its tracked child; the launcher's stop-app
# (SIGTERM) is forwarded to whichever child runs, so it stops in time.
#
# --check: play nothing; exit 0 if a stick is there, else print the reason on
# one line and exit 1 (the button's available_command: dims the tile).

CHECK=0
[ "$1" = "--check" ] && CHECK=1

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$SCRIPT_DIR/../../bin"
PLAYER="$BIN_DIR/dual-video-player"
[ -x "$PLAYER" ] || PLAYER="$(command -v dual-video-player)"
APP="$BIN_DIR/usb-media-app"
[ -x "$APP" ] || APP="$(command -v usb-media-app)"
TMP_PLAYLIST=/tmp/usb-media-playlist.json

. "$SCRIPT_DIR/kodi-usb-common.sh"

unavailable() {
    if [ "$CHECK" = 1 ]; then echo "$1"; else echo "usb-media: $1" >&2; fi
    exit 1
}

find_stick() {
    root=$(detect_usb_media_path ".") || return 1
    echo "${root%/.}"
}

usb_root=$(find_stick) || unavailable "Insert a USB stick with pictures or videos"
[ -n "$PLAYER" ] || unavailable "dual-video-player not installed"
[ -n "$APP" ] || unavailable "usb-media-app not installed"
[ "$CHECK" = 1 ] && exit 0

child=""
stop=0
trap 'stop=1; [ -n "$child" ] && kill -TERM "$child" 2>/dev/null' TERM INT

# run CMD...: run it in the background so the trap can forward SIGTERM;
# returns its exit code
run() {
    "$@" &
    child=$!
    while :; do
        wait "$child"
        rc=$?
        kill -0 "$child" 2>/dev/null || break   # really gone (not just a trap)
    done
    child=""
    return "$rc"
}

message=""
while [ "$stop" = 0 ]; do
    rm -f "$TMP_PLAYLIST"
    if [ -n "$message" ]; then
        run "$APP" --root "$usb_root" --player "$PLAYER" --temp-playlist "$TMP_PLAYLIST" --message "$message"
    else
        run "$APP" --root "$usb_root" --player "$PLAYER" --temp-playlist "$TMP_PLAYLIST"
    fi
    rc=$?
    [ "$stop" = 0 ] && [ "$rc" = 10 ] || break

    if [ -s "$TMP_PLAYLIST" ]; then
        run "$PLAYER" --playlist "$TMP_PLAYLIST" --root="$usb_root"
    else
        run "$PLAYER" --playlist "$usb_root/micropanel-playlist.json"
    fi
    rc=$?
    [ "$stop" = 0 ] || break
    case "$rc" in
        0) message="" ;;
        1) message="Nothing in the playlist could be played" ;;
        3) message="The USB stick was removed during playback" ;;
        *) message="Playback stopped (error $rc)" ;;
    esac
    if [ "$rc" = 3 ]; then
        # the stick may be back (or another one); without one, leave
        sleep 1
        usb_root=$(find_stick) || break
    fi
done
rm -f "$TMP_PLAYLIST"
exit 0
