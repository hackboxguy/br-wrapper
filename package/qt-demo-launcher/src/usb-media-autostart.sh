#!/bin/bash
# usb-media-autostart.sh - at boot, start USB Media playback when the stick
# asks for it (usb-media-autostart.service)
#
# Waits for qt-demo-launcher's API (port 8081; the launcher sleeps in its
# ExecStartPre, so service ordering alone is not enough), gives the stick up
# to STICK_WAIT seconds to appear, and when its micropanel-playlist.json has
# "autostart": true and something playable, asks the launcher to start the
# hidden usb-media-autostart entry (usb-media.sh --autostart: countdown, then
# playback). Anything else: nothing happens, the launcher stays.
#
# Always exits 0: a stick without a playlist is the normal case, not a
# failure (and no failed unit should appear in the system's health).

PORT=${USB_MEDIA_LAUNCHER_PORT:-8081}
LAUNCHER_WAIT=${USB_MEDIA_LAUNCHER_WAIT:-120}
STICK_WAIT=${USB_MEDIA_STICK_WAIT:-15}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WRAPPER="$SCRIPT_DIR/usb-media.sh"

log() { echo "usb-media-autostart: $*"; }

# launcher <command>: one request, the first line of the answer
launcher() {
    local reply=""
    exec 3<>"/dev/tcp/127.0.0.1/$PORT" 2>/dev/null || return 1
    printf '%s\n' "$1" >&3
    IFS= read -r -t 5 reply <&3
    exec 3>&- 3<&-
    printf '%s\n' "$reply"
    [ -n "$reply" ]
}

waited=0
until launcher get-running-app >/dev/null; do
    [ "$waited" -ge "$LAUNCHER_WAIT" ] && { log "launcher API not reachable after ${LAUNCHER_WAIT}s"; exit 0; }
    sleep 2
    waited=$((waited + 2))
done

waited=0
until reason=$("$WRAPPER" --autostart-check 2>/dev/null); do
    case "$reason" in
        "Insert a USB stick"*)
            [ "$waited" -ge "$STICK_WAIT" ] && { log "no USB stick"; exit 0; }
            sleep 1
            waited=$((waited + 1)) ;;
        *)
            log "not starting: ${reason:-no reason}"
            exit 0 ;;
    esac
done

running=$(launcher get-running-app)
if [ "$running" != none ]; then
    log "not starting: $running is already running"
    exit 0
fi
log "starting playback ($(launcher "start-app usb-media-autostart"))"
exit 0
