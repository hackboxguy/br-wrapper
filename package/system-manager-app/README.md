# system-manager-app

System Manager for the display rig, started from qt-demo-launcher's **System Manager** button.
Its first section is **Firmware**: it shows the firmware each board runs against the images this
system ships, and installs them over I²C. Later sections (Pi image update from USB, FPGA update,
system status) are meant to live in the same app.

## Firmware section

| Board | Where | Notes |
|---|---|---|
| 983HH serializer board | RH850 F1KM-S1, I²C 0x67 | Always present |
| Display controller | RH850 F1KM-S1/S4 in the display, I²C 0x66 | Variant (OTS, remote display, Spartan-7/S4) picked by board code; "Not fitted" on displays without one |

The app never opens the I²C bus itself. Everything goes through `update-iocs.sh`
(space6-architecture, installed beside `disptool`):

- **Check** on start and on "Check again": `update-iocs.sh --check --image-dir <dir>` — read-only.
- **Update**, after a 1.5 s hold on the button: `update-iocs.sh --image-dir <dir> --keep-stream`.
  The script picks each board's `_ota`/`_otaB` pair by board code, quiesces the drivers and
  services that share the bus, updates the display controller first and the 983HH second, and
  leaves the video stream up so this screen stays visible where the hardware allows it.

Per-board status comes from the script's `RESULT` lines; the outcome from its exit code:

| Exit | Shown as | Offered next |
|---|---|---|
| 0 | Firmware updated — **power cycle required** | Back to home |
| 2 | The update did not finish (unit still usable) | Check again and retry |
| 3 | The update must be repeated now | Check again and retry |
| 4 | The new firmware was rolled back | Contact service |
| 5 | Update refused — this image was rolled back before | Never retried with the same image |
| 6 | A board stopped answering | Power cycle, then check |

After a successful update the app writes `Power cycle required` to `/tmp/micropanel-notice`,
which the launcher shows as an amber header chip until the next boot. The update cannot be left
from the UI while it runs, and the app ignores SIGTERM/SIGINT during it. Each run's output is
logged under `<prefix>/usr/logs/system-update/`.

## Launcher badge

`system-update-check.sh` prints `Update available` when a board carries firmware other than the
shipped image (same read-only check). The launcher runs it as the button's `badge_command`
about 20 s after start and whenever an app exits, and shows the line as a badge on the tile. It
skips while `/tmp/system-update.lock` exists (an update is running).

## Options

| Option | Default | Purpose |
|---|---|---|
| `--update-tool <path>` | `<bindir>/update-iocs.sh` | The update script |
| `--image-dir <dir>` | `<prefix>/share/sp6bins/firmware/bios-bin` | Shipped firmware images |
| `--log-dir <dir>` | `<prefix>/usr/logs/system-update` | Update logs |
| `--notice-file <path>` | `/tmp/micropanel-notice` | Launcher header notice |
| `--dry-run` | off | Check images and show the flow; nothing is written |
| `--auto-update` | off | Automated validation: start the update as soon as a check finds one (no hold) |

`<bindir>` is the directory of the binary and `<prefix>` its parent, so the defaults work for
both `/home/pi/micropanel/bin` (PiOS) and `/usr/bin` (Buildroot).

## Build

Part of the br-wrapper CMake build (`add_subdirectory(package/system-manager-app)`) and a
Buildroot package (`BR2_PACKAGE_SYSTEM_MANAGER_APP`). Needs Qt 5.15 (Quick, QML); no Quick
Controls.
