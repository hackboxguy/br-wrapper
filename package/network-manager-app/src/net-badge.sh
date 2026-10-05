#!/bin/sh
# net-badge.sh - the launcher's Network tile.
#
#   net-badge.sh --available   exit 0 when NetworkManager is there (net-ctl.sh
#                              available); else prints "Needs NetworkManager",
#                              which the launcher shows on the dimmed tile
#   net-badge.sh               prints "Serving addresses" while a port hands out
#                              addresses (DHCP-server mode: NetworkManager's
#                              shared mode, or the panel menu's own server), and
#                              nothing otherwise
#
# qt-demo-launcher runs both through /bin/sh -c, never while an app runs
# ("available_command" and "badge_command"). Read-only, no sudo, no nmcli
# call for the badge and no ping: a process list and a file test. Being
# offline is not badged - a bench rig is often offline on purpose.
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
NET_CTL=${NET_CTL:-$HERE/net-ctl.sh}
# NetworkManager's shared mode starts one dnsmasq per port with this conf-dir
SHARED_DIR=${NET_CTL_SHARED_DIR:-/etc/NetworkManager/dnsmasq-shared.d}
# the panel menu's server: the system dnsmasq with this file
LEGACY_CONF=${NET_CTL_LEGACY_CONF:-/etc/dnsmasq.d/micropanel-dhcp-server.conf}

if [ "${1:-}" = --available ]; then
    "$NET_CTL" available >/dev/null 2>&1 && exit 0
    echo "Needs NetworkManager"
    exit 1
fi

if pgrep -f -- "--conf-dir=$SHARED_DIR" >/dev/null 2>&1 \
   || { [ -f "$LEGACY_CONF" ] && pgrep -x dnsmasq >/dev/null 2>&1; }; then
    echo "Serving addresses"
fi
exit 0
