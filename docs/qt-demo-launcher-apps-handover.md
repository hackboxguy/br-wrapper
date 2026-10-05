# qt-demo-launcher and its apps — handover

For whoever (person or Claude session) develops a new app for the display rig's launcher, or maintains the
existing ones. It records the visual language, the integration contract with the launcher, the conventions the
apps follow, how they are built, deployed and tested, and what went wrong on the way so it does not go wrong
again. Written 2026-09-29, at br-wrapper `656f36c`.

The package READMEs are the reference for each app; this document is the part that is *not* in any one of them.

---

## 1. What is where

| Package (`br-wrapper/package/…`) | What | Tech | Source of truth for |
|---|---|---|---|
| `qt-demo-launcher` | the home screen: header, tile grid, pages/sub-pages, badges, TCP API | Qt Widgets, custom-painted (`TileButton`, `LauncherHeader`, `BackdropWidget`, `PagerBar`, `SlideOverlay`), no moc for those | the look (tokens, header), the button JSON, the API |
| `system-manager-app` | Firmware (RH850 over I²C), System image (SD-card A/B from USB), Display FPGA (A/B over I²C) | QML (Qt 5.15 inline components) + one C++ controller per section | the app pattern: controllers, scripts, RESULT/PROGRESS, locks, logs, hold-to-confirm, dry-run seams, offscreen screenshots |
| `disp-settings` | brightness, temperatures + VBATT, panel features (FPGA), firmware versions, touch info | QML + Quick Controls 2 + C++ controllers (I²C) | the card layout that adapts to 1920×720 / 1920×1080 |
| `disp-tester` | pattern generator (incl. `edge-ruler`), calibration flows | QML | test patterns |
| `touch-gallery` | photo / report viewer: one-finger pan, double-tap zoom, momentum | QML | touch handling |
| `network-manager-app` | the Network tile: interfaces, WiFi, wired port modes (client / fixed / DHCP server), ping, internet check, iperf3 | QML (Qt 5.15 inline components, no Quick Controls) + one C++ controller per section; `net-ctl.sh` (POSIX sh) the only thing that talks to NetworkManager | the on-screen keyboard and numeric pad; a change that must outlive the app (`systemd-run`); per-port internet checks |

Scripts the apps drive live in **space6-architecture** `code/disptool/tools/` (`update-iocs.sh`, `update-fpga.sh`,
installed beside `disptool` in `/home/pi/micropanel/bin` by that repo's CMake). Firmware and FPGA images and the
FPGA procedure (`docs/fpga-ab-update-procedure.md`) are in **sp6bins**. The SD-card A/B engine (`ab-update`) is
`misc-tools/packages/pi-ab-update`. The Pi image is built by **misc-tools** (`board-configs/micropanel`), which clones
br-wrapper `main` and space6-architecture `master` when its apps stage starts.

## 2. Visual language

The launcher's `"style": "tiles"` theme; every app copies it so the rig looks like one product.

### 2.1 Colour tokens

The base palette (`bg` … `sub`, `dim`) sits on RGB565 steps (R and B multiples of 8, G of 4), so the 16 bpp
linuxfb shows large areas of it without dithering; keep new background/card colours on that grid. The status and
accent colours (`ok` … `accent`) are Tailwind-400 values and not on the grid — fine for text, pills and stripes.

| Token | Value | Use |
|---|---|---|
| `bg` | `#080C18` | window background |
| `grid` | `#101828` | 1 px backdrop grid, 48 px pitch, centred |
| `card` | `#182030` | cards, round buttons |
| `tile` | `#1C2638` | tiles inside a card (disp-settings) |
| `cardPressed` | `#202C40` | pressed state |
| `border` | `#283450` | card and chip borders |
| `text` | `#F0F4F8` | primary text |
| `sub` | `#8894A8` | secondary text, labels |
| `dim` | `#58647A` | N/A values, disabled |
| `ok` | `#34D399` | good / up to date |
| `warn` | `#FBBF24` | attention, update available, "power cycle" |
| `bad` | `#F87171` | error, refused |
| `info` | `#38BDF8` | in progress, neutral info |
| `accent` | `#60A5FA` | primary action, selection |

Card accents (the 6 px left stripe and icon badge) come from a Tailwind-400 palette: `#38BDF8 #22D3EE #FB7185
#F59E0B #FB923C #FACC15 #60A5FA #34D399 #A78BFA #F472B6`. Tinted backgrounds are the token at alpha 0.14–0.22 with
the border at 0.45–0.7 (`withAlpha(c, a)` helper in every QML file).

The header strip is SMPTE 75 % bars: `#C0C0C0 #C0C000 #00C0C0 #00C000 #C000C0 #C00000 #0000C0`, 6 px high.

### 2.2 Type

Roboto when installed (the image has it); C++ passes `uiFont` as a context property
(`QFontDatabase().families().contains("Roboto") ? "Roboto" : ""`), QML uses `t.font`. Weights: Bold for titles and
big numbers, DemiBold for card titles and buttons, Medium for values, Normal for body.

### 2.3 Size and screens

Design at **1920×720** and scale with `s = Math.min(width / 1920, height / 720)`. That keeps sizes identical on
1920×1080 (s = 1, extra height) and scales up on the 17" OLED (2880×1620, s = 1.5). Every pixel size in QML is
`N * s`.

Screens on the rigs: 12.3" NQ5 1920×720, 14.6" (EJ scan-mode and direct-drive) 1920×1080, 17" OLED OTS 2880×1620.
**Always check 1920×720 first** — it is the tight one (report v2 of the image update found an overlap only there).

### 2.4 Page skeleton (every app screen)

```
margins: left/right 40·s, top 22·s, bottom 24–30·s
header   96·s high:  [round back button 76·s] 28·s [Title 36·s Bold / breadcrumb 19·s "Home › App › Section"]
                     … optional centred section switch (segmented, 60·s) … [status pills 40·s, right]
strip    12·s below the header: SMPTE bars, 6·s
content  20–26·s below the strip
```

- **Back button**: `card` circle 76·s, 1 px `border`, chevron drawn with `Canvas` (points 0.56/0.32 → 0.40/0.50 →
  0.56/0.68, line width 6 % of the diameter, round caps). The launcher's sub-pages use the same size.
- **Breadcrumb** always starts with "Home".
- **Section switch** (System Manager): pill of tabs, the selection slides (`Behavior` 220 ms OutCubic); tabs are a
  `Repeater` over a JS array, each tab reports its own x/width to the switch (`place()` on `selected`/`x`/`width`
  change). A tab can carry an amber dot for news. Disabled (opacity 0.4) while an update runs.

### 2.5 Components (copy them; inline `component` blocks in QML)

| Component | Where to copy from | Notes |
|---|---|---|
| `Pill` | system-manager, disp-settings | 40·s high, radius h/2, tint at 0.16 / border 0.45; optional dot; clickable in disp-settings |
| `Card` (title + badge + body column) | disp-settings | radius 18·s, 6·s stripe, 40·s icon badge, `default property alias content: body.data`, fade/slide-in with `order` stagger (60 + 70·order ms) |
| `ComponentCard` / `ImageCard` | system-manager | 150·s row card: stripe, 84·s badge, title 28·s, up to three lines (sub, note, amber warning), pill on the right |
| `ActionButton` | system-manager | 84·s (72·s for secondary), radius 20·s; `primary` fills with accent |
| `HoldButton` | system-manager | **1.5 s hold** fills left to right; for anything that writes firmware, installs, or restarts |
| `ResultIcon` | system-manager | 76·s circle + SVG glyph (check / update / info / warn / bad) |
| `Bullet` | system-manager | accent dot + wrapped text 20·s |
| `Spinner` | system-manager | 8 dots rotating |
| `ThemedSwitch`, `KeyValues`, `SensorTile`, `Chip`, `FeatureTile`, `VersionRow` | disp-settings | Switch with custom indicator; key/value grid (1 or 2 pairs per row); a tile that toggles on tap anywhere |

Icons: 48×48 viewBox SVG, white stroke 3, round caps/joins, fills at opacity 0.12–0.5; tinted by the badge behind
them, not by the SVG. Add each to `qml.qrc`. The Qt SVG image plugin must be present to render them.

Animations are welcome but cheap: `Behavior on x/width/color`, one-shot fade/slide on start; nothing runs
continuously except spinners.

## 3. Launcher integration

### 3.1 Button JSON (`qt-demo-launcher-pios.json`, key `launcher.buttons`)

```json
{ "id": "system-manager", "text": "System Manager", "subtitle": "Firmware updates",
  "icon": "/usr/share/qt-apps/icons/system-manager.svg", "program": "/usr/bin/system-manager-app",
  "arguments": [], "working_directory": "/tmp", "accent_color": "#60A5FA",
  "badge_command": "/usr/bin/system-update-check.sh",
  "position": { "row": 2, "column": 0 } }
```

- Grid 3×3 per screen; `position.screen` places a tile on a later screen, a `row` past the grid spills over, the
  rest auto-fill. Swipe / pager / API move between screens. Sub-pages (e.g. Calibration Tools) get the header back
  button instead of a "Back" tile.
- `update-config-paths.sh <prefix>` rewrites `/usr/...` paths for the PiOS layout (`/home/pi/micropanel/...`). Add
  any new program or badge script there.
- `badge_command` runs through `sh -c` about 20 s after start and whenever an app exits; its first stdout line is
  the tile badge ("Update available"). Keep it read-only and bounded (System Manager's takes ~6 s incl. the FPGA
  scan); skip while `/tmp/system-update.lock` is held by a live pid.
- Theme `notice_file` (`/tmp/micropanel-notice`): its first line is an amber header chip ("Power cycle required",
  "FPGA restart required") until the file goes away (tmpfs: next boot).
- Header chips, left to right: notice, `SW-VER` (IMAGE_VERSION from the image manifest), `RES`, `IP`, `API`; they
  are dropped from the left when they would hit the title.

### 3.2 Launcher TCP API (port 8081, one line per connection)

`start-app <id>`, `stop-app`, `get-running-app` (→ `none` or the id), `home`, `back`, `navigate <sub-page-id>`,
`screen <n>`, `next-screen`, `prev-screen`, `get-screen`, `get-page`, `list-apps`, `list-page-apps`,
`list-all-buttons`, `get-button-status`, `set-button-enabled`, `reload-config`. disp-tester has its own API on 8082
(allow ~1 s between connections).

### 3.3 Lifecycle facts

- The launcher runs apps as `pi`; apps call root tools with **`sudo -n`** (passwordless on these images; `-n` fails
  fast instead of hanging on an invisible prompt). Pass environment through sudo as `sudo -n VAR=value tool`.
- `systemctl restart qt-demo-launcher` **SIGKILLs the whole cgroup, including a running app** — ignoring SIGTERM does
  not protect it. Never restart the launcher, `stop-app`, or replace a running binary while
  `get-running-app` is not `none` — re-check in every step of a script, not once.
- An app quits with `Qt.quit()` (C++: `QCoreApplication::quit()`); Escape quits too. disp-settings quits on
  *press* because a touch release is not guaranteed on every panel.

## 4. App conventions (the System Manager pattern)

1. **The UI never owns hardware.** Firmware, FPGA and image updates go through scripts (`update-iocs.sh`,
   `update-fpga.sh`, `ab-update`) that own locking, quiescing and policy. The app composes and shows; it never
   re-implements a script's policy.
2. **One controller per section**, exposed as a context property (`updater`, `imageUpdate`, `fpgaUpdate`), one
   `QProcess` at a time, `MergedChannels`, ANSI escapes stripped.
3. **Line protocols**: `RESULT key=value … [reason=free text last]`, `NOTICE <text>`, `PROGRESS phase= [done= total=]
   percent=`; exit codes carry the outcome (tables in the READMEs and the FPGA procedure). Progress comes from the
   tool's lines or files, never from guessing.
4. **One I²C user at a time**: sequence probes/checks (System Manager probes the FPGA only after the firmware check
   finished); nothing touches the bus during an update. Apps that must read registers themselves (disp-settings) use
   **one `I2C_RDWR` transaction** (write address + repeated-start read), never `write()` then `read()` — als-dimmer
   polls the same MCU and a split read returns its bytes (32 of 3000 wrong on the OLED rig).
5. **Anything that writes or restarts** needs a 1.5 s hold, runs with SIGTERM/SIGINT ignored, holds
   `/tmp/system-update.lock` (content: the pid; stale locks are dropped by pid), and cannot be left from the UI.
6. **Logs** go to `<data>/logs/<section>/`, one file per run, **`flush()` + `fsync()` per line** (every update ends
   in a power cut or reboot). `<data>` = `/data/system-manager` on the A/B images (only `/data` survives a reboot;
   the image's data skeleton creates it pi-owned), else `<prefix>/usr`. Create log directories **as the app user at
   start**, before any `sudo` tool runs — otherwise the tool creates them root-owned and the app loses its log.
7. **Tell the user what the hardware will do** before they commit: time estimates, "the screen goes dark",
   "power-cycle to finish" (RH850 updates blank the OLED panel until a power cycle; FPGA activation restarts the
   display and then the system).
8. **Test seams**: `--dry-run` never elevates and never starts the real tool for anything that writes;
   `tests/fake-*` stand-ins (`fake-ab-update`, `fake-update-fpga`, `fake-systemctl`); `SYSTEM_MANAGER_DATA`,
   `AB_UPDATE_CONFIG`, `SYSTEM_IMAGE_SCAN_FAKE`; `--auto-*` options for unattended rig runs (never used by the
   launcher button).
9. **Offscreen screenshots**: `--screenshot <file> [--screenshot-delay ms] --window-size WxH` grabs the window after
   the shown section's first result. `QQuickWindow::grabWindow()` is null under `QT_QPA_PLATFORM=offscreen
   QT_QUICK_BACKEND=software`; fall back to `window->screen()->grabWindow(winId())`.
   `tests/offscreen-shots.sh <binary> <out> [WxH]` renders every state.

## 5. Build, deploy, test

### 5.1 Build (host is Arch; targets are Pi 4 bookworm arm64)

Local docker images (not in any registry — recreate if missing):

```
qtbuild:bookworm-arm64        debian:bookworm arm64 + qtbase5-dev qtdeclarative5-dev qmake (build base)
qtbuild:bookworm-arm64-qqc2   FROM the above + qtquickcontrols2-5-dev          (disp-settings)
qtbuild:bookworm-arm64-shots  FROM the above + qml-module-qtquick2 qml-module-qtquick-window2
                              fonts-roboto fontconfig                           (offscreen screenshots)
```

- system-manager-app: `qmake system-manager-app.pro && make` in `src/`.
- disp-settings: its `.pro` (now with `concurrent`) in the qqc2 image.
- qt-demo-launcher: **CMake only** (its `.pro` lacks the `NetworkInterface` moc). `cmake` is not in the base image:
  `apt-get install -y cmake` in the same `docker run` — a fresh container forgets it, and `make` then fails
  silently behind a grep (a stale binary got deployed once that way; check `strings binary | grep <new text>`).
- Host builds for quick offscreen runs: `qmake-qt5` (Qt 5.15). The host lacks the Qt SVG plugin, so use the
  `-shots` container for screenshots.
- Scripts must be committed **executable** (`git ls-files -s` → `100755`); CMake/Buildroot install 0755 anyway,
  but a source-tree run does not.

### 5.2 Deploy to a rig by hand

- PiOS layout: binaries `/home/pi/micropanel/bin`, data/config `/home/pi/micropanel/share/qt-apps`, launcher JSON
  `…/share/qt-apps/qt-demo-launcher.json`, service `qt-demo-launcher`.
- **A/B images (2.x): `/` is an overlay on RAM** — anything copied into `/home/pi/micropanel` is gone at the next
  reboot (including the launcher JSON). Persistent test files belong in `/data/<dir>` (e.g. `/data/fpga-test`),
  created with `sudo install -d -o pi -g pi`.
- Replacing a running binary: `systemctl stop` → copy → `start` (a busy executable cannot be overwritten), and only
  when no app runs.
- Screenshot: `cat /dev/fb0` → PIL `Image.frombytes('RGB', (W, H), data, 'raw', 'BGR;16')`.
- The A/B images regenerate SSH host keys at every boot (being fixed elsewhere): use a throwaway known-hosts
  (`-o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no`) for lab rigs. Never write rig passwords into a
  repository.

### 5.3 Rigs (2026-09-29)

| Rig | Pi | Display | Tasmota (powers Pi + 983 + display) |
|---|---|---|---|
| 1 | 192.168.1.170 | 14.6" FHD EJ scan-mode (Spartan-7, FPGA identity `01 00 01 46`) | 192.168.1.232 |
| 2 | 192.168.1.144 | 12.3" NQ5 1920×720 (Spartan-7, `00 01 01 23`) | 192.168.1.186 |
| 3 | 192.168.1.167 | 17" OLED OTS 2880×1620 (no FPGA, VBATT on the display IOC) | — |

Power cycle: `curl "http://<tasmota>/cm?cmnd=Power%20Off"`, wait 5 s, `…Power%20On`; SSH is back after ~25 s.
Rig assignments change; confirm with the owner before touching one, and never power-cycle during someone else's
flash.

### 5.4 Test checklist for a UI change

1. Build arm64 and host, zero warnings in your files.
2. Offscreen at **1920×720**, then 1920×1080 and 2880×1620 (`tests/offscreen-shots.sh`), every state you touched.
3. On a rig: `get-running-app` is `none` → deploy → open via the API → framebuffer capture → close.
4. Touch behaviour needs the owner's hands on the panel; say what to try.
5. Commit and push to br-wrapper `main` (the owner's rule for this repo); update the package README.

## 6. Things that went wrong once (so they don't again)

- A split I²C `write()`/`read()` while another process polled the same MCU returned foreign bytes (−0.1 °C on the
  OLED rig). Use `I2C_RDWR`.
- `ota-check` reported one false "differs" in 18 scans → a false "update available". `update-fpga.sh` now confirms
  with a second scan; apps should treat a single scan as a hint, not truth, anywhere a false positive shows up on
  the home screen.
- A 983HH restart resets the 983's I²C passthrough and interrupt setup (`0x07`, `0x51`, `0xC6`): touch and the
  display IOC (0x66) are unreachable until a power cycle; a "Check again" after an update then shows the display
  controller as absent.
- The FPGA display-link wedge (class W, ~1 in 38 flashes): exit 6 → power cycle → rerun resumes. The GOLDEN keeps
  the display alive meanwhile.
- The first `sudo` tool run created a shared log directory root-owned; the app's own log silently failed.
- A dry run wrote a `last-install` record that a later real fallback message would have named. Dry runs write no
  state that real runs read.
- 1920×720 is where layouts overflow (offer bullets under the hold button); a warning line moved to the card.
- `QT_LOGGING_TO_CONSOLE`/journald: offscreen runs printed nothing until `QT_ASSUME_STDERR_HAS_CONSOLE=1`.
- Inline QML components can use the root's `t`, `s`, `withAlpha`; properties of a *parent item* are not in scope
  unqualified — qualify them (`sectionSwitch.selX`).
- `state` is an existing `Item` property: don't name your own property `state`.
- `nmcli -t` escapes `:` in list output (`connection show`, `device wifi list`) but **not** in `device show` or
  `connection show <id>` values: a MAC address read from `device show` was split at its colons. Split such lines on
  the first `:` only.
- `IFS` with TAB (or any whitespace) folds runs of separators: empty tab-separated fields vanish and the next field
  shifts left. `net-ctl.sh` separates with `\037` (unit separator), which `IFS` does not fold.
- A running system `dnsmasq.service` holds port 53 and NetworkManager's shared mode then fails to start its own
  dnsmasq. Stop and mask it before serving (the micropanel image masks it at build time).
- A second DHCP server on the same wire moves addresses: the rig's own lease or a bench PC's can come from the
  wrong server. Probe (one DISCOVER, listen ~4.5 s — a dnsmasq took 3.1 s with its ping check) before serving.
- NetworkManager lists a veth as `ethernet`; tell virtual devices apart by `/sys/class/net/<dev>` pointing into
  `devices/virtual`.
- The launcher's `stop-app` and a launcher restart kill the app's whole cgroup with SIGKILL: anything the app must
  finish (a network change) has to run outside it (`systemd-run --pipe --wait`), and helper processes must notice
  their caller is gone by themselves (poll `kill -0 $PPID`), since no signal reaches them.

## 7. Open threads (as of this handover)

- The Pi image must ship space6-architecture `master` ≥ `8e78cb6` (disptool with `ota-check`, `update-fpga.sh`
  with `--probe`/`PROGRESS`) for the Display FPGA section to appear; misc-tools' micropanel board clones `master`.
- FPGA activation is untested on the 14.6" direct-drive (no rig); the app falls back to "power-cycle" there.
- Lattice-25/45 FPGA boards: extend `update-fpga.sh` (identity → image); the app keys off the RESULT `display=`.
- disp-settings' FPGA reader still uses split I²C reads (its slave was not checked for repeated start).
- A system-status section in System Manager (CPU, memory, temperatures) was requested for later.
- Reports with the full history: `tmp-docs/sdcard-a-b-image-update-report-v{1,2}.md` (outside the repos) and the
  package READMEs.
