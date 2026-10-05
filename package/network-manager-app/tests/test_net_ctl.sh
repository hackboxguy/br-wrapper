#!/bin/sh
# test_net_ctl.sh - the real net-ctl.sh against tests/fake-nmcli.
#
# Checks the exact nmcli commands net-ctl.sh composes, the restore after a
# failed join or a failed wired-set, the exit codes, that a WiFi password
# reaches nmcli only on stdin - never in an argument list -, the wired modes,
# the MAC binding of USB adapters, the DHCP-server preconditions, the probe's
# parsing, the per-port internet check, the systemd-run detach and the Tools
# (ping, internet-check, iperf3 against output captured on the rig). No root,
# no network, no NetworkManager: nmcli, ip, ping, systemctl, pgrep,
# systemd-run, iw, rfkill, curl, iperf3, ss, the DNS question and the probe
# are stand-ins.
#   tests/test_net_ctl.sh            (also run by ctest with -DBUILD_TESTS=ON)
here=$(cd "$(dirname "$0")" && pwd)
script=$here/../src/net-ctl.sh
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# fake nmcli, iw and rfkill first in PATH (net-ctl.sh appends /usr/sbin, never prepends)
mkdir -p "$work/bin" "$work/sys/eth0/statistics" "$work/sys/wlan0" "$work/leases" "$work/modprobe"
cp "$here/fake-nmcli" "$work/bin/nmcli"
cat > "$work/bin/iw" <<'EOF'
#!/bin/sh
d=${FAKE_NM:?}
echo "iw $*" >> "$d/calls"
[ "$1 $2" = "reg get" ] && echo "country $(cat "$d/country" 2>/dev/null || echo 00): DFS-UNSET"
exit 0
EOF
cat > "$work/bin/rfkill" <<'EOF'
#!/bin/sh
echo "rfkill $*" >> "${FAKE_NM:?}/calls"
EOF
cat > "$work/bin/ip" <<'FAKE'
#!/bin/sh
# addresses from $FAKE_NM/addrs ("dev a.b.c.d/n"), routes from $FAKE_NM/routes ("dev gateway metric")
d=${FAKE_NM:?}
touch "$d/addrs" "$d/routes"
case "$*" in
    "-4 route show default")
        sort -k3n "$d/routes" | awk '{ print "default via " $2 " dev " $1 " proto dhcp metric " $3 }' ;;
    "-4 route show default dev "*)
        awk -v dv="$6" '$1 == dv { print "default via " $2 " dev " $1 " proto dhcp metric " $3 }' "$d/routes" ;;
    "-4 -o addr show dev "*)
        awk -v dv="$6" '$1 == dv { print "2: " $1 "    inet " $2 " brd 0.0.0.0 scope global " $1 }' "$d/addrs" ;;
    "-4 -o addr show")
        awk '{ print "2: " $1 "    inet " $2 " brd 0.0.0.0 scope global " $1 }' "$d/addrs" ;;
esac
exit 0
FAKE
cat > "$work/bin/ping" <<'FAKE'
#!/bin/sh
# answers on the ports listed in $FAKE_NM/inet; the ping tool's tests give
# the whole output in $FAKE_NM/ping-out (exit $FAKE_NM/ping-rc), or hang
d=${FAKE_NM:?}
echo "ping $*" >> "$d/calls"
[ -f "$d/ping-hang" ] && exec sleep 37
[ -f "$d/ping-slow" ] && { sleep 2; exit 1; }
if [ -f "$d/ping-out" ]; then cat "$d/ping-out"; exit "$(cat "$d/ping-rc" 2>/dev/null || echo 0)"; fi
dev=''
while [ $# -gt 0 ]; do [ "$1" = -I ] && dev=$2; shift; done
echo "ping $dev" >> "$d/pings"
grep -qx "$dev" "$d/inet" 2>/dev/null || exit 1
echo "64 bytes from x: icmp_seq=1 ttl=64 time=0.31 ms"
FAKE
cat > "$work/bin/systemctl" <<'FAKE'
#!/bin/sh
# the system dnsmasq: $FAKE_NM/dnsmasq-active (yes|no), dnsmasq-enabled (enabled|masked)
d=${FAKE_NM:?}
echo "systemctl $*" >> "$d/calls"
case "$*" in
    "cat dnsmasq.service") exit 0 ;;
    "is-active --quiet dnsmasq.service") [ "$(cat "$d/dnsmasq-active" 2>/dev/null)" = yes ] ;;
    "is-enabled dnsmasq.service") cat "$d/dnsmasq-enabled" 2>/dev/null || echo enabled ;;
    "stop dnsmasq.service") echo no > "$d/dnsmasq-active" ;;
    "mask dnsmasq.service") echo masked > "$d/dnsmasq-enabled" ;;
esac
FAKE
cat > "$work/bin/pgrep" <<'FAKE'
#!/bin/sh
cat "${FAKE_NM:?}/dnsmasq" 2>/dev/null
FAKE
cat > "$work/bin/systemd-run" <<'FAKE'
#!/bin/sh
# records the call, then runs the command the way the unit would: only the
# variables named with --setenv reach it
d=${FAKE_NM:?}
echo "systemd-run $*" >> "$d/calls"
envs=''
while [ $# -gt 0 ]; do
    case $1 in
        --setenv=*=*) envs="$envs ${1#--setenv=}" ;;
        --setenv=*) v=${1#--setenv=}; eval "envs=\"\$envs $v=\$$v\"" ;;
        --*) ;;
        *) break ;;
    esac
    shift
done
# shellcheck disable=SC2086
exec env -i PATH="$PATH" FAKE_NM="$d" $envs "$@"
FAKE
cat > "$work/bin/curl" <<'FAKE'
#!/bin/sh
# prints $FAKE_NM/curl (as curl -w '%{http_code} %{time_total}' does), exits $FAKE_NM/curl-rc
d=${FAKE_NM:?}
echo "curl $*" >> "$d/calls"
cat "$d/curl" 2>/dev/null || printf '000 8.0'
exit "$(cat "$d/curl-rc" 2>/dev/null || echo 0)"
FAKE
cat > "$work/bin/fake-python3" <<'FAKE'
#!/bin/sh
# the internet check's DNS question: records name, server and bound port,
# answers with $FAKE_NM/dns ("ok <bound> <ms> <addr>", "servfail <bound> <ms>", ...)
d=${FAKE_NM:?}
echo "python3 $3 $4 $5" >> "$d/calls"
cat "$d/dns" 2>/dev/null || echo "no-answer 0"
FAKE
cat > "$work/bin/iperf3" <<'FAKE'
#!/bin/sh
d=${FAKE_NM:?}
echo "iperf3 $*" >> "$d/calls"
[ -f "$d/iperf-hang" ] && exec sleep 38
cat "$d/iperf-out" 2>/dev/null
exit "$(cat "$d/iperf-rc" 2>/dev/null || echo 0)"
FAKE
cat > "$work/bin/ss" <<'FAKE'
#!/bin/sh
[ -f "${FAKE_NM:?}/ss-busy" ] && echo "LISTEN 0      5            *:5201            *:*"
exit 0
FAKE
cat > "$work/bin/probe" <<'FAKE'
#!/bin/sh
# net-dhcp-probe.py's stand-in: $FAKE_NM/offers, as the probe prints them
cat "${FAKE_NM:?}/offers" 2>/dev/null
echo "DONE servers=x"
FAKE
chmod +x "$work/bin/"*
echo 1 > "$work/sys/eth0/carrier"; echo 1000 > "$work/sys/eth0/speed"
echo 2c:cf:67:4f:66:ca > "$work/sys/eth0/address"
# eth1: a USB adapter (its device link goes through a USB path)
mkdir -p "$work/sys/devices/usb1/1-1.4/1-1.4:1.0" "$work/sys/eth1"
ln -s ../devices/usb1/1-1.4/1-1.4:1.0 "$work/sys/eth1/device"
echo 1 > "$work/sys/eth1/carrier"; echo 00:e0:4c:69:b1:0e > "$work/sys/eth1/address"
# a veth is virtual in sysfs, whatever type NetworkManager gives it
mkdir -p "$work/sys/devices/virtual/net/vethnm"
ln -s devices/virtual/net/vethnm "$work/sys/vethnm"
printf 'overlayroot / overlay rw 0 0\n' > "$work/mounts"

export PATH="$work/bin:$PATH" FAKE_NM="$work/nm" NET_CTL_PROBE="$work/bin/probe" NET_CTL_PYTHON="$work/bin/fake-python3"
export NET_CTL_SYSFS="$work/sys" NET_CTL_LEASE_DIR="$work/leases" NET_CTL_LOCK="$work/lock"
export NET_CTL_LEGACY_CONF="$work/legacy.conf" NET_CTL_MOUNTS="$work/mounts" NET_CTL_MODPROBE_DIR="$work/modprobe"

PASSWORD='s3cret pass=%"word'   # spaces, =, % and a quote: none may reach argv
failures=0
ok() { echo "  ok  $1"; }
bad() { echo "FAIL: $1"; failures=$((failures + 1)); }
check() { # <label> <command...>: passes when the command succeeds
    label=$1; shift
    if "$@"; then ok "$label"; else bad "$label"; fi
}
called() { grep -qF -- "$1" "$FAKE_NM/calls"; }
not_called() { ! grep -qF -- "$1" "$FAKE_NM/calls"; }
out_has() { printf '%s\n' "$out" | grep -qF -- "$1"; }
no_password_in_argv() { ! grep -qF "s3cret" "$FAKE_NM/calls"; }

# A fresh fake NetworkManager: <mode> [profiles...] ("uuid ssid autoconnect active")
# eth0 is up on NetworkManager's automatic profile; eth1 (USB) on its own.
# Every reset starts in fresh directories (nothing is removed until the end).
# The per-port internet check is off unless a test asks for it.
W1='aaaaaaaa-0000-0000-0000-000000000001|Wired connection 1|auto||||eth0||yes|-999|100|/run/NetworkManager/system-connections/Wired connection 1.nmconnection|eth0|no|auto'
W2='aaaaaaaa-0000-0000-0000-000000000002|Wired connection 2|auto||||eth1||yes|-999|90|/run/NetworkManager/system-connections/Wired connection 2.nmconnection|eth1|no|auto'
round=0
reset() {
    round=$((round + 1))
    FAKE_NM="$work/nm.$round"; export FAKE_NM
    NET_CTL_SHARED_DIR="$FAKE_NM/shared.d"; TMPDIR="$FAKE_NM/tmp"; export NET_CTL_SHARED_DIR TMPDIR
    mkdir -p "$FAKE_NM" "$TMPDIR"
    echo "$1" > "$FAKE_NM/mode"; shift
    : > "$FAKE_NM/profiles"
    for p in "$@"; do printf '%s\n' "$p" | tr ' ' '\t' >> "$FAKE_NM/profiles"; done
    printf '%s\n%s\n' "$W1" "$W2" > "$FAKE_NM/eth"
    printf 'eth0 192.168.1.170/24\neth1 192.168.20.164/24\n' > "$FAKE_NM/addrs"
    printf 'eth0 192.168.1.1 100\n' > "$FAKE_NM/routes"
    echo 1 > "$work/sys/eth1/carrier"
    : > "$FAKE_NM/calls"
    NET_CTL_INET=skip; export NET_CTL_INET
}
run() { out=$("$script" "$@" 2>&1); rc=$?; }
run_pw() { out=$(printf '%s\n' "$PASSWORD" | "$script" "$@" 2>&1); rc=$?; }
run_nopw() { out=$("$script" "$@" 2>&1 < /dev/null); rc=$?; }

echo "== available"
reset ok
run available
check "available: exit 0" [ "$rc" = 0 ]
echo stopped > "$FAKE_NM/running"
run status
check "status without NetworkManager: exit 4" [ "$rc" = 4 ]
check "  says why" out_has "reason=no-nm"

echo "== status"
reset ok
run status
check "status: exit 0" [ "$rc" = 0 ]
check "eth0 line" out_has "RESULT kind=iface name=eth0 type=ethernet usb=0 mac=2C:CF:67:4F:66:CA carrier=1 speed=1000 state=connected profile=Wired%20connection%201 mode=client ip=192.168.1.170 prefix=24 gateway=192.168.1.1 dns=192.168.1.1,9.9.9.9"
check "lease from DHCP4 options" out_has "dhcpserver=192.168.1.1 leasetime=86400"
check "wlan0 listed" out_has "name=wlan0 type=wifi"
check "loopback and p2p hidden" sh -c "! printf '%s' \"\$1\" | grep -qE 'name=(lo|p2p-dev-wlan0) '" _ "$out"
check "veth hidden by default" sh -c "! printf '%s' \"\$1\" | grep -q 'name=vethnm'" _ "$out"
check "summary" out_has "RESULT kind=summary internet="
check "summary: overlay root seen" out_has "volatile=1 wifiboot=off"
NET_CTL_INCLUDE_VETH=1 run status
check "veth as a wired port with NET_CTL_INCLUDE_VETH=1" out_has "name=vethnm type=ethernet"
printf '# micropanel\ninterface=eth0\nbind-interfaces\n' > "$work/legacy.conf"
run status
check "OLED menu's server mode: mode=legacy-server" out_has "name=eth0 type=ethernet usb=0 mac=2C:CF:67:4F:66:CA carrier=1 speed=1000 state=connected profile=Wired%20connection%201 mode=legacy-server"
rm -f "$work/legacy.conf"
echo "options cfg80211 ieee80211_regdom=DE" > "$work/modprobe/cfg80211.conf"
run status
check "image with WiFi on at boot: wifiboot=on" out_has "wifiboot=on"
rm -f "$work/modprobe/cfg80211.conf"

echo "== leases"
printf '1791262000 b8:27:eb:01:02:03 192.168.50.23 pi-bench-2 01:b8:27:eb:01:02:03\n1791262500 3c:22:fb:aa:bb:cc 192.168.50.61 * *\n' \
    > "$work/leases/dnsmasq-eth1.leases"
run leases --iface=eth1
check "leases: two lines and a count" out_has "RESULT kind=leases iface=eth1 count=2"
check "lease with host name" out_has "RESULT kind=lease ip=192.168.50.23 mac=B8:27:EB:01:02:03 host=pi-bench-2 expires=1791262000"
check "lease without (*)" out_has "RESULT kind=lease ip=192.168.50.61 mac=3C:22:FB:AA:BB:CC host= expires=1791262500"

echo "== wifi-scan"
reset ok "u-saved Workshop yes yes"
run wifi-scan
check "strongest BSSID wins" out_has "RESULT kind=ap ssid=Workshop signal=71 security=wpa2 band=5 saved=1 active=0"
check "one line per SSID" [ "$(printf '%s\n' "$out" | grep -c 'ssid=Workshop ')" = 2 ]   # the ap and the saved line
check "percent-encoded SSID (space, non-ASCII)" out_has "ssid=Caf%C3%A9%20Am%20Markt signal=52 security=open band=2.4"
check "escaped colon unescaped" out_has "ssid=softliQ:MC_ab0f32 signal=47 security=wpa2"
check "WPA3 only" out_has "ssid=Lab-Bench signal=58 security=wpa3 band=5"
check "enterprise" out_has "ssid=Corp signal=41 security=enterprise"
check "hidden (empty SSID) skipped" sh -c "! printf '%s' \"\$1\" | grep -q 'ssid= '" _ "$out"
check "saved profile line" out_has "RESULT kind=saved ssid=Workshop uuid=u-saved autoconnect=1 active=1 hidden=0 inrange=1"
check "no rescan unless asked" called "device|wifi|list|--rescan|no|"
run wifi-scan --rescan
check "--rescan rescans" called "device|wifi|list|--rescan|yes|"

echo "== wifi-connect: a new network, password on stdin"
reset ok "u-old Workshop yes no"
run_pw wifi-connect --ssid=Workshop
check "exit 0" [ "$rc" = 0 ]
check "profile added with WPA-PSK, no password in it" called "connection|add|type|wifi|con-name|Workshop|ifname|*|ssid|Workshop|connection.autoconnect|yes|wifi-sec.key-mgmt|wpa-psk|"
check "up with passwd-file on stdin" called "--wait|60|connection|up|uuid|bbbbbbbb-0000-0000-0000-000000000002|passwd-file|/dev/stdin|"
check "password reached nmcli on stdin" grep -qxF "802-11-wireless-security.psk:$PASSWORD" "$FAKE_NM/stdin"
check "password in no argument list" no_password_in_argv
check "password not printed" sh -c "! printf '%s' \"\$1\" | grep -q s3cret" _ "$out"
check "the older profile for it is deleted" called "connection|delete|uuid|u-old|"
check "progress phases, forwards only" [ "$(printf '%s\n' "$out" | grep '^PROGRESS' | tr '\n' ' ')" = "PROGRESS phase=associating PROGRESS phase=authenticating PROGRESS phase=address " ]
check "result line" out_has "RESULT kind=connect ssid=Workshop ok=1 ip=192.168.20.138"

echo "== wifi-connect: wrong password, the previous network comes back"
reset bad-password "u-prev Lab-Bench yes yes"
run_pw wifi-connect --ssid=Workshop
check "exit 3 (restored)" [ "$rc" = 3 ]
check "reason=bad-password, restored named" out_has "restored=Lab-Bench"
check "  reason last" sh -c "printf '%s\n' \"\$1\" | grep -q 'reason=bad-password\$'" _ "$out"
check "the looping attempt is stopped" called "connection|down|uuid|bbbbbbbb-0000-0000-0000-000000000002|"
check "the attempt's profile is deleted" called "connection|delete|uuid|bbbbbbbb-0000-0000-0000-000000000002|"
check "the previous one is brought up" called "--wait|30|connection|up|uuid|u-prev|"
check "password in no argument list" no_password_in_argv
check "no profile left behind" sh -c "! grep -q bbbbbbbb \"\$FAKE_NM/profiles\""

echo "== wifi-connect: no address, nothing before"
reset no-address
run_pw wifi-connect --ssid=Workshop
check "exit 2 (nothing changed)" [ "$rc" = 2 ]
check "reason=no-address" out_has "reason=no-address"
check "no profile left behind" [ ! -s "$FAKE_NM/profiles" ]

echo "== wifi-connect: timeout"
reset timeout "u-prev Lab-Bench yes yes"
run_pw wifi-connect --ssid=Workshop
check "exit 3, reason=timeout" sh -c "[ $rc = 3 ] && printf '%s' \"\$1\" | grep -q 'reason=timeout'" _ "$out"

echo "== wifi-connect: a saved network, no password"
reset ok "u-saved Lab-Bench yes no"
run_nopw wifi-connect --ssid=Lab-Bench
check "exit 0" [ "$rc" = 0 ]
check "the saved profile is brought up" called "--wait|60|connection|up|uuid|u-saved|"
check "  without passwd-file" not_called "passwd-file"
check "  and nothing added" not_called "connection|add|"

echo "== wifi-connect: refusals"
reset ok
run_nopw wifi-connect --ssid=Workshop
check "secured, unsaved, no password: exit 1 need-password" sh -c "[ $rc = 1 ] && printf '%s' \"\$1\" | grep -q 'reason=need-password'" _ "$out"
check "  nothing added" not_called "connection|add|"
run_pw wifi-connect --ssid=Corp
check "enterprise: exit 1 unsupported" sh -c "[ $rc = 1 ] && printf '%s' \"\$1\" | grep -q 'reason=unsupported'" _ "$out"
run_pw wifi-connect --ssid=Nowhere
check "not in range: exit 2 not-found" sh -c "[ $rc = 2 ] && printf '%s' \"\$1\" | grep -q 'reason=not-found'" _ "$out"
out=$(printf 'short\n' | "$script" wifi-connect --ssid=Workshop 2>&1); rc=$?
check "7-character password: exit 1" [ "$rc" = 1 ]
echo disabled > "$FAKE_NM/radio"
run_pw wifi-connect --ssid=Workshop
check "radio off: exit 1 radio-off" sh -c "[ $rc = 1 ] && printf '%s' \"\$1\" | grep -q 'reason=radio-off'" _ "$out"
echo enabled > "$FAKE_NM/radio"
sleep 30 & live=$!
echo "$live" > "$work/lock"
run_pw wifi-connect --ssid=Workshop
check "update lock held: exit 1 locked" sh -c "[ $rc = 1 ] && printf '%s' \"\$1\" | grep -q 'reason=locked'" _ "$out"
kill "$live"; wait "$live" 2>/dev/null
echo 999999 > "$work/lock"
run wifi-forget --ssid=Nothing
check "stale lock ignored" sh -c "! printf '%s' \"\$1\" | grep -q 'reason=locked'" _ "$out"
rm -f "$work/lock"

echo "== wifi-connect: open, encoded name, hidden"
reset ok
run_nopw wifi-connect --ssid=Caf%C3%A9%20Am%20Markt
check "open network: added without security" called "connection|add|type|wifi|con-name|Café Am Markt|ifname|*|ssid|Café Am Markt|connection.autoconnect|yes|"
check "  no key-mgmt" not_called "key-mgmt"
reset ok
run_pw wifi-connect --ssid=Bench%20Hidden --hidden --security=wpa3
check "hidden WPA3: hidden yes, key-mgmt sae" called "ssid|Bench Hidden|connection.autoconnect|yes|802-11-wireless.hidden|yes|wifi-sec.key-mgmt|sae|"
check "  no scan needed" not_called "device|wifi|list"
check "  password in no argument list" no_password_in_argv

echo "== dry run"
reset ok
run_pw wifi-connect --ssid=Workshop --dry-run
check "dry-run connect: exit 0, nothing changed" sh -c "[ $rc = 0 ] && ! grep -qE 'connection\|(add|up|delete|down)' \"\$FAKE_NM/calls\"" _
run wifi-radio --off --dry-run
check "dry-run radio: nothing changed" not_called "radio|wifi|off|"

echo "== forget, auto-connect, disconnect, radio"
reset ok "u-1 Workshop yes no" "u-2 Workshop yes no" "u-3 Other yes no"
run wifi-forget --ssid=Workshop
check "forget deletes every profile of the network" sh -c "called() { grep -qF -- \"\$1\" \"\$FAKE_NM/calls\"; }; called 'connection|delete|uuid|u-1|' && called 'connection|delete|uuid|u-2|' && ! called 'connection|delete|uuid|u-3|'"
run wifi-forget --ssid=Workshop
check "forget an unknown network: exit 2" [ "$rc" = 2 ]
run wifi-autoconnect --ssid=Other --off
check "auto-connect off" called "connection|modify|uuid|u-3|connection.autoconnect|no|"
run wifi-disconnect
check "disconnect" called "device|disconnect|wlan0|"
reset ok
echo disabled > "$FAKE_NM/radio"
run wifi-radio --on
check "radio on: rfkill unblock" called "rfkill unblock wifi"
check "radio on: nmcli radio wifi on" called "radio|wifi|on|"
check "radio on: country set when unset (00)" called "iw reg set DE"
reset ok
echo DE > "$FAKE_NM/country"
run wifi-radio --on
check "radio on: country left alone when set" not_called "iw reg set"
run wifi-radio --off
check "radio off" called "radio|wifi|off|"

echo "== arguments"
run bogus
check "unknown command: exit 1" [ "$rc" = 1 ]
run status --frobnicate
check "unknown option: exit 1" [ "$rc" = 1 ]

echo "== status: the profile of a port, its configured mode"
reset ok
run status
check "eth0: the active profile, bound by name, not saved" out_has "profileuuid=aaaaaaaa-0000-0000-0000-000000000001 cfgprofile=Wired%20connection%201 saved=0 binding=name"
check "eth1 is a USB port" out_has "name=eth1 type=ethernet usb=1 mac=00:E0:4C:69:B1:0E"
# eth1 unplugged, its saved static profile bound to the MAC: the mode it would have
reset ok
echo 0 > "$work/sys/eth1/carrier"
printf '%s\n' 'aaaaaaaa-0000-0000-0000-000000000001|Wired connection 1|auto||||eth0||yes|-999|100|/run/NetworkManager/system-connections/Wired connection 1.nmconnection|eth0|no|auto' \
    'dddddddd-0000-0000-0000-000000000001|USB adapter|manual|10.9.8.7/24|10.9.8.1|10.9.8.1||00:E0:4C:69:B1:0E|yes|0|50|/etc/NetworkManager/system-connections/USB adapter.nmconnection||no|auto' \
    'dddddddd-0000-0000-0000-000000000002|Other adapter|manual|10.1.1.1/24|||||00:11:22:33:44:55|yes|0|60|/etc/NetworkManager/system-connections/Other.nmconnection||no|auto' > "$FAKE_NM/eth"
run status
check "unplugged port: the mode of the profile that would activate" out_has "name=eth1 type=ethernet usb=1 mac=00:E0:4C:69:B1:0E carrier=0"
check "  static" sh -c "printf '%s\n' \"\$1\" | grep 'name=eth1 ' | grep -q ' mode=static '" _ "$out"
check "  the configured values" out_has "profileuuid=dddddddd-0000-0000-0000-000000000001 cfgprofile=USB%20adapter saved=1 binding=mac cfgip=10.9.8.7 cfgprefix=24 cfggateway=10.9.8.1 cfgdns=10.9.8.1"
check "  another adapter's profile is not taken" sh -c "! printf '%s' \"\$1\" | grep -q 'cfgprofile=Other'" _ "$out"

echo "== status: which port reaches the internet"
reset ok
unset NET_CTL_INET
printf 'eth1 192.168.20.1 100\neth0 192.168.1.1 101\n' > "$FAKE_NM/routes"
echo eth0 > "$FAKE_NM/inet"
run status
check "eth0 reaches it" out_has "name=eth0 type=ethernet usb=0 mac=2C:CF:67:4F:66:CA carrier=1 speed=1000 state=connected"
check "  inet=yes on eth0" sh -c "printf '%s\n' \"\$1\" | grep 'name=eth0 ' | grep -q ' inet=yes '" _ "$out"
check "  inet=no on eth1" sh -c "printf '%s\n' \"\$1\" | grep 'name=eth1 ' | grep -q ' inet=no '" _ "$out"
check "via the port that reaches it, not the default route's" out_has "RESULT kind=summary internet=yes via=eth0 defaultdev=eth1"
check "  both targets tried on each port, in parallel" [ "$(grep -c '^ping ' "$FAKE_NM/pings")" = 4 ]
run status
check "the answer is kept: no new pings within 20 s" [ "$(grep -c '^ping ' "$FAKE_NM/pings")" = 4 ]
reset ok
unset NET_CTL_INET
: > "$FAKE_NM/inet"
run status
check "no port reaches it: internet=no" out_has "RESULT kind=summary internet=no via= defaultdev=eth0"
check "a port without a gateway is not checked" sh -c "! grep -q '^ping eth1' \"\$FAKE_NM/pings\"" _

echo "== wired-set: static on the USB adapter (bound to its MAC)"
reset ok
run wired-set --iface=eth1 --mode=static --ip=192.168.20.250 --prefix=24 --gateway=192.168.20.1 --dns=1.1.1.1,9.9.9.9
check "exit 0" [ "$rc" = 0 ]
check "modify: manual, address, gateway, DNS, MAC bound, name binding dropped" called "connection|modify|uuid|aaaaaaaa-0000-0000-0000-000000000002|ipv4.method|manual|ipv4.addresses|192.168.20.250/24|ipv4.gateway|192.168.20.1|ipv4.dns|1.1.1.1,9.9.9.9|ipv4.never-default|no|ipv6.method|auto|802-3-ethernet.mac-address|00:E0:4C:69:B1:0E|connection.interface-name||"
check "up on that port" called "--wait|60|connection|up|uuid|aaaaaaaa-0000-0000-0000-000000000002|ifname|eth1|"
check "result: ok, address, MAC binding" out_has "RESULT kind=wired iface=eth1 mode=static ok=1 ip=192.168.20.250 profileuuid=aaaaaaaa-0000-0000-0000-000000000002 binding=mac"
check "the profile is saved now" grep -q '^aaaaaaaa-0000-0000-0000-000000000002|.*|/etc/NetworkManager/system-connections/' "$FAKE_NM/eth"
run status
check "status then: static, bound to the MAC, saved" out_has "cfgprofile=Wired%20connection%202 saved=1 binding=mac cfgip=192.168.20.250 cfgprefix=24 cfggateway=192.168.20.1 cfgdns=1.1.1.1,9.9.9.9"

echo "== wired-set: client, and eth0 keeps its name binding"
reset ok
run wired-set --iface=eth0 --mode=client
check "client: auto, everything else cleared" called "connection|modify|uuid|aaaaaaaa-0000-0000-0000-000000000001|ipv4.method|auto|ipv4.addresses||ipv4.gateway||ipv4.dns||ipv4.never-default|no|ipv6.method|auto|"
check "  no MAC binding for the built-in port" sh -c "! grep '^connection|modify' \"\$FAKE_NM/calls\" | grep -q mac-address"
check "  exit 0" [ "$rc" = 0 ]

echo "== wired-set: server mode and its preconditions"
reset ok
echo yes > "$FAKE_NM/dnsmasq-active"; echo enabled > "$FAKE_NM/dnsmasq-enabled"
run wired-set --iface=eth1 --mode=server --ip=192.168.50.1
check "exit 0" [ "$rc" = 0 ]
check "the system dnsmasq is stopped" called "systemctl stop dnsmasq.service"
check "  and masked" called "systemctl mask dnsmasq.service"
check "the drop-in is written" grep -qx 'dhcp-option=3' "$NET_CTL_SHARED_DIR/90-micropanel-no-gateway.conf"
check "  both options" grep -qx 'dhcp-option=6' "$NET_CTL_SHARED_DIR/90-micropanel-no-gateway.conf"
check "shared, /24 by default, never-default, no IPv6" called "ipv4.method|shared|ipv4.addresses|192.168.50.1/24|ipv4.gateway||ipv4.dns||ipv4.never-default|yes|ipv6.method|disabled|"
check "success only after NetworkManager's dnsmasq runs with the address and the drop-in directory" out_has "RESULT kind=wired iface=eth1 mode=server ok=1 ip=192.168.50.1"
reset ok
echo no > "$FAKE_NM/dnsmasq-active"; echo masked > "$FAKE_NM/dnsmasq-enabled"
mkdir -p "$NET_CTL_SHARED_DIR"
printf '# network-manager-app: serve addresses only - no router, no DNS server announced\ndhcp-option=3\ndhcp-option=6\n' \
    > "$NET_CTL_SHARED_DIR/90-micropanel-no-gateway.conf"
run wired-set --iface=eth1 --mode=server --ip=192.168.50.1
check "preconditions already true (the image): no stop, no mask" sh -c "! grep -qE 'systemctl (stop|mask)' \"\$FAKE_NM/calls\""
check "  no rewrite" sh -c "! printf '%s' \"\$1\" | grep -q 'NOTICE writing'" _ "$out"

echo "== wired-set: the check fails, the previous settings come back"
reset ok
echo nodnsmasq > "$FAKE_NM/wiredmode"
run wired-set --iface=eth1 --mode=server --ip=192.168.50.1
check "no dnsmasq for the port: exit 3" [ "$rc" = 3 ]
check "  reason=check-failed, restored=1" out_has "restored=1"
check "  reason last" sh -c "printf '%s\n' \"\$1\" | grep -q 'reason=check-failed\$'" _ "$out"
check "  the previous values put back, binding included" called "connection|modify|uuid|aaaaaaaa-0000-0000-0000-000000000002|ipv4.method|auto|ipv4.addresses||ipv4.gateway||ipv4.dns||ipv4.never-default|no|ipv6.method|auto|802-3-ethernet.mac-address||connection.interface-name|eth1|"
check "  and up again" [ "$(grep -c '^--wait|60|connection|up|uuid|aaaaaaaa-0000-0000-0000-000000000002|ifname|eth1|' "$FAKE_NM/calls")" = 2 ]
check "  the profile is a DHCP client again" grep -q '^aaaaaaaa-0000-0000-0000-000000000002|Wired connection 2|auto|' "$FAKE_NM/eth"
reset ok
echo fail > "$FAKE_NM/wiredmode"
run wired-set --iface=eth1 --mode=static --ip=192.168.20.250 --prefix=24
check "activation fails: exit 3, reason=activation-failed" sh -c "[ $rc = 3 ] && printf '%s' \"\$1\" | grep -q 'reason=activation-failed'" _ "$out"
check "  the previous method is back" grep -q '^aaaaaaaa-0000-0000-0000-000000000002|Wired connection 2|auto|' "$FAKE_NM/eth"
check "  the detail is the failure's, not the restore's" out_has "detail=Error:%20Connection%20activation%20failed:%20IP%20configuration%20could%20not%20be%20reserved"

echo "== wired-set: refusals"
reset ok
run wired-set --iface=eth1 --mode=server --ip=192.168.1.5
check "server subnet overlaps eth0's: exit 1 overlap" sh -c "[ $rc = 1 ] && printf '%s' \"\$1\" | grep -q 'reason=overlap'" _ "$out"
check "  says which" out_has "detail=192.168.1.5/24%20overlaps%20192.168.1.170/24%20on%20eth0"
check "  nothing modified" not_called "connection|modify"
for bad in "--mode=static --ip=192.168.1.300 --prefix=24" "--mode=static --ip=10.0.0.5 --prefix=31" \
           "--mode=static --ip=10.0.0.0 --prefix=24" "--mode=static --ip=10.0.0.5 --prefix=24 --gateway=gw" \
           "--mode=static --ip=10.0.0.5 --prefix=24 --dns=1.1.1.1,x" "--mode=bridge" ""; do
    # shellcheck disable=SC2086
    run wired-set --iface=eth1 $bad
    check "refused: ${bad:-no mode}" sh -c "[ $rc = 1 ] && printf '%s' \"\$1\" | grep -q 'reason=bad-arguments'" _ "$out"
done
run wired-set --iface=wlan0 --mode=client
check "not a wired port: exit 1" [ "$rc" = 1 ]
run wired-set --iface=vethnm --mode=client
check "a veth is not a port without the test seam" [ "$rc" = 1 ]
check "nothing modified by any refusal" not_called "connection|modify"

echo "== wired-set: no cable, no profile, legacy takeover, dry run"
reset ok
echo 0 > "$work/sys/eth1/carrier"
run wired-set --iface=eth1 --mode=static --ip=10.20.30.2 --prefix=24
check "no cable: saved, pending, nothing brought up" sh -c "[ $rc = 0 ] && printf '%s' \"\$1\" | grep -q 'pending=1'" _ "$out"
check "  no activation" not_called "connection|up|"
reset ok
printf '%s\n' "$W1" > "$FAKE_NM/eth"
run wired-set --iface=eth1 --mode=client
check "no profile: a new one for the adapter, bound to its MAC" called "connection|add|type|ethernet|con-name|USB adapter 00:E0:4C:69:B1:0E|ifname|eth1|connection.autoconnect|yes|ipv4.method|auto|"
check "  MAC set on the new profile" called "802-3-ethernet.mac-address|00:E0:4C:69:B1:0E|connection.interface-name||"
check "  exit 0" [ "$rc" = 0 ]
reset ok
printf '# Micropanel DHCP Server Configuration\ninterface=eth1\nbind-interfaces\n' > "$work/legacy.conf"
run wired-set --iface=eth1 --mode=client
check "legacy server: the OLED menu's stop first (stop, mask, conf removed)" sh -c "grep -q 'systemctl stop dnsmasq.service' \"\$FAKE_NM/calls\" && grep -q 'systemctl mask dnsmasq.service' \"\$FAKE_NM/calls\" && [ ! -f '$work/legacy.conf' ]"
check "  then the new mode" called "connection|modify|uuid|aaaaaaaa-0000-0000-0000-000000000002|ipv4.method|auto|"
reset ok
run wired-set --iface=eth1 --mode=server --ip=192.168.50.1 --dry-run
check "dry run: exit 0, says what it would do" sh -c "[ $rc = 0 ] && printf '%s' \"\$1\" | grep -q 'NOTICE dry-run: nmcli connection modify uuid aaaaaaaa-0000-0000-0000-000000000002'" _ "$out"
check "  nothing modified, nothing up, no systemctl" sh -c "! grep -qE 'connection\|(modify|up)|systemctl (stop|mask)' \"\$FAKE_NM/calls\""

echo "== dhcp-probe"
reset ok
printf 'OFFER server=192.168.1.1 offered=192.168.1.57 router=192.168.1.1\nOFFER server=192.168.20.1 offered=192.168.20.137 router=192.168.20.1\nOFFER server=192.168.1.1 offered=192.168.1.58\nOFFER server=192.168.1.170 offered=192.168.1.200\n' > "$FAKE_NM/offers"
run dhcp-probe --iface=eth0
check "one line per server" out_has "RESULT kind=offer iface=eth0 server=192.168.1.1 offered=192.168.1.57 router=192.168.1.1"
check "  the second server" out_has "RESULT kind=offer iface=eth0 server=192.168.20.1 offered=192.168.20.137 router=192.168.20.1"
check "  a server seen twice counts once, the rig's own address not at all" out_has "RESULT kind=probe iface=eth0 servers=2 carrier=1"
reset ok
: > "$FAKE_NM/offers"
run dhcp-probe --iface=eth0
check "nobody answers: servers=0" out_has "RESULT kind=probe iface=eth0 servers=0 carrier=1"
echo 0 > "$work/sys/eth1/carrier"
run dhcp-probe --iface=eth1
check "no cable: not sent, carrier=0" out_has "RESULT kind=probe iface=eth1 servers=0 carrier=0"
if command -v python3 >/dev/null 2>&1; then
    check "net-dhcp-probe.py: DISCOVER and OFFER packets (self-test)" python3 "$here/../src/net-dhcp-probe.py" --self-test
fi

echo "== a change runs as its own systemd unit (rule 6a)"
reset ok
NET_CTL_UID=0 NET_CTL_INCLUDE_VETH=1 run wired-set --iface=eth1 --mode=client
check "as root: re-executed through systemd-run --pipe --wait" called "systemd-run --quiet --collect --pipe --wait --description=net-ctl.sh wired-set --setenv=NET_CTL_DETACHED=1"
check "  NET_CTL_* settings go along" sh -c "grep '^systemd-run' \"\$FAKE_NM/calls\" | grep -q -- '--setenv=NET_CTL_INCLUDE_VETH'"
check "  only once" [ "$(grep -c '^systemd-run' "$FAKE_NM/calls")" = 1 ]
check "  inside, it says so" out_has "NOTICE detached"
check "  and does the change" sh -c "[ $rc = 0 ] && grep -q '^connection|modify|uuid|aaaaaaaa-0000-0000-0000-000000000002|' \"\$FAKE_NM/calls\""
NET_CTL_UID=0 run wired-set --iface=eth1 --mode=bridge
check "the exit code comes back through it" [ "$rc" = 1 ]
reset ok
NET_CTL_UID=0 run status
check "a read does not detach" not_called "systemd-run"
NET_CTL_UID=0 run wired-set --iface=eth1 --mode=client --dry-run
check "a dry run does not detach" not_called "systemd-run"
run wired-set --iface=eth1 --mode=client
check "not root: runs in place" not_called "systemd-run"
reset ok
NET_CTL_UID=0 run_pw wifi-connect --ssid=Workshop
check "a WiFi join detaches too, password still on stdin only" sh -c "grep -q '^systemd-run' \"\$FAKE_NM/calls\" && ! grep -q s3cret \"\$FAKE_NM/calls\" && grep -qxF '802-11-wireless-security.psk:$PASSWORD' \"\$FAKE_NM/stdin\""

echo "== OWE is not open"
reset ok
run wifi-scan
check "OWE (enhanced open) is 'other'" out_has "ssid=Enhanced%20Open signal=44 security=other"
run_nopw wifi-connect --ssid=Enhanced%20Open
check "  and refused" sh -c "[ $rc = 1 ] && printf '%s' \"\$1\" | grep -q 'reason=unsupported'" _ "$out"

echo "== ping (output as ping -O printed it on the rig)"
reset ok
cat > "$FAKE_NM/ping-out" <<'EOF'
PING 10.99.0.2 (10.99.0.2) 56(84) bytes of data.
no answer yet for icmp_seq=1
64 bytes from 10.99.0.2: icmp_seq=2 ttl=64 time=0.092 ms
no answer yet for icmp_seq=3
no answer yet for icmp_seq=4

--- 10.99.0.2 ping statistics ---
5 packets transmitted, 1 received, 80% packet loss, time 4090ms
rtt min/avg/max/mdev = 0.092/0.092/0.092/0.000 ms
EOF
run ping --target=10.99.0.2 --iface=eth1 --count=5
check "exit 0 when something answered" [ "$rc" = 0 ]
check "ping -n -O, count, timeout, port" called "ping -n -O -c 5 -W 2 -I eth1 10.99.0.2"
check "a loss as it happens" out_has "RESULT kind=lost seq=1"
check "a reply with its time" out_has "RESULT kind=reply seq=2 ms=0.092 from=10.99.0.2"
check "the summary" out_has "RESULT kind=ping target=10.99.0.2 sent=5 received=1 avg=0.092 loss=80"
printf 'PING 10.99.0.77 (10.99.0.77) 56(84) bytes of data.\nno answer yet for icmp_seq=1\n\n--- 10.99.0.77 ping statistics ---\n2 packets transmitted, 0 received, 100%% packet loss, time 1004ms\n' > "$FAKE_NM/ping-out"
echo 1 > "$FAKE_NM/ping-rc"
run ping --target=10.99.0.77 --count=2
check "nothing answered: exit 2" [ "$rc" = 2 ]
check "  all lost" out_has "RESULT kind=ping target=10.99.0.77 sent=2 received=0 avg= loss=100"
echo "ping: no.such.host: Name or service not known" > "$FAKE_NM/ping-out"
echo 2 > "$FAKE_NM/ping-rc"
run ping --target=no.such.host
check "unknown host named" sh -c "[ $rc = 2 ] && printf '%s' \"\$1\" | grep -q 'reason=unknown-host'" _ "$out"
run ping --target=-f
check "an option as target: refused" sh -c "[ $rc = 1 ] && printf '%s' \"\$1\" | grep -q 'reason=bad-arguments'" _ "$out"
run ping --target='a;reboot'
check "shell characters: refused" [ "$rc" = 1 ]
run ping --target=1.2.3.4 --count=101
check "count over 100: refused" [ "$rc" = 1 ]
check "  and no ping ran" not_called "ping -n -O -c 101"

echo "== a tool stops with its caller (also after SIGKILL)"
reset ok
: > "$FAKE_NM/ping-hang"
sh -c '"$1" ping --target=10.99.0.2 --count=50 > /dev/null 2>&1' _ "$script" &
caller=$!
n=0
# shellcheck disable=SC2009 # pgrep is a stand-in here
while ! ps -eo args | grep -q '^sleep 37$' && [ $n -lt 50 ]; do sleep 0.1; n=$((n + 1)); done
check "the ping runs" sh -c "ps -eo args | grep -q '^sleep 37$'"
kill -KILL "$caller"
n=0
# shellcheck disable=SC2009
while ps -eo args | grep -q '^sleep 37$' && [ $n -lt 40 ]; do sleep 0.1; n=$((n + 1)); done
check "  and is gone within seconds of its caller" sh -c "! ps -eo args | grep -q '^sleep 37$'"
check "  its FIFO too" sh -c "! ls \"\$TMPDIR\"/net-ctl-tool.* >/dev/null 2>&1"

echo "== internet-check"
reset ok
echo eth0 > "$FAKE_NM/inet"
echo "ok 1 11.5 142.251.151.119" > "$FAKE_NM/dns"
printf '204 0.141' > "$FAKE_NM/curl"
NET_CTL_UID=0 run internet-check --iface=eth0
check "exit 0" [ "$rc" = 0 ]
check "the gateway answers" out_has "RESULT kind=check step=gateway ok=1 ms=0.31 target=192.168.1.1"
check "the port's DNS server, bound to the port" sh -c "printf '%s' \"\$1\" | grep -q 'step=dns ok=1 ms=11.5 server=192.168.1.1 name=www.google.com addr=142.251.151.119 bound=1' && grep -qx 'python3 www.google.com 192.168.1.1 eth0' \"\$FAKE_NM/calls\"" _ "$out"
check "HTTPS 204, out of the port, to the address the port's DNS gave" sh -c "printf '%s' \"\$1\" | grep -q 'step=https ok=1 ms=141 code=204' && grep -q 'curl .*--interface if!eth0 --resolve www.google.com:443:142.251.151.119 https://www.google.com/generate_204' \"\$FAKE_NM/calls\"" _ "$out"
check "the verdict" out_has "RESULT kind=internet iface=eth0 ok=1"
run internet-check --iface=eth0
check "not root: curl by interface name, DNS not bound" sh -c "grep -q 'curl .*--interface eth0 ' \"\$FAKE_NM/calls\" && grep -qx 'python3 www.google.com 192.168.1.1 ' \"\$FAKE_NM/calls\""
echo "servfail 1 3.2" > "$FAKE_NM/dns"
printf 'curl: (28) Operation timed out after 8001 milliseconds\n000 8.001' > "$FAKE_NM/curl"
NET_CTL_UID=0 run internet-check --iface=eth0
check "DNS refuses: named, exit 2" sh -c "[ $rc = 2 ] && printf '%s' \"\$1\" | grep -q 'step=dns ok=0 server=192.168.1.1 name=www.google.com bound=1 reason=servfail'" _ "$out"
check "  HTTPS still tried, without an address: timeout" out_has "step=https ok=0 url=https://www.google.com/generate_204 bound=1 reason=timeout"
check "  verdict" out_has "RESULT kind=internet iface=eth0 ok=0"
echo "ok 1 9.0 142.251.151.119" > "$FAKE_NM/dns"
printf '302 0.120' > "$FAKE_NM/curl"
NET_CTL_UID=0 run internet-check --iface=eth0
check "a captive portal's redirect is not internet" out_has "step=https ok=0 url=https://www.google.com/generate_204 bound=1 reason=http-302"
: > "$FAKE_NM/inet"
NET_CTL_UID=0 run internet-check --iface=eth0
check "a gateway that does not answer" out_has "step=gateway ok=0 target=192.168.1.1 reason=no-reply"
: > "$FAKE_NM/routes"
run internet-check
check "no default route: says so, exit 2" sh -c "[ $rc = 2 ] && printf '%s' \"\$1\" | grep -q 'step=gateway ok=0 reason=no-route'" _ "$out"

echo "== iperf3 (output as iperf3 3.12 printed it on the rig)"
reset ok
cat > "$FAKE_NM/iperf-out" <<'EOF'
Connecting to host 10.99.0.2, port 5201
[  5] local 10.99.0.1 port 44332 connected to 10.99.0.2 port 5201
[ ID] Interval           Transfer     Bitrate         Retr  Cwnd
[  5]   0.00-1.00   sec  1.33 GBytes  11.4 Gbits/sec    0    781 KBytes
[  5]   1.00-2.00   sec   112 MBytes   940 Mbits/sec    3    781 KBytes
- - - - - - - - - - - - - - - - - - - - - - - - -
[ ID] Interval           Transfer     Bitrate         Retr
[  5]   0.00-2.00   sec  3.43 GBytes  14.7 Gbits/sec    3             sender
[  5]   0.00-2.00   sec  3.43 GBytes  14.7 Gbits/sec                  receiver

iperf Done.
EOF
run iperf-client --host=10.99.0.2 --secs=5
check "exit 0" [ "$rc" = 0 ]
check "iperf3 -c, port, time, flushed lines" called "iperf3 -c 10.99.0.2 -p 5201 -t 5 -i 1 --forceflush --connect-timeout 3000"
check "one line a second, in Mbit/s" sh -c "printf '%s' \"\$1\" | grep -q 'RESULT kind=iperf interval=0.00-1.00 mbit=11400.0' && printf '%s' \"\$1\" | grep -q 'RESULT kind=iperf interval=1.00-2.00 mbit=940.0'" _ "$out"
check "sender, with retransmissions" out_has "RESULT kind=iperf-sum role=sender interval=0.00-2.00 mbit=14700.0 retr=3"
check "receiver" out_has "RESULT kind=iperf-sum role=receiver interval=0.00-2.00 mbit=14700.0"
check "done" out_has "RESULT kind=iperf-done ok=1 host=10.99.0.2"
cat > "$FAKE_NM/iperf-out" <<'EOF'
Connecting to host 10.99.0.2, port 5201
Reverse mode, remote host 10.99.0.2 is sending
[  5] local 10.99.0.1 port 60797 connected to 10.99.0.2 port 5201
[ ID] Interval           Transfer     Bitrate         Jitter    Lost/Total Datagrams
[  5]   0.00-1.00   sec  11.9 MBytes  99.9 Mbits/sec  0.002 ms  0/8626 (0%)
- - - - - - - - - - - - - - - - - - - - - - - - -
[ ID] Interval           Transfer     Bitrate         Jitter    Lost/Total Datagrams
[  5]   0.00-2.00   sec  23.8 MBytes   100 Mbits/sec  0.000 ms  0/17261 (0%)  sender
[  5]   0.00-2.00   sec  23.8 MBytes  98.2 Mbits/sec  0.031 ms  12/17258 (0.07%)  receiver

iperf Done.
EOF
run iperf-client --host=10.99.0.2 --secs=10 --udp --reverse
check "UDP at 100 Mbit/s, reverse" called "iperf3 -c 10.99.0.2 -p 5201 -t 10 -i 1 --forceflush --connect-timeout 3000 -u -b 100M -R"
check "UDP: jitter and losses" out_has "RESULT kind=iperf-sum role=receiver interval=0.00-2.00 mbit=98.2 jitter=0.031 lost=12 packets=17258"
echo "iperf3: error - unable to connect to server: Connection refused" > "$FAKE_NM/iperf-out"
echo 1 > "$FAKE_NM/iperf-rc"
run iperf-client --host=192.168.1.1 --secs=5
check "no server: refused, exit 2" sh -c "[ $rc = 2 ] && printf '%s' \"\$1\" | grep -q 'RESULT kind=iperf-done ok=0 host=192.168.1.1 reason=refused'" _ "$out"
run iperf-client --host=10.99.0.2 --secs=7
check "seconds other than 5, 10, 30: refused" sh -c "[ $rc = 1 ] && printf '%s' \"\$1\" | grep -q 'kind=iperf-done ok=0'" _ "$out"
reset ok
: > "$FAKE_NM/ss-busy"
run iperf-server --start
check "port 5201 taken: port-busy, exit 2" sh -c "[ $rc = 2 ] && printf '%s' \"\$1\" | grep -q 'RESULT kind=iperf-server running=0 port=5201 reason=port-busy'" _ "$out"
check "  and no second server" not_called "iperf3 -s"
rm "$FAKE_NM/ss-busy"
cat > "$FAKE_NM/iperf-out" <<'EOF'
-----------------------------------------------------------
Server listening on 5201 (test #1)
-----------------------------------------------------------
Accepted connection from 10.99.0.2, port 43934
[  5] local 10.99.0.1 port 5201 connected to 10.99.0.2 port 43938
[ ID] Interval           Transfer     Bitrate
[  5]   0.00-1.00   sec  1.02 GBytes  8.75 Gbits/sec
- - - - - - - - - - - - - - - - - - - - - - - - -
[ ID] Interval           Transfer     Bitrate
[  5]   0.00-2.00   sec  2.44 GBytes  10.5 Gbits/sec                  receiver
EOF
run iperf-server --start
check "server: its addresses" out_has "RESULT kind=iperf-server running=1 port=5201 addrs=192.168.1.170,192.168.20.164"
check "  iperf3 -s on 5201" called "iperf3 -s -p 5201 -i 1 --forceflush"
check "  the client, its seconds and its result" sh -c "printf '%s' \"\$1\" | grep -q 'RESULT kind=iperf-peer from=10.99.0.2' && printf '%s' \"\$1\" | grep -q 'RESULT kind=iperf interval=0.00-1.00 mbit=8750.0' && printf '%s' \"\$1\" | grep -q 'RESULT kind=iperf-sum role=receiver interval=0.00-2.00 mbit=10500.0'" _ "$out"
check "  ends with running=0" sh -c "printf '%s\n' \"\$1\" | tail -n 1 | grep -q 'RESULT kind=iperf-server running=0'" _ "$out"

echo "== stopped tools and reads leave nothing behind"
reset ok
: > "$FAKE_NM/iperf-hang"
"$script" iperf-server --start > /dev/null 2>&1 &
n=0
while [ ! -s "$TMPDIR/net-ctl-iperf-server-$(id -u).pid" ] && [ $n -lt 50 ]; do sleep 0.1; n=$((n + 1)); done
run iperf-server --stop
n=0
# shellcheck disable=SC2009 # pgrep is a stand-in here
while ps -eo args | grep -q '^sleep 38$' && [ $n -lt 40 ]; do sleep 0.1; n=$((n + 1)); done
check "iperf-server --stop ends the server and its iperf3" sh -c "! ps -eo args | grep -q '^sleep 38\$'"
check "  and the pid file is gone" [ ! -e "$TMPDIR/net-ctl-iperf-server-$(id -u).pid" ]
sleep 60 &
bystander=$!
echo "$bystander" > "$TMPDIR/net-ctl-iperf-server-$(id -u).pid"
run iperf-server --stop
check "a stale pid file: --stop signals no one else" kill -0 "$bystander"
kill "$bystander" 2>/dev/null
# dash (the rig's sh) runs no EXIT trap when a signal ends the shell: the
# app's TERM to a status used to leave its directory behind (bash, the host's
# sh, runs it anyway, so on bash this only shows the fix does no harm). A
# trapped TERM waits for the running pings (here 2 s; real ones give up in 1 s)
reset ok
echo eth0 > "$FAKE_NM/inet"
: > "$FAKE_NM/ping-slow"
NET_CTL_INET='' "$script" status > /dev/null 2>&1 &
st=$!
n=0
while ! ls "$TMPDIR"/net-ctl-st.* >/dev/null 2>&1 && [ $n -lt 100 ]; do sleep 0.1; n=$((n + 1)); done
kill -TERM "$st"
n=0
while ls "$TMPDIR"/net-ctl-st.* >/dev/null 2>&1 && [ $n -lt 100 ]; do sleep 0.1; n=$((n + 1)); done
check "a status stopped with TERM removes its directory" sh -c "! ls \"\$TMPDIR\"/net-ctl-st.* >/dev/null 2>&1"
wait "$st" 2>/dev/null

if [ "$failures" -gt 0 ]; then echo "net-ctl: $failures failure(s)"; exit 1; fi
echo "net-ctl: PASS"
