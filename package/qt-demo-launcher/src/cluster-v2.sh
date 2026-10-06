#!/bin/sh
# cluster-v2.sh - Cluster Demo V2: the modern cluster from the qt-cluster-demo
# repository (installed in its own prefix, /home/pi/qt-cluster-demo on the
# micropanel image), fed by can-proxyd on vcan0, with the FocusDrive DMS.
#
#   cluster-v2.sh --theme=NAME   run the cluster with that theme (the tiles of
#                                the launcher's "cluster-v2" page)
#   cluster-v2.sh --check        exit 0 when it can run; else print why on one
#                                line and exit 1 (the tiles' available_command)
#
# It does what qt-cluster-demo.service does around the app - the same
# environment file, the eglfs settings, vsomeip's library path, the SOME/IP
# multicast route and preflight - with two differences for a launcher tile:
#   - nothing stops the tile: no address on the SOME/IP port, a failed route
#     or preflight are logged and the cluster starts anyway (its DMS panel
#     shows that it is waiting);
#   - the tile's --theme= replaces any --theme= in the environment file.
# The app is exec'd, so the launcher's stop-app (SIGTERM) reaches it, and it
# exits back to the launcher from its own exit button (tap the screen, then X).
#
# Where the V2 install is: $CLUSTER_V2_HOME, else /home/pi/qt-cluster-demo,
# else a qt-cluster-demo directory beside this script's prefix (so a relocated
# tree still works). Its binary is where the repo's unit runs it from.
# Log: /tmp/cluster-v2.log (this script and the app; rewritten at each start).

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_REL=build-pi-agx/src/qt-cluster-demo

CHECK=0 THEME=""
for arg in "$@"; do
    case $arg in
        --check) CHECK=1 ;;
        --theme=*) THEME=${arg#--theme=} ;;
    esac
done

unavailable() {
    if [ "$CHECK" = 1 ]; then echo "$1"; else echo "cluster-v2: $1" >&2; fi
    exit 1
}

find_home() {
    for d in "${CLUSTER_V2_HOME:-}" /home/pi/qt-cluster-demo "$SCRIPT_DIR/../../../qt-cluster-demo"; do
        [ -n "$d" ] && [ -x "$d/$BIN_REL" ] && { (cd "$d" && pwd -P); return 0; }
    done
    return 1
}

HOME_DIR=$(find_home) || unavailable "Cluster V2 not installed"
systemctl is-active --quiet can-proxyd.service || unavailable "CAN proxy not running"
[ "$CHECK" = 1 ] && exit 0

LOG=/tmp/cluster-v2.log
exec >"$LOG" 2>&1
log() { echo "cluster-v2: $*"; }

# The service's environment file: KEY=value lines, read as data (no eval)
CLUSTER_ARGS="--source=proxy --contract-if=vcan0" EXTRA_ARGS="" DMS_ENABLED=0 SOMEIP_IFACE=eth0
ENV_FILE="$HOME_DIR/systemd/qt-cluster-demo.env"
if [ -r "$ENV_FILE" ]; then
    while IFS= read -r line; do
        case $line in
            CLUSTER_ARGS=*) CLUSTER_ARGS=${line#CLUSTER_ARGS=} ;;
            EXTRA_ARGS=*) EXTRA_ARGS=${line#EXTRA_ARGS=} ;;
            DMS_ENABLED=*) DMS_ENABLED=${line#DMS_ENABLED=} ;;
            SOMEIP_IFACE=*) SOMEIP_IFACE=${line#SOMEIP_IFACE=} ;;
        esac
    done < "$ENV_FILE"
else
    log "no $ENV_FILE; defaults: $CLUSTER_ARGS"
fi

# The tile's theme wins over the file's
ARGS=""
for a in $CLUSTER_ARGS $EXTRA_ARGS; do
    case $a in --theme=*) [ -n "$THEME" ] && continue ;; esac
    ARGS="$ARGS $a"
done
[ -n "$THEME" ] && ARGS="$ARGS --theme=$THEME"

if [ "$DMS_ENABLED" = 1 ]; then
    # The address the cluster advertises to FocusDrive: the SOME/IP port's
    # own (with eth1 on the home LAN the app's own pick depends on interface
    # order). No address yet: the app waits and retries by itself.
    addr=$(ip -4 -o addr show dev "$SOMEIP_IFACE" scope global 2>/dev/null | awk '{ split($4, a, "/"); print a[1]; exit }')
    if [ -n "$addr" ]; then
        log "SOME/IP: $SOMEIP_IFACE has $addr"
        case " $ARGS " in *" --dms-advertise-ip="*) ;; *) ARGS="$ARGS --dms-advertise-ip=$addr" ;; esac
    else
        log "SOME/IP: WARNING $SOMEIP_IFACE has no IPv4 address; the DMS waits for one"
    fi
    # vsomeip's service discovery needs a multicast route on that port. The
    # app runs as the launcher's user, hence sudo; failures do not stop it.
    if sudo -n /sbin/ip route replace 224.0.0.0/4 dev "$SOMEIP_IFACE" 2>&1; then
        log "SOME/IP: multicast route 224.0.0.0/4 on $SOMEIP_IFACE"
    else
        log "SOME/IP: WARNING could not set the multicast route on $SOMEIP_IFACE"
    fi
    if [ -x "$HOME_DIR/scripts/pi-dms-production-baseline.sh" ]; then
        SOMEIP_IFACE="$SOMEIP_IFACE" timeout 10 "$HOME_DIR/scripts/pi-dms-production-baseline.sh" someip-preflight \
            || log "SOME/IP: preflight failed (logged, not fatal)"
    fi
fi

# eglfs as in the unit, also when started by the launcher (which runs on
# linuxfb and passes that on); the touch device the launcher found
# (QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS) is inherited as it is
export QT_QPA_PLATFORM=eglfs
export QT_QPA_EGLFS_ALWAYS_SET_MODE=1
export QSG_RENDER_LOOP=threaded
VSOMEIP_LIB=/home/pi/.codex-deps/prefix/vsomeip-3.5.11/lib
export LD_LIBRARY_PATH="$VSOMEIP_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# The DMS's vsomeip configuration path in the repo's IDs file is relative to
# the install, as in the unit (WorkingDirectory)
cd "$HOME_DIR" || unavailable "cannot enter $HOME_DIR"
log "exec $HOME_DIR/$BIN_REL$ARGS"
# shellcheck disable=SC2086 # ARGS is a word list, as the unit's $CLUSTER_ARGS
exec "$HOME_DIR/$BIN_REL" $ARGS
