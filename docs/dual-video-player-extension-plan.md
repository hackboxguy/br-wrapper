# dual-video-player extension: USB Media playlist player — plan

Status: **planned, not started** — bench study done 2026-10-03 (see "Bench findings"). Written so a fresh session can implement it phase by
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
- **Decode path per file** (chosen from the parse-only probe, see "Bench findings"):
  - **Hardware**: H.264 (Baseline/Main/High, 8-bit 4:2:0) up to 1920x1088 → `v4l2h264dec`, dmabuf,
    zero copy. The only hardware path: **HEVC is not hardware-decodable through GStreamer on this
    image** — the kernel has `rpi-hevc-dec` (`/dev/video19`, stateless V4L2) but GStreamer 1.22 on
    Pi OS ships no element for it (`v4l2slh265dec`/`v4l2h265dec` missing).
  - **Software**: HEVC (`avdec_h265`) and H.264 above 1080p (`avdec_h264`), from **gst-libav** —
    measured faster than real time with headroom (findings §3/§4). Frames are copied into DRM dumb
    buffers and scaled by the display planes.
  - **Too heavy** (greyed in the app with the reason): estimated decode cost above the software budget.
    Start with `width × height × fps ≤ 140 M pixels/s` for software files (3840x1440@25 = 138 M decoded
    at 1.8× real time) and refine with real camera/phone clips; H.264 10-bit/4:2:2 and other codecs
    (VP9, AV1, MPEG-2…) are unsupported for now.
  - Images: any size (decoded in software, downscaled to the display).
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
- **H.264 above 1080p** and **HEVC**: no hardware decode path on this image; **software decode
  (gst-libav) is fast enough** for the measured cases (findings §3/§4) → software path with a cost
  budget (Defaults).
- **Containers**: MP4/MOV (`qtdemux`), MKV (`matroskademux`); `decodebin`/`parsebin` can pick the
  demuxer.
- **Images** of any size and orientation (phone photos: 12–50 MP JPEGs with EXIF orientation):
  `jpegparse` must precede `jpegdec`, else the EXIF orientation is lost (findings §5).

Experiments and results on the bench go into "Bench findings" below.

## Bench findings (2026-10-03, micropanel Pi4 `.170`, image 2.03, GStreamer 1.22)

Stick content: 4 videos (all H.264 1920x720: two 25 fps High@4.1 with full BT.709 tags, one 30 fps
High@4 BT.709, one 30 fps Main@4.1 **with no colour description at all**) and 12 JPEGs (1920x1080,
2560x1440). Extra test files made on the host: H.264 with an incomplete colour description, HEVC
1080p25, H.264 3840x1440@25, a 4000x3000 JPEG with EXIF orientation 6.

1. **Never probe videos with GstDiscoverer / `gst-discoverer-1.0` / decodebin.** They open the
   hardware decoder per file. Probing ~10 files in a row **wedged the VideoCore codec firmware**
   (`bcm2835_mmal_vchiq: timed out waiting for sync completion`, `failed to create component
   ril.video_decode (Not enough GPU mem?)`, 58 failures; the board has `gpu_mem=76M`). Reloading
   `bcm2835_codec` then failed to probe (`-12`) and **removed `/dev/video10`**; only a reboot recovered.
   → **Parse-only probe**: `filesrc ! parsebin ! fakesink` (in the app via the API, reading the caps):
   ~76 ms per file incl. process start, gives codec, profile, width, height, framerate, colorimetry,
   and touches no decoder (0 decoder kernel messages over 6 files).
2. **Hardware decode** (`qtdemux ! h264parse ! v4l2h264dec capture-io-mode=dmabuf ! fakesink
   sync=false`): all stick videos decode completely, ~110–120 fps for 1920x720 (≈4× real time). A
   stream **without** a colour description is fine; one with an **incomplete** description
   (`colorimetry=0:3:0:0`) fails `not-negotiated` — and decodes 300/300 frames with
   `capssetter caps="video/x-h264,colorimetry=(string)bt709" join=true replace=false` between
   parser and decoder. → In the player: a CAPS-event probe on the decoder sink pad that replaces an
   unknown/partial colorimetry with `bt709` (HD) / `bt601` (SD) — no extra element.
3. **Software decode speed** (`avdec_*`, `fakesink sync=false`): H.264 3840x1440@25 → 45 fps (1.8×
   real time); HEVC 1080p25 (a low-bitrate sample) → 82 fps (3.3×). The hardware decoder refuses
   3840x1440 at negotiation, so the path must be chosen *before* building the pipeline.
4. **Software frames on screen** (`avdec_* ! queue ! kmssink can-scale=true`, single display): H.264
   3840x1440 → 298 rendered / **0 dropped**, ~27 % of 4 cores (decode + copy + plane downscale);
   HEVC 1080p → 300 / 0 dropped, ~23 %. Hardware path for comparison ~5 %.
5. **Images**: 2560x1440 JPEG decode ~260 ms, 12 MP ~150–360 ms (incl. process start). `jpegdec`
   alone ignores EXIF orientation (4000x3000 out); **`jpegparse ! jpegdec ! videoflip
   video-direction=auto`** (or `decodebin`, which includes `jpegparse`) gives 3000x4000. → Decode the
   next image while the current item is shown.
6. **Time to first frame** (minus ~50 ms process start): hardware H.264 ~90 ms, software HEVC
   ~460 ms, software H.264 3840x1440 ~580 ms. → Start (preroll) the next item **≥ 1 s before** the
   current one ends; two hardware decoder instances at once are fine (Dual Video runs two).
7. **Mixed frame rates** are real (this stick: 25 and 30 fps). One refresh mode per playlist means
   the minority judders (2/3 cadence); the HDMI-A-1 panel is fixed at 60.07 Hz anyway (30 fps fine,
   25 fps judders). Default stays "per playlist, from the first video"; a per-item mode switch is a
   possible later option (cost: an HDMI resync blank at each change, monitor-dependent, not measured).
8. **Packages**: `capssetter`, `parsebin`, `jpegparse`, `jpegdec`, `pngdec`, `videoflip`,
   `imagefreeze` are in the image already; **`gstreamer1.0-libav` must be added** to misc-tools
   `runtime-deps*.txt` for the software path.

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
  - *Video, hardware path*: `filesrc ! qtdemux|matroskademux ! h264parse ! [colorimetry probe] !
    v4l2h264dec ! appsink` (dmabuf, zero copy), as today.
  - *Video, software path* (HEVC, H.264 > 1080p): `... ! h265parse|h264parse ! avdec_h265|avdec_h264 !
    appsink` (system memory, I420); the presenter copies each frame into a DRM dumb buffer (keep a
    small ring of them) and lets the planes scale it. Revisit HEVC if a newer GStreamer with
    `v4l2slh265dec` for `rpi-hevc-dec` lands in the base image.
  - The path is chosen from the probe **before** building the pipeline (the hardware decoder refuses
    oversize streams only at negotiation).
  - *Image*: decode once (`filesrc ! jpegparse ! jpegdec` (PNG: `pngdec`) `! videoflip video-direction=auto !
    videoconvert ! videoscale` to fit the larger display,
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
  videos **parse-only** (`parsebin` caps + a duration query — never GstDiscoverer/decodebin, findings
  §1: codec, profile, width, height, fps, colorimetry, duration → decode path or "too heavy"), load/save
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
1. Player: `--playlist` + `--mirror`, video items on the hardware or software path (findings §2–4),
   colorimetry normalisation probe, image items with EXIF orientation (findings §5), gapless prefetch
   ≥ 1 s ahead (findings §6), loop, skip bad items, one refresh mode per playlist, EXIT popup,
   stick-removal stop.
2. `usb-media.sh` (`--check`, `--play`).
3. `usb-media-app`: scan, probe, list with checkboxes, select all/none, reorder, image duration, loop,
   save to stick, play.
4. Launcher button + hidden play entry + path rewrites; CMake/install; README.
5. Board config: add **`gstreamer1.0-libav`** to misc-tools `runtime-deps.txt` and
   `runtime-deps-ab.txt` (software path). Everything else is already pinned: GStreamer base/good/bad
   provide `parsebin`, `capssetter`, `jpegparse`, `jpegdec`, `pngdec`, `videoflip`, `imagefreeze`
   (checked 2026-10-03).
6. Move the player to `package/micropanel-media-player` (CMake target + install unchanged:
   `bin/dual-video-player`; `dual-video.sh` and `usb-media.sh` call it), update the qt-demo-launcher
   CMake/README and misc-tools references if any.

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
