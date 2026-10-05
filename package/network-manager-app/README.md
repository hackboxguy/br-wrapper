# network-manager-app

**Network** for the display rig: what every network interface is doing, WiFi (scan, join, saved
networks, hidden networks), each wired port's mode (DHCP client, fixed address, DHCP server), and —
in a later phase — bench diagnostics (ping, internet check, iperf3). Built the way
`system-manager-app` is built; the design is `docs/network-manager-app-plan.md`.

Status: phases 1–3 of the plan (Overview, WiFi, on-screen keyboard, Wired). The Tools tab says
"Coming in a later version". There is no launcher tile yet (phase 5); this README is completed
then.

## Pieces

| File | What |
|---|---|
| `src/net-ctl.sh` | the only thing that talks to NetworkManager (`nmcli`); POSIX `sh`, line protocol `RESULT` / `PROGRESS` / `NOTICE` |
| `src/net-dhcp-probe.py` | one DHCPDISCOVER on a port, every DHCPOFFER for 4.5 s; never a REQUEST (`--self-test` checks the packet code) |
| `src/NetTool.*` | runs `net-ctl.sh` (one command at a time per controller), parses its lines, logs to `/tmp/network-manager-app.log` |
| `src/StatusController.*` | Overview and header: `status`, `monitor`, `leases`; byte counters and carrier from `/sys/class/net` once a second |
| `src/WifiController.*` | WiFi section: `wifi-scan`, `wifi-connect` (password on stdin), `wifi-forget`, `wifi-autoconnect`, `wifi-disconnect`, `wifi-radio` |
| `src/WiredController.*` | Wired section: `wired-set`, `dhcp-probe`, the lease table of a serving port |
| `src/main.qml`, `src/Keyboard.qml`, `src/NumPad.qml` | the page; the keyboard (layers abc / ABC / 123 / #+=) and the numeric pad (keys act on press) |

Reads run as the app user; changes run as `sudo -n net-ctl.sh …`. A WiFi password goes to the
script on stdin and from there to `nmcli … passwd-file /dev/stdin` — never in an argument list or
a log.

**A change outlives the app** (plan rule 6a): run as root with `systemd-run` present, a change
re-executes itself as a transient unit (`systemd-run --pipe --wait`), so a launcher restart (which
SIGKILLs the app's whole cgroup) cannot stop it between "modified" and "up or restored". It prints
`NOTICE detached` first; its NOTICE and RESULT lines also go to the journal (`journalctl -t
net-ctl.sh`), since nobody may be reading any more. `NET_CTL_*` variables are carried into the
unit. Reads never detach; without `systemd-run` (Buildroot) a change runs in place.

## net-ctl.sh

Exit codes: `0` done · `1` refused (bad arguments, update lock held, unsupported security, a
password is needed, an overlapping subnet) · `2` failed, nothing changed · `3` failed, the
previous settings were restored · `4` NetworkManager not available.

| Command | Root | Output |
|---|---|---|
| `available` | no | `RESULT kind=available ok=0\|1 [reason=…]` |
| `status` | no | `RESULT kind=iface …` per wired/WiFi device (incl. `inet=yes\|no`, the configured profile `profileuuid= cfgprofile= saved= binding=name\|mac\|any cfgip= cfgprefix= cfggateway= cfgdns=`), then `RESULT kind=summary internet= via= defaultdev= wifi= country= volatile= wifiboot=` |
| `monitor` | no | `NOTICE changed` on every NetworkManager event, until killed (or until its caller is gone) |
| `leases --iface=` | yes | `RESULT kind=lease ip= mac= host= expires=` …, `RESULT kind=leases count=` |
| `wifi-scan [--rescan]` | rescan | `RESULT kind=ap ssid= signal= security= band= saved= active=` (strongest BSSID per SSID), then `RESULT kind=saved …` per profile |
| `wifi-connect --ssid= [--hidden] [--security=open\|wpa2\|wpa3]` | yes | `PROGRESS phase=associating\|authenticating\|address`, `RESULT kind=connect ok= … [reason=bad-password\|not-found\|no-address\|timeout\|unsupported\|need-password\|radio-off\|locked]` |
| `wifi-disconnect`, `wifi-forget --ssid=`, `wifi-autoconnect --ssid= --on\|--off`, `wifi-radio --on\|--off` | yes | `RESULT kind=… ok=` |
| `wired-set --iface= --mode=client` | yes | `PROGRESS phase=activating\|checking`, `RESULT kind=wired ok= ip= binding= [reason=activation-failed\|check-failed\|overlap\|bad-arguments\|locked]` |
| `wired-set --iface= --mode=static --ip= --prefix= [--gateway=] [--dns=a,b]` | yes | as above |
| `wired-set --iface= --mode=server --ip= [--prefix=24]` | yes | as above; stops and masks the system `dnsmasq.service` and writes the no-gateway drop-in if needed |
| `dhcp-probe --iface=` | yes | `RESULT kind=offer server= offered= router=` per other server, `RESULT kind=probe servers=N carrier=0\|1` |

Values are percent-encoded (every byte outside `[A-Za-z0-9._~:/,@+-]`); `--ssid=` takes the same
encoding. Changes take `--dry-run`.

### Wired ports

`wired-set` edits the profile NetworkManager has active on the port, else the autoconnect profile
that would activate there, else a new one — the same rule as the OLED menu's script. It records
the profile's IPv4 settings, applies the new ones, brings the port up and checks the result
(client: an address; static: that address; server: the address, NetworkManager's dnsmasq running
with `--listen-address=<ip>` and the drop-in directory, the drop-in itself). If the activation or
the check fails, the old settings go back and the port comes up again: exit 3. A port with no
cable is only saved (`pending=1`).

The first `wired-set` turns NetworkManager's in-memory auto profile into a saved one (on `/data`
on the A/B image). A **USB adapter's** profile is bound to its MAC address and loses its
interface-name binding, so the adapter keeps its settings in any port and another adapter does
not inherit them; the built-in `eth0` keeps its name binding. A port the OLED menu put in its own
DHCP-server mode (`mode=legacy-server`) is taken over: the menu's stop first (stop and mask the
system dnsmasq, remove its conf), then the new mode.

Server mode is NetworkManager's shared mode with `dhcp-option=3` and `dhcp-option=6` emptied by
`/etc/NetworkManager/dnsmasq-shared.d/90-micropanel-no-gateway.conf`: clients get an address
(`.10–.254`, one hour) and no router, no DNS. The section probes a port before it starts serving,
and again whenever a serving port's cable comes in while the section is shown.

### Is there internet — and through which port?

NetworkManager's connectivity word cannot say: the image configures no connectivity check, and
then NetworkManager reports `full` for any device with a default route — also for a port whose
gateway has no way out. `status` therefore pings two addresses (`1.1.1.1`, `8.8.8.8`) once per port
that has a gateway, bound to the port (`ping -I`), in parallel, at most about a second, and keeps
the answer 20 s per port, address and gateway. `via` is the default-route port when it reaches the
internet, else the first port that does; `defaultdev` is the default route's port either way.

### The wrong-password rule, and its limit

NetworkManager does not tell a refused WiFi key apart from other failures; given a key through
`passwd-file` it stores it and, when the access point refuses it, retries the same key until the
activation's wait runs out. `wifi-connect` therefore watches `nmcli device monitor`: every
activation passes `need authentication` once (NetworkManager fetching the key, even a stored one),
so a **second** request is taken as "the network refused the key" and the attempt is stopped
(5–13 s on the rig). This is this app's rule, not a NetworkManager guarantee: a link that drops
during the handshake (weak signal) looks the same and is reported as a wrong password. The cost is
a retyped password. The access point's own log (`AP-STA-POSSIBLE-PSK-MISMATCH`) confirmed the real
case on the rig.

### SSIDs that are not UTF-8

`net-ctl.sh` passes any SSID through, percent-encoded byte by byte. The app decodes to UTF-8 for
display and encodes from that when it joins, so a network whose name is not valid UTF-8 is shown
with replacement characters and cannot be joined from the app (`nmcli` can).

## Options

| Option | Default | Purpose |
|---|---|---|
| `--net-tool <path>` | `<bindir>/net-ctl.sh` | the helper (tests: `tests/fake-net-ctl`) |
| `--dry-run` | off | never elevates; changes run with `--dry-run` |
| `--section overview\|wifi\|wired\|tools` | overview | section to open |
| `--screenshot <file>`, `--screenshot-delay <ms>`, `--window-size WxH` | | offscreen screenshots |
| `--open-sheet …` | | overlays for screenshots: `detail[:if]`, `keyboard[:layer[:shown]]`, `hidden`, `scroll-end`, `numpad[:field]`, `probe-warning`, `wired-<mode>[:if]`, `apply-<mode>[:if]` (applies at once) |
| `--auto-connect <ssid>` | | automated validation: join as if tapped (never used by the launcher) |
| `--sysfs <dir>`, `--sample-ms <ms>`, `--log-file <path>` | | test seams |

`NET_CTL_INCLUDE_VETH=1` in the app's environment makes `veth*` devices count as wired ports (the
server-mode tests run on a veth pair); the app passes `NET_CTL_*` through `sudo` to the script.

## Tests

```sh
cmake -DBUILD_TESTS=ON .. && make && ctest      # test_parser + tests/test_net_ctl.sh (fake nmcli & co.)
tests/offscreen-shots.sh <binary> <out> 1920x720 # every state, with tests/fake-net-ctl
src/net-dhcp-probe.py --self-test                # the probe's DHCP packet code
```

Desktop run with the fake tool:
`FAKE_NET_SCENARIO=online-serving ./network-manager-app --dry-run --net-tool tests/fake-net-ctl --window-size 1920x720`
