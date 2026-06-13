#!/bin/sh
# kodi-slideshow.sh - Launch Kodi and start picture slideshow
# Kodi uses GBM/DRM for display on Pi OS Lite (headless, no X11/Wayland)
#
# USB priority: if a USB stick with a Pictures/ folder is detected,
# slideshow plays from USB. Otherwise falls back to local pictures.
#
# This script is launched by qt-demo-launcher as m_runningProcess.
# To avoid deadlock (launcher tracks this script as the running app),
# we do NOT call launcher-client. Instead, we launch Kodi directly
# and send the slideshow command via JSON-RPC in the background.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KODI="http://127.0.0.1:8080/jsonrpc"
LOCAL_PICTURES="/home/pi/micropanel/share/qt-apps/Pictures/"

# Ensure runtime directory exists
mkdir -p /tmp/runtime-kodi
export XDG_RUNTIME_DIR=/tmp/runtime-kodi

# Audio sink for Pi OS Lite (no PulseAudio)
export KODI_AE_SINK=ALSA

# Start slideshow command in background — waits for Kodi JSON-RPC, then opens pictures
(
    # Source USB detection helpers
    . "$SCRIPT_DIR/kodi-usb-common.sh"

    # Resolve pictures path — USB priority, local fallback
    PICTURES="$LOCAL_PICTURES"
    usb_pictures=$(detect_usb_media_path "Pictures") && PICTURES="$usb_pictures"

    # Wait for Kodi JSON-RPC to become available (up to 15 seconds)
    for i in $(seq 1 15); do
        curl -s "$KODI" -H "Content-Type: application/json" \
            -d '{"jsonrpc":"2.0","method":"JSONRPC.Ping","id":1}' \
            --connect-timeout 1 > /dev/null 2>&1 && break
        sleep 1
    done

    # Start slideshow
    curl -s "$KODI" -H "Content-Type: application/json" \
        -d "{\"jsonrpc\":\"2.0\",\"method\":\"Player.Open\",\"params\":{\"item\":{\"directory\":\"$PICTURES\"},\"options\":{\"shuffled\":false,\"repeat\":\"all\"}},\"id\":1}" \
        --connect-timeout 2 > /dev/null 2>&1
) &

# Launch Kodi in standalone mode (replaces this process)
exec /usr/bin/kodi --standalone
