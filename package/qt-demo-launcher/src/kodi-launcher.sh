#!/bin/sh
# kodi-launcher.sh - Launch Kodi from qt-demo-launcher
# Kodi uses GBM/DRM for display on Pi OS Lite (headless, no X11/Wayland)

# Ensure runtime directory exists
mkdir -p /tmp/runtime-kodi
export XDG_RUNTIME_DIR=/tmp/runtime-kodi

# Audio sink for Pi OS Lite (no PulseAudio)
export KODI_AE_SINK=ALSA

# Clone Kodi's output onto the 2nd HDMI (Kodi GBM only drives one connector).
# Set KODI_MIRROR_DISABLE=1 to turn off, KODI_MIRROR_CONNECTOR to pick output.
KODI_MIRROR_LIB="$(cd "$(dirname "$0")" && pwd)/libkodi-drm-mirror.so"
[ -f "$KODI_MIRROR_LIB" ] && export LD_PRELOAD="$KODI_MIRROR_LIB${LD_PRELOAD:+:$LD_PRELOAD}"

# Launch Kodi in standalone mode (exits cleanly on user "Exit" from UI)
exec /usr/bin/kodi --standalone
