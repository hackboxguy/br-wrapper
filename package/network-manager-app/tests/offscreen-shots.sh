#!/bin/bash
# offscreen-shots.sh <network-manager-app binary> <out dir> [WxH]
#
# Renders every state of the app without a network: QT_QPA_PLATFORM=offscreen,
# the software scene graph, --dry-run, tests/fake-net-ctl for net-ctl.sh, and a
# private counter directory standing in for /sys/class/net (a background loop
# makes traffic, so the rates and sparklines have something to draw). Nothing
# here needs root or touches a device.
set -euo pipefail
app=$(realpath "$1"); out=$(realpath -m "$2"); size=${3:-1920x720}
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
mkdir -p "$out"

# Fake counters: eth0, wlan0, eth1, eth2
for i in eth0 wlan0 eth1 eth2; do
    mkdir -p "$work/sys/$i/statistics"
    for c in rx_bytes tx_bytes rx_packets tx_packets; do echo 1000 > "$work/sys/$i/statistics/$c"; done
    for c in rx_errors tx_errors rx_dropped tx_dropped; do echo 0 > "$work/sys/$i/statistics/$c"; done
done
echo 1 > "$work/sys/eth0/carrier"; echo 1 > "$work/sys/eth1/carrier"; echo 0 > "$work/sys/eth2/carrier"
(
    n=0
    while :; do
        n=$((n + 1))
        w=$(( (n % 40) < 20 ? n % 20 : 40 - n % 40 ))
        # no cable, no traffic
        if [ "$(cat "$work/sys/eth0/carrier")" = 1 ]; then
            echo $((1000 + n * (90000 + w * 9000))) > "$work/sys/eth0/statistics/rx_bytes"
            echo $((1000 + n * 12000)) > "$work/sys/eth0/statistics/tx_bytes"
        fi
        echo $((1000 + n * (40000 + (n % 7) * 30000))) > "$work/sys/wlan0/statistics/rx_bytes"
        echo $((1000 + n * (8000 + (n % 5) * 6000))) > "$work/sys/wlan0/statistics/tx_bytes"
        echo $((1000 + n * (20000 + w * 3000))) > "$work/sys/eth1/statistics/rx_bytes"
        echo $((1000 + n * (150000 - w * 5000))) > "$work/sys/eth1/statistics/tx_bytes"
        for i in eth0 wlan0 eth1; do echo $((n * 211)) > "$work/sys/$i/statistics/rx_packets"; echo $((n * 97)) > "$work/sys/$i/statistics/tx_packets"; done
        sleep 0.05
    done
) &
traffic=$!
trap 'kill $traffic 2>/dev/null; rm -rf "$work"' EXIT

export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
# Qt built with journald support sends qInfo to the journal when stderr is not
# a terminal; shot() reads it from stderr
export QT_FORCE_STDERR_LOGGING=1

shot() { # <name> <scenario> <extra app args...>
    local name=$1 scenario=$2; shift 2
    FAKE_NET_SCENARIO=$scenario "$app" --dry-run --window-size "$size" --net-tool "$here/fake-net-ctl" \
        --sysfs "$work/sys" --sample-ms 100 --log-file "$work/app.log" \
        --screenshot "$out/$name.png" --screenshot-delay 6500 "$@" 2>&1 \
        | grep -E 'screenshot|QML|qml|Error|error' | sed "s/^/  [$name] /" || true
}

# Overview (Phase 1)
shot overview-online-serving online-serving
echo 0 > "$work/sys/eth0/carrier"
shot overview-offline offline
echo 1 > "$work/sys/eth0/carrier"
shot overview-usb-adapter usb-adapter
shot overview-usb-adapter-scrolled usb-adapter --open-sheet scroll-end
shot overview-no-nm no-nm --screenshot-delay 800
shot overview-detail-eth0 online-serving --open-sheet detail:eth0
shot overview-detail-wlan0 online-serving --open-sheet detail:wlan0
shot overview-detail-eth1 online-serving --open-sheet detail:eth1
shot wired-empty online-serving --section wired --screenshot-delay 800
shot tools-empty online-serving --section tools --screenshot-delay 800

# WiFi (Phase 2)
shot wifi-connected online-serving --section wifi --screenshot-delay 1500
shot wifi-not-connected offline --section wifi --screenshot-delay 1500
shot wifi-off wifi-blocked --section wifi --screenshot-delay 800
shot wifi-absent wifi-absent --section wifi --screenshot-delay 800
shot wifi-keyboard-abc online-serving --section wifi --open-sheet keyboard --screenshot-delay 1000
shot wifi-keyboard-ABC online-serving --section wifi --open-sheet keyboard:ABC --screenshot-delay 1000
shot wifi-keyboard-123 online-serving --section wifi --open-sheet keyboard:123:shown --screenshot-delay 1000
shot wifi-keyboard-sym online-serving --section wifi --open-sheet keyboard:sym --screenshot-delay 1000
shot wifi-hidden online-serving --section wifi --open-sheet hidden --screenshot-delay 1000
# A join of the saved "Lab-Bench" (no password asked), in progress and each failure
FAKE_NET_STEP=2 shot wifi-joining online-serving --section wifi --auto-connect Lab-Bench --screenshot-delay 3000
FAKE_NET_STEP=0.1 shot wifi-joined connect-ok --section wifi --auto-connect Lab-Bench --screenshot-delay 2500
FAKE_NET_STEP=0.1 shot wifi-fail-bad-password connect-bad-password --section wifi --auto-connect Lab-Bench --screenshot-delay 2500
FAKE_NET_STEP=0.1 shot wifi-fail-not-found connect-not-found --section wifi --auto-connect Lab-Bench --screenshot-delay 2500
FAKE_NET_STEP=0.1 shot wifi-fail-no-address connect-no-address --section wifi --auto-connect Lab-Bench --screenshot-delay 2500
FAKE_NET_STEP=0.1 shot wifi-fail-timeout connect-timeout --section wifi --auto-connect Lab-Bench --screenshot-delay 2500
# An enterprise network: listed dimmed, refused with a reason
shot wifi-unsupported online-serving --section wifi --auto-connect "Corp Wireless" --screenshot-delay 1500
ls -1 "$out"
