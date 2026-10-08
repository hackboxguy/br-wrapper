#!/bin/sh
# kodi-video.sh - Launch Kodi and play video with loop enabled
# Kodi uses GBM/DRM for display on Pi OS Lite (headless, no X11/Wayland)
#
# USB priority: a video on a USB stick - the first in its Videos/ folder,
# else the first at its top level - plays first. Otherwise prefers the
# rootfs reference video, then falls back to local flower.mkv. (The Stream
# Deck's Default Ref Video key uses the same rule, kodi-usb-common.sh.)
#
# This script is launched by qt-demo-launcher as m_runningProcess.
# To avoid deadlock (launcher tracks this script as the running app),
# we do NOT call launcher-client. Instead, we launch Kodi directly
# and send the play command via JSON-RPC in the background.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KODI="http://127.0.0.1:8080/jsonrpc"
REF_VIDEO="/home/pi/micropanel/share/sp6bins/config/ref-video.mp4"
FALLBACK_VIDEO="/home/pi/micropanel/usr/share/micropanel/media/videos/flower.mkv"

# Ensure runtime directory exists
mkdir -p /tmp/runtime-kodi
export XDG_RUNTIME_DIR=/tmp/runtime-kodi

# Audio sink for Pi OS Lite (no PulseAudio)
export KODI_AE_SINK=ALSA

# Clone Kodi's output onto the 2nd HDMI (Kodi GBM only drives one connector).
# Set KODI_MIRROR_DISABLE=1 to turn off, KODI_MIRROR_CONNECTOR to pick output.
KODI_MIRROR_LIB="$(cd "$(dirname "$0")" && pwd)/libkodi-drm-mirror.so"
[ -f "$KODI_MIRROR_LIB" ] && export LD_PRELOAD="$KODI_MIRROR_LIB${LD_PRELOAD:+:$LD_PRELOAD}"

# Start video playback command in background — waits for Kodi JSON-RPC, then plays video
(
    # Source USB detection helpers
    . "$SCRIPT_DIR/kodi-usb-common.sh"

    # Resolve video path — USB priority, rootfs reference video, local fallback
    if [ -f "$REF_VIDEO" ]; then
        VIDEO="$REF_VIDEO"
    else
        VIDEO="$FALLBACK_VIDEO"
    fi

    # A USB stick's video: Videos/ first, then the stick's top level
    usb_video=$(find_usb_video) && VIDEO="$usb_video"

    # Wait for Kodi JSON-RPC to become available (up to 15 seconds)
    for i in $(seq 1 15); do
        curl -s "$KODI" -H "Content-Type: application/json" \
            -d '{"jsonrpc":"2.0","method":"JSONRPC.Ping","id":1}' \
            --connect-timeout 1 > /dev/null 2>&1 && break
        sleep 1
    done

    # Play video
    curl -s "$KODI" -H "Content-Type: application/json" \
        -d "{\"jsonrpc\":\"2.0\",\"method\":\"Player.Open\",\"params\":{\"item\":{\"file\":\"$VIDEO\"}},\"id\":1}" \
        --connect-timeout 2 > /dev/null 2>&1

    # Wait briefly for playback to start, then enable video loop
    sleep 2
    curl -s "$KODI" -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","method":"Player.SetRepeat","params":{"playerid":1,"repeat":"one"},"id":1}' \
        --connect-timeout 2 > /dev/null 2>&1
) &

# Launch Kodi in standalone mode (replaces this process)
exec /usr/bin/kodi --standalone
