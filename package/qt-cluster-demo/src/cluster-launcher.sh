#!/bin/sh
# Resolve the installation prefix from this script's location
# e.g. /home/pi/micropanel/share/qt-apps/cluster-launcher.sh -> /home/pi/micropanel
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTALL_PREFIX="$(cd "$SCRIPT_DIR/../.." && pwd)"
BIN="$INSTALL_PREFIX/bin/qt-cluster-demo"

# The row's T and B buttons, as the user left them: where /data/cluster is
# writable (the micropanel A/B image) the app writes each change to a file
# there and the next start begins that way (only a binary that knows the
# options gets them). Elsewhere they last for the run. A reset forgets them.
# (The LD/PC choice is the app's own: /data/cluster/fpga-ldpc-state.json.)
DATA_DIR=${CLUSTER_DATA_DIR:-/data/cluster}
ARGS="--can can0"
for opt in telltale-min-dark-level info-bar; do
    file=$DATA_DIR/classic-$opt.state
    if [ ! -w "$DATA_DIR" ] || ! grep -qa -- "$opt-state" "$BIN"; then continue; fi
    ARGS="$ARGS --$opt-state=$file"
    case $(head -c 3 "$file" 2>/dev/null) in
        on*) ARGS="$ARGS --$opt=on" ;;
        off*) ARGS="$ARGS --$opt=off" ;;
    esac
done

export QT_QPA_PLATFORM=eglfs
export QT_QPA_EGLFS_ALWAYS_SET_MODE=1
# shellcheck disable=SC2086 # ARGS is a word list
exec "$BIN" $ARGS
