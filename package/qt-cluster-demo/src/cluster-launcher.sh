#!/bin/sh
# Resolve the installation prefix from this script's location
# e.g. /home/pi/micropanel/share/qt-apps/cluster-launcher.sh -> /home/pi/micropanel
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTALL_PREFIX="$(cd "$SCRIPT_DIR/../.." && pwd)"

export QT_QPA_PLATFORM=eglfs
export QT_QPA_EGLFS_ALWAYS_SET_MODE=1
exec "$INSTALL_PREFIX/bin/qt-cluster-demo" --can can0
