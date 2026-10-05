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
# Changes take --dry-run: say what would run, change nothing.
#
# Test seams (never set on a rig): NET_CTL_SYSFS (/sys/class/net),
# NET_CTL_LEASE_DIR, NET_CTL_LEGACY_CONF, NET_CTL_LOCK, NET_CTL_MOUNTS,
# NET_CTL_MODPROBE_DIR, NET_CTL_INCLUDE_VETH=1 (treat veth devices as wired
# ports), NET_CTL_COUNTRY (country set by wifi-radio --on, default DE).

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
CONNECT_WAIT=60

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

# emit <kind>: reads "key<TAB>value" lines, prints one RESULT line
emit() {
    awk -v kind="$1" "$AWK_ORD"'
        { p = index($0, "\t"); if (p == 0) next
          line = line " " substr($0, 1, p - 1) "=" enc(substr($0, p + 1)) }
        END { print "RESULT kind=" kind line }'
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
        printf 'RESULT kind=%s ok=0 detail=%s reason=%s\n' "$2" "$(printf '%s' "$4" | tr '\n' ' ' | pct_stdin)" "$3"
    else
        printf 'RESULT kind=%s ok=0 reason=%s\n' "$2" "$3"
    fi
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
        *OWE*) echo open ;;
        *) echo other ;;
    esac
}

# The profile's IPv4 method as the app's mode word
mode_of() { # <uuid>
    m=$(nmcli -t -f ipv4.method connection show uuid "$1" 2>/dev/null | sed 's/^ipv4\.method://')
    case $m in
        auto) echo client ;; manual) echo static ;; shared) echo server ;;
        disabled) echo off ;; '') echo off ;; *) echo "$m" ;;
    esac
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

iface_result() { # <dev> <type> <active uuid or ""> <default dev> <legacy iface>
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

    mode=off
    [ -n "$uuid" ] && mode=$(mode_of "$uuid")
    if [ -n "$5" ] && [ "$5" = "$dev" ]; then mode="legacy-server"; fi

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
        kv mac "$(field GENERAL.HWADDR)"; kv carrier "$carrier"; kv speed "$speed"
        kv state "$state"; kv profile "$(field GENERAL.CONNECTION)"; kv mode "$mode"
        kv ip "${addr%/*}"; kv prefix "$(case $addr in */*) echo "${addr#*/}" ;; esac)"
        kv gateway "$(field IP4.GATEWAY)"; kv dns "$(fields IP4.DNS | paste -s -d, -)"
        kv ssid "$ssid"; kv signal "$signal"; kv band "$band"
        kv default "$([ "$4" = "$dev" ] && echo 1 || echo 0)"
        kv driver "$driver"; kv product "$product"
        kv ip6 "$(fields IP6.ADDRESS | paste -s -d, -)"
        kv dhcpserver "$(dhcp dhcp_server_identifier)"; kv leasetime "$(dhcp dhcp_lease_time)"
        kv leaseexpiry "$(dhcp expiry)"
    } | emit iface
}

cmd_status() {
    need_nm
    defdev=$(default_route_dev)
    legacy=$(legacy_server_iface)
    active=$(nmcli -t -e yes -f DEVICE,UUID connection show --active 2>/dev/null | untab)
    list_devices | while IFS="$us" read -r dev type; do
        uuid=$(printf '%s\n' "$active" | awk -F "$us" -v d="$dev" '$1 == d { print $2; exit }')
        iface_result "$dev" "$type" "$uuid" "$defdev" "$legacy"
    done

    conn=$(nmcli -t -f CONNECTIVITY general 2>/dev/null)
    case $conn in
        full) internet=yes ;; none|limited|portal) internet=no ;; *) internet=unknown ;;
    esac
    [ -z "$defdev" ] && internet=no

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
        kv internet "$internet"; kv via "$defdev"; kv connectivity "$conn"
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

    # From here on the change runs to its end: a launcher restart kills the
    # app, not the attempt or the restore
    trap '' HUP TERM INT

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

# ---- arguments --------------------------------------------------------------

cmd=${1:-}
[ $# -gt 0 ] && shift
iface='' ssid_enc='' opt_hidden=0 opt_security='' opt_rescan=0 opt_onoff='' dry_run=0
for arg in "$@"; do
    case $arg in
        --iface=*) iface=${arg#--iface=} ;;
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
    *)
        echo "usage: net-ctl.sh available|status|monitor|leases|wifi-scan|wifi-connect|wifi-disconnect|wifi-forget|wifi-autoconnect|wifi-radio [options]" >&2
        exit 1 ;;
esac
