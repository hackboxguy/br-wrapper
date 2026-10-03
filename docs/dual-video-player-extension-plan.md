# dual-video-player extension: USB Media playlist player — plan

Status: **planned, not started** (2026-10-03). Written so a fresh session can implement it phase by
phase. Read [qt-demo-launcher-apps-handover.md](qt-demo-launcher-apps-handover.md) first (visual
language, launcher contract, app conventions, build/deploy/test), then this.

## Goal

A new launcher button **"USB Media"** (home screen, page 2, next to "Dual Video") opens a new app,
`package/usb-media-app`, where the user picks media files on the USB stick, orders them, and plays
them as a playlist — **the same content on both HDMI outputs** — optionally looping forever and
optionally starting automatically at boot. Playback is done by **extending the existing
`dual-video-player`** (GStreamer decode + DRM presenter), not by Kodi or mpv.

## Decisions (agreed 2026-10-03)

| Topic | Decision |
|---|---|
| Engine | Extend `dual-video-player` (`package/qt-demo-launcher/src/dual-video-player.c`) |
| Audio | Not required (no audio pipeline) |
| Outputs | Mirror: the same item on both HDMIs. Per-display content stays the job of "Dual Video" |
| Mixed playlists | Yes: images and videos in one list, played in list order |
| Image duration | One value for the whole playlist (seconds) |
| Playlist location | On the USB stick (portable: prepare once, plug into any unit) |
| Autostart switch | Device setting (persistent under `/data`) |
| Per-item durations | Not now |
| Transitions, video thumbnails | Nice to have (phase 3) |
| Media files | **Played as stored on the stick — no conversion step.** The player must cope with real-world files (see "Playing files as stored") |
| Player location | Moves to its own package **`package/micropanel-media-player`** (used by Dual Video and USB Media) |

## Defaults (accepted 2026-10-03)

- **Playlist file**: `micropanel-playlist.json` in the stick root, paths **relative** to the stick root.
- **Scan**: stick root + subfolders, max depth 3, skip hidden/system dirs (`.Trashes`, `System Volume
  Information`, `$RECYCLE.BIN`). Videos: `mp4 mkv mov m4v`. Images: `jpg jpeg png` (`jpegdec`/`pngdec`
  are in the image: gstreamer1.0-plugins-good).
- **Image duration**: default 10 s, range 2–600 s.
- **Unsupported/heavy files** (probed in the app): anything but **H.264 up to 1920x1080** — the only
  hardware-decoded format here. H.264 above 1080p exceeds the decoder (software decode stutters; the
  original 3840x1440 clips of 2026-10-03 were such a case). **HEVC is not hardware-decodable through
  GStreamer on this image**: the kernel has `rpi-hevc-dec` (`/dev/video19`, stateless V4L2), but
  GStreamer 1.22 on Pi OS ships no element for it (`v4l2slh265dec`/`v4l2h265dec` missing, checked
  2026-10-03). Shown greyed with the reason; cannot be checked. Images: any size (downscaled at decode).
- **Refresh rate**: one mode per playlist, not per item (a mode switch blanks HDMI for ~1 s): the
  integer multiple of the **first video's** frame rate, if the display offers one (existing
  `set_refresh` logic). Image-only playlists keep the current mode.
- **Stick removed during playback**: stop and return to the launcher.
- **Write-protected / read-only stick**: Save shows an error ("Stick is read-only"); Play still works
  (the list is passed to the player directly).
- **No Loop**: return to the launcher after the last item.
- **Autostart countdown**: 5 s, full-screen "Starting playlist… tap to cancel".

## Playing files as stored (no conversion)

Users copy whatever their camera, phone or PC produced. The player must therefore handle, without
re-encoding:

- **H.264 with an incomplete VUI colour description** (seen 2026-10-03: the stream carried the BT.709
  matrix but "unspecified" primaries/transfer → caps `colorimetry=0:3:0:0`, which `v4l2h264dec`
  rejects with `not-negotiated`). Normalise the caps between parser and decoder instead of fixing
  the file.
- **H.264 above 1080p** and **HEVC**: no hardware decode path on this image (see Defaults). Options,
  to be decided by measurement: software decode (`avdec_h264`/`avdec_h265` from gst-libav, CPU-bound,
  will drop frames for large/high-fps content), or mark the file unsupported in the app.
- **Containers**: MP4/MOV (`qtdemux`), MKV (`matroskademux`); `decodebin`/`parsebin` can pick the
  demuxer.
- **Images** of any size and orientation (phone photos: 12–50 MP JPEGs with EXIF orientation).

Experiments and results on the bench go into "Bench findings" below.

## Architecture

```
qt-demo-launcher ── "USB Media" button ──> usb-media-app (Qt Quick)
      │  available_command: usb-media.sh --check (dims the tile without a stick)
      │                               │ Play: writes/updates micropanel-playlist.json on the stick,
      │                               │ then asks the launcher (TCP 8081) to start "usb-media-play"
      │ <── start-app usb-media-play ─┘
      └──> usb-media.sh --play  ──exec──>  dual-video-player --playlist <file> --mirror
boot: usb-media-autostart (systemd oneshot, after qt-demo-launcher) ──> countdown ──> start-app usb-media-play
```

### Player (`dual-video-player`, extended in place)

Keep one binary; add a playlist mode next to the existing two-file mode.

- **CLI**: `dual-video-player --playlist <micropanel-playlist.json> --mirror` (existing
  `VIDEO1 VIDEO2` mode unchanged). `--loop`/`--image-duration` come from the playlist file; CLI
  overrides allowed for testing.
- **Mirror presenter**: one decoded frame, shown on *both* outputs in the same atomic commit (each
  output's plane scaled/centred for its own mode — the HVS scales, still zero copy). Reuse the existing
  presenter thread, pacing (master display = refresh matching the frame rate), black primaries, EXIT
  popup, input grab, page-flip statistics.
- **Per item**:
  - *Video*: `filesrc ! qtdemux|matroskademux ! h264parse ! v4l2h264dec ! appsink` (dmabuf, zero copy),
    as today. HEVC: unsupported for now (see above); revisit if a newer GStreamer with
    `v4l2slh265dec` for `rpi-hevc-dec` lands in the base image.
  - *Image*: decode once (`filesrc ! decodebin ! videoconvert ! videoscale` to fit the larger display,
    respect **EXIF orientation** — phone photos), copy into a DRM dumb buffer (XRGB8888), show it for
    `image_duration` (counted in vblanks of the master display). `videoflip video-direction=auto`
    applies the EXIF orientation tag; `imagefreeze` is available if a pipeline-based still is simpler.
- **Gapless**: preroll the **next** item while the current one plays (a second pipeline in PAUSED, or
  the next image already decoded), so item changes do not flash black.
- **Errors**: an item that fails to open/decode is skipped with a log line; if every item fails, exit 1.
- **Loop**: restart at item 0 (videos use the existing segment-seek idea only within one file; the
  playlist loop is a new pipeline per item).
- **Stats**: per item, log frames held (existing page-flip stats) — the way to prove smoothness.

### App (`package/usb-media-app`, new)

Follow touch-gallery / System Manager structure: Qt Quick (`main.qml` + C++ controllers), CMake
target added to the top-level `CMakeLists.txt` under the Qt apps, launcher-style visual language.

- `UsbMediaController` (C++): find/mount the stick (reuse the logic of `kodi-usb-common.sh`: removable
  `/sys/block/sd*`, existing mount, else `udisksctl mount`, else `sudo -n mount`), scan files, probe
  videos (GstDiscoverer or `gst-discoverer-1.0` output: codec, width, height, fps, duration), load/save
  the playlist JSON, read/write the autostart setting, start playback via the launcher TCP API.
- UI (touch, one screen): header + file list (checkbox, type icon, name, folder, duration/resolution
  or image size, greyed + reason when unsupported), **Select all / None**, **Up / Down** to reorder the
  checked items, **Image duration** (stepper), **Loop**, **Autostart on boot**, **Save**, **Play**,
  Back. Image thumbnails in phase 3.
- The app exits when it hands off to the player (launcher contract: one running app).

### Launcher integration

- New button in `qt-demo-launcher-pios.json`, page 2 next to `dual-video` (row 3, column 2):
  `id: usb-media`, program = the app, `available_command: .../usb-media.sh --check` (dims the tile
  without a stick — the `available_command` mechanism exists since `383b436`).
- A **hidden** launcher entry `usb-media-play` (`enabled: false` keeps it off the grid but `start-app`
  can still find it — verify; otherwise give the TCP API a "start by program path" or use a dedicated
  invisible id) whose program is `usb-media.sh --play`, so playback runs as the launcher's tracked app
  (stop-app works, badges/availability are not run during playback).
- `update-config-paths.sh`: add sed lines for `usb-media.sh` and the app binary (each program path
  is rewritten individually — a missed one leaves the button pointing at `/usr/...`).

### Autostart

- Setting: `/data/usb-media/settings.json` (`{"autostart": true}`); `/data` survives reboots and A/B
  updates. (Check how other apps persist under `/data` — see misc-tools `PERSISTENCE.md` /
  `board-configs/micropanel/packages/micropanel-data-skeleton.sh`; a new `/data` subdirectory may need
  the data skeleton hook.)
- `usb-media-autostart.service` (oneshot, `After=qt-demo-launcher.service`): if autostart is on, wait
  up to 15 s for a stick with a valid playlist, show the countdown (the player itself can render it:
  `--countdown 5`, tap = cancel → exit 0 without playing), then `start-app usb-media-play` via TCP.
  No stick / no playlist / cancelled → nothing happens, the launcher stays.
- Never auto-play without a way out: the countdown and the in-playback tap → EXIT both stay.

## Interfaces

**Playlist file** (`micropanel-playlist.json`, stick root):

```json
{
  "version": 1,
  "image_duration_s": 10,
  "loop": true,
  "items": [
    "Videos/intro.mp4",
    "Pictures/slide-01.jpg",
    "Pictures/slide-02.png",
    "Videos/demo.mkv"
  ]
}
```

Paths relative to the stick root, `/` separators, order = play order. Unknown keys are ignored
(forward compatibility). Missing items are skipped at play time.

**Player exit codes**: 0 = finished (no loop) or stopped by the user/SIGTERM, 1 = nothing playable /
setup error, 2 = usage.

**`usb-media.sh`**: `--check` (exit 0 if a stick is mounted, else one line of reason, like
`dual-video.sh --check`), `--play` (find the playlist on the stick, exec the player), `--autostart`
(the boot flow above).

## Phases

### Phase 1 — playlist playback + app + button
1. Player: `--playlist` + `--mirror`, video and image items, gapless prefetch, loop, skip bad items,
   one refresh mode per playlist, EXIT popup, stick-removal stop.
2. `usb-media.sh` (`--check`, `--play`).
3. `usb-media-app`: scan, probe, list with checkboxes, select all/none, reorder, image duration, loop,
   save to stick, play.
4. Launcher button + hidden play entry + path rewrites; CMake/install; README.
5. Board config: no new packages expected — GStreamer base/good/bad are already pinned in misc-tools
   `runtime-deps*.txt`, and `jpegdec`, `pngdec`, `imagefreeze`, `videoflip`, `decodebin` are in them
   (checked 2026-10-03). The app's video probe needs `gst-discoverer-1.0` (gstreamer1.0-plugins-base-apps)
   or the GstPbutils API (libgstreamer-plugins-base1.0, already pinned) — prefer the API.

**Done when**: a mixed list (≥2 videos, ≥3 images) plays mirrored on both HDMIs with no black gaps,
loops for ≥10 minutes, page-flip stats show regular holds on the master display (as for Dual Video:
every 25 fps frame held exactly 2 vblanks at 50 Hz), EXIT returns to the launcher on both displays,
pulling the stick stops playback cleanly, and the USB Media tile is dimmed without a stick.

### Phase 2 — autostart on boot
1. Settings file + app checkbox.
2. `usb-media-autostart.service` + countdown/cancel.
3. Board config: install/enable the unit (br-wrapper CMake install + misc-tools hook post-command,
   like `qt-demo-launcher.service`).

**Done when**: with autostart on and the prepared stick in, a cold boot ends in mirrored playback
after the countdown; a tap during the countdown cancels; without the stick the launcher stays;
toggling autostart off in the app stops it on the next boot.

### Phase 3 — nice to have
Image thumbnails (cached on the device, not the stick), video thumbnails (first keyframe via a
`thumbnailer` pipeline), crossfade transitions between items (two planes, alpha ramp), per-item
duration, shuffle.

## Lessons from building dual-video-player (do not relearn these)

- **One atomic commit for both displays.** vc4 makes a commit on one CRTC wait for the other CRTC's
  pending flip; two independent presenters (two kmssinks, two players) stutter on both outputs.
- **Vblank grid from page-flip *timestamps*.** The sequence numbers vc4 puts in page-flip events are
  wrong for one CRTC. `drmWaitVBlank` *relative 0* queries return a zero timestamp while vblank IRQs
  are idle — use a real wait (`sequence = 1`) to start, then flip events.
- **Submit commits just after a vblank**, not mid-period: delays only make a commit later, so leave
  the most room before the next vblank (see `submit_time()`).
- **25 fps on 60 Hz always shows a 2/3 cadence**; switch to an integer-multiple mode when offered
  (HDMI-A-1 on the bench rig is a fixed 60.07 Hz panel, HDMI-A-2 offers 50 Hz).
- **Never touch the FPGA's I2C** from the player. The 0x1E slave keeps its register pointer between
  transactions and other tools move it; legacy register 0x29 locks the FPGA (see the RTL handover
  `tmp-docs/blocked-i2c-issue.md` in the workspace and br-wrapper `eee1d32`).
- The H.264 hardware decoder rejects streams whose **VUI colour description** is incomplete
  (`colorimetry=0:3:0:0` → `not-negotiated`); fix files losslessly with
  `ffmpeg -c copy -bsf:v h264_metadata=video_full_range_flag=0:colour_primaries=1:transfer_characteristics=1:matrix_coefficients=1`.
- The launcher runs program paths verbatim after `update-config-paths.sh` rewrote them; every new
  program needs its own sed line there.

## Bench notes (micropanel Pi4, `pi@192.168.1.170`)

- Root is a tmpfs overlay: live patches vanish on reboot; `apt-get update` is needed before *every*
  `apt-get install` (the lists are removed after each run). Build on the Pi after installing
  `cmake qtbase5-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libdrm-dev`.
- Launcher TCP API on 8081 via bash `/dev/tcp` (no `nc`): `start-app`, `stop-app`,
  `get-running-app`, `screen 2`, `get-screen`, `reload-config`.
- Screenshots: the launcher draws to `/dev/fb0` (RGB565 1920x1080); `cat /dev/fb0 > f.raw`, convert
  with PIL `Image.frombytes('RGB',(1920,1080),raw,'raw','BGR;16')`. While a DRM app runs, check
  planes with `kmsprint`.
- Fake touch for EXIT-popup tests: a `uinput` device with ABS_X/ABS_Y 0..4095 + BTN_TOUCH (the player
  grabs every device with BTN_TOUCH); see the 2026-10-03 session's `faketouch.c` pattern.
- Simulating a USB unplug via sysfs `authorized`: target the **stick's** device (e.g. `1-1.4`), not
  the hub `1-1` — deauthorizing the hub drops every USB device (measurement probes too).
- `pkill -f <pattern>` over ssh matches the ssh command line itself and kills the session; kill by
  PID or use `pkill -x`.
