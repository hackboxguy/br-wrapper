# network-manager-app

**Network** for the display rig: what every network interface is doing, WiFi (scan, join, saved
networks, hidden networks), and — in later phases — each wired port's mode (DHCP client, fixed
address, DHCP server) and bench diagnostics (ping, internet check, iperf3). Built the way
`system-manager-app` is built; the design is `docs/network-manager-app-plan.md`.

Status: phases 1 and 2 of the plan (Overview, WiFi, on-screen keyboard). The Wired and Tools tabs
say "Coming in a later version". There is no launcher tile yet (phase 5); this README is a stub
until then.

## Pieces

| File | What |
|---|---|
| `src/net-ctl.sh` | the only thing that talks to NetworkManager (`nmcli`); POSIX `sh`, line protocol `RESULT` / `PROGRESS` / `NOTICE` |
| `src/NetTool.*` | runs `net-ctl.sh` (one command at a time per controller), parses its lines, logs to `/tmp/network-manager-app.log` |
| `src/StatusController.*` | Overview and header: `status`, `monitor`, `leases`; byte counters from `/sys/class/net` once a second |
| `src/WifiController.*` | WiFi section: `wifi-scan`, `wifi-connect` (password on stdin), `wifi-forget`, `wifi-autoconnect`, `wifi-disconnect`, `wifi-radio` |
| `src/main.qml`, `src/Keyboard.qml` | the page; the on-screen keyboard (layers abc / ABC / 123 / #+=, keys act on press) |

Reads run as the app user; changes run as `sudo -n net-ctl.sh …`. A WiFi password goes to the
script on stdin and from there to `nmcli … passwd-file /dev/stdin` — never in an argument list or
a log.

## net-ctl.sh

Exit codes: `0` done · `1` refused (bad arguments, update lock held, unsupported security, a
password is needed) · `2` failed, nothing changed · `3` failed, the previous connection was
restored · `4` NetworkManager not available.

| Command | Root | Output |
|---|---|---|
| `available` | no | `RESULT kind=available ok=0\|1 [reason=…]` |
| `status` | no | `RESULT kind=iface …` per wired/WiFi device, then `RESULT kind=summary internet= via= wifi= country= volatile= wifiboot=` |
| `monitor` | no | `NOTICE changed` on every NetworkManager event, until killed |
| `leases --iface=` | yes | `RESULT kind=lease ip= mac= host= expires=` …, `RESULT kind=leases count=` |
| `wifi-scan [--rescan]` | rescan | `RESULT kind=ap ssid= signal= security= band= saved= active=` (strongest BSSID per SSID), then `RESULT kind=saved …` per profile |
| `wifi-connect --ssid= [--hidden] [--security=open\|wpa2\|wpa3]` | yes | `PROGRESS phase=associating\|authenticating\|address`, `RESULT kind=connect ok= … [reason=bad-password\|not-found\|no-address\|timeout\|unsupported\|need-password\|radio-off\|locked]` |
| `wifi-disconnect`, `wifi-forget --ssid=`, `wifi-autoconnect --ssid= --on\|--off`, `wifi-radio --on\|--off` | yes | `RESULT kind=… ok=` |

Values are percent-encoded (every byte outside `[A-Za-z0-9._~:/,@+-]`); `--ssid=` takes the same
encoding. Changes take `--dry-run`.

## Options

| Option | Default | Purpose |
|---|---|---|
| `--net-tool <path>` | `<bindir>/net-ctl.sh` | the helper (tests: `tests/fake-net-ctl`) |
| `--dry-run` | off | never elevates; changes run with `--dry-run` |
| `--section overview\|wifi\|wired\|tools` | overview | section to open |
| `--screenshot <file>`, `--screenshot-delay <ms>`, `--window-size WxH` | | offscreen screenshots |
| `--open-sheet detail[:if]\|keyboard[:layer[:shown]]\|hidden\|scroll-end` | | overlays for screenshots |
| `--auto-connect <ssid>` | | automated validation: join as if tapped (never used by the launcher) |
| `--sysfs <dir>`, `--sample-ms <ms>`, `--log-file <path>` | | test seams |

## Tests

```sh
cmake -DBUILD_TESTS=ON .. && make && ctest      # test_parser + tests/test_net_ctl.sh (fake nmcli)
tests/offscreen-shots.sh <binary> <out> 1920x720 # every state, with tests/fake-net-ctl
```

Desktop run with the fake tool:
`FAKE_NET_SCENARIO=online-serving ./network-manager-app --dry-run --net-tool tests/fake-net-ctl --window-size 1920x720`
