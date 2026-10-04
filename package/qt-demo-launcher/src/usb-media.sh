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
# --check            play nothing; exit 0 if a stick is there, else print the
#                    reason on one line and exit 1 (the button's
#                    available_command: dims the tile)
# --autostart-check  exit 0 if the stick's playlist asks for autostart and
#                    has something playable, else print why and exit 1
#                    (usb-media-autostart.sh, at boot)
# --autostart        the hidden usb-media-autostart launcher entry: countdown
#                    (a tap cancels into the app), then playback; while
#                    nobody has touched the unit, a failed playback (a display
#                    that stopped for a moment, ...) is retried
#                    AUTOSTART_RETRIES times in a row, AUTOSTART_RETRY_PAUSE s
#                    apart; a playback that ran AUTOSTART_HEALTHY_S seconds
#                    before failing refills the budget (it guards against a
#                    failure loop, not against hiccups weeks apart)

MODE=run
case "$1" in
    --check) MODE=check ;;
    --autostart-check) MODE=autostart-check ;;
    --autostart) MODE=autostart ;;
esac
CHECK=0
[ "$MODE" = check ] || [ "$MODE" = autostart-check ] && CHECK=1

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$SCRIPT_DIR/../../bin"
PLAYER="$BIN_DIR/dual-video-player"
[ -x "$PLAYER" ] || PLAYER="$(command -v dual-video-player)"
APP="$BIN_DIR/usb-media-app"
[ -x "$APP" ] || APP="$(command -v usb-media-app)"
TMP_PLAYLIST=/tmp/usb-media-playlist.json
PLAYLIST_NAME=micropanel-playlist.json
COUNTDOWN=${AUTOSTART_COUNTDOWN:-5}
RETRIES=${AUTOSTART_RETRIES:-3}
RETRY_PAUSE=${AUTOSTART_RETRY_PAUSE:-5}
HEALTHY_S=${AUTOSTART_HEALTHY_S:-60}

. "$SCRIPT_DIR/kodi-usb-common.sh"

unavailable() {
    if [ "$CHECK" = 1 ]; then echo "$1"; else echo "usb-media: $1" >&2; fi
    exit 1
}

find_stick() {
    root=$(detect_usb_media_path ".") || return 1
    echo "${root%/.}"
}

# wants_autostart <stick>: the playlist says autostart and something in it
# plays (the player's own parser and classifier decide: flag first, then only
# up to the first playable item); else print why
wants_autostart() {
    [ -f "$1/$PLAYLIST_NAME" ] || { echo "no $PLAYLIST_NAME on the stick"; return 1; }
    out=$("$PLAYER" --autostart-check --playlist "$1/$PLAYLIST_NAME" 2>&1)
    rc=$?
    [ "$rc" = 0 ] && return 0
    echo "$out" | sed 's/^dual-video-player: //' | head -n 1
    return 1
}

usb_root=$(find_stick) || unavailable "Insert a USB stick with pictures or videos"
[ -n "$PLAYER" ] || unavailable "dual-video-player not installed"
[ -n "$APP" ] || unavailable "usb-media-app not installed"
[ "$MODE" = check ] && exit 0
if [ "$MODE" = autostart-check ]; then
    why=$(wants_autostart "$usb_root") || unavailable "$why"
    exit 0
fi

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

play() {
    if [ -s "$TMP_PLAYLIST" ]; then
        run "$PLAYER" --playlist "$TMP_PLAYLIST" --root="$usb_root"
    else
        run "$PLAYER" --playlist "$usb_root/$PLAYLIST_NAME"
    fi
}

message_for() {
    case "$1" in
        0) echo "" ;;
        1) echo "Nothing in the playlist could be played" ;;
        3) echo "The USB stick was removed during playback" ;;
        *) echo "Playback stopped (error $1)" ;;
    esac
}

message=""
rm -f "$TMP_PLAYLIST"
if [ "$MODE" = autostart ]; then
    # Started at boot (usb-media-autostart.sh has just run the full check):
    # only the cheap flag test is repeated here, else the app
    if grep -Eq '"autostart"[[:space:]]*:[[:space:]]*true' "$usb_root/$PLAYLIST_NAME" 2>/dev/null; then
        run "$APP" --root "$usb_root" --player "$PLAYER" --countdown "$COUNTDOWN"
        rc=$?
        if [ "$stop" = 0 ] && [ "$rc" = 10 ]; then
            # nobody at the unit: retry what can recover (exit 3 and a user's
            # EXIT, exit 0, are final)
            attempt=0
            while :; do
                started=$(cut -d. -f1 /proc/uptime)
                play
                rc=$?
                [ "$stop" = 0 ] || break
                case "$rc" in 0|3) break ;; esac
                # a long healthy run before this failure: not a loop, start over
                [ $(( $(cut -d. -f1 /proc/uptime) - started )) -lt "$HEALTHY_S" ] || attempt=0
                attempt=$((attempt + 1))
                [ "$attempt" -le "$RETRIES" ] || break
                echo "usb-media: playback ended with $rc, retry $attempt of $RETRIES in ${RETRY_PAUSE}s" >&2
                sleep "$RETRY_PAUSE" &
                child=$!
                wait "$child"
                child=""
                [ "$stop" = 0 ] || break
            done
            message=$(message_for "$rc")
            if [ "$rc" = 3 ]; then
                sleep 1
                usb_root=$(find_stick) || exit 0
            fi
        elif [ "$stop" = 0 ] && [ "$rc" = 0 ]; then
            message="Autostart cancelled - the playlist starts again at the next power-on"
        fi
    else
        echo "usb-media: autostart skipped: the playlist does not ask for it" >&2
    fi
fi

while [ "$stop" = 0 ]; do
    rm -f "$TMP_PLAYLIST"
    if [ -n "$message" ]; then
        run "$APP" --root "$usb_root" --player "$PLAYER" --temp-playlist "$TMP_PLAYLIST" --message "$message"
    else
        run "$APP" --root "$usb_root" --player "$PLAYER" --temp-playlist "$TMP_PLAYLIST"
    fi
    rc=$?
    [ "$stop" = 0 ] && [ "$rc" = 10 ] || break

    play
    rc=$?
    [ "$stop" = 0 ] || break
    message=$(message_for "$rc")
    if [ "$rc" = 3 ]; then
        # the stick may be back (or another one); without one, leave
        sleep 1
        usb_root=$(find_stick) || break
    fi
done
rm -f "$TMP_PLAYLIST"
exit 0
