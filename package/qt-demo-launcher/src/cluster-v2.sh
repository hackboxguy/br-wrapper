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
# tree still works). In it, the installed app (bin/qt-cluster-demo, the DMS
# files in share/qt-cluster-demo/ - the micropanel image) or the repo's build
# tree (build-pi-agx/src/qt-cluster-demo, where the repo's unit runs it from).
# Environment: systemd/qt-cluster-demo.env of the install, then the operator's
# override /data/cluster/qt-cluster-demo.env (the later file wins; both read
# as data). On the A/B image the first is image content, the second survives
# reboots and updates.
# The DMS button in the app's control row (the DMS panel: on, camera off,
# off - written as on/off/none) is remembered
# across starts in /data/cluster/dms-video-view.state, which the app writes
# (--dms-video-view-state=) and this script turns back into --dms-video-view=
# - only where /data/cluster is writable (the A/B image) and the app knows the
# option. The operator's env file is never written. A reset forgets it.
# The row's on/off switches are remembered the same way, with or without
# the DMS, one file each: MAP (the map behind every theme) in
# map-backdrop.state, T (the telltales' ghost) in telltale-min-dark-level.state,
# B (the info bar) in info-bar.state - written by the app through
# --<option>-state= and passed back as --<option>= (map-backdrop,
# telltale-min-dark-level, info-bar). The LD/PC choice is the app's own
# (/data/cluster/fpga-ldpc-state.json, written and applied by the app).
# Two displays (the Pi's two HDMI outputs both connected): the second shows
# the same picture as the first - Qt's eglfs draws one window on one output
# and leaves the other on the console, so the script hands Qt a KMS
# configuration in which every further connected HDMI output clones the
# first (/tmp/cluster-v2-kms.json). One output: nothing changes. An
# environment that sets QT_QPA_EGLFS_KMS_CONFIG itself keeps it, and
# KMS_MIRROR=0 in the environment files turns the mirror off.
# Log: /tmp/cluster-v2.log (this script and the app; rewritten at each start).
# (CLUSTER_V2_DATA_DIR, CLUSTER_V2_LOG and CLUSTER_V2_DRM move /data/cluster,
# the log and /sys/class/drm for the tests.)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR=${CLUSTER_V2_DATA_DIR:-/data/cluster}
DATA_ENV=$DATA_DIR/qt-cluster-demo.env
VIEW_STATE=$DATA_DIR/dms-video-view.state

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

# find_home: the install directory and the binary in it, as "<dir> <rel>"
find_home() {
    for d in "${CLUSTER_V2_HOME:-}" /home/pi/qt-cluster-demo "$SCRIPT_DIR/../../../qt-cluster-demo"; do
        [ -n "$d" ] || continue
        for rel in bin/qt-cluster-demo build-pi-agx/src/qt-cluster-demo; do
            [ -x "$d/$rel" ] && { echo "$(cd "$d" && pwd -P) $rel"; return 0; }
        done
    done
    return 1
}

found=$(find_home) || unavailable "Cluster V2 not installed"
HOME_DIR=${found% *} BIN_REL=${found##* }
systemctl is-active --quiet can-proxyd.service || unavailable "CAN proxy not running"
[ "$CHECK" = 1 ] && exit 0

LOG=${CLUSTER_V2_LOG:-/tmp/cluster-v2.log}
exec >"$LOG" 2>&1
log() { echo "cluster-v2: $*"; }

# The environment files: KEY=value lines, read as data (no eval)
read_env() {
    while IFS= read -r line; do
        case $line in
            CLUSTER_ARGS=*) CLUSTER_ARGS=${line#CLUSTER_ARGS=} ;;
            EXTRA_ARGS=*) EXTRA_ARGS=${line#EXTRA_ARGS=} ;;
            DMS_ENABLED=*) DMS_ENABLED=${line#DMS_ENABLED=} ;;
            SOMEIP_IFACE=*) SOMEIP_IFACE=${line#SOMEIP_IFACE=} ;;
            KMS_MIRROR=*) KMS_MIRROR=${line#KMS_MIRROR=} ;;
        esac
    done < "$1"
}
CLUSTER_ARGS="--source=proxy --contract-if=vcan0" EXTRA_ARGS="" DMS_ENABLED=0 SOMEIP_IFACE=eth0 KMS_MIRROR=1
ENV_FILE="$HOME_DIR/systemd/qt-cluster-demo.env"
if [ -r "$ENV_FILE" ]; then
    read_env "$ENV_FILE"
else
    log "no $ENV_FILE; defaults: $CLUSTER_ARGS"
fi
if [ -r "$DATA_ENV" ]; then
    read_env "$DATA_ENV"
    log "override: $DATA_ENV"
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
    for preflight in "$HOME_DIR/share/qt-cluster-demo/pi-dms-production-baseline.sh" \
                     "$HOME_DIR/scripts/pi-dms-production-baseline.sh"; do
        [ -x "$preflight" ] || continue
        SOMEIP_IFACE="$SOMEIP_IFACE" timeout 10 "$preflight" someip-preflight \
            || log "SOME/IP: preflight failed (logged, not fatal)"
        break
    done
fi

# eglfs as in the unit, also when started by the launcher (which runs on
# linuxfb and passes that on); the touch device the launcher found
# (QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS) is inherited as it is
# The DMS button's last choice (the app writes it; read here as data)
if [ "$DMS_ENABLED" = 1 ] && [ -w "${VIEW_STATE%/*}" ] \
        && grep -qa -- "dms-video-view-state" "$HOME_DIR/$BIN_REL"; then
    ARGS="$ARGS --dms-video-view-state=$VIEW_STATE"
    if [ -r "$VIEW_STATE" ]; then
        case $(head -c 4 "$VIEW_STATE") in
            on*) ARGS="$ARGS --dms-video-view=on"; log "DMS panel: on (last choice)" ;;
            off*) ARGS="$ARGS --dms-video-view=off"; log "DMS panel: camera off (last choice)" ;;
            # the panel switched off: only a cluster that has that state
            none*) if grep -qa -- "on, off or none" "$HOME_DIR/$BIN_REL"; then
                       ARGS="$ARGS --dms-video-view=none"; log "DMS panel: off (last choice)"
                   fi ;;
        esac
    fi
fi

# The on/off switches' last choices, likewise (not part of the DMS)
remember() { # <option> <what, for the log>: <option>.state <-> --<option>=
    file=$DATA_DIR/$1.state
    [ -w "$DATA_DIR" ] && grep -qa -- "$1-state" "$HOME_DIR/$BIN_REL" || return 0
    ARGS="$ARGS --$1-state=$file"
    [ -r "$file" ] || return 0
    case $(head -c 3 "$file") in
        on*) ARGS="$ARGS --$1=on"; log "$2: on (last choice)" ;;
        off*) ARGS="$ARGS --$1=off"; log "$2: off (last choice)" ;;
    esac
}
remember map-backdrop map
remember telltale-min-dark-level "telltale ghost"
remember info-bar "info bar"

export QT_QPA_PLATFORM=eglfs
export QT_QPA_EGLFS_ALWAYS_SET_MODE=1

# Mirror onto a second display: the connected HDMI outputs in DRM's order
# (HDMI-A-1, HDMI-A-2), named as Qt names them (HDMI1, HDMI2)
if [ "$KMS_MIRROR" != 0 ] && [ -z "${QT_QPA_EGLFS_KMS_CONFIG:-}" ]; then
    outputs=""
    for st in "${CLUSTER_V2_DRM:-/sys/class/drm}"/card*-HDMI-A-*/status; do
        [ -r "$st" ] && [ "$(cat "$st")" = connected ] || continue
        c=${st%/status}; outputs="$outputs HDMI${c##*-HDMI-A-}"
    done
    # shellcheck disable=SC2086 # the names, one word each
    set -- $outputs
    if [ $# -ge 2 ]; then
        first=$1; shift
        kms=/tmp/cluster-v2-kms.json
        { printf '{ "outputs": ['; sep=""
          for o in "$@"; do printf '%s { "name": "%s", "clones": "%s" }' "$sep" "$o" "$first"; sep=","; done
          printf ' ] }\n'; } > "$kms"
        export QT_QPA_EGLFS_KMS_CONFIG="$kms"
        log "displays: $first, mirrored on $*"
    fi
fi
export QSG_RENDER_LOOP=threaded
VSOMEIP_LIB=/home/pi/.codex-deps/prefix/vsomeip-3.5.11/lib
export LD_LIBRARY_PATH="$VSOMEIP_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# As the unit (WorkingDirectory): an IDs file from before qt-cluster-demo
# named its vsomeip configuration relative to the IDs file needs this; the
# current app resolves it beside the IDs file from any directory
cd "$HOME_DIR" || unavailable "cannot enter $HOME_DIR"
log "exec $HOME_DIR/$BIN_REL$ARGS"
# shellcheck disable=SC2086 # ARGS is a word list, as the unit's $CLUSTER_ARGS
exec "$HOME_DIR/$BIN_REL" $ARGS
