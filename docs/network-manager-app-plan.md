# network-manager-app — implementation plan

A touch app for the display rig, started from a **Network** tile on qt-demo-launcher's second
screen. It shows what every network interface is doing, joins WiFi networks, sets each wired port
to DHCP client / fixed address / DHCP server, and runs ping and iperf3. It is the touch
counterpart of the OLED menu's Network branch (`micropanel/screens/config-pios-new.json`), built
the way `system-manager-app` is built.

Written 2026-10-05 at br-wrapper `c5ab396`, from an interview with the owner. Read
`docs/qt-demo-launcher-apps-handover.md` first: the visual language, the page skeleton, the
component list, the build containers and the test checklist there all apply and are not repeated
here.

**For the implementer:** work phase by phase (section 10). Each phase ends with the listed
evidence. Section 2.1 lists what was measured on a rig before this plan was finalised; the design
rests on those results. The few items still marked **VERIFY** could not be checked then — check
them when their phase comes, and if one fails, report it instead of working around it.

---

## 1. What the owner wants

The typical setup:

```
            internet
               |
        router / phone hotspot
               |  WiFi (DHCP client)                 fallback uplink when there is no WiFi:
          +---------+                                USB-Ethernet adapter as DHCP client
          |  Pi 4   |---- USB-Ethernet adapter ----> (or any of the three modes)
          +---------+
               |  eth0: DHCP server, private subnet, no routing
        other Pi 4 rigs / a laptop
```

Decisions from the interview:

| Topic | Decision |
|---|---|
| Purpose | Get the Pi online; direct link to a laptop or other Pi 4s; bench diagnostics |
| WiFi | Client only. Open, WPA2 and WPA3 personal, plus joining a hidden network by name. No access-point mode, no WPA-Enterprise |
| Wired ports | eth0 and every USB-Ethernet adapter get the same three modes — DHCP client, fixed address, DHCP server — remembered per adapter |
| DHCP server | Addresses only. The Pi does **not** announce itself as gateway; clients keep their own routing. No internet sharing |
| DHCP server safety | Before enabling, probe the port for an existing DHCP server (~3 s). If one answers: red warning naming it, and the 1.5 s hold. If none: the normal 1.5 s hold |
| Text entry | An on-screen keyboard built into the app (the image has none), plus a numeric pad for addresses |
| Diagnostics in v1 | Live status and traffic, ping and internet check, iperf3 server and client. No internet speed test |
| Backend | One helper script, `net-ctl.sh`, wraps `nmcli`; the app never calls `nmcli` itself |
| Targets | Pi OS image (NetworkManager). The package builds everywhere; without NetworkManager the tile is dimmed and the app says so |
| Delivery | Phases with a review after each |

Not in v1: access-point mode, WPA-Enterprise, internet sharing/NAT, IPv6 configuration (IPv6
addresses are shown, not edited), VPN, speed test, Bluetooth, changes to the micropanel repo.

## 2. What exists today

- **Network stack.** The Pi image is bookworm with NetworkManager. On the A/B images
  `/etc/NetworkManager/system-connections` is bind-mounted from `/data`
  (`misc-tools/board-configs/micropanel/PERSISTENCE.md`), so a NetworkManager profile survives
  reboots and image updates. The rest of `/etc` is a RAM overlay there and is lost at reboot.
- **OLED menu** (`micropanel`): interface info and per-interface stats, ping, internet test,
  speed test, iperf3 server/client, and IP Settings for `eth0` through
  `scripts/dhcp-net-settings-pios.sh`. That script edits whichever NetworkManager profile is
  active on the interface (creating one named after the interface if there is none) and has a
  `dhcp-server` mode: a manual-address profile plus the **system** `dnsmasq` service, configured
  in `/etc/dnsmasq.d/micropanel-dhcp-server.conf` and unmasked/masked on start/stop. Its range is
  `.100–.200` and it announces the Pi as gateway. The WiFi Settings screen is a stub (its
  actions are commented out).
- **Image contents:** `dnsmasq`, `iperf3` (3.12), `python3`, `jq`, `curl`, `busybox` (with
  `udhcpc`, `nc`), `nft`, `iw`, `rfkill`. No `iptables`, no on-screen keyboard package.
- **Launcher:** screen 2 currently holds Photo Gallery, Dual Video and USB Media in its first row
  (rows past 2 spill to the next screen).

### 2.1 Measured on rig 1 (image 2.07, A/B layout, NetworkManager 1.42.4), 2026-10-05

| Question | Result |
|---|---|
| WiFi radio | Off after every boot: soft-blocked by rfkill and `WirelessEnabled=false` in NetworkManager's state file; regulatory country unset (`00`). `rfkill unblock wifi` + `nmcli radio wifi on` + `iw reg set DE` bring it up and a scan lists networks on 2.4 and 5 GHz. **None of the three survives a reboot** — they live in `/var` and the kernel, both volatile on the A/B image (rebooted and checked) |
| System `dnsmasq.service` | Enabled and running in the image, listening on port 53 on every address. With it running, a shared-mode profile **fails to activate**: NetworkManager's own dnsmasq exits with "failed to create listening socket … Address already in use" |
| Shared mode with the system dnsmasq stopped | Works. NetworkManager starts `dnsmasq --bind-interfaces --listen-address=<ip> --dhcp-range=<.10>,<.254>,60m --conf-dir=/etc/NetworkManager/dnsmasq-shared.d`; `nft` rules (masquerade + forward) are installed; a client got a lease |
| `ipv4.never-default yes` | Does **not** remove the router option on 1.42.4: the client was offered router and DNS = the Pi |
| Drop-in `/etc/NetworkManager/dnsmasq-shared.d/*.conf` with `dhcp-option=3` and `dhcp-option=6` | Works: the client was offered an address and mask only — no router, no DNS |
| Lease file | `/var/lib/NetworkManager/dnsmasq-<iface>.leases`; the directory is root-only, so reading it needs sudo |
| DHCP probe (python3, one DISCOVER, UDP 68 bound to the interface) | Works, also while NetworkManager's DHCP client is active on that interface: found the lab router on `eth0` |
| The profile on `eth0` | NetworkManager's auto-generated `Wired connection 1`, held in `/run` (memory). Modifying it writes it to `/etc/NetworkManager/system-connections`, which is on `/data` |
| Tools' location | `nft`, `iw`, `rfkill` are in `/usr/sbin`, which is not in `pi`'s non-login `PATH` — scripts set `PATH` themselves |
| `sudo -n` as `pi` | Works |

Added 2026-10-05 after round one (same rig, measured by the reviewer and the implementer):

| Question | Result |
|---|---|
| Why the radio is blocked at boot | `/etc/modprobe.d/rfkill_default.conf` (`options rfkill default_state=0`, from Pi OS) blocks every radio, and the image holds no saved rfkill state for the WiFi radio |
| The 5.4 image changes, written into the rig's read-only image root and rebooted | Work as a set: WiFi enabled and unblocked at boot (the saved rfkill state wins over `default_state=0`), country DE from the cfg80211 module option, `dnsmasq.service` masked and inactive, drop-in present, no failed unit. Rig 1's slot B carries these changes since then |
| A change run as a transient systemd unit (`sudo -n systemd-run --quiet --collect --pipe --wait <script>`) | stdin (the secret), stdout and the exit code pass through; the script runs in `system.slice`, and **finished its work after its caller was killed with SIGKILL** |
| USB-Ethernet adapter (Realtek RTL8153) | Comes up as `eth1`; NetworkManager's auto profile `Wired connection 2` is in `/run` and bound by interface name, not MAC |
| Two wired uplinks | Both get a default route, metrics 100 and 101 in activation order. NetworkManager reported `full` connectivity for a port whose gateway had no internet, so its connectivity word alone cannot be trusted for "Internet via X" |
| A second DHCP server on the LAN | With one present, `eth0` took an address from either server at boot. The rig is then not at its usual address; it stays reachable over IPv6 link-local |

The shared-mode test ran on a `veth` pair with the client (`busybox udhcpc`) in a network
namespace, so the rig's `eth0` uplink was never touched. That is also how the implementer tests
server mode (section 10).

Three consequences, which section 5.4 turns into image changes: WiFi needs to be on by default
with country DE; the system dnsmasq must not run; the no-gateway drop-in must be in the image.

## 3. Architecture

```
package/network-manager-app/
  Config.in  network-manager-app.mk  CMakeLists.txt  README.md
  src/
    main.cpp                 options, context properties, screenshot seam (as system-manager-app)
    NetTool.{h,cpp}          runs net-ctl.sh: one QProcess at a time per controller, line parser
    StatusController         interface list, roles, live counters
    WifiController           scan, connect, saved networks
    WiredController          per-port mode, DHCP probe, leases
    ToolsController          ping, internet check, iperf3
    main.qml                 page skeleton, section switch, inline components
    Keyboard.qml  NumPad.qml the text-entry sheet
    net-ctl.sh               the only thing that talks to NetworkManager
    net-dhcp-probe.py        DHCPDISCOVER on one interface, prints the offers
    net-badge.sh             launcher badge and availability check
    icons/*.svg  qml.qrc  network-manager-app.pro
  tests/
    fake-net-ctl             canned scenarios for the UI
    fake-nmcli               records the commands net-ctl.sh composes
    test_net_ctl.sh          net-ctl.sh against fake-nmcli
    test_parser.cpp          NetTool line parser (QtCore only)
    offscreen-shots.sh       every state to PNG
```

Rules, all taken from the System Manager pattern:

1. **The UI never owns the network.** Every read of NetworkManager state and every change goes
   through `net-ctl.sh`. The one exception: byte/packet counters are read straight from
   `/sys/class/net/<if>/statistics/` once a second (spawning a script every second for a number
   is waste).
2. **One controller per section**, each a context property, `MergedChannels`, ANSI stripped.
3. **Line protocol** as in the handover: `RESULT key=value … [reason=free text last]`,
   `NOTICE <text>`, `PROGRESS phase=…`; the exit code carries the outcome.
4. **Reads run as `pi`; changes (and the lease list) run as `sudo -n net-ctl.sh …`.** The script
   starts with `PATH=/usr/sbin:/sbin:$PATH`. Secrets (WiFi passwords) go to
   the script on **stdin**, never in the argument list, and are never written to a log.
5. **The app keeps no state of its own.** Everything durable is a NetworkManager profile. Logs go
   to `/tmp/network-manager-app.log` (volatile; a network change does not end in a power cut, so
   System Manager's fsync-to-`/data` logging is not needed).
6. **Changes are refused while `/tmp/system-update.lock` is held by a live pid** (an image update
   may be downloading).
6a. **A change outlives the app.** A launcher restart kills the app's whole cgroup with SIGKILL,
   which no trap survives. `net-ctl.sh`, when it runs a change as root and `systemd-run` exists,
   re-executes itself as a transient unit (`systemd-run --quiet --collect --pipe --wait`, measured
   in 2.1) and ignores SIGPIPE, so the change and its restore run to the end even with nobody
   reading. Without `systemd-run` (Buildroot) it runs in place, as before.
7. **Refresh is event-driven where possible:** a long-running `nmcli monitor` (started through
   `net-ctl.sh monitor`) triggers a status refresh on any change; a 5 s timer is the fallback.

### 3.1 `net-ctl.sh` commands

POSIX `sh`, shellcheck-clean, so the micropanel repo can call it later. Read commands print one
`RESULT` line per object.

| Command | Root | Output / effect |
|---|---|---|
| `available` | no | exit 0 when `nmcli` exists and NetworkManager is active; else one line saying why |
| `status` | no | per interface: `RESULT kind=iface name= type=wifi\|ethernet usb=0\|1 mac= carrier= speed= state= profile= mode=client\|static\|server\|legacy-server\|off ip= prefix= gateway= dns= ssid= signal= default=0\|1`; then `RESULT kind=summary internet=yes\|no\|unknown via=` |
| `monitor` | no | runs until killed; prints `NOTICE changed` when NetworkManager reports a change |
| `wifi-scan [--rescan]` | rescan: yes | `RESULT kind=ap ssid= signal= security=open\|wpa2\|wpa3\|enterprise\|other band= saved=0\|1 active=0\|1`; one line per SSID (strongest BSSID wins) |
| `wifi-connect --ssid= [--hidden]` | yes | password on stdin when needed; `PROGRESS phase=associating\|authenticating\|address`; on failure `reason=` is one of `bad-password`, `not-found`, `no-address`, `timeout`, `unsupported` |
| `wifi-forget --ssid=` | yes | deletes the saved profile |
| `wifi-autoconnect --ssid= --on\|--off` | yes | |
| `wifi-radio --on\|--off` | yes | `--on` also unblocks rfkill and sets the country when it is unset (5.4) |
| `wired-set --iface= --mode=client` | yes | |
| `wired-set --iface= --mode=static --ip= --prefix= [--gateway=] [--dns=]` | yes | |
| `wired-set --iface= --mode=server --ip= --prefix=` | yes | section 5 |
| `dhcp-probe --iface=` | yes | `RESULT kind=offer server= offered=` per answering server, `RESULT kind=probe servers=N carrier=0\|1` |
| `leases --iface=` | yes | `RESULT kind=lease ip= mac= host= expires=` |
| `ping --target= [--iface=] [--count=]` | no | `RESULT kind=reply seq= ms=` live, then `RESULT kind=ping sent= received= avg=` |
| `internet-check` | no | three steps, one line each: `RESULT kind=check step=gateway\|dns\|https ok=0\|1 ms= reason=` |
| `iperf-server --start\|--stop`, `iperf-client --host= [--secs=] [--udp]` | no | `RESULT kind=iperf interval= mbit=` live, then a summary line; `reason=port-busy` when 5201 is taken (the OLED menu may be running its own server) |

Exit codes: `0` done; `1` refused (bad arguments, lock held, unsupported security);
`2` failed, nothing changed; `3` failed and the previous settings were restored;
`4` NetworkManager not available.

`wired-set` and `wifi-connect` keep the previous settings until the new ones are up: on a failed
activation the script restores the old profile values, brings them up again, and exits `3`.

Which profile `wired-set` edits — the same rule as the OLED script, so the two agree: the profile
NetworkManager has active on the interface; if none, a new one. New profiles are bound to the
adapter's **MAC address**, not its name, so a USB adapter keeps its settings whichever port it is
in. **VERIFY** in Phase 3, with a USB adapter on the rig: how it is named (`eth1` vs `enx…`) and
that the profile NetworkManager auto-creates for it can be re-bound to the MAC this way. Round
one found the name to be `eth1` and the auto profile bound by interface name (2.1); the re-bind
is still to be tried.

## 4. Screens

Page skeleton, header, back button, SMPTE strip and tokens exactly as in the handover (2.4).
Breadcrumb `Home › Network › <Section>`. Section switch in the header, System Manager's sliding
pill: **Overview · WiFi · Wired · Tools**. Right-hand header pill: `Internet via wlan0` (ok),
`No internet` (dim — being offline is a normal state for a bench rig, not an error), or
`Checking…`.

### 4.1 Overview

One card per interface (`wlan0`, `eth0`, each USB adapter; `lo` and virtual interfaces hidden),
in a row that fits three at 1920 wide and scrolls sideways past that:

- title: a friendly name (`WiFi`, `Ethernet`, `USB adapter (ASIX)`) with the interface name small
- role pill: `Internet` (ok) · `Connected` (info) · `Serving addresses` (accent) · `Fixed address`
  · `No cable` / `Off` (dim) · `Connecting…`
- address/prefix; SSID and signal bars for WiFi; link speed for wired
- RX and TX rate, with a 60 s sparkline (repaints once a second; nothing else animates)
- a serving port shows its client count

Tapping a card opens a detail sheet: MAC, gateway, DNS, lease time and server, IPv6 addresses,
packet, error and drop counters, and a button to that port's settings in WiFi or Wired.

### 4.2 WiFi

- Left column: WiFi on/off switch; the current connection (SSID, signal, band, address) with
  **Disconnect**; saved networks with **Forget** and an auto-connect switch each.
- Right column: the scan list, strongest first, saved ones marked, a lock glyph for secured ones,
  **Scan again**, and **Hidden network…**. Enterprise networks are listed dimmed with
  "Not supported".
- Tapping a network: open or saved → connect at once; secured → the password sheet (keyboard,
  show/hide password, **Connect**).
- While connecting: the row shows the `PROGRESS` phase. Failure text by `reason`:
  wrong password (the sheet reopens with the password kept) · network no longer in range ·
  connected but got no address · timed out. The previous connection is back by then; say so.
- The WiFi switch: turning it on unblocks the radio and sets the country if needed. On the A/B
  images the radio state is volatile, so the switch position does not survive a reboot — after
  the image changes of 5.4 WiFi is on at every boot, and "off" lasts until the next one. Say so
  under the switch ("WiFi turns on again at the next start") when the root is an overlay.
- On an image without the 5.4 changes WiFi is off at boot: the section then shows one card,
  "WiFi is switched off", with the switch.

### 4.3 Wired

One wide card per wired port with a three-way mode selector and the mode's fields:

- **Automatic (DHCP client)** — no fields; shows the lease it holds.
- **Fixed address** — address, prefix (shown as `/24` with the dotted mask beside it), optional
  gateway and DNS; numeric pad.
- **DHCP server** — the Pi's address; the prefix is fixed at `/24` and the range (`.10–.254`,
  one-hour leases — NetworkManager's choice) is shown read-only.
  Defaults `192.168.50.1/24` for the first serving port, `192.168.51.1/24` for the next; a subnet
  that overlaps another interface's is refused with the reason. Below it the lease table (address,
  MAC, host name, expiry); tapping a lease offers **Ping**.

Nothing changes until **Apply** — a `HoldButton` (1.5 s), since a wrong setting here cuts the rig
off the network for whoever is using it remotely. A line above it says what will happen ("eth0
stops being a DHCP client; this rig will no longer be reachable at 192.168.1.170 on that port").

Enabling DHCP-server mode runs `dhcp-probe` first:

| Probe result | Shown | Action |
|---|---|---|
| No answer | "No other DHCP server found on this port" | hold to apply |
| A server answered | red card: "Another DHCP server (192.168.1.1) is already on this network. Serving addresses here will disrupt other devices on it." | hold to apply anyway |
| No cable | "No cable: the port could not be checked. Plug in the other end first, or apply and check when it is connected." | hold to apply |
| Port is a DHCP client holding a lease | treated as "a server answered", named from the lease, without probing | hold to apply anyway |

A port that the OLED menu put in its own DHCP-server mode shows as "DHCP server (set from the
panel menu)"; applying any mode from the app takes it over (section 5.3).

### 4.4 Tools

- **Ping**: target chips (gateway, each DHCP client from the leases, `8.8.8.8`) or a typed
  address; replies listed live, then sent/received/average.
- **Internet check**: gateway reachable → name resolves → HTTPS answers; three rows that tick or
  fail in turn, each failure with its reason.
- **iperf3 server**: start/stop; shows the command to run on the other side
  (`iperf3 -c <this address>`), one line per address this rig has; live Mbit/s while a client
  runs.
- **iperf3 client**: host (chips from the leases, or typed), 5/10/30 s, TCP/UDP; live Mbit/s
  graph and the summary.

Leaving the section or the app stops whatever is running (`iperf3` must not outlive the app).

### 4.5 Keyboard and numeric pad

A sheet that slides up from the bottom; the field being edited sits just above it and the rest of
the page dims. Designed at 1920×720 first — that is where it is tight.

- Keyboard: four rows, keys ≥ 120·s wide and ≥ 68·s high; layers `abc` / `ABC` / `123` / `#+=`
  covering every printable ASCII character (WiFi passwords use all of them); backspace with
  auto-repeat; show/hide password; **Done**. Total height about 330·s.
- Numeric pad: digits, `.`, backspace, **Next** (moves to the next field), **Done**. Address
  fields validate as typed (each octet 0–255; prefix 1–30) and Done stays disabled until valid.
- Keys act on **press**, with a pressed tint — a touch release is not guaranteed on every panel
  (handover 3.3).
- Fields are ordinary `TextInput`s, so a USB keyboard plugged into the Pi types into them too.
  Do not break that; it needs no extra work.

## 5. DHCP server

### 5.1 Mechanism

NetworkManager's own shared mode, with the gateway announcement removed:

```
ipv4.method        shared
ipv4.addresses     192.168.50.1/24
ipv4.never-default yes
ipv6.method        disabled      (on the serving profile)
```

plus one file, `/etc/NetworkManager/dnsmasq-shared.d/90-micropanel-no-gateway.conf`:

```
# network-manager-app: serve addresses only - no router, no DNS server announced
dhcp-option=3
dhcp-option=6
```

NetworkManager then starts a private `dnsmasq` for that interface. Why this and not the OLED
menu's system-dnsmasq approach: the mode is one profile, the profile is already persistent on the
A/B images, and it starts with the interface at boot. All of this was measured (2.1). Shared mode
also installs a masquerade rule and enables forwarding; with no router announced no client routes
through the Pi, so they stay unused.

Two conditions, both from 2.1: the system `dnsmasq.service` must not be running, and the drop-in
must exist. `net-ctl.sh wired-set --mode=server` makes both true before it activates the profile
(stop and mask `dnsmasq.service`; write the drop-in if missing) and refuses to report success
unless a check of the running `dnsmasq` command line and the drop-in passes. That covers the
running system. For the mode to come back **after a reboot** on an A/B image, both must already
be true in the image — section 5.4.

### 5.2 Probe

`net-dhcp-probe.py` (python3 is in the image; standard library only): binds UDP port 68 on the
interface (`SO_BINDTODEVICE`, `SO_BROADCAST`, `SO_REUSEADDR`), broadcasts one DHCPDISCOVER with a
random transaction id and the port's MAC, collects DHCPOFFERs for 3 s, prints server id and
offered address for each. It never sends a REQUEST, so no address is taken. A prototype of
exactly this found the lab router on rig 1's `eth0` while NetworkManager's DHCP client was active
there (2.1).

### 5.3 Living next to the OLED menu

Both UIs edit the same NetworkManager profiles for client and static modes, so those agree
already. For server mode:

- `status` reports `mode=legacy-server` when `/etc/dnsmasq.d/micropanel-dhcp-server.conf` names
  the interface.
- Any `wired-set` on such a port first does what the OLED script's own stop does (stop and mask
  the system dnsmasq, remove that file), then applies the new mode.
- The reverse direction is a gap in v1: the OLED menu's IP Settings will show a port that the app
  put in server mode as neither DHCP nor static. Fixing it means teaching
  `dhcp-net-settings-pios.sh` that `ipv4.method=shared` is its `dhcp-server` (better: have it call
  `net-ctl.sh`). That is a change in the micropanel repo — listed in section 11, not done here.

### 5.4 Image changes (misc-tools, `board-configs/micropanel`)

The app works within one boot without these; they make its settings hold across reboots on the
A/B images, where `/etc` and `/var` are rebuilt from the image at every start. They go into the
image build (the appliance hook or a package script there), in the image's root — not on `/data`.

| Change | Why |
|---|---|
| Declare in `runtime-deps.txt` **and** `runtime-deps-ab.txt`: `network-manager`, `wpasupplicant`, `firmware-brcm80211`, `wireless-regdb`, `iw`, `rfkill`, `nftables`, `dnsmasq-base`, `python3` | All are on the 2.07 image already, but only as part of the Pi OS base. `slim-remove.txt` records a purge that once took `iw`, `rfkill` and twenty others with it; the lists' own rule is "present via the base, declared anyway". `nftables` is what shared mode uses (there is no `iptables`); `dnsmasq-base` is NetworkManager's dnsmasq; `python3` runs the DHCP probe. `dnsmasq` and `iperf3` are declared already |
| `systemctl disable dnsmasq.service` and mask it (`/etc/systemd/system/dnsmasq.service` → `/dev/null`) | It blocks shared mode (2.1). Nothing in the image needs it running: the OLED menu's DHCP-server mode unmasks and starts it itself |
| Install `/etc/NetworkManager/dnsmasq-shared.d/90-micropanel-no-gateway.conf` (content in 5.1) | A serving port that comes up at boot must not announce a gateway |
| WiFi on by default, country **DE**: `WirelessEnabled=true` in `/var/lib/NetworkManager/NetworkManager.state`; a saved rfkill state `0` for the WiFi radio (`/var/lib/systemd/rfkill/platform-fe300000.mmcnr:wlan` on the Pi 4 — it overrides `rfkill_default.conf`); `options cfg80211 ieee80211_regdom=DE` in `/etc/modprobe.d/` (cfg80211 is a module in this kernel) | Saved WiFi networks reconnect at boot without anyone opening the app. Germany as the default country is the owner's decision (2026-10-05). `net-ctl.sh status` reports `wifiboot=on` from the regdom line, so keep that form |

This exact set was written into rig 1's image root and booted (2.1). The rfkill file name is the
Pi 4's; a Pi 5 board needs its own. `PERSISTENCE.md` gets a line that the WiFi radio state is
volatile and comes from the image.

A static test beside the existing ones (`tests/test_ab_layout_static.sh`) asserts the three.

## 6. Launcher integration

In `qt-demo-launcher-pios.json` (and `qt-demo-launcher.json` if it carries the same tiles):

```json
{ "id": "network", "enabled": true, "text": "Network", "subtitle": "WiFi, Ethernet & diagnostics",
  "icon": "/usr/share/qt-apps/icons/network.svg", "program": "/usr/bin/network-manager-app",
  "arguments": [], "working_directory": "/tmp", "accent_color": "#38BDF8",
  "available_command": "/usr/bin/net-badge.sh --available",
  "badge_command": "/usr/bin/net-badge.sh",
  "position": { "row": 4, "column": 0 } }
```

plus the size/icon/font keys the neighbouring tiles carry. Row 4 is the second row of screen 2.

- `icons/network.svg` in the launcher's icon set: 48×48, white stroke 3, round caps (handover 2.5).
- `update-config-paths.sh`: add `network-manager-app` and `net-badge.sh`.
- `net-badge.sh --available`: exit 0 when `net-ctl.sh available` does; otherwise prints
  `Needs NetworkManager` (the launcher shows it as the dimmed tile's subtitle).
- `net-badge.sh` (the badge): read-only, under a second, no sudo. It prints
  `Serving addresses` when any port is in server mode, and nothing otherwise. Being offline is not
  badged — a bench rig is often offline on purpose, and the header already has the IP chip.
- Wiring: `add_subdirectory(package/network-manager-app)` and the status line in the top-level
  `CMakeLists.txt`; `source` line in `package/Config.in`; `.mk` and `Config.in` modelled on
  system-manager-app's (same Qt selections; no Quick Controls). The `.mk` installs the binary and
  the three scripts to `/usr/bin`.

## 7. Options and test seams

| Option | Default | Purpose |
|---|---|---|
| `--net-tool <path>` | `<bindir>/net-ctl.sh` | the helper (tests: `tests/fake-net-ctl`) |
| `--dry-run` | off | never elevates, never changes anything; changes are shown as they would run |
| `--section overview\|wifi\|wired\|tools` | overview | section to open |
| `--screenshot <file>`, `--screenshot-delay <ms>`, `--window-size WxH` | | as system-manager-app, including the `screen()->grabWindow()` fallback |
| `--open-sheet keyboard\|numpad\|detail\|probe-warning` | off | screenshots of the overlays |

- `tests/fake-net-ctl` answers every command from a scenario (`FAKE_NET_SCENARIO=online-serving`,
  `offline`, `wifi-blocked`, `no-nm`, `usb-adapter`, `probe-found`, `connect-bad-password`, …) and
  paces `PROGRESS` lines with `FAKE_NET_STEP`.
- `tests/test_net_ctl.sh` runs the real `net-ctl.sh` with `tests/fake-nmcli` first in `PATH` and
  checks the exact `nmcli` commands composed for each mode, the restore-on-failure path, and that
  a password never appears in an argument list.
- `tests/test_parser.cpp` (QtCore only, `-DBUILD_TESTS=ON`, `ctest`): `RESULT` lines with spaces
  in SSIDs, `=` in values, a trailing free-text `reason=`, empty fields.
- `tests/offscreen-shots.sh <binary> <out> [WxH]`: every section in every scenario, plus the
  sheets.

SSIDs are arbitrary bytes. `net-ctl.sh` percent-encodes them in `RESULT` lines and accepts them
percent-encoded in `--ssid=`; the parser test covers a space, `=`, `%`, a quote and non-ASCII.

## 8. Risks to keep in view

- **Serving addresses on someone else's LAN** is the one way this app can hurt other people. The
  probe and the hold cover the moment of enabling; they do not cover "enabled with no cable, later
  plugged into the office switch". v1 answer: the Wired section re-probes a serving port when its
  carrier comes up while the app is open, and shows the red card if a server answers. Anything
  stronger (a NetworkManager dispatcher hook that pauses serving) needs an image change and is in
  section 11.
- **Remote users.** The rigs are driven over SSH and the launcher API as well as by touch. The
  "what will happen" line before every Apply names the address that goes away.
- **A launcher restart kills the app** (handover 3.3). `net-ctl.sh` therefore finishes or
  restores a change on its own once started: the restore logic lives in the script, not in the
  app, and the script ignores SIGHUP/SIGTERM between "modify" and "up or restored".
- **1920×720** with the keyboard up is the tightest layout in any of the apps so far.

## 9. README

`package/network-manager-app/README.md` in the style of system-manager-app's: what each section
does, the `net-ctl.sh` command and exit-code tables, the DHCP-server mechanism with the measured
findings, the options table, the desktop run with the fake tool, and one 1920×720 screenshot
under `docs/`.

## 10. Phases

Each phase: arm64 and host builds with no warnings in the new files, offscreen screenshots at
1920×720, 1920×1080 and 2880×1620 of every state touched, the rig check listed, a commit on
br-wrapper `main`. Confirm the rig with the owner before touching it (handover 5.3), and never
deploy while `get-running-app` is not `none`.

**Testing on a rig without cutting it off.** The rigs are reached over `eth0`, which gets its
address from the lab router. So:

- **Never put a real port that is plugged into the lab network into DHCP-server mode**, not even
  briefly — it would hand addresses to the owner's other devices. Server mode is tested on a
  `veth` pair with the client in a network namespace (`nmcli con add type veth ifname vethnm
  veth.peer vethpeer …`, `ip netns`, `busybox udhcpc` with a script that prints what it was
  offered). `net-ctl.sh` therefore treats a `veth` device like a wired port when
  `NET_CTL_INCLUDE_VETH=1` is set — a test seam, off by default.
- **Do not change `eth0`'s mode on the rig remotely** unless a second path (WiFi) is up and
  confirmed; a static address test on `eth0` uses the address it already has.
- Tests that need hands, a cable, a second Pi or a WiFi password are listed for the owner in the
  phase report, with the exact steps.
- Remove test profiles, namespaces and drop-ins afterwards; a reboot restores everything in
  `/etc` and `/var` but **not** profiles under `/etc/NetworkManager/system-connections` (that is
  `/data`).

**Phase 0 — remaining rig facts (short).** Most facts are in 2.1. Left to check: whether the OLED
menu's own DHCP-server mode survives an A/B reboot (expected: no — relevant to 5.3 only), and the
image's state after the 5.4 changes once an image with them exists.

**Phase 1 — helper and Overview.** `net-ctl.sh` read commands (`available`, `status`, `monitor`,
`leases`), `NetTool`, `StatusController`, the page skeleton with all four tabs (the other three
empty), Overview cards with live rates and the detail sheet, the fake tool, the parser test, the
package wiring (CMake, `.mk`, `Config.in`).
*Evidence:* screenshots of `online-serving`, `offline`, `usb-adapter`, `no-nm`; on the rig, the
cards match `ip addr` and `nmcli device`; pulling the cable updates the card within 2 s.

**Phase 2 — WiFi and the keyboard.** The `wifi-*` commands, `WifiController`, the WiFi section,
`Keyboard.qml`.
*Evidence:* screenshots incl. the keyboard on all three sizes and each failure text; on the rig:
scan, a wrong password against one of the owner's networks, and — with credentials from the
owner — join, forget, hidden network, reboot and reconnect unaided; `test_net_ctl.sh` shows the
password only on stdin.

**Phase 3 — Wired modes.** `wired-set`, `dhcp-probe`, `net-dhcp-probe.py`, `WiredController`,
the Wired section, `NumPad.qml`, the legacy-server takeover.
*Evidence:* screenshots of the three modes, both probe outcomes and the no-cable case; on the
rig: server mode on the `veth` pair — the namespaced client gets an address, no router and no
DNS, and appears in the lease table; the probe finds the lab router on `eth0`; a forced
activation failure restores the previous settings (exit 3); the system dnsmasq is stopped and
masked and the drop-in written by the script. For the owner, with a second Pi and a USB adapter:
client → static → server → client on a real port, the adapter keeps its mode after moving ports.

**Phase 4 — Tools.** `ping`, `internet-check`, `iperf-*`, `ToolsController`, the Tools section.
*Evidence:* screenshots; on the rig: ping a lease client, internet check online and with the
uplink off, iperf3 both directions against a second rig, no `iperf3` process left after leaving
the app.

**Phase 5 — launcher and documentation.** The tile, icon, `net-badge.sh`,
`update-config-paths.sh`, the README, an entry for this app in the handover's table (section 1).
*Evidence:* framebuffer capture of screen 2 with the tile; the badge with and without a serving
port; the tile dimmed with `Needs NetworkManager` when `nmcli` is hidden from `PATH`; open and
close through the launcher API.

**Phase 6 — image changes.** The package declarations and the three changes of 5.4 in misc-tools
`board-configs/micropanel`, with the static test. Committed in misc-tools, **not pushed** — the owner builds the image
(`build-image.sh --board=micropanel --base-profile=qt-bookworm --layout=ab …`) and decides.
*Evidence:* the diff; the static tests pass; the same three changes applied by hand on the rig
show, after a reboot, WiFi on with country DE, `dnsmasq.service` inactive, the drop-in present.

## 11. Follow-ups outside this plan

Each needs the owner's go-ahead; none belongs in a br-wrapper commit.

- **micropanel:** make `dhcp-net-settings-pios.sh` call `net-ctl.sh` (or at least read
  `ipv4.method=shared` as its `dhcp-server`), so the OLED menu and the app show the same mode and
  the OLED menu's server mode starts surviving A/B reboots. *Done in round 4:* a new
  `dhcp-net-settings-netctl.sh` adapter, chosen by `dhcp-net-settings.sh` on Pi OS when
  `net-ctl.sh` is found and NetworkManager runs.
- **Stronger DHCP guard:** a NetworkManager dispatcher script that probes when a serving port's
  link comes up and stops serving if another server answers. *Done in round 4* (README, "The
  DHCP guard"); misc-tools' network hook installs it.
- **Later features:** access-point mode ("join the rig from a phone"), an "internet sharing"
  switch on a serving port, a QR code on the Overview with the rig's SSH/API address, the
  launcher header's IP chip showing which interface it is.

## 12. As built (2026-10-05)

Where the implementation differs from this plan, one line each; the package README describes what was built.

- **Internet word:** NetworkManager's connectivity state cannot be used (the image configures no check, so any
  default route reads `full`); `status` pings `1.1.1.1`/`8.8.8.8` per port with a gateway, cached 20 s.
- **Changes detach:** as root with `systemd-run`, every change re-runs itself as a transient unit, so a launcher
  restart cannot interrupt it; its lines also go to the journal (`-t net-ctl.sh`). Not in the plan's section 3.
- **Wrong WiFi password:** detected as a second `need authentication` in `nmcli device monitor` (NetworkManager
  gives no reason code); a weak link can look the same.
- **OWE** ("enhanced open") is listed as `other` and not joined; the plan treated it as open.
- **Port order:** built-in port, WiFi, USB adapters by name, veth last — not NetworkManager's order.
- **Probe window** 4.5 s, not 3 s (a dnsmasq's ping check delayed an offer to 3.1 s).
- **Server leases** of the OLED menu's server are read from `/var/lib/misc/dnsmasq.leases`; its own range
  (`.100–.200`, 12 h) is not shown on the card.
- **Internet check:** the DNS step asks the port's own DNS server with a small `python3` query (no `dig`/`nslookup`
  on the image); success is HTTP 204 from `generate_204` exactly, so a captive portal fails the check.
- **Tools targets** include `1.1.1.1` beside `8.8.8.8`, and ping can be sent from one chosen port.
- **iperf3 client UDP** runs at 100 Mbit/s (iperf3's default 1 Mbit/s measures nothing); the plan named no rate.
- **iperf3 3.12** has no `--json-stream`: its text lines are parsed (`--forceflush`); output was captured on the
  rig for the tests.
- **Buildroot:** no tile in `qt-demo-launcher.json` (no NetworkManager there).
- **Image:** besides section 5.4, misc-tools declares `curl`, `ca-certificates` and `iproute2` for the Tools.
- **Icon** stroke 2.6 as the neighbouring launcher icons, not 3.
- **Round 4, DHCP guard:** a `pre-up` hook cannot keep NetworkManager's dnsmasq from starting
  (it runs before any dispatcher event); an nftables gate on the port's DHCP replies closes
  ~0.1 s later, and the probe runs in its own systemd unit. A stopped port is taken down, not
  reconfigured.
- **Round 4, `--iface=`:** every command checks it against `list_devices`.
- **Round 4, launcher:** the header's notice chip was the first chip dropped when space ran out
  (never shown at 1920 wide); now the others give way.
