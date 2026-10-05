#!/bin/sh
# test_net_ctl.sh - the real net-ctl.sh against tests/fake-nmcli.
#
# Checks the exact nmcli commands net-ctl.sh composes, the restore after a
# failed join, the exit codes, and that a WiFi password reaches nmcli only on
# stdin - never in an argument list. No root, no network, no NetworkManager.
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
chmod +x "$work/bin/"*
echo 1 > "$work/sys/eth0/carrier"; echo 1000 > "$work/sys/eth0/speed"
# a veth is virtual in sysfs, whatever type NetworkManager gives it
mkdir -p "$work/sys/devices/virtual/net/vethnm"
ln -s devices/virtual/net/vethnm "$work/sys/vethnm"
printf 'overlayroot / overlay rw 0 0\n' > "$work/mounts"

export PATH="$work/bin:$PATH" FAKE_NM="$work/nm"
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
reset() {
    rm -rf "$FAKE_NM"; mkdir -p "$FAKE_NM"
    echo "$1" > "$FAKE_NM/mode"; shift
    : > "$FAKE_NM/profiles"
    for p in "$@"; do printf '%s\n' "$p" | tr ' ' '\t' >> "$FAKE_NM/profiles"; done
    : > "$FAKE_NM/calls"
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
check "lease with host name" out_has "RESULT kind=lease ip=192.168.50.23 mac=b8:27:eb:01:02:03 host=pi-bench-2 expires=1791262000"
check "lease without (*)" out_has "RESULT kind=lease ip=192.168.50.61 mac=3c:22:fb:aa:bb:cc host= expires=1791262500"

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

if [ "$failures" -gt 0 ]; then echo "net-ctl: $failures failure(s)"; exit 1; fi
echo "net-ctl: PASS"
