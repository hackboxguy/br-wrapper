#!/bin/sh
# net-ctl.sh - network-manager-app's only link to NetworkManager.
#
# Every read of network state and every change the app makes goes through this
# script; the app never calls nmcli itself. POSIX sh, so the micropanel menu
# can call it too. Reads run as the app user; changes (and the lease list) run
# as root through "sudo -n".
#
# Output, one line each (the app's parser and tests/test_parser.cpp):
#   RESULT key=value ... [reason=<code>]   one per object; values percent-encoded
#   PROGRESS phase=<name>                  while a change runs
#   NOTICE <text>                          anything else worth showing
# Values are percent-encoded: every byte outside [A-Za-z0-9._~:/,@+-] becomes
# %XX, so an SSID with spaces, "=", "%", quotes or non-ASCII bytes survives the
# line protocol. --ssid= takes the same encoding.
#
# Exit codes: 0 done; 1 refused (bad arguments, lock held, unsupported
# security, a password is needed); 2 failed, nothing changed; 3 failed and the
# previous settings were restored; 4 NetworkManager not available.
#
# Commands (network-manager-app-plan.md, section 3.1):
#   available                       exit 0 when nmcli exists and NetworkManager runs
#   status                          RESULT kind=iface ... per interface, then kind=summary
#   monitor                         NOTICE changed whenever NetworkManager reports a change
#   leases --iface=IF               RESULT kind=lease ... (root: the lease file is root-only)
#   wifi-scan [--rescan]            RESULT kind=ap ... per SSID, then kind=saved ... per profile
#   wifi-connect --ssid=S [--hidden] [--security=open|wpa2|wpa3]
#                                   password on stdin when one is needed
#   wifi-disconnect                 disconnects the WiFi device
#   wifi-forget --ssid=S            deletes the saved profile(s) for S
#   wifi-autoconnect --ssid=S --on|--off
#   wifi-radio --on|--off           --on also unblocks rfkill and sets the country if unset
#   wired-set --iface=IF --mode=client
#   wired-set --iface=IF --mode=static --ip=A --prefix=N [--gateway=G] [--dns=D[,D]]
#   wired-set --iface=IF --mode=server --ip=A [--prefix=24]
#                                   the port's profile (bound to the MAC for a USB
#                                   adapter); restores the previous settings if the
#                                   new ones do not come up (exit 3)
#   dhcp-probe --iface=IF           RESULT kind=offer ... per answering DHCP server,
#                                   then kind=probe servers=N carrier=0|1 (root)
# Changes take --dry-run: say what would run, change nothing.
#
# A change outlives its caller (plan rule 6a): run as root with systemd-run
# available, a change re-executes itself as a transient unit
# (systemd-run --pipe --wait) - stdin, output and exit code pass through, and
# a SIGKILL of the app, of sudo or of systemd-run (a launcher restart kills
# the app's whole cgroup) leaves the change and its restore running to the
# end. NET_CTL_* variables are carried into the unit. Reads never detach.
#
# Test seams (never set on a rig): NET_CTL_SYSFS (/sys/class/net),
# NET_CTL_LEASE_DIR, NET_CTL_LEGACY_CONF, NET_CTL_LOCK, NET_CTL_MOUNTS,
# NET_CTL_MODPROBE_DIR, NET_CTL_INCLUDE_VETH=1 (treat veth devices as wired
# ports), NET_CTL_COUNTRY (country set by wifi-radio --on, default DE),
# NET_CTL_SHARED_DIR (NetworkManager's dnsmasq-shared.d), NET_CTL_PROBE (the
# probe program), NET_CTL_INET=yes|no|skip (the internet check's answer, or
# none), NET_CTL_INET_TARGETS, NET_CTL_UID (the uid the detach rule sees),
# NET_CTL_DETACHED=1 (set inside the transient unit).

# nft, iw and rfkill live in /usr/sbin, which is not in pi's non-login PATH.
# Appended, not prepended: on merged-/usr hosts /usr/sbin also holds nmcli, and
# the tests put a fake nmcli first in PATH.
PATH=$PATH:/usr/sbin:/sbin
LC_ALL=C
export PATH LC_ALL

SYSFS=${NET_CTL_SYSFS:-/sys/class/net}
LEASE_DIR=${NET_CTL_LEASE_DIR:-/var/lib/NetworkManager}
LEGACY_CONF=${NET_CTL_LEGACY_CONF:-/etc/dnsmasq.d/micropanel-dhcp-server.conf}
LOCK=${NET_CTL_LOCK:-/tmp/system-update.lock}
MOUNTS=${NET_CTL_MOUNTS:-/proc/mounts}
MODPROBE_DIR=${NET_CTL_MODPROBE_DIR:-/etc/modprobe.d}
COUNTRY=${NET_CTL_COUNTRY:-DE}
SHARED_DIR=${NET_CTL_SHARED_DIR:-/etc/NetworkManager/dnsmasq-shared.d}
DROPIN=$SHARED_DIR/90-micropanel-no-gateway.conf
PROBE=${NET_CTL_PROBE:-$(dirname "$(readlink -f "$0")")/net-dhcp-probe.py}
INET_TARGETS=${NET_CTL_INET_TARGETS:-1.1.1.1 8.8.8.8}
CONNECT_WAIT=60
WIRED_WAIT=60

tab=$(printf '\t')
us=$(printf '\037')

# ---- output helpers ---------------------------------------------------------

# Percent-encoding, the same set in both directions (LC_ALL=C: byte-wise awk)
AWK_ORD='BEGIN { for (i = 1; i < 256; i++) ord[sprintf("%c", i)] = i }
function enc(s,    out, i, c) {
    out = ""
    for (i = 1; i <= length(s); i++) {
        c = substr(s, i, 1)
        if (c ~ /[A-Za-z0-9._~:\/,@+-]/) out = out c
        else out = out sprintf("%%%02X", ord[c])
    }
    return out
}'

# A change running as its own unit may have nobody reading its output any
# more: its NOTICE and RESULT lines also go to the journal (never a secret:
# passwords are only ever on stdin)
journal() { [ -n "${NET_CTL_DETACHED:-}" ] && logger -t net-ctl.sh -- "$*" 2>/dev/null; return 0; }
notice() { echo "NOTICE $*"; journal "NOTICE $*"; }

# emit <kind>: reads "key<TAB>value" lines, prints one RESULT line
emit() {
    line=$(awk -v kind="$1" "$AWK_ORD"'
        { p = index($0, "\t"); if (p == 0) next
          line = line " " substr($0, 1, p - 1) "=" enc(substr($0, p + 1)) }
        END { print "RESULT kind=" kind line }')
    printf '%s\n' "$line"
    journal "$line"
}

# kv <key> <value>: one input line for emit
kv() { printf '%s\t%s\n' "$1" "$2"; }

pct_decode() {
    printf '%s' "$1" | awk 'BEGIN { for (i = 1; i < 256; i++) { c = sprintf("%c", i)
                                       chr[sprintf("%02X", i)] = c; chr[sprintf("%02x", i)] = c } }
        { if (NR > 1) printf "\n"; s = $0; out = ""
          while ((p = index(s, "%")) > 0) {
              h = substr(s, p + 1, 2); out = out substr(s, 1, p - 1)
              if (h in chr) { out = out chr[h]; s = substr(s, p + 3) } else { out = out "%"; s = substr(s, p + 1) }
          }
          printf "%s", out s }'
}

# A failure as the last line: RESULT kind=<kind> ok=0 ... reason=<code>
fail() { # <exit> <kind> <reason> [detail]
    if [ -n "${4:-}" ]; then
        line=$(printf 'RESULT kind=%s ok=0 detail=%s reason=%s' "$2" "$(printf '%s' "$4" | tr '\n' ' ' | pct_stdin)" "$3")
    else
        line=$(printf 'RESULT kind=%s ok=0 reason=%s' "$2" "$3")
    fi
    printf '%s\n' "$line"
    journal "$line (exit $1)"
    exit "$1"
}
pct_stdin() { awk "$AWK_ORD"'{ if (NR > 1) printf "%%0A"; printf "%s", enc($0) }'; }

# nmcli -t -e yes escapes ":" and "\" in values; split such a line into
# fields joined by US (\037). Not TAB: TAB is IFS white space, and "read"
# would fold the empty SECURITY column of an open network into its neighbour
untab() {
    awk '{ out = ""; n = length($0)
           for (i = 1; i <= n; i++) {
               c = substr($0, i, 1)
               if (c == "\\" && i < n) { i++; out = out substr($0, i, 1) }
               else if (c == ":") out = out "\037"
               else out = out c
           }
           print out }'
}

# ---- preconditions ----------------------------------------------------------

nm_available() {
    if ! command -v nmcli >/dev/null 2>&1; then
        why="nmcli is not installed"; return 1
    fi
    if [ "$(nmcli -t -f RUNNING general 2>/dev/null)" != "running" ]; then
        why="NetworkManager is not running"; return 1
    fi
    return 0
}

need_nm() {
    nm_available || fail 4 error no-nm "$why"
}

# Changes wait for an image update: the lock holds the updater's pid
need_unlocked() {
    [ -f "$LOCK" ] || return 0
    pid=$(cat "$LOCK" 2>/dev/null)
    case $pid in ''|*[!0-9]*) return 0 ;; esac
    if kill -0 "$pid" 2>/dev/null || [ -d "/proc/$pid" ]; then
        fail 1 "$1" locked "a system update is running (pid $pid)"
    fi
}

dry_note() { printf 'NOTICE dry-run: %s\n' "$*"; }

# ---- devices ----------------------------------------------------------------

# "<device><US><type>" for every wired port and WiFi device NetworkManager
# knows. Virtual devices (veth, bridges, docker) have no hardware behind them
# in sysfs; NetworkManager 1.42 lists a veth as type "ethernet", so the type
# alone does not tell. With NET_CTL_INCLUDE_VETH=1 (tests only) a veth counts
# as a wired port.
list_devices() {
    nmcli -t -e yes -f DEVICE,TYPE device 2>/dev/null | untab | while IFS="$us" read -r dev type; do
        case $type in
            wifi) printf '%s\037%s\n' "$dev" "$type" ;;
            ethernet|veth)
                case $(readlink -f "$SYSFS/$dev" 2>/dev/null) in
                    */virtual/*)
                        if [ "${NET_CTL_INCLUDE_VETH:-0}" = 1 ]; then
                            case $dev in veth*) printf '%s\037ethernet\n' "$dev" ;; esac
                        fi ;;
                    *) [ "$type" = ethernet ] && printf '%s\037ethernet\n' "$dev" ;;
                esac ;;
        esac
    done
}

wifi_device() { list_devices | awk -F "$us" '$2 == "wifi" { print $1; exit }'; }

state_name() { # NetworkManager device state number -> word
    case $1 in
        10) echo unmanaged ;; 20) echo unavailable ;; 30) echo disconnected ;;
        40|50|60|70|80|90) echo connecting ;; 100) echo connected ;;
        110) echo deactivating ;; 120) echo failed ;; *) echo unknown ;;
    esac
}

band_of() { # "2412 MHz" -> 2.4 | 5 | 6
    f=${1%% *}
    case $f in ''|*[!0-9]*) echo "" ; return ;; esac
    if [ "$f" -lt 3000 ]; then echo 2.4; elif [ "$f" -lt 5925 ]; then echo 5; else echo 6; fi
}

security_of() { # nmcli SECURITY column -> open | wpa2 | wpa3 | enterprise | other
    case $1 in
        ''|--) echo open ;;
        *802.1X*) echo enterprise ;;
        *WPA1*|*WPA2*) echo wpa2 ;;
        *WPA3*) echo wpa3 ;;
        *OWE*) echo other ;;   # enhanced open: not a plain open profile
        *) echo other ;;
    esac
}

# A profile's IPv4 method as the app's mode word
mode_word() { # <ipv4.method>
    case $1 in
        auto) echo client ;; manual) echo static ;; shared) echo server ;;
        disabled|'') echo off ;; *) echo "$1" ;;
    esac
}

# Every wired (802-3-ethernet) profile, one line each:
# uuid US name US method US addresses US gateway US dns US ifname US mac US
# autoconnect US priority US timestamp US file
eth_profiles() {
    nmcli -t -e yes -f UUID,TYPE,FILENAME connection show 2>/dev/null | untab | while IFS="$us" read -r u t file; do
        [ "$t" = 802-3-ethernet ] || continue
        # "connection show <id>" prints "name:value" without escaping the value
        nmcli -t -f connection.id,ipv4.method,ipv4.addresses,ipv4.gateway,ipv4.dns,connection.interface-name,802-3-ethernet.mac-address,connection.autoconnect,connection.autoconnect-priority,connection.timestamp \
            connection show uuid "$u" 2>/dev/null | awk -v u="$u" -v file="$file" '
            { p = index($0, ":"); v[substr($0, 1, p - 1)] = substr($0, p + 1) }
            END { printf "%s\037%s\037%s\037%s\037%s\037%s\037%s\037%s\037%s\037%s\037%s\037%s\n", u,
                  v["connection.id"], v["ipv4.method"], v["ipv4.addresses"], v["ipv4.gateway"], v["ipv4.dns"],
                  v["connection.interface-name"], toupper(v["802-3-ethernet.mac-address"]), v["connection.autoconnect"],
                  v["connection.autoconnect-priority"], v["connection.timestamp"], file }'
    done
}

# The profile that is, or would be, active on a wired port: the active one;
# else the autoconnect profile that matches the port (interface name and MAC,
# where set) with the highest priority, then the most recently used
port_profile() { # <profiles> <dev> <MAC> <active uuid>
    printf '%s\n' "$1" | awk -F "$us" -v d="$2" -v m="$(printf '%s' "$3" | tr 'a-f' 'A-F')" -v a="$4" '
        $1 == "" { next }
        a != "" { if ($1 == a) { print; found = 1; exit } next }
        ($7 == "" || $7 == d) && ($8 == "" || $8 == m) && $9 == "yes" {
            if (best == "" || $10 + 0 > bp || ($10 + 0 == bp && $11 + 0 > bt)) { best = $0; bp = $10 + 0; bt = $11 + 0 } }
        END { if (!found && best != "") print best }'
}

# Interface named in the OLED menu's own DHCP-server config (system dnsmasq)
legacy_server_iface() {
    [ -f "$LEGACY_CONF" ] && sed -n 's/^interface=//p' "$LEGACY_CONF" | head -n 1
}

default_route_dev() { # the device of the lowest-metric IPv4 default route
    ip -4 route show default 2>/dev/null | awk '
        { dev = ""; metric = 0
          for (i = 1; i < NF; i++) { if ($i == "dev") dev = $(i + 1); if ($i == "metric") metric = $(i + 1) + 0 }
          if (dev != "" && (best == "" || metric < bestm)) { best = dev; bestm = metric } }
        END { print best }'
}

read_sys() { cat "$SYSFS/$1/$2" 2>/dev/null; }

# The friendly part of a wired port's name: driver and USB product
usb_of() { case $(readlink -f "$SYSFS/$1/device" 2>/dev/null) in */usb*) echo 1 ;; *) echo 0 ;; esac; }

iface_result() { # <dev> <type> <active uuid or ""> <default dev> <legacy iface> <profiles> <inet dir>
    dev=$1 type=$2 uuid=$3
    # "device show" prints "NAME:value" without escaping the value
    show=$(nmcli -t -f GENERAL,IP4,DHCP4,IP6 device show "$dev" 2>/dev/null | sed "s/:/$tab/")
    field() { printf '%s\n' "$show" | awk -F '\t' -v k="$1" '$1 == k { print substr($0, length(k) + 2); exit }'; }
    fields() { printf '%s\n' "$show" | awk -F '\t' -v k="$1" 'index($1, k "[") == 1 { print substr($0, length($1) + 2) }'; }
    dhcp() { printf '%s\n' "$show" | awk -F '\t' -v k="$1" 'index($1, "DHCP4.OPTION[") == 1 {
                 v = substr($0, length($1) + 2); if (index(v, k " = ") == 1) { print substr(v, length(k) + 4); exit } }'; }

    state=$(state_name "$(field GENERAL.STATE | cut -d' ' -f1)")
    addr=$(fields IP4.ADDRESS | head -n 1)
    carrier=$(read_sys "$dev" carrier)
    [ "$carrier" = 1 ] || carrier=0
    speed=$(read_sys "$dev" speed)
    case $speed in ''|-*|*[!0-9]*) speed= ;; esac
    driver=$(basename "$(readlink -f "$SYSFS/$dev/device/driver" 2>/dev/null)" 2>/dev/null)
    product=
    usb=$(usb_of "$dev")
    if [ "$usb" = 1 ]; then
        product=$(cat "$SYSFS/$dev/device/../product" 2>/dev/null)
    fi

    mac=$(field GENERAL.HWADDR)
    mode=off cfg='' cfgaddr='' cfggw='' cfgdns='' binding='' saved='' profuuid='' profname=''
    if [ "$type" = ethernet ]; then
        # Wired ports: the mode of the profile that is or would be active,
        # so a port without a cable still shows (and edits) its settings
        cfg=$(port_profile "$6" "$dev" "$mac" "$uuid")
        if [ -n "$cfg" ]; then
            profuuid=$(printf '%s' "$cfg" | cut -d "$us" -f1)
            profname=$(printf '%s' "$cfg" | cut -d "$us" -f2)
            mode=$(mode_word "$(printf '%s' "$cfg" | cut -d "$us" -f3)")
            cfgaddr=$(printf '%s' "$cfg" | cut -d "$us" -f4 | cut -d, -f1 | tr -d ' ')
            cfggw=$(printf '%s' "$cfg" | cut -d "$us" -f5)
            cfgdns=$(printf '%s' "$cfg" | cut -d "$us" -f6 | tr -d ' ')
            if [ -n "$(printf '%s' "$cfg" | cut -d "$us" -f8)" ]; then binding=mac
            elif [ -n "$(printf '%s' "$cfg" | cut -d "$us" -f7)" ]; then binding=name
            else binding=any; fi
            case $(printf '%s' "$cfg" | cut -d "$us" -f12) in /run/*|'') saved=0 ;; *) saved=1 ;; esac
        fi
    elif [ -n "$uuid" ]; then
        mode=$(mode_word "$(nmcli -t -f ipv4.method connection show uuid "$uuid" 2>/dev/null | sed 's/^ipv4\.method://')")
    fi
    if [ -n "$5" ] && [ "$5" = "$dev" ]; then mode="legacy-server"; fi
    inet=$(cat "$7/$dev" 2>/dev/null)

    ssid='' signal='' band=''
    if [ "$type" = wifi ]; then
        line=$(nmcli -t -e yes -f ACTIVE,SSID,SIGNAL,FREQ device wifi list ifname "$dev" --rescan no 2>/dev/null \
               | untab | awk -F "$us" '$1 == "yes" { print; exit }')
        if [ -n "$line" ]; then
            ssid=$(printf '%s' "$line" | cut -d "$us" -f2)
            signal=$(printf '%s' "$line" | cut -d "$us" -f3)
            band=$(band_of "$(printf '%s' "$line" | cut -d "$us" -f4)")
        fi
        # a WiFi link has no cable: "carrier" is being associated
        carrier=0; [ "$state" = connected ] && carrier=1
    fi

    {
        kv name "$dev"; kv type "$type"; kv usb "$usb"
        kv mac "$mac"; kv carrier "$carrier"; kv speed "$speed"
        kv state "$state"; kv profile "$(field GENERAL.CONNECTION)"; kv mode "$mode"
        kv ip "${addr%/*}"; kv prefix "$(case $addr in */*) echo "${addr#*/}" ;; esac)"
        kv gateway "$(field IP4.GATEWAY)"; kv dns "$(fields IP4.DNS | paste -s -d, -)"
        kv ssid "$ssid"; kv signal "$signal"; kv band "$band"
        kv default "$([ "$4" = "$dev" ] && echo 1 || echo 0)"
        kv driver "$driver"; kv product "$product"
        kv ip6 "$(fields IP6.ADDRESS | paste -s -d, -)"
        kv dhcpserver "$(dhcp dhcp_server_identifier)"; kv leasetime "$(dhcp dhcp_lease_time)"
        kv leaseexpiry "$(dhcp expiry)"
        kv inet "$inet"
        kv profileuuid "$profuuid"; kv cfgprofile "$profname"; kv saved "$saved"; kv binding "$binding"
        kv cfgip "${cfgaddr%/*}"; kv cfgprefix "$(case $cfgaddr in */*) echo "${cfgaddr#*/}" ;; esac)"
        kv cfggateway "$cfggw"; kv cfgdns "$cfgdns"
    } | emit iface
}

# Does a port reach the internet? NetworkManager's connectivity word cannot
# say: on this image connectivity checking is not configured, and then it
# reports "full" for any device with a default route (measured: "full" for a
# port whose gateway had no way out). So: one ICMP echo to each of
# INET_TARGETS, bound to the port (ping -I), in parallel for all ports with a
# gateway, at most ~1 s; the answer is kept 20 s per port, address and
# gateway, so the 5 s status refresh does not ping every time.
inet_checks() { # <out dir> ; reads "dev US gateway US ip" lines
    out=$1
    cache=${TMPDIR:-/tmp}/net-ctl-inet-$(id -u)
    mkdir -p "$cache" 2>/dev/null
    now=$(date +%s)
    while IFS="$us" read -r dev gw ip; do
        [ -n "$dev" ] || continue
        if [ -z "$gw" ] || [ -z "$ip" ]; then continue; fi
        case ${NET_CTL_INET:-} in skip) continue ;; yes|no) echo "$NET_CTL_INET" > "$out/$dev"; continue ;; esac
        key="$ip $gw"
        if [ -f "$cache/$dev" ]; then
            read -r t k1 k2 v < "$cache/$dev" 2>/dev/null
            if [ "$k1 $k2" = "$key" ] && [ $((now - ${t:-0})) -lt 20 ]; then echo "$v" > "$out/$dev"; continue; fi
        fi
        (
            ok=no
            for target in $INET_TARGETS; do
                (ping -n -q -c 1 -W 1 -I "$dev" "$target" >/dev/null 2>&1 && : > "$out/.$dev.ok") &
            done
            wait
            [ -f "$out/.$dev.ok" ] && ok=yes
            echo "$ok" > "$out/$dev"
            echo "$now $key $ok" > "$cache/$dev" 2>/dev/null
        ) &
    done
    wait
}

cmd_status() {
    need_nm
    defdev=$(default_route_dev)
    legacy=$(legacy_server_iface)
    active=$(nmcli -t -e yes -f DEVICE,UUID connection show --active 2>/dev/null | untab)
    profiles=$(eth_profiles)
    devices=$(list_devices)
    inetdir=$(mktemp -d "${TMPDIR:-/tmp}/net-ctl-st.XXXXXX") || exit 2
    trap 'rm -rf "$inetdir"' EXIT
    # gateway and address per port, from the kernel (cheap), for the check
    printf '%s\n' "$devices" | while IFS="$us" read -r dev type; do
        [ -n "$dev" ] || continue
        gw=$(ip -4 route show default dev "$dev" 2>/dev/null | awk '{ print $3; exit }')
        ip4=$(ip -4 -o addr show dev "$dev" 2>/dev/null | awk '{ print $4; exit }')
        printf '%s\037%s\037%s\n' "$dev" "$gw" "$ip4"
    done | inet_checks "$inetdir"
    printf '%s\n' "$devices" | while IFS="$us" read -r dev type; do
        [ -n "$dev" ] || continue
        uuid=$(printf '%s\n' "$active" | awk -F "$us" -v d="$dev" '$1 == d { print $2; exit }')
        iface_result "$dev" "$type" "$uuid" "$defdev" "$legacy" "$profiles" "$inetdir"
    done

    conn=$(nmcli -t -f CONNECTIVITY general 2>/dev/null)
    # "via": the default route's port when it reaches the internet, else the
    # first port (by route metric) that does
    via=''
    for dev in $defdev $(ip -4 route show default 2>/dev/null | awk '{ for (i = 1; i < NF; i++) if ($i == "dev") print $(i + 1) }'); do
        if [ "$(cat "$inetdir/$dev" 2>/dev/null)" = yes ]; then via=$dev; break; fi
    done
    if [ -n "$via" ]; then internet=yes
    elif [ -n "$(find "$inetdir" -maxdepth 1 -type f ! -name '.*' 2>/dev/null | head -n 1)" ]; then internet=no
    elif [ -z "$defdev" ]; then internet=no
    else
        case $conn in none|limited|portal) internet=no ;; *) internet=unknown ;; esac
    fi

    wdev=$(wifi_device)
    radio=absent
    if [ -n "$wdev" ]; then
        radio=$(nmcli -t -f WIFI radio 2>/dev/null)
        case $radio in enabled) radio=on ;; *) radio=off ;; esac
    fi
    country=$(iw reg get 2>/dev/null | sed -n 's/^country \([A-Z0-9][A-Z0-9]\):.*/\1/p' | head -n 1)
    volatile=0
    awk '$2 == "/" && $3 == "overlay" { found = 1 } END { exit !found }' "$MOUNTS" 2>/dev/null && volatile=1
    # The image's WiFi default: the image changes that switch WiFi on at boot
    # (plan 5.4) set cfg80211's regulatory domain; without them it starts off
    wifiboot=off
    grep -qs 'ieee80211_regdom=' "$MODPROBE_DIR"/*.conf && wifiboot=on
    {
        kv internet "$internet"; kv via "$via"; kv defaultdev "$defdev"; kv connectivity "$conn"
        kv wifi "$radio"; kv country "$country"; kv volatile "$volatile"; kv wifiboot "$wifiboot"
    } | emit summary
}

cmd_available() {
    if nm_available; then
        echo "RESULT kind=available ok=1"
        exit 0
    fi
    printf 'RESULT kind=available ok=0 reason=%s\n' "$why"
    exit 4
}

cmd_monitor() {
    need_nm
    echo "NOTICE changed"
    # Runs as long as its caller: when the app is gone (killed with the
    # launcher's stop-app, say), nmcli monitor would otherwise wait for the
    # next NetworkManager event to notice - possibly hours
    caller=$PPID
    fifo=$(mktemp -u "${TMPDIR:-/tmp}/net-ctl-mon.XXXXXX") && mkfifo -m 600 "$fifo" || exit 2
    nmcli monitor > "$fifo" 2>/dev/null &
    nm=$!
    (while kill -0 "$caller" 2>/dev/null; do sleep 2; done; kill "$nm" 2>/dev/null) &
    watch=$!
    trap 'kill "$nm" "$watch" 2>/dev/null; rm -f "$fifo"; exit 0' HUP INT TERM
    # nmcli flushes each line (g_print), so the events arrive as they happen
    while IFS= read -r _; do
        echo "NOTICE changed" || break
    done < "$fifo"
    kill "$nm" "$watch" 2>/dev/null
    rm -f "$fifo"
}

cmd_leases() {
    [ -n "$iface" ] || fail 1 lease bad-arguments "--iface= is required"
    f="$LEASE_DIR/dnsmasq-$iface.leases"
    [ -r "$f" ] || { echo "RESULT kind=leases iface=$iface count=0"; exit 0; }
    n=0
    while read -r expires mac ip host _; do
        [ -n "$ip" ] || continue
        [ "$host" = "*" ] && host=
        { kv ip "$ip"; kv mac "$mac"; kv host "$host"; kv expires "$expires"; } | emit lease
        n=$((n + 1))
    done < "$f"
    echo "RESULT kind=leases iface=$iface count=$n"
}

# ---- WiFi -------------------------------------------------------------------

# "<uuid><TAB><ssid><TAB><autoconnect><TAB><active><TAB><hidden>" per WiFi profile
saved_profiles() {
    nmcli -t -e yes -f UUID,TYPE,AUTOCONNECT,ACTIVE connection show 2>/dev/null | untab \
    | while IFS="$us" read -r u t ac act; do
        [ "$t" = 802-11-wireless ] || continue
        # "connection show <id>" prints "name:value" without escaping the value
        s=$(nmcli -t -f 802-11-wireless.ssid,802-11-wireless.hidden connection show uuid "$u" 2>/dev/null)
        ssid=$(printf '%s\n' "$s" | sed -n 's/^802-11-wireless\.ssid://p')
        hid=$(printf '%s\n' "$s" | sed -n 's/^802-11-wireless\.hidden://p')
        printf '%s\037%s\037%s\037%s\037%s\n' "$u" "$ssid" "$ac" "$act" "$hid"
    done
}

uuids_for_ssid() { saved_profiles | awk -F "$us" -v s="$1" '$2 == s { print $1 }'; }

# "<ssid><TAB><signal><TAB><security><TAB><band><TAB><in-use>", strongest BSSID per SSID
scan_list() { # <rescan: yes|no>
    nmcli -t -e yes -f IN-USE,SSID,SIGNAL,SECURITY,FREQ device wifi list --rescan "$1" 2>/dev/null | untab \
    | while IFS="$us" read -r use ssid sig sec freq; do
        [ -n "$ssid" ] || continue
        printf '%s\037%s\037%s\037%s\037%s\n' "$ssid" "$sig" "$(security_of "$sec")" "$(band_of "$freq")" \
            "$([ "$use" = "*" ] && echo 1 || echo 0)"
    done | awk -F "$us" '
        !($1 in sig) { order[++n] = $1 }
        !($1 in sig) || $2 + 0 > sig[$1] + 0 { sig[$1] = $2; row[$1] = $0 }
        $5 == 1 { active[$1] = 1 }
        END { for (i = 1; i <= n; i++) { s = order[i]; r = row[s]
                                          if (active[s]) sub(/[01]$/, "1", r); print r } }' \
    | sort -t "$us" -k2,2nr
}

cmd_wifi_scan() {
    need_nm
    [ -n "$(wifi_device)" ] || fail 2 scan no-wifi "this system has no WiFi device"
    rescan=no
    if [ "$opt_rescan" = 1 ]; then
        rescan=yes
        [ "$dry_run" = 1 ] && { dry_note "nmcli device wifi list --rescan yes"; rescan=no; }
    fi
    saved=$(saved_profiles)
    aps=$(scan_list "$rescan")
    printf '%s\n' "$aps" | while IFS="$us" read -r ssid sig sec band use; do
        [ -n "$ssid" ] || continue
        is_saved=0
        printf '%s\n' "$saved" | awk -F "$us" -v s="$ssid" '$2 == s { f = 1 } END { exit !f }' && is_saved=1
        { kv ssid "$ssid"; kv signal "$sig"; kv security "$sec"; kv band "$band"
          kv saved "$is_saved"; kv active "$use"; } | emit ap
    done
    printf '%s\n' "$saved" | while IFS="$us" read -r u ssid ac act hid; do
        [ -n "$u" ] || continue
        inrange=0
        printf '%s\n' "$aps" | awk -F "$us" -v s="$ssid" '$1 == s { f = 1 } END { exit !f }' && inrange=1
        { kv ssid "$ssid"; kv uuid "$u"; kv autoconnect "$([ "$ac" = yes ] && echo 1 || echo 0)"
          kv active "$([ "$act" = yes ] && echo 1 || echo 0)"; kv hidden "$([ "$hid" = yes ] && echo 1 || echo 0)"
          kv inrange "$inrange"; } | emit saved
    done
}

# The secret goes to nmcli through its passwd-file, read from a pipe: it is
# never in an argument list and never written to a file. NetworkManager stores
# it in the profile (psk-flags 0) once the activation was asked for it.
# PROGRESS only forwards: the key fetch makes NetworkManager pass prepare and
# configuring twice
progress() { # <rank> <phase>
    if [ -z "$act_reason" ] && [ "$1" -gt "$rank" ]; then
        rank=$1; echo "PROGRESS phase=$2"
    fi
}

activate() { # <uuid> <dev> <secret or ""> ; sets act_rc, act_reason, act_detail
    fifo=$(mktemp -u "${TMPDIR:-/tmp}/net-ctl.XXXXXX") || return 1
    mkfifo -m 600 "$fifo" || return 1
    nmcli device monitor "$2" > "$fifo" 2>/dev/null &
    mon=$!
    if [ -n "$3" ]; then
        (printf '802-11-wireless-security.psk:%s\n' "$3" \
            | nmcli --wait "$CONNECT_WAIT" connection up uuid "$1" passwd-file /dev/stdin 2>&1
         echo "UPDONE $?") > "$fifo" &
    else
        (nmcli --wait "$CONNECT_WAIT" connection up uuid "$1" 2>&1 < /dev/null
         echo "UPDONE $?") > "$fifo" &
    fi
    up=$!
    act_rc='' act_reason='' act_detail='' rank=0 asks=0
    while IFS= read -r line; do
        case $line in
            "UPDONE "*) act_rc=${line#UPDONE }; break ;;
            *": connecting (need authentication)"*)
                # Every WiFi activation passes need-auth once: NetworkManager
                # fetches the key (its own store, or passwd-file). A second
                # request means the network refused the key - and
                # NetworkManager would retry the same one until the wait ran out
                asks=$((asks + 1))
                if [ "$asks" -gt 1 ]; then
                    act_reason=bad-password
                    nmcli connection down uuid "$1" >/dev/null 2>&1
                fi
                ;;
            *": connecting (prepare)"*) progress 1 associating ;;
            *": connecting (configuring)"*) progress 2 authenticating ;;
            *": connecting (getting IP configuration)"*|*": connecting (checking IP connectivity)"*)
                progress 3 address ;;
            Error:*) act_detail=${line#Error: } ;;
        esac
    done < "$fifo"
    kill "$mon" 2>/dev/null
    wait "$up" 2>/dev/null
    rm -f "$fifo"
    [ -n "$act_rc" ] || act_rc=1
    if [ "$act_rc" = 0 ]; then act_reason=; return 0; fi
    if [ -z "$act_reason" ]; then
        case $act_detail in
            *"ecrets were required"*|*"no secrets"*|*"802.1X supplicant"*) act_reason=bad-password ;;
            *"IP configuration could not be reserved"*|*"IP config"*) act_reason=no-address ;;
            *"Timeout expired"*) act_reason=timeout ;;
            *"could not be found"*|*"No network with SSID"*|*"not found"*) act_reason=not-found ;;
            *) act_reason=failed ;;
        esac
    fi
    return 1
}

cmd_wifi_connect() {
    need_nm
    [ -n "$ssid_enc" ] || fail 1 connect bad-arguments "--ssid= is required"
    ssid=$(pct_decode "$ssid_enc")
    need_unlocked connect
    dev=$(wifi_device)
    [ -n "$dev" ] || fail 1 connect unsupported "this system has no WiFi device"
    [ "$(nmcli -t -f WIFI radio 2>/dev/null)" = enabled ] || fail 1 connect radio-off "WiFi is switched off"

    # The password, when there is one: the first line of stdin
    password=
    if [ ! -t 0 ]; then IFS= read -r password || true; fi

    # What the network asks for: the scan, or the caller for a hidden one
    if [ "$opt_hidden" = 1 ]; then
        security=${opt_security:-$([ -n "$password" ] && echo wpa2 || echo open)}
    else
        security=$(scan_list no | awk -F "$us" -v s="$ssid" '$1 == s { print $3; exit }')
        [ -n "$security" ] || security=$(scan_list yes | awk -F "$us" -v s="$ssid" '$1 == s { print $3; exit }')
        [ -n "$security" ] || fail 2 connect not-found "no network named $ssid is in range"
    fi
    case $security in
        open|wpa2|wpa3) ;;
        *) fail 1 connect unsupported "$ssid uses $security security" ;;
    esac

    saved=$(uuids_for_ssid "$ssid" | head -n 1)
    if [ -z "$password" ] && [ "$security" != open ] && [ -z "$saved" ]; then
        fail 1 connect need-password "$ssid needs a password"
    fi
    if [ -n "$password" ]; then
        len=${#password}
        if [ "$len" -lt 8 ] || [ "$len" -gt 63 ]; then
            fail 1 connect bad-password "a WiFi password has 8 to 63 characters"
        fi
    fi

    prev=$(nmcli -t -e yes -f DEVICE,UUID connection show --active 2>/dev/null | untab \
           | awk -F "$us" -v d="$dev" '$1 == d { print $2; exit }')
    prev_ssid=
    [ -n "$prev" ] && prev_ssid=$(nmcli -t -f 802-11-wireless.ssid connection show uuid "$prev" 2>/dev/null \
                                  | sed -n 's/^802-11-wireless\.ssid://p')

    if [ "$dry_run" = 1 ]; then
        if [ -n "$password" ] || [ -z "$saved" ]; then
            dry_note "nmcli connection add type wifi ssid <$ssid> (security $security), then up with the password on stdin"
        else
            dry_note "nmcli connection up uuid $saved"
        fi
        { kv ssid "$ssid"; kv ok 1; kv dryrun 1; } | emit connect
        exit 0
    fi

    # Already on it, and no new password: nothing to do
    if [ -n "$prev" ] && [ "$prev" = "$saved" ] && [ -z "$password" ]; then
        { kv ssid "$ssid"; kv ok 1; } | emit connect
        exit 0
    fi

    # From here on the change runs to its end: deaf to the signals of a caller
    # that goes away; with rule 6a it is detached from that caller anyway
    trap '' HUP TERM INT PIPE

    created=
    target=$saved
    if [ -n "$password" ] || [ -z "$saved" ]; then
        # A new profile for the attempt; the saved one (if any) stays as it is
        # until the new one is up
        set -- connection add type wifi con-name "$ssid" ifname "*" ssid "$ssid" connection.autoconnect yes
        [ "$opt_hidden" = 1 ] && set -- "$@" 802-11-wireless.hidden yes
        case $security in
            wpa2) set -- "$@" wifi-sec.key-mgmt wpa-psk ;;
            wpa3) set -- "$@" wifi-sec.key-mgmt sae ;;
        esac
        out=$(nmcli "$@" 2>&1) || fail 2 connect failed "$out"
        created=$(printf '%s\n' "$out" | sed -n 's/.*(\([0-9a-fA-F-]\{36\}\)) successfully added.*/\1/p' | tail -n 1)
        [ -n "$created" ] || fail 2 connect failed "$out"
        target=$created
    fi

    if activate "$target" "$dev" "$password"; then
        if [ -n "$created" ]; then
            # The new profile replaces any older one for this network
            uuids_for_ssid "$ssid" | while read -r u; do
                [ "$u" = "$created" ] || nmcli connection delete uuid "$u" >/dev/null 2>&1
            done
        fi
        addr=$(nmcli -t -f IP4.ADDRESS device show "$dev" 2>/dev/null | sed -n '1s/^[^:]*://p')
        { kv ssid "$ssid"; kv ok 1; kv ip "${addr%/*}"; } | emit connect
        exit 0
    fi

    # Failed: drop the attempt, bring the previous connection back
    nmcli connection down uuid "$target" >/dev/null 2>&1
    [ -n "$created" ] && nmcli connection delete uuid "$created" >/dev/null 2>&1
    restored=
    if [ -n "$prev" ] && [ "$prev" != "$target" ]; then
        if nmcli --wait 30 connection up uuid "$prev" >/dev/null 2>&1; then restored=$prev_ssid; fi
    fi
    {
        kv ssid "$ssid"; kv ok 0; kv restored "$restored"
        kv detail "$(printf '%s' "$act_detail" | tr '\n' ' ')"
    } | emit connect | sed "s/\$/ reason=$act_reason/"
    journal "reason=$act_reason"
    [ -n "$restored" ] && exit 3
    exit 2
}

cmd_wifi_disconnect() {
    need_nm
    need_unlocked disconnect
    dev=$(wifi_device)
    [ -n "$dev" ] || fail 1 disconnect unsupported "this system has no WiFi device"
    if [ "$dry_run" = 1 ]; then dry_note "nmcli device disconnect $dev"; echo "RESULT kind=disconnect ok=1 dryrun=1"; exit 0; fi
    out=$(nmcli device disconnect "$dev" 2>&1) || fail 2 disconnect failed "$out"
    echo "RESULT kind=disconnect ok=1"
}

cmd_wifi_forget() {
    need_nm
    [ -n "$ssid_enc" ] || fail 1 forget bad-arguments "--ssid= is required"
    ssid=$(pct_decode "$ssid_enc")
    need_unlocked forget
    uuids=$(uuids_for_ssid "$ssid")
    [ -n "$uuids" ] || fail 2 forget not-saved "no saved network named $ssid"
    for u in $uuids; do
        if [ "$dry_run" = 1 ]; then dry_note "nmcli connection delete uuid $u"; continue; fi
        out=$(nmcli connection delete uuid "$u" 2>&1) || fail 2 forget failed "$out"
    done
    { kv ssid "$ssid"; kv ok 1; } | emit forget
}

cmd_wifi_autoconnect() {
    need_nm
    [ -n "$ssid_enc" ] && [ -n "$opt_onoff" ] || fail 1 autoconnect bad-arguments "--ssid= and --on or --off are required"
    ssid=$(pct_decode "$ssid_enc")
    need_unlocked autoconnect
    uuids=$(uuids_for_ssid "$ssid")
    [ -n "$uuids" ] || fail 2 autoconnect not-saved "no saved network named $ssid"
    value=no; [ "$opt_onoff" = on ] && value=yes
    for u in $uuids; do
        if [ "$dry_run" = 1 ]; then dry_note "nmcli connection modify uuid $u connection.autoconnect $value"; continue; fi
        out=$(nmcli connection modify uuid "$u" connection.autoconnect "$value" 2>&1) || fail 2 autoconnect failed "$out"
    done
    { kv ssid "$ssid"; kv ok 1; kv autoconnect "$([ "$value" = yes ] && echo 1 || echo 0)"; } | emit autoconnect
}

cmd_wifi_radio() {
    need_nm
    [ -n "$opt_onoff" ] || fail 1 radio bad-arguments "--on or --off is required"
    need_unlocked radio
    [ -n "$(wifi_device)" ] || fail 1 radio unsupported "this system has no WiFi device"
    if [ "$opt_onoff" = off ]; then
        if [ "$dry_run" = 1 ]; then dry_note "nmcli radio wifi off"; echo "RESULT kind=radio ok=1 wifi=off dryrun=1"; exit 0; fi
        out=$(nmcli radio wifi off 2>&1) || fail 2 radio failed "$out"
        echo "RESULT kind=radio ok=1 wifi=off"
        exit 0
    fi
    country=$(iw reg get 2>/dev/null | sed -n 's/^country \([A-Z0-9][A-Z0-9]\):.*/\1/p' | head -n 1)
    if [ "$dry_run" = 1 ]; then
        dry_note "rfkill unblock wifi; nmcli radio wifi on$([ "$country" = 00 ] || [ -z "$country" ] && echo "; iw reg set $COUNTRY")"
        echo "RESULT kind=radio ok=1 wifi=on dryrun=1"
        exit 0
    fi
    rfkill unblock wifi 2>/dev/null
    out=$(nmcli radio wifi on 2>&1) || fail 2 radio failed "$out"
    if [ -z "$country" ] || [ "$country" = 00 ]; then
        iw reg set "$COUNTRY" 2>/dev/null && country=$COUNTRY
    fi
    echo "RESULT kind=radio ok=1 wifi=on country=$country"
}

# ---- wired ports --------------------------------------------------------------

valid_ip() { # a.b.c.d with every octet 0-255
    printf '%s' "$1" | awk -F . 'NF != 4 { exit 1 }
        { for (i = 1; i <= 4; i++) if ($i !~ /^[0-9]+$/ || $i + 0 > 255 || length($i) > 3) exit 1 }'
}

# Another port's IPv4 network that overlaps <ip>/<prefix>: prints "dev a.b.c.d/n"
overlap() { # <dev> <ip> <prefix>
    ip -4 -o addr show 2>/dev/null | awk -v self="$1" -v ip="$2" -v pfx="$3" '
        function num(a,    p) { split(a, p, "."); return ((p[1] * 256 + p[2]) * 256 + p[3]) * 256 + p[4] }
        function net(n, b) { return int(n / 2 ^ (32 - b)) }
        $2 == self || $2 == "lo" { next }
        { split($4, a, "/"); b = (a[2] + 0 < pfx + 0) ? a[2] + 0 : pfx + 0
          if (net(num(a[1]), b) == net(num(ip), b)) { print $2 " " $4; exit } }'
}

has_addr() { # <dev> <ip>
    ip -4 -o addr show dev "$1" 2>/dev/null | awk -v ip="$2" '{ split($4, a, "/"); if (a[1] == ip) f = 1 } END { exit !f }'
}

# NetworkManager's dnsmasq for a serving port: running, with this address,
# reading the drop-in directory
server_running() { # <ip>
    pgrep -a -x dnsmasq 2>/dev/null | grep -F -- "--listen-address=$1" | grep -qF -- "--conf-dir=$SHARED_DIR"
}

# Shared mode needs the system dnsmasq gone (port 53 clash, plan 2.1) and the
# no-gateway drop-in in place (plan 5.1). The image provides both (plan 5.4);
# on an image without them this makes them true for the running system.
server_preconditions() {
    if systemctl cat dnsmasq.service >/dev/null 2>&1; then
        if systemctl is-active --quiet dnsmasq.service; then
            notice "stopping the system dnsmasq"
            systemctl stop dnsmasq.service || return 1
        fi
        if [ "$(systemctl is-enabled dnsmasq.service 2>/dev/null)" != masked ]; then
            notice "masking the system dnsmasq"
            systemctl mask dnsmasq.service >/dev/null 2>&1 || return 1
        fi
    fi
    want='# network-manager-app: serve addresses only - no router, no DNS server announced
dhcp-option=3
dhcp-option=6'
    if [ "$(cat "$DROPIN" 2>/dev/null)" != "$want" ]; then
        notice "writing $DROPIN"
        mkdir -p "$SHARED_DIR" && printf '%s\n' "$want" > "$DROPIN" || return 1
    fi
    return 0
}

# What the OLED menu's own stop does (dhcp-net-settings-pios.sh): a port it
# put in its DHCP-server mode is taken over by any wired-set
legacy_takeover() { # <dev>
    [ "$(legacy_server_iface)" = "$1" ] || return 0
    notice "taking over from the panel menu's DHCP server"
    systemctl stop dnsmasq.service 2>/dev/null
    systemctl mask dnsmasq.service >/dev/null 2>&1
    rm -f "$LEGACY_CONF"
}

nm_get() { # <uuid> <setting> - one value of a profile
    nmcli -t -f "$2" connection show uuid "$1" 2>/dev/null | sed -n "s/^$2://p"
}

wired_up() { # <uuid> <dev> ; sets up_out
    up_out=$(nmcli --wait "$WIRED_WAIT" connection up uuid "$1" ifname "$2" 2>&1)
}

cmd_wired_set() {
    need_nm
    [ -n "$iface" ] || fail 1 wired bad-arguments "--iface= is required"
    case $opt_mode in client|static|server) ;; *) fail 1 wired bad-arguments "--mode= is client, static or server" ;; esac
    list_devices | awk -F "$us" -v d="$iface" '$1 == d && $2 == "ethernet" { f = 1 } END { exit !f }' \
        || fail 1 wired bad-arguments "$iface is not a wired port"
    prefix=$opt_prefix
    if [ "$opt_mode" != client ]; then
        valid_ip "$opt_ip" || fail 1 wired bad-arguments "--ip= must be an address like 192.168.50.1"
        [ "$opt_mode" = server ] && prefix=${prefix:-24}
        case $prefix in ''|*[!0-9]*) fail 1 wired bad-arguments "--prefix= is 1 to 30" ;; esac
        if [ "$prefix" -lt 1 ] || [ "$prefix" -gt 30 ]; then fail 1 wired bad-arguments "--prefix= is 1 to 30"; fi
        last=${opt_ip##*.}
        [ "$last" != 0 ] && [ "$last" != 255 ] || fail 1 wired bad-arguments "$opt_ip is not a host address"
    fi
    if [ "$opt_mode" = static ]; then
        [ -z "$opt_gateway" ] || valid_ip "$opt_gateway" || fail 1 wired bad-arguments "--gateway= must be an address"
        for d in $(printf '%s' "$opt_dns" | tr ',' ' '); do
            valid_ip "$d" || fail 1 wired bad-arguments "--dns= must be addresses, separated by commas"
        done
    fi
    if [ "$opt_mode" = server ]; then
        clash=$(overlap "$iface" "$opt_ip" "$prefix")
        [ -z "$clash" ] || fail 1 wired overlap "$opt_ip/$prefix overlaps ${clash#* } on ${clash%% *}"
    fi
    need_unlocked wired

    mac=$(read_sys "$iface" address | tr 'a-f' 'A-F')
    usb=$(usb_of "$iface")
    active=$(nmcli -t -e yes -f DEVICE,UUID connection show --active 2>/dev/null | untab \
             | awk -F "$us" -v d="$iface" '$1 == d { print $2; exit }')
    cfg=$(port_profile "$(eth_profiles)" "$iface" "$mac" "$active")
    uuid=$(printf '%s' "$cfg" | cut -d "$us" -f1)
    carrier=$(read_sys "$iface" carrier); [ "$carrier" = 1 ] || carrier=0

    case $opt_mode in
        client) set -- ipv4.method auto ipv4.addresses "" ipv4.gateway "" ipv4.dns "" ipv4.never-default no ipv6.method auto ;;
        static) set -- ipv4.method manual ipv4.addresses "$opt_ip/$prefix" ipv4.gateway "$opt_gateway" \
                       ipv4.dns "$opt_dns" ipv4.never-default no ipv6.method auto ;;
        server) set -- ipv4.method shared ipv4.addresses "$opt_ip/$prefix" ipv4.gateway "" ipv4.dns "" \
                       ipv4.never-default yes ipv6.method disabled ;;
    esac
    # A USB adapter keeps its settings in any port, and another adapter does
    # not inherit them: bind to the MAC. The built-in port keeps its name.
    if [ "$usb" = 1 ] && [ -n "$mac" ]; then
        set -- "$@" 802-3-ethernet.mac-address "$mac" connection.interface-name ""
    fi

    if [ "$dry_run" = 1 ]; then
        [ -n "$(legacy_server_iface)" ] && [ "$(legacy_server_iface)" = "$iface" ] && dry_note "take over the panel menu's DHCP server on $iface"
        [ "$opt_mode" = server ] && dry_note "stop and mask dnsmasq.service if needed; write $DROPIN if missing"
        if [ -n "$uuid" ]; then dry_note "nmcli connection modify uuid $uuid $*"
        else dry_note "nmcli connection add type ethernet con-name $iface ifname $iface $*"; fi
        [ "$carrier" = 1 ] && dry_note "nmcli connection up uuid ${uuid:-<new>} ifname $iface"
        { kv iface "$iface"; kv mode "$opt_mode"; kv ok 1; kv dryrun 1; } | emit wired
        exit 0
    fi

    # From here on the change runs to its end (rule 6a: detached, and deaf
    # to the signals of a caller that goes away)
    trap '' HUP TERM INT PIPE

    legacy_takeover "$iface"
    if [ "$opt_mode" = server ]; then
        server_preconditions || fail 2 wired failed "could not stop the system dnsmasq or write $DROPIN"
    fi

    created=''
    if [ -n "$uuid" ]; then
        # What to put back if the new settings do not come up
        prev_method=$(nm_get "$uuid" ipv4.method); prev_addr=$(nm_get "$uuid" ipv4.addresses)
        prev_gw=$(nm_get "$uuid" ipv4.gateway); prev_dns=$(nm_get "$uuid" ipv4.dns)
        prev_nd=$(nm_get "$uuid" ipv4.never-default); prev_v6=$(nm_get "$uuid" ipv6.method)
        prev_mac=$(nm_get "$uuid" 802-3-ethernet.mac-address); prev_if=$(nm_get "$uuid" connection.interface-name)
        notice "previous: $prev_method $prev_addr"
        out=$(nmcli connection modify uuid "$uuid" "$@" 2>&1) || fail 2 wired failed "$out"
    else
        name=$iface; [ "$usb" = 1 ] && name="USB adapter $mac"
        out=$(nmcli connection add type ethernet con-name "$name" ifname "$iface" connection.autoconnect yes "$@" 2>&1) \
            || fail 2 wired failed "$out"
        created=$(printf '%s\n' "$out" | sed -n 's/.*(\([0-9a-fA-F-]\{36\}\)) successfully added.*/\1/p' | tail -n 1)
        [ -n "$created" ] || fail 2 wired failed "$out"
        uuid=$created
    fi

    # No cable: saved, used when one is plugged in (nothing to check yet)
    if [ "$carrier" = 0 ]; then
        { kv iface "$iface"; kv mode "$opt_mode"; kv ok 1; kv pending 1; kv profileuuid "$uuid"; } | emit wired
        exit 0
    fi

    echo "PROGRESS phase=activating"
    reason=''
    if wired_up "$uuid" "$iface"; then
        echo "PROGRESS phase=checking"
        i=0
        while :; do
            case $opt_mode in
                client) ip4=$(ip -4 -o addr show dev "$iface" 2>/dev/null | awk '{ split($4, a, "/"); print a[1]; exit }')
                        [ -n "$ip4" ] && break ;;
                static) has_addr "$iface" "$opt_ip" && break ;;
                server) has_addr "$iface" "$opt_ip" && server_running "$opt_ip" && [ -f "$DROPIN" ] && break ;;
            esac
            i=$((i + 1))
            if [ $i -ge 20 ]; then
                reason=check-failed
                [ "$opt_mode" = server ] && up_out="the address, NetworkManager's dnsmasq or $DROPIN is missing"
                [ "$opt_mode" != server ] && up_out="$iface has no address"
                break
            fi
            sleep 0.5
        done
    else
        reason=activation-failed
    fi

    if [ -z "$reason" ]; then
        ip4=$(ip -4 -o addr show dev "$iface" 2>/dev/null | awk '{ split($4, a, "/"); print a[1]; exit }')
        { kv iface "$iface"; kv mode "$opt_mode"; kv ok 1; kv ip "$ip4"; kv profileuuid "$uuid"
          kv binding "$([ "$usb" = 1 ] && echo mac || echo name)"; } | emit wired
        exit 0
    fi

    # Failed: the previous settings back, and up again (the failure's own
    # message is kept; the restore's activation would overwrite up_out)
    why=$up_out
    notice "restoring the previous settings"
    restored=0
    if [ -n "$created" ]; then
        nmcli connection delete uuid "$created" >/dev/null 2>&1
    else
        if nmcli connection modify uuid "$uuid" ipv4.method "$prev_method" ipv4.addresses "$prev_addr" \
               ipv4.gateway "$prev_gw" ipv4.dns "$prev_dns" ipv4.never-default "$prev_nd" ipv6.method "$prev_v6" \
               802-3-ethernet.mac-address "$prev_mac" connection.interface-name "$prev_if" >/dev/null 2>&1; then
            wired_up "$uuid" "$iface" && restored=1
        fi
    fi
    { kv iface "$iface"; kv mode "$opt_mode"; kv ok 0; kv restored "$restored"
      kv detail "$(printf '%s' "$why" | tr '\n' ' ')"; } | emit wired | sed "s/\$/ reason=$reason/"
    journal "reason=$reason restored=$restored"
    [ "$restored" = 1 ] && exit 3
    exit 2
}

# One DHCPDISCOVER on the port (net-dhcp-probe.py): who answers. Never a
# REQUEST, so no address is taken. The rig's own server on this port (a port
# in server mode answers itself) is left out.
cmd_dhcp_probe() {
    need_nm
    [ -n "$iface" ] || fail 1 probe bad-arguments "--iface= is required"
    carrier=$(read_sys "$iface" carrier); [ "$carrier" = 1 ] || carrier=0
    if [ "$carrier" = 0 ]; then
        echo "RESULT kind=probe iface=$iface servers=0 carrier=0"
        exit 0
    fi
    own=$(ip -4 -o addr show 2>/dev/null | awk '{ split($4, a, "/"); printf "%s ", a[1] }')
    out=$("$PROBE" --iface "$iface" --timeout 4.5 2>&1) || fail 2 probe failed "$out"
    # "OFFER server=a.b.c.d offered=a.b.c.d [router=a.b.c.d]" per server
    offers=$(printf '%s\n' "$out" | awk -v own=" $own " '
        $1 == "OFFER" { s = o = r = ""
            for (i = 2; i <= NF; i++) { p = index($i, "="); k = substr($i, 1, p - 1); v = substr($i, p + 1)
                if (k == "server") s = v; else if (k == "offered") o = v; else if (k == "router") r = v }
            if (s == "" || index(own, " " s " ") || seen[s]++) next
            print s "\037" o "\037" r }')
    n=0
    while IFS="$us" read -r server offered router; do
        [ -n "$server" ] || continue
        { kv iface "$iface"; kv server "$server"; kv offered "$offered"; kv router "$router"; } | emit offer
        n=$((n + 1))
    done <<EOF_OFFERS
$offers
EOF_OFFERS
    echo "RESULT kind=probe iface=$iface servers=$n carrier=1"
}

# ---- a change outlives its caller ------------------------------------------------

# Rule 6a: as a transient unit. Only for changes, only as root, only once,
# never in a dry run; without systemd-run (Buildroot) the change runs here.
detach() { # <command> <args...>
    case $1 in wifi-connect|wifi-disconnect|wifi-forget|wifi-autoconnect|wifi-radio|wired-set) ;; *) return 0 ;; esac
    [ "$dry_run" = 1 ] && return 0
    [ -z "${NET_CTL_DETACHED:-}" ] || return 0
    [ "${NET_CTL_UID:-$(id -u)}" = 0 ] || return 0
    command -v systemd-run >/dev/null 2>&1 || return 0
    self=$(readlink -f "$0")
    # NET_CTL_* (test seams such as NET_CTL_INCLUDE_VETH) go along; names
    # only ("--setenv=NAME" copies the value), and names have no spaces
    envs=''
    for v in $(env | sed -n 's/^\(NET_CTL_[A-Z0-9_]*\)=.*/\1/p'); do envs="$envs --setenv=$v"; done
    # shellcheck disable=SC2086
    exec systemd-run --quiet --collect --pipe --wait --description="net-ctl.sh $1" \
        --setenv=NET_CTL_DETACHED=1 $envs "$self" "$@"
}

# ---- arguments --------------------------------------------------------------

cmd=${1:-}
[ $# -gt 0 ] && shift
iface='' ssid_enc='' opt_hidden=0 opt_security='' opt_rescan=0 opt_onoff='' dry_run=0
opt_mode='' opt_ip='' opt_prefix='' opt_gateway='' opt_dns=''
for arg in "$@"; do
    case $arg in
        --iface=*) iface=${arg#--iface=} ;;
        --mode=*) opt_mode=${arg#--mode=} ;;
        --ip=*) opt_ip=${arg#--ip=} ;;
        --prefix=*) opt_prefix=${arg#--prefix=} ;;
        --gateway=*) opt_gateway=${arg#--gateway=} ;;
        --dns=*) opt_dns=${arg#--dns=} ;;
        --ssid=*) ssid_enc=${arg#--ssid=} ;;
        --hidden) opt_hidden=1 ;;
        --security=*) opt_security=${arg#--security=} ;;
        --rescan) opt_rescan=1 ;;
        --on) opt_onoff=on ;;
        --off) opt_onoff=off ;;
        --dry-run) dry_run=1 ;;
        *) printf 'RESULT kind=error ok=0 reason=unknown argument %s\n' "$arg"; exit 1 ;;
    esac
done
case $opt_security in ''|open|wpa2|wpa3) ;; *) fail 1 connect bad-arguments "--security= is open, wpa2 or wpa3" ;; esac

detach "$cmd" "$@"
if [ -n "${NET_CTL_DETACHED:-}" ]; then
    # Inside the transient unit: nobody may be reading any more, and that
    # must not end the change (a write to a closed pipe then just fails)
    trap '' PIPE HUP TERM INT
    echo "NOTICE detached"
    journal "start: $cmd $*"
fi

case $cmd in
    available) cmd_available ;;
    status) cmd_status ;;
    monitor) cmd_monitor ;;
    leases) cmd_leases ;;
    wifi-scan) cmd_wifi_scan ;;
    wifi-connect) cmd_wifi_connect ;;
    wifi-disconnect) cmd_wifi_disconnect ;;
    wifi-forget) cmd_wifi_forget ;;
    wifi-autoconnect) cmd_wifi_autoconnect ;;
    wifi-radio) cmd_wifi_radio ;;
    wired-set) cmd_wired_set ;;
    dhcp-probe) cmd_dhcp_probe ;;
    *)
        echo "usage: net-ctl.sh available|status|monitor|leases|wifi-scan|wifi-connect|wifi-disconnect|wifi-forget|wifi-autoconnect|wifi-radio|wired-set|dhcp-probe [options]" >&2
        exit 1 ;;
esac
