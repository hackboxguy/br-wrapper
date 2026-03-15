#!/bin/sh
# kodi-launcher.sh - Launch Kodi from qt-demo-launcher
# Kodi uses GBM/DRM for display on Pi OS Lite (headless, no X11/Wayland)

# Ensure runtime directory exists
mkdir -p /tmp/runtime-kodi
export XDG_RUNTIME_DIR=/tmp/runtime-kodi

# Audio sink for Pi OS Lite (no PulseAudio)
export KODI_AE_SINK=ALSA

# Launch Kodi in standalone mode (exits cleanly on user "Exit" from UI)
exec /usr/bin/kodi --standalone
