#!/bin/sh
export QT_QPA_PLATFORM=eglfs
export QT_QPA_EGLFS_ALWAYS_SET_MODE=1
exec /usr/bin/qt-cluster-demo --can can0
