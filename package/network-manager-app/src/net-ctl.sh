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
#   leases --iface=IF               RESULT kind=lease ... (root: the lease file is root-only),
#                                   then kind=reservation mac= ip= per reservation in the
#                                   port's subnet
#   wifi-scan [--rescan]            RESULT kind=ap ... per SSID, then kind=saved ... per profile
#   wifi-connect --ssid=S [--hidden] [--security=open|wpa2|wpa3]
#                                   password on stdin when one is needed
#   wifi-disconnect                 disconnects the WiFi device
#   wifi-forget --ssid=S            deletes the saved profile(s) for S
#   wifi-autoconnect --ssid=S --on|--off
#   wifi-radio --on|--off           --on also unblocks rfkill and sets the country if unset;
#                                   the choice is kept in /data/network/wifi-radio.state
#                                   where that directory exists (the A/B image)
#   wifi-radio-restore              (root, at boot BEFORE NetworkManager starts: the
#                                   micropanel-wifi-radio-restore unit) a kept "off" goes
#                                   into NetworkManager's state file (WirelessEnabled=false),
#                                   so NetworkManager starts with WiFi off; anything else
#                                   (no file, "on") leaves the image's default.
#                                   RESULT kind=radio-restore wifi=off|default
#   wired-set --iface=IF --mode=client
#   wired-set --iface=IF --mode=static --ip=A --prefix=N [--gateway=G] [--dns=D[,D]]
#   wired-set --iface=IF --mode=server --ip=A [--prefix=24]
#                                   the port's profile (bound to the MAC for a USB
#                                   adapter); restores the previous settings if the
#                                   new ones do not come up (exit 3)
#   dhcp-reserve --iface=IF --mac=M --ip=A
#   dhcp-reserve --iface=IF --mac=M --forget
#                                   (root) pins (or frees) the address a client of a
#                                   serving port gets: a dhcp-host line in the file the
#                                   image's drop-in names (dhcp-hostsfile=), then SIGHUP
#                                   to the port's dnsmasq. RESULT kind=reservation
#                                   iface= mac= ip= action=added|removed|unchanged.
#                                   The client moves at its next renewal or reconnect.
#   dhcp-probe --iface=IF           RESULT kind=offer ... per answering DHCP server,
#                                   then kind=probe servers=N carrier=0|1 (root)
#   ping --target=HOST [--iface=IF] [--count=N]
#                                   RESULT kind=reply|lost ... as they come, then kind=ping
#   internet-check [--iface=IF]     RESULT kind=check step=gateway|dns|https ..., then
#                                   kind=internet; root binds each step to the port
#   iperf-server --start|--stop     runs iperf3 -s until stopped: kind=iperf-peer,
#                                   kind=iperf ... per second; reason=port-busy when 5201 is taken
#   iperf-client --host=H [--secs=5|10|30] [--udp] [--reverse]
#                                   kind=iperf per second, kind=iperf-sum, kind=iperf-done
# The tools are reads: they never detach, and stop with their caller.
#   dhcp-guard --iface=IF --event=pre-up|check|expire|down   (root; the dispatcher script;
#                                   expire: the 30 s limit pre-up arms for an undecided check)
#                                   a serving port that comes up: gate its DHCP replies,
#                                   probe; another server -> the port down, the notice
#   dhcp-guard --iface=IF --retry   (root, a change) the port up again; the guard decides
#                                   RESULT kind=guard iface= action=serving|stopped|checking|none
#                                   [server=]
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
# NET_CTL_DETACHED=1 (set inside the transient unit), NET_CTL_GUARD_DIR
# (/run/net-ctl-guard), NET_CTL_NOTICE_FILE (/tmp/micropanel-notice),
# NET_CTL_GUARD_INLINE=1 (the guard's check without systemd-run),
# NET_CTL_GUARD_TIMEOUT (30 s: an undecided check has failed), NET_CTL_PID_DIR
# (/run: NetworkManager's nm-dnsmasq-<port>.pid), NET_CTL_RADIO_STATE
# (/data/network/wifi-radio.state), NET_CTL_NM_STATE
# (/var/lib/NetworkManager/NetworkManager.state).

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
# Reservations: the image's drop-in names the file (dhcp-hostsfile=), which
# dnsmasq re-reads on SIGHUP - a dhcp-host line in the drop-in directory would
# need the port's dnsmasq restarted, i.e. the port re-activated
RES_DROPIN=$SHARED_DIR/91-micropanel-reservations.conf
PID_DIR=${NET_CTL_PID_DIR:-/run}
# The WiFi switch, as the user left it: on the A/B image /var is rebuilt from
# the read-only root at every boot, so NetworkManager's own memory of it
# (WirelessEnabled in its state file) does not last; this file on /data does,
# and the restore unit puts it back before NetworkManager starts. A factory
# reset wipes it: the image's default (WiFi on) again.
RADIO_STATE=${NET_CTL_RADIO_STATE:-/data/network/wifi-radio.state}
NM_STATE=${NET_CTL_NM_STATE:-/var/lib/NetworkManager/NetworkManager.state}
PROBE=${NET_CTL_PROBE:-$(dirname "$(readlink -f "$0")")/net-dhcp-probe.py}
INET_TARGETS=${NET_CTL_INET_TARGETS:-1.1.1.1 8.8.8.8}
# The internet check (Tools) asks the port's DNS server for CHECK_NAME and
# fetches CHECK_URL over HTTPS - only when someone presses the button. A
# well-known, stable and tiny answer: HTTP 204, no body.
CHECK_NAME=${NET_CTL_CHECK_NAME:-www.google.com}
CHECK_URL=${NET_CTL_CHECK_URL:-https://www.google.com/generate_204}
CHECK_CODE=${NET_CTL_CHECK_CODE:-204}   # anything else (a captive portal's 200 or 302) is no internet
IPERF_PORT=5201
PYTHON=${NET_CTL_PYTHON:-python3}
# The OLED menu's own DHCP server (the system dnsmasq) keeps its leases here
LEGACY_LEASES=${NET_CTL_LEGACY_LEASES:-/var/lib/misc/dnsmasq.leases}
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
        # the DHCP guard's verdict on a serving port (dhcp-guard)
        if [ -f "$GUARD_DIR/$dev.stopped" ]; then
            kv guard stopped; kv guardserver "$(sed -n 's/^server=//p' "$GUARD_DIR/$dev.stopped")"
            kv guardtime "$(sed -n 's/^time=//p' "$GUARD_DIR/$dev.stopped")"
        elif [ -f "$GUARD_DIR/$dev.checking" ] && [ "$(file_age "$GUARD_DIR/$dev.checking")" -le "$GUARD_TIMEOUT" ]; then
            kv guard checking; kv guardserver ""; kv guardtime ""
        else
            kv guard ""; kv guardserver ""; kv guardtime ""
        fi
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
    # dash runs no EXIT trap when a signal ends it: the app stops a status
    # it no longer needs (TERM), and the directory must still go
    trap 'rm -rf "$inetdir"' EXIT
    trap 'exit 143' HUP INT TERM
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
    # Is the switch kept across a restart? Where the state's directory exists
    # (the A/B image's /data), wifi-radio writes it and the boot restores it
    wifikept=0
    [ -d "${RADIO_STATE%/*}" ] && wifikept=1
    {
        kv internet "$internet"; kv via "$via"; kv defaultdev "$defdev"; kv connectivity "$conn"
        kv wifi "$radio"; kv country "$country"; kv volatile "$volatile"; kv wifiboot "$wifiboot"
        kv wifikept "$wifikept"
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
    # A port in the OLED menu's server mode: the system dnsmasq's lease file
    [ "$(legacy_server_iface)" = "$iface" ] && f=$LEGACY_LEASES
    [ -r "$f" ] || { echo "RESULT kind=leases iface=$iface count=0"; exit 0; }
    n=0
    while read -r expires mac ip host _; do
        [ -n "$ip" ] || continue
        [ "$host" = "*" ] && host=
        # MACs upper-case everywhere, as NetworkManager prints them
        mac=$(printf '%s' "$mac" | tr 'a-f' 'A-F')
        { kv ip "$ip"; kv mac "$mac"; kv host "$host"; kv expires "$expires"; } | emit lease
        n=$((n + 1))
    done < "$f"
    # The reservations in this port's subnet (the file holds every port's)
    net=$(port_net "$iface")
    rf=$(res_file)
    if [ -n "$net" ] && [ -n "$rf" ] && [ -r "$rf" ]; then
        res_in_net "$rf" "$net" | while IFS=, read -r rmac rip; do
            { kv mac "$rmac"; kv ip "$rip"; } | emit reservation
        done
    fi
    echo "RESULT kind=leases iface=$iface count=$n"
}

# ---- reservations ------------------------------------------------------------

# The file the image's drop-in names, or nothing
res_file() { sed -n 's/^dhcp-hostsfile=//p' "$RES_DROPIN" 2>/dev/null | head -n 1; }
# A port's IPv4 network as "a.b.c.d/n" (its address/prefix), or nothing
port_net() { ip -4 -o addr show dev "$1" scope global 2>/dev/null | awk '{ print $4; exit }'; }
# "MAC,ip" lines of <file> whose ip is in <a.b.c.d/n>; MACs upper-case
res_in_net() { # <file> <net>
    awk -F , -v net="$2" '
        function num(a,    p) { split(a, p, "."); return ((p[1] * 256 + p[2]) * 256 + p[3]) * 256 + p[4] }
        BEGIN { split(net, n, "/"); size = 2 ^ (32 - n[2]); base = int(num(n[1]) / size) * size }
        /^[[:space:]]*(#|$)/ { next }
        NF == 2 && int(num($2) / size) * size == base { print toupper($1) "," $2 }' "$1"
}
# in_net <ip> <a.b.c.d/n>: same subnet, and neither its network nor its broadcast address
in_net() {
    awk -v ip="$1" -v net="$2" '
        function num(a,    p) { split(a, p, "."); return ((p[1] * 256 + p[2]) * 256 + p[3]) * 256 + p[4] }
        BEGIN { split(net, n, "/"); size = 2 ^ (32 - n[2]); base = int(num(n[1]) / size) * size
                v = num(ip); exit !(int(v / size) * size == base && v != base && v != base + size - 1) }'
}

cmd_dhcp_reserve() {
    [ "${NET_CTL_UID:-$(id -u)}" = 0 ] || fail 1 reservation bad-arguments "dhcp-reserve runs as root"
    [ -n "$iface" ] || fail 1 reservation bad-arguments "--iface= is required"
    mac=$(printf '%s' "$opt_mac" | tr 'a-f' 'A-F')
    printf '%s' "$mac" | grep -Eqx '([0-9A-F]{2}:){5}[0-9A-F]{2}' \
        || fail 1 reservation bad-arguments "--mac= is six hex pairs, e.g. 48:B0:2D:87:68:8D"
    if [ "$opt_forget" = 0 ]; then
        valid_ip "$opt_ip" || fail 1 reservation bad-arguments "--ip= is not an IPv4 address"
    fi
    need_unlocked reservation
    rf=$(res_file)
    [ -n "$rf" ] || fail 2 reservation unsupported "this image has no reservation file ($RES_DROPIN)"
    [ "$(legacy_server_iface)" = "$iface" ] && \
        fail 1 reservation legacy-server "$iface serves through the panel menu's DHCP server, which keeps no reservations"
    net=$(port_net "$iface")
    pidf="$PID_DIR/nm-dnsmasq-$iface.pid"
    { [ -n "$net" ] && [ -f "$pidf" ]; } || fail 1 reservation not-serving "$iface is not serving addresses"
    port_ip=${net%/*}

    current=''
    [ -r "$rf" ] && current=$(res_in_net "$rf" "$net" | awk -F , -v m="$mac" '$1 == m { print $2; exit }')
    if [ "$opt_forget" = 1 ]; then
        ip_out=$current
        [ -n "$current" ] || { { kv iface "$iface"; kv mac "$mac"; kv ip ""; kv action unchanged; } | emit reservation; exit 0; }
        action=removed
    else
        in_net "$opt_ip" "$net" || fail 1 reservation bad-arguments "$opt_ip is not a client address in $iface's network $net"
        [ "$opt_ip" = "$port_ip" ] && fail 1 reservation bad-arguments "$opt_ip is $iface's own address"
        if [ -r "$rf" ]; then
            other=$(res_in_net "$rf" "$net" | awk -F , -v m="$mac" -v ip="$opt_ip" '$2 == ip && $1 != m { print $1; exit }')
            [ -z "$other" ] || fail 1 reservation in-use "$opt_ip is reserved for $other"
        fi
        lf="$LEASE_DIR/dnsmasq-$iface.leases"
        if [ -r "$lf" ]; then
            other=$(awk -v ip="$opt_ip" -v m="$mac" '$3 == ip && toupper($2) != m { print toupper($2); exit }' "$lf")
            [ -z "$other" ] || fail 1 reservation in-use "$opt_ip is leased to $other"
        fi
        ip_out=$opt_ip
        if [ "$current" = "$opt_ip" ]; then
            { kv iface "$iface"; kv mac "$mac"; kv ip "$opt_ip"; kv action unchanged; } | emit reservation; exit 0
        fi
        action=added
    fi
    if [ "$dry_run" = 1 ]; then
        dry_note "$action $mac,$ip_out in $rf; SIGHUP $(cat "$pidf")"
        { kv iface "$iface"; kv mac "$mac"; kv ip "$ip_out"; kv action "$action"; } | emit reservation; exit 0
    fi

    # The new file beside the old one, then a rename: dnsmasq never reads half a file
    dir=$(dirname "$rf")
    install -d -m0755 "$dir"
    tmp=$(mktemp "$dir/.dhcp-reservations.XXXXXX") || fail 2 reservation write-failed "cannot write in $dir"
    {
        echo "# dhcp-host lines for the serving ports (network-manager-app, net-ctl.sh dhcp-reserve)"
        if [ -r "$rf" ]; then
            # every other line, minus this MAC's line in this port's network
            awk -F , -v m="$mac" -v keep="$(res_in_net "$rf" "$net" | awk -F , -v m="$mac" '$1 == m { print $2 }')" '
                /^[[:space:]]*#/ { next } /^[[:space:]]*$/ { next }
                toupper($1) == m && $2 == keep { next }
                { print }' "$rf"
        fi
        [ "$opt_forget" = 1 ] || echo "$mac,$opt_ip"
    } > "$tmp" || { rm -f "$tmp"; fail 2 reservation write-failed "cannot write $tmp"; }
    chmod 0644 "$tmp"
    mv -f "$tmp" "$rf" || { rm -f "$tmp"; fail 2 reservation write-failed "cannot replace $rf"; }
    # dnsmasq (running as nobody) must read it after SIGHUP
    kill -HUP "$(cat "$pidf")" 2>/dev/null || notice "the reservation is saved; $iface's dnsmasq did not take the reload signal"
    journal "reservation $action: $iface $mac $ip_out"
    { kv iface "$iface"; kv mac "$mac"; kv ip "$ip_out"; kv action "$action"; } | emit reservation
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

# The switch's state, whole or not at all (root; the directory is the
# skeleton's). No directory: nothing to keep (a writable root keeps
# NetworkManager's own state). A failure costs the memory, not the switch.
save_radio() { # on|off
    [ -d "${RADIO_STATE%/*}" ] || return 0
    tmp="$RADIO_STATE.tmp.$$"
    if printf '%s\n' "$1" > "$tmp" && mv -f "$tmp" "$RADIO_STATE"; then
        sync "$RADIO_STATE" 2>/dev/null
    else
        rm -f "$tmp"; notice "could not keep the WiFi switch in $RADIO_STATE"
    fi
}

# At boot, before NetworkManager: a kept "off" becomes WirelessEnabled=false
# in NetworkManager's (volatile) state file, which it reads at start
cmd_wifi_radio_restore() {
    saved=$(head -c 3 "$RADIO_STATE" 2>/dev/null)
    case $saved in
        off*) ;;
        *) echo "RESULT kind=radio-restore ok=1 wifi=default"; exit 0 ;;
    esac
    if [ "$dry_run" = 1 ]; then dry_note "WirelessEnabled=false in $NM_STATE"; echo "RESULT kind=radio-restore ok=1 wifi=off dryrun=1"; exit 0; fi
    mkdir -p "${NM_STATE%/*}" || fail 2 radio-restore failed "cannot create ${NM_STATE%/*}"
    tmp="$NM_STATE.tmp.$$"
    if [ -s "$NM_STATE" ]; then
        awk 'BEGIN { done = 0 }
             /^\[/ { if (sect == "main" && !done) { print "WirelessEnabled=false"; done = 1 }
                     sect = ($0 == "[main]") ? "main" : "other" }
             sect == "main" && /^WirelessEnabled=/ { if (!done) print "WirelessEnabled=false"; done = 1; next }
             { print }
             END { if (!done) { if (sect != "main") print "[main]"; print "WirelessEnabled=false" } }' \
            "$NM_STATE" > "$tmp" || { rm -f "$tmp"; fail 2 radio-restore failed "cannot write $NM_STATE"; }
    else
        printf '[main]\nWirelessEnabled=false\n' > "$tmp" || fail 2 radio-restore failed "cannot write $NM_STATE"
    fi
    mv -f "$tmp" "$NM_STATE" || { rm -f "$tmp"; fail 2 radio-restore failed "cannot write $NM_STATE"; }
    journal "WiFi kept off: WirelessEnabled=false"
    echo "RESULT kind=radio-restore ok=1 wifi=off"
}

cmd_wifi_radio() {
    need_nm
    [ -n "$opt_onoff" ] || fail 1 radio bad-arguments "--on or --off is required"
    need_unlocked radio
    [ -n "$(wifi_device)" ] || fail 1 radio unsupported "this system has no WiFi device"
    if [ "$opt_onoff" = off ]; then
        if [ "$dry_run" = 1 ]; then dry_note "nmcli radio wifi off"; echo "RESULT kind=radio ok=1 wifi=off dryrun=1"; exit 0; fi
        out=$(nmcli radio wifi off 2>&1) || fail 2 radio failed "$out"
        save_radio off
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
    save_radio on
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
    # the owner chose a mode: a DHCP guard's "stopped" for this port is answered
    # (server mode again: the guard probes again when the port comes up)
    if [ -f "$GUARD_DIR/$iface.stopped" ]; then
        rm -f "$GUARD_DIR/$iface.stopped"
        notice_remove "$iface"
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
    offers=$(probe_offers "$iface" 2>&1) || fail 2 probe failed "$offers"
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

# ---- tools: ping, internet check, iperf3 ---------------------------------------

# A host name or an address: no option injection, nothing a shell would mind
valid_host() {
    case $1 in
        ''|-*) return 1 ;;
        *[!A-Za-z0-9.:_-]*) return 1 ;;
    esac
    return 0
}

# stream <parser> <command...>: runs a long tool with its output into a FIFO
# that <parser> reads. The tool is stopped when this script is stopped
# (TERM, HUP, INT) and - because the app may be killed with SIGKILL, which
# reaches nobody - when the caller is gone: a watcher checks the caller every
# second, as `monitor` does. No ping or iperf3 of ours outlives the app.
stream_files=''   # more files for stream() to remove when it is stopped
stream() {
    parser=$1; shift
    caller=$PPID
    sfifo=$(mktemp -u "${TMPDIR:-/tmp}/net-ctl-tool.XXXXXX") && mkfifo -m 600 "$sfifo" || exit 2
    "$@" > "$sfifo" 2>&1 &
    child=$!
    (while kill -0 "$caller" 2>/dev/null && kill -0 "$child" 2>/dev/null; do sleep 1; done
     kill "$child" 2>/dev/null; rm -f "$sfifo") &
    watch=$!
    # shellcheck disable=SC2086 # stream_files: a list
    trap 'kill "$child" "$watch" 2>/dev/null; rm -f "$sfifo" $stream_files; exit 130' HUP INT TERM
    "$parser" < "$sfifo"
    wait "$child"; stream_rc=$?
    kill "$watch" 2>/dev/null
    rm -f "$sfifo"
    trap - HUP INT TERM
}

# ping: one RESULT line per reply or loss as they come, then the summary
ping_parse() {
    p_sent=0 p_recv=0 p_avg='' p_reason='' p_lost=''
    while IFS= read -r line; do
        case $line in
            *" bytes from "*"icmp_seq="*)
                seq=${line#*icmp_seq=}; seq=${seq%% *}
                ms=${line#*time=}; ms=${ms%% *}
                from=${line#* bytes from }; from=${from%%:*}; from=${from%% *}
                echo "RESULT kind=reply seq=$seq ms=$ms from=$from" ;;
            "no answer yet for icmp_seq="*)
                seq=${line#no answer yet for icmp_seq=}
                case " $p_lost " in *" $seq "*) ;; *) p_lost="$p_lost $seq"; echo "RESULT kind=lost seq=$seq" ;; esac ;;
            *"Destination Host Unreachable"*|*"Destination Net Unreachable"*)
                # reported once per packet (ping may also have said "no answer yet")
                seq=${line#*icmp_seq=}; seq=${seq%% *}
                case " $p_lost " in *" $seq "*) ;; *) p_lost="$p_lost $seq"; echo "RESULT kind=lost seq=$seq reason=unreachable" ;; esac ;;
            *" packets transmitted, "*)
                p_sent=${line%% packets transmitted*}
                p_recv=${line#*transmitted, }; p_recv=${p_recv%% received*} ;;
            "rtt "*|"round-trip "*)
                p_avg=$(printf '%s' "$line" | awk -F'= ' '{ split($2, v, "/"); print v[2] }') ;;
            *"Name or service not known"*|*"Temporary failure in name resolution"*|*"unknown host"*)
                p_reason=unknown-host ;;
            *"Network is unreachable"*) p_reason=unreachable ;;
            *"Cannot assign requested address"*|*"unknown iface"*|*"SO_BINDTODEVICE"*) p_reason=bad-interface ;;
        esac
    done
}

cmd_ping() {
    valid_host "$opt_target" || fail 1 ping bad-arguments "--target= is an address or a host name"
    count=${opt_count:-10}
    case $count in ''|*[!0-9]*) fail 1 ping bad-arguments "--count= is 1 to 100" ;; esac
    if [ "$count" -lt 1 ] || [ "$count" -gt 100 ]; then fail 1 ping bad-arguments "--count= is 1 to 100"; fi
    set -- ping -n -O -c "$count" -W 2
    [ -n "$iface" ] && set -- "$@" -I "$iface"
    stream ping_parse "$@" "$opt_target"
    loss=''
    [ "$p_sent" -gt 0 ] 2>/dev/null && loss=$(( (p_sent - p_recv) * 100 / p_sent ))
    if [ -n "$p_reason" ] && [ "${p_recv:-0}" = 0 ]; then
        echo "RESULT kind=ping target=$opt_target sent=$p_sent received=0 avg= loss=100 reason=$p_reason"
        exit 2
    fi
    echo "RESULT kind=ping target=$opt_target sent=$p_sent received=$p_recv avg=$p_avg loss=$loss"
    [ "${p_recv:-0}" -gt 0 ] 2>/dev/null && exit 0
    exit 2
}

# One DNS question (A record of the check name) to one server, bound to the
# port when root may bind: python3, standard library only
DNS_QUERY='import random, socket, struct, sys, time
name, server, iface = sys.argv[1], sys.argv[2], sys.argv[3]
q = struct.pack("!HHHHHH", random.getrandbits(16), 0x0100, 1, 0, 0, 0)
q += b"".join(bytes([len(p)]) + p.encode() for p in name.split(".")) + b"\0" + struct.pack("!HH", 1, 1)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
bound = 0
if iface:
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, iface.encode() + b"\0"); bound = 1
    except OSError:
        pass
s.settimeout(3)
t = time.monotonic()
try:
    s.sendto(q, (server, 53)); r = s.recv(1500)
except socket.timeout:
    print("no-answer", bound); sys.exit(0)
except OSError as e:
    print("unreachable", bound); sys.exit(0)
ms = round((time.monotonic() - t) * 1000, 1)
rcode = r[3] & 15
if rcode == 3: print("nxdomain", bound, ms); sys.exit(0)
if rcode != 0: print("servfail", bound, ms); sys.exit(0)
an = struct.unpack("!H", r[6:8])[0]
i = 12
while r[i] != 0: i += r[i] + 1
i += 5
for _ in range(an):
    if r[i] & 0xC0 == 0xC0: i += 2
    else:
        while r[i] != 0: i += r[i] + 1
        i += 1
    typ, cls, ttl, ln = struct.unpack("!HHIH", r[i:i + 10]); i += 10
    if typ == 1 and ln == 4:
        print("ok", bound, ms, socket.inet_ntoa(r[i:i + 4])); sys.exit(0)
    i += ln
print("no-address", bound, ms)'

# internet-check [--iface=IF]: the port's gateway answers -> its DNS server
# resolves CHECK_NAME -> CHECK_URL answers over HTTPS. Each step is bound to
# the port (as root; the app runs it through sudo -n), so a port's answer is
# that port's, not the default route's. Without --iface: the default route's.
cmd_internet_check() {
    need_nm
    dev=${iface:-$(default_route_dev)}
    [ -n "$dev" ] || { echo "RESULT kind=check step=gateway ok=0 reason=no-route"
                       echo "RESULT kind=check step=dns ok=0 reason=skipped"
                       echo "RESULT kind=check step=https ok=0 reason=skipped"
                       echo "RESULT kind=internet iface= ok=0"; exit 2; }
    root=0; [ "${NET_CTL_UID:-$(id -u)}" = 0 ] && root=1
    all=1

    gw=$(ip -4 route show default dev "$dev" 2>/dev/null | awk '{ print $3; exit }')
    if [ -z "$gw" ]; then
        echo "RESULT kind=check step=gateway ok=0 reason=no-gateway"; all=0
    else
        out=$(ping -n -c 1 -W 2 -I "$dev" "$gw" 2>&1)
        ms=$(printf '%s\n' "$out" | sed -n 's/.*time=\([0-9.]*\).*/\1/p' | head -n 1)
        if [ -n "$ms" ]; then echo "RESULT kind=check step=gateway ok=1 ms=$ms target=$gw"
        else echo "RESULT kind=check step=gateway ok=0 target=$gw reason=no-reply"; all=0; fi
    fi

    dns=$(nmcli -t -f IP4.DNS device show "$dev" 2>/dev/null | sed -n 's/^IP4\.DNS\[1\]://p')
    # the port's own DNS server; the system's only for the default-route check
    if [ -z "$dns" ] && [ -z "$iface" ]; then
        dns=$(awk '$1 == "nameserver" && $2 ~ /^[0-9.]+$/ { print $2; exit }' /etc/resolv.conf 2>/dev/null)
    fi
    addr=''
    if [ -z "$dns" ]; then
        echo "RESULT kind=check step=dns ok=0 reason=no-dns-server"; all=0
    else
        bind=''; [ "$root" = 1 ] && bind=$dev
        # shellcheck disable=SC2046
        set -- $("$PYTHON" -c "$DNS_QUERY" "$CHECK_NAME" "$dns" "$bind" 2>/dev/null)
        if [ "${1:-}" = ok ]; then
            addr=$4
            echo "RESULT kind=check step=dns ok=1 ms=$3 server=$dns name=$CHECK_NAME addr=$addr bound=$2"
        else
            echo "RESULT kind=check step=dns ok=0 server=$dns name=$CHECK_NAME bound=${2:-0} reason=${1:-failed}"; all=0
        fi
    fi

    # HTTPS through the port; to the address the port's DNS gave, if it gave one
    host=${CHECK_URL#https://}; host=${host%%/*}
    set -- curl -sS -o /dev/null --max-time 8 -w '%{http_code} %{time_total}'
    if [ "$root" = 1 ]; then set -- "$@" --interface "if!$dev"; else set -- "$@" --interface "$dev"; fi
    [ -n "$addr" ] && set -- "$@" --resolve "$host:443:$addr"
    out=$("$@" "$CHECK_URL" 2>&1)
    code=$(printf '%s\n' "$out" | tail -n 1 | awk '{ print $1 }')
    secs=$(printf '%s\n' "$out" | tail -n 1 | awk '{ print $2 }')
    case $code in
        "$CHECK_CODE") echo "RESULT kind=check step=https ok=1 ms=$(awk -v s="$secs" 'BEGIN { printf "%d", s * 1000 }') code=$code url=$CHECK_URL bound=$root" ;;
        *)
            case $out in
                *"timed out"*|*"Timeout"*) why=timeout ;;
                *"SSL"*|*"certificate"*) why=tls ;;
                *"Could not resolve"*) why=no-name ;;
                *"Failed to connect"*|*"Couldn't connect"*|*"No route"*) why=unreachable ;;
                *) case $code in 000|'') why=failed ;; *) why=http-$code ;; esac ;;
            esac
            echo "RESULT kind=check step=https ok=0 url=$CHECK_URL bound=$root reason=$why"; all=0 ;;
    esac
    echo "RESULT kind=internet iface=$dev ok=$all"
    [ "$all" = 1 ] && exit 0
    exit 2
}

# iperf3 3.12 (the image's): no --json-stream; the interval and summary lines
# are parsed as printed, with --forceflush so they come as they happen
iperf_line() { # one iperf3 output line -> RESULT lines
    case $1 in
        *"Accepted connection from "*)
            from=${1#*Accepted connection from }; from=${from%%,*}
            echo "RESULT kind=iperf-peer from=$from" ;;
        *"unable to start listener"*|*"Address already in use"*) i_reason=port-busy ;;
        *"Connection refused"*) i_reason=refused ;;
        *"No route to host"*|*"Network is unreachable"*|*"timed out"*) i_reason=unreachable ;;
        *"server is busy"*) i_reason="server-busy" ;;
        *"Name or service not known"*|*"unknown host"*|*"Temporary failure in name"*) i_reason=unknown-host ;;
        "["*"]"*"sec"*"/sec"*)
            printf '%s\n' "$1" | awk '
                function mbit(v, u) { if (u ~ /^G/) return v * 1000; if (u ~ /^M/) return v; if (u ~ /^K/) return v / 1000; return v / 1000000 }
                { sub(/^\[ *[0-9A-Z]+\] */, "")
                  iv = $1; rate = ""; unit = ""
                  for (i = 1; i <= NF; i++) if ($i ~ /bits\/sec$/) { rate = $(i - 1); unit = $i; idx = i }
                  if (rate == "") next
                  m = sprintf("%.1f", mbit(rate + 0, unit))
                  role = ($NF == "sender" || $NF == "receiver") ? $NF : ""
                  if (role == "") { print "RESULT kind=iperf interval=" iv " mbit=" m; next }
                  extra = ""
                  if ($(idx + 1) ~ /^[0-9]+$/ && $(idx + 2) == role) extra = " retr=" $(idx + 1)
                  if ($(idx + 2) == "ms") { split($(idx + 3), lp, "/"); extra = " jitter=" $(idx + 1) " lost=" lp[1] " packets=" lp[2] }
                  print "RESULT kind=iperf-sum role=" role " interval=" iv " mbit=" m extra }' ;;
    esac
}
iperf_parse() { i_reason=''; while IFS= read -r line; do iperf_line "$line"; done; }

IPERF_PIDFILE=${TMPDIR:-/tmp}/net-ctl-iperf-server-$(id -u).pid

cmd_iperf_server() {
    command -v iperf3 >/dev/null 2>&1 || fail 1 iperf-server unsupported "iperf3 is not installed"
    if [ "$opt_stop" = 1 ]; then
        # only our own server: a stale file's pid may belong to anyone by now
        pid=$(cat "$IPERF_PIDFILE" 2>/dev/null)
        case $pid in ''|*[!0-9]*) pid='' ;; esac
        if [ -n "$pid" ] && tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | grep -q 'net-ctl[^ ]* iperf-server'; then
            kill "$pid" 2>/dev/null
        fi
        rm -f "$IPERF_PIDFILE"
        echo "RESULT kind=iperf-server running=0"
        exit 0
    fi
    # 5201 taken: the panel menu's own iperf3 server, most likely
    if ss -ltnH "sport = :$IPERF_PORT" 2>/dev/null | grep -q .; then
        echo "RESULT kind=iperf-server running=0 port=$IPERF_PORT reason=port-busy"
        exit 2
    fi
    addrs=$(ip -4 -o addr show 2>/dev/null | awk '$2 != "lo" { split($4, a, "/"); printf "%s%s", (n++ ? "," : ""), a[1] }')
    echo "RESULT kind=iperf-server running=1 port=$IPERF_PORT addrs=$addrs"
    echo $$ > "$IPERF_PIDFILE"
    stream_files=$IPERF_PIDFILE
    stream iperf_parse iperf3 -s -p "$IPERF_PORT" -i 1 --forceflush
    rm -f "$IPERF_PIDFILE"
    if [ -n "$i_reason" ]; then echo "RESULT kind=iperf-server running=0 reason=$i_reason"; exit 2; fi
    echo "RESULT kind=iperf-server running=0"
}

cmd_iperf_client() {
    command -v iperf3 >/dev/null 2>&1 || fail 1 iperf-done unsupported "iperf3 is not installed"
    valid_host "$opt_host" || fail 1 iperf-done bad-arguments "--host= is an address or a host name"
    secs=${opt_secs:-10}
    case $secs in 5|10|30) ;; *) fail 1 iperf-done bad-arguments "--secs= is 5, 10 or 30" ;; esac
    set -- iperf3 -c "$opt_host" -p "$IPERF_PORT" -t "$secs" -i 1 --forceflush --connect-timeout 3000
    [ "$opt_udp" = 1 ] && set -- "$@" -u -b 100M
    [ "$opt_reverse" = 1 ] && set -- "$@" -R
    echo "RESULT kind=iperf-client host=$opt_host secs=$secs udp=$opt_udp reverse=$opt_reverse"
    stream iperf_parse "$@"
    if [ -n "$i_reason" ]; then echo "RESULT kind=iperf-done ok=0 host=$opt_host reason=$i_reason"; exit 2; fi
    if [ "$stream_rc" != 0 ]; then echo "RESULT kind=iperf-done ok=0 host=$opt_host reason=failed"; exit 2; fi
    echo "RESULT kind=iperf-done ok=1 host=$opt_host"
}

# ---- the DHCP guard -----------------------------------------------------------
# A port in server mode serves as soon as it has link - at boot, or when a
# cable goes in, with the app open or not. NetworkManager starts its dnsmasq
# before any dispatcher event (measured on 1.42.4: it already runs at
# pre-up), so no hook can keep it from starting. What pre-up can do, ~0.1 s
# later, is close a gate: an nftables rule that drops the port's outgoing
# DHCP replies (UDP source port 67). The probe then asks the network; if
# another server answers, the port is taken down (its profile stays in
# server mode, for the owner to decide), the launcher's notice chip and badge
# say so, and the Wired card shows it. If none answers, the gate opens.
#
# The dispatcher script (src/90-net-ctl-guard) calls
#   dhcp-guard --iface=IF --event=pre-up    gate, then the check in its own unit
#   dhcp-guard --iface=IF --event=down      gate open (the port is gone anyway)
# and the app calls dhcp-guard --iface=IF --retry ("Try again": the port up
# again; the guard's probe decides).
GUARD_DIR=${NET_CTL_GUARD_DIR:-/run/net-ctl-guard}
# A check that has not decided after this long has failed (its unit did not
# start, or died before its EXIT trap): the gate opens, as on a probe failure
GUARD_TIMEOUT=${NET_CTL_GUARD_TIMEOUT:-30}
NOTICE_FILE=${NET_CTL_NOTICE_FILE:-/tmp/micropanel-notice}

# seconds since a file was last written (busybox stat has -c %Y too)
file_age() { echo $(( $(date +%s) - $(stat -c %Y "$1" 2>/dev/null || date +%s) )); }

guard_log() { logger -t net-ctl.sh -- "dhcp-guard: $*" 2>/dev/null; return 0; }
notice_line() { echo "DHCP serving stopped: $1"; }

# The launcher shows the notice file's first line as a header chip. Another
# writer's line (System Manager's "Power cycle required") stays first; the
# file stays writable by the user who runs the launcher's apps.
notice_add() {
    line=$(notice_line "$1")
    if [ -f "$NOTICE_FILE" ]; then
        grep -qxF "$line" "$NOTICE_FILE" 2>/dev/null || printf '%s\n' "$line" >> "$NOTICE_FILE"
    else
        printf '%s\n' "$line" > "$NOTICE_FILE" && chmod 0666 "$NOTICE_FILE"
    fi
}
notice_remove() {
    [ -f "$NOTICE_FILE" ] || return 0
    line=$(notice_line "$1")
    grep -qxF "$line" "$NOTICE_FILE" 2>/dev/null || return 0
    rest=$(grep -vxF "$line" "$NOTICE_FILE")
    if [ -n "$rest" ]; then printf '%s\n' "$rest" > "$NOTICE_FILE"; else rm -f "$NOTICE_FILE"; fi
}

gate_on() {
    nft list table inet net_ctl_guard >/dev/null 2>&1 || nft -f - <<'EOF'
table inet net_ctl_guard {
    set gated { type ifname; }
    chain out {
        type filter hook output priority 0; policy accept;
        oifname @gated udp sport 67 drop
    }
}
EOF
    nft add element inet net_ctl_guard gated "{ \"$1\" }"
}
gate_off() { nft delete element inet net_ctl_guard gated "{ \"$1\" }" 2>/dev/null; return 0; }

# The active profile on the port is in the app's server mode
port_serves() {
    u=$(nmcli -t -e yes -f DEVICE,UUID connection show --active 2>/dev/null | untab \
        | awk -F "$us" -v d="$1" '$1 == d { print $2; exit }')
    [ -n "$u" ] && [ "$(nm_get "$u" ipv4.method)" = shared ]
}

# Other DHCP servers on the port: "server US offered US router" lines; the
# rig's own addresses are not "other"
probe_offers() {
    own=$(ip -4 -o addr show 2>/dev/null | awk '{ split($4, a, "/"); printf "%s ", a[1] }')
    out=$("$PROBE" --iface "$1" --timeout 4.5 2>&1) || { printf '%s' "$out" >&2; return 2; }
    printf '%s\n' "$out" | awk -v own=" $own " '
        $1 == "OFFER" { s = o = r = ""
            for (i = 2; i <= NF; i++) { p = index($i, "="); k = substr($i, 1, p - 1); v = substr($i, p + 1)
                if (k == "server") s = v; else if (k == "offered") o = v; else if (k == "router") r = v }
            if (s == "" || index(own, " " s " ") || seen[s]++) next
            print s "\037" o "\037" r }'
}

guard_check() {
    : > "$GUARD_DIR/$iface.checking"
    trap 'gate_off "$iface"; rm -f "$GUARD_DIR/$iface.checking"' EXIT
    trap 'exit 143' HUP INT TERM
    t0=$(date +%s)
    if ! offers=$(probe_offers "$iface" 2>/dev/null); then
        # no verdict: serve, as without the guard, and say so
        guard_log "$iface: the probe failed; serving"
        echo "RESULT kind=guard iface=$iface action=serving reason=probe-failed"
        exit 2
    fi
    server=$(printf '%s\n' "$offers" | awk -F "$us" 'NF { print $1; exit }')
    if [ -n "$server" ]; then
        printf 'server=%s\ntime=%s\n' "$server" "$t0" > "$GUARD_DIR/$iface.stopped"
        nmcli device disconnect "$iface" >/dev/null 2>&1
        notice_add "$iface"
        guard_log "$iface: another DHCP server ($server) answered; serving stopped, $iface taken down (its mode is still DHCP server)"
        echo "RESULT kind=guard iface=$iface action=stopped server=$server"
        exit 0
    fi
    rm -f "$GUARD_DIR/$iface.stopped"
    notice_remove "$iface"
    guard_log "$iface: no other DHCP server answered; serving"
    echo "RESULT kind=guard iface=$iface action=serving"
}

# "Try again": forget the verdict, bring the profile up; the guard's pre-up
# gates and probes before the port serves. Waits for that verdict.
guard_retry() {
    if [ "$dry_run" = 1 ]; then
        dry_note "forget the guard's verdict on $iface; nmcli connection up its profile; the guard probes first"
        echo "RESULT kind=guard iface=$iface action=serving dryrun=1"
        exit 0
    fi
    need_unlocked guard
    profiles=$(eth_profiles)
    mac=$(read_sys "$iface" address)
    uuid=$(port_profile "$profiles" "$iface" "$mac" "" | cut -d "$us" -f1)
    [ -n "$uuid" ] || fail 1 guard bad-arguments "$iface has no saved profile"
    rm -f "$GUARD_DIR/$iface.stopped"
    notice_remove "$iface"
    notice "bringing $iface up; the guard probes before it serves"
    up_out=$(nmcli --wait 60 connection up uuid "$uuid" ifname "$iface" 2>&1) \
        || fail 2 guard activation-failed "$up_out"
    n=0
    # the check starts with pre-up and takes ~5 s
    while { [ -e "$GUARD_DIR/$iface.checking" ] || [ $n -lt 4 ]; } && [ $n -lt 60 ]; do sleep 0.5; n=$((n + 1)); done
    if [ -f "$GUARD_DIR/$iface.stopped" ]; then
        server=$(sed -n 's/^server=//p' "$GUARD_DIR/$iface.stopped")
        echo "RESULT kind=guard iface=$iface action=stopped server=$server"
        exit 2
    fi
    echo "RESULT kind=guard iface=$iface action=serving"
    exit 0
}

cmd_dhcp_guard() {
    need_nm
    [ -n "$iface" ] || fail 1 guard bad-arguments "--iface= is required"
    [ "${NET_CTL_UID:-$(id -u)}" = 0 ] || fail 1 guard bad-arguments "dhcp-guard runs as root"
    mkdir -p "$GUARD_DIR" && chmod 0755 "$GUARD_DIR"
    [ "$opt_retry" = 1 ] && guard_retry
    case $opt_event in
        pre-up)
            if ! port_serves "$iface"; then
                echo "RESULT kind=guard iface=$iface action=none reason=not-serving"
                exit 0
            fi
            gate_on "$iface" || guard_log "$iface: could not close the gate (nft)"
            : > "$GUARD_DIR/$iface.checking"
            # the check in its own unit: the dispatcher does not wait for it,
            # and its exit when idle cannot end the check halfway
            if [ -z "${NET_CTL_GUARD_INLINE:-}" ] && command -v systemd-run >/dev/null 2>&1; then
                envs=''
                for v in $(env | sed -n 's/^\(NET_CTL_[A-Z0-9_]*\)=.*/\1/p'); do envs="$envs --setenv=$v"; done
                # shellcheck disable=SC2086
                systemd-run --quiet --collect --no-block --unit="net-ctl-guard-$iface" \
                    --description="net-ctl.sh DHCP guard $iface" $envs \
                    "$(readlink -f "$0")" dhcp-guard --iface="$iface" --event=check >/dev/null 2>&1 \
                    || guard_log "$iface: a check is already running"
                # and a timer: a check that never decides must not leave the
                # gate shut and "checking" for good (the app may be closed)
                # shellcheck disable=SC2086
                systemd-run --quiet --collect --no-block --on-active="$GUARD_TIMEOUT" \
                    --unit="net-ctl-guard-expire-$iface-$(date +%s)" \
                    --description="net-ctl.sh DHCP guard $iface: time limit" $envs \
                    "$(readlink -f "$0")" dhcp-guard --iface="$iface" --event=expire >/dev/null 2>&1 \
                    || guard_log "$iface: could not arm the check's time limit"
                echo "RESULT kind=guard iface=$iface action=checking"
            else
                guard_check
            fi ;;
        check) guard_check ;;
        expire)
            # the time limit armed at pre-up: a check still undecided has failed
            m="$GUARD_DIR/$iface.checking"
            if [ -e "$m" ] && [ "$(file_age "$m")" -ge "$GUARD_TIMEOUT" ]; then
                systemctl stop "net-ctl-guard-$iface.service" >/dev/null 2>&1
                gate_off "$iface"
                rm -f "$m"
                guard_log "$iface: the check did not finish within $GUARD_TIMEOUT s; gate opened, serving unchecked"
                echo "RESULT kind=guard iface=$iface action=serving reason=check-timeout"
            else
                echo "RESULT kind=guard iface=$iface action=none"
            fi ;;
        down)
            gate_off "$iface"
            echo "RESULT kind=guard iface=$iface action=none" ;;
        *) fail 1 guard bad-arguments "--event= is pre-up, check, expire or down; or --retry" ;;
    esac
}

# ---- a change outlives its caller ------------------------------------------------

# Rule 6a: as a transient unit. Only for changes, only as root, only once,
# never in a dry run; without systemd-run (Buildroot) the change runs here.
detach() { # <command> <args...>
    case $1 in
        wifi-connect|wifi-disconnect|wifi-forget|wifi-autoconnect|wifi-radio|wired-set|dhcp-reserve) ;;
        dhcp-guard) [ "$opt_retry" = 1 ] || return 0 ;;
        *) return 0 ;;
    esac
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
opt_target='' opt_count='' opt_host='' opt_secs='' opt_udp=0 opt_reverse=0 opt_stop=0
opt_event='' opt_retry=0 opt_mac='' opt_forget=0
for arg in "$@"; do
    case $arg in
        --iface=*) iface=${arg#--iface=} ;;
        --mode=*) opt_mode=${arg#--mode=} ;;
        --ip=*) opt_ip=${arg#--ip=} ;;
        --prefix=*) opt_prefix=${arg#--prefix=} ;;
        --gateway=*) opt_gateway=${arg#--gateway=} ;;
        --dns=*) opt_dns=${arg#--dns=} ;;
        --target=*) opt_target=${arg#--target=} ;;
        --count=*) opt_count=${arg#--count=} ;;
        --host=*) opt_host=${arg#--host=} ;;
        --secs=*) opt_secs=${arg#--secs=} ;;
        --udp) opt_udp=1 ;;
        --reverse) opt_reverse=1 ;;
        --start) opt_stop=0 ;;
        --stop) opt_stop=1 ;;
        --event=*) opt_event=${arg#--event=} ;;
        --retry) opt_retry=1 ;;
        --mac=*) opt_mac=${arg#--mac=} ;;
        --forget) opt_forget=1 ;;
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

# One rule for --iface= (review v3, 2.2): a device NetworkManager lists
# (list_devices), for every command that takes one
case $cmd in
    leases|wired-set|dhcp-probe|dhcp-guard|dhcp-reserve|ping|internet-check)
        if [ -n "$iface" ]; then
            case $cmd in leases) k=lease ;; wired-set) k=wired ;; dhcp-probe) k=probe ;; dhcp-guard) k=guard ;;
                         dhcp-reserve) k=reservation ;;
                         ping) k=ping ;; *) k=internet ;; esac
            need_nm
            list_devices | awk -F "$us" -v d="$iface" '$1 == d { f = 1 } END { exit !f }' \
                || fail 1 "$k" bad-arguments "$iface is not a network port here"
        fi ;;
esac

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
    wifi-radio-restore) cmd_wifi_radio_restore ;;
    wired-set) cmd_wired_set ;;
    dhcp-probe) cmd_dhcp_probe ;;
    dhcp-guard) cmd_dhcp_guard ;;
    dhcp-reserve) cmd_dhcp_reserve ;;
    ping) cmd_ping ;;
    internet-check) cmd_internet_check ;;
    iperf-server) cmd_iperf_server ;;
    iperf-client) cmd_iperf_client ;;
    *)
        echo "usage: net-ctl.sh available|status|monitor|leases|wifi-scan|wifi-connect|wifi-disconnect|wifi-forget|wifi-autoconnect|wifi-radio|wired-set|dhcp-reserve|dhcp-probe|dhcp-guard|ping|internet-check|iperf-server|iperf-client [options]" >&2
        exit 1 ;;
esac
