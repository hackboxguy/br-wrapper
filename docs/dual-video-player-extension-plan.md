# dual-video-player extension: USB Media playlist player — plan (v2)

Status: **phase 1 in progress** (started 2026-10-03). v2 folds in Fable's review
(`tmp-docs/fable-dual-video-player-extension-review-v1.md` in the workspace, measured on the rig) and the
owner's decisions of 2026-10-03. Read [qt-demo-launcher-apps-handover.md](qt-demo-launcher-apps-handover.md)
first (visual language, launcher contract, app conventions, build/deploy/test), then this.

## Goal

A launcher button **"USB Media"** (home screen, page 2, next to "Dual Video") opens `usb-media-app`, where
the user picks media files on the USB stick, orders them and plays them as a playlist — **the same content
on both HDMI outputs** — optionally looping forever and optionally starting at boot. Playback extends the
existing `dual-video-player` (GStreamer decode + one DRM presenter), not Kodi or mpv.

## Product constraints

Minimum hardware: **Pi 4 with 2 GB RAM, 16 GB SD card, A/B slots updated in-system from a `.mpupdate`.**
The bench rig is a 4 GB board with a 119 GB card — its numbers flatter the product. On 2 GB: Linux sees
~1.8 GB, `/` and `/tmp` are a tmpfs overlay capped at ~900 MB, **no swap** (over-use ends in the OOM
killer), CMA 512 MB. The player and app write nothing that grows in `/` or `/tmp`; every queue is
bounded.

The rig runs **firmware KMS** (`dtoverlay=vc4-fkms-v3d`, dmesg `bound fe600000.firmwarekms`): planes are
firmware layers. The vc4 notes below were measured on this setup.

## Decisions

| # | Topic | Decision |
|---|---|---|
| 1 | Engine | Extend `dual-video-player`; it moves to **`package/micropanel-media-player`** (used by Dual Video and USB Media) |
| 2 | Audio | Not required |
| 3 | Outputs | Mirror: the same item on both HDMIs. Per-display content stays the job of "Dual Video" |
| 4 | Mixed playlists | Images and videos in one list, in list order |
| 5 | Image duration | One value per playlist (seconds) |
| 6 | Files | **Played as stored — no conversion** |
| 7 | Playlist location | On the stick: `micropanel-playlist.json` in its root, paths relative to the root |
| 8 | Autostart flag | **In the playlist file** (`"autostart": true`), set by the app's checkbox, written with Save. The unit stores nothing. A new stick has no playlist → nothing autostarts; a prepared stick autostarts on any unit. Evaluated at boot only |
| 9 | No playlist on the stick | No "play everything" fallback; the user builds the playlist in the app |
| 10 | Display mode | **Playlist mode never changes the display mode** (`--refresh=keep` is the default with `--playlist`; the option stays for bench tests). No blanking, nothing to restore. 25 fps on 60 Hz shows the 2/3 cadence — accepted. Dual Video keeps its refresh matching |
| 11 | `gpu_mem` | **128** in the image base config, "as long as it doesn't cause any regression" (see `gpu_mem`) |
| 12 | HEVC | Software path, **"may play slowly" is acceptable**: labelled in the app, late frames logged per item. HEVC Main10 and 4K software items are unsupported |
| 13 | 50/60 fps in mirror mode | ~1 % repeated frames accepted |
| 14 | Portrait (rotated) videos | Not required now; later via software `videoflip`. Until then shown unrotated, with a note in the app |
| 15 | Per-item durations | Not now. Transitions, thumbnails: nice to have (phase 3) |

## Defaults

- **Scan**: stick root + subfolders, max depth 3. Skip every name starting with `.` or `$` (macOS
  `._name.mp4` AppleDouble files match the extension filter), `LOST.DIR`, `System Volume Information`.
  Videos: `mp4 mkv mov m4v`. Images: `jpg jpeg png`. HEIC (iPhone default) and other unknown media are
  **listed as unsupported with a reason**, not hidden.
- **Image duration**: default 10 s, range 2–600 s in the app (the player's CLI allows less, for tests).
- **Image size cap**: 50 MP (a 50 MP JPEG is ~75 MB decoded); larger → unsupported.
- **Classification per file** (one classifier, `dual-video-player --probe`, see Player):
  - `hw` — H.264 Baseline/Main/High, 8-bit 4:2:0, ≤ 1920x1088 → `v4l2h264dec`, dmabuf, zero copy.
  - `sw` — HEVC Main ≤ 1920x1088 and H.264 above 1080p up to 2560x1600 → `avdec_h265`/`avdec_h264`;
    labelled **"may play slowly"** when HEVC above ~4 Mbit/s or any sw item above 1080p (bitrate =
    file size ÷ duration; calibrate with real phone clips).
  - `unsupported` + reason — HEVC Main10 (decodes to `I420_10LE`, no matching plane format), anything
    above 2560x1600 in software (memory on 2 GB), 10-bit/4:2:2 H.264, other codecs (VP9, AV1, MPEG-2…).
  - `rotated` flag — `image-orientation` tag on a video (shown unrotated for now).
- **Stick removed during playback**: stop with a distinct exit code; the app says "Stick removed".
- **Read-only stick**: Play works; Save and the Autostart checkbox are disabled with the reason.
- **No Loop**: back to the app after the last item.
- **Autostart countdown**: 5 s, "Starting playlist… tap to cancel" (cancel = this boot only; the flag
  stays).

## Bench findings (2026-10-03, rig `.170`, image 2.03, GStreamer 1.22)

Measured unless marked. F = Fable's review, C = Claude's study.

1. **Never probe with GstDiscoverer / `gst-discoverer-1.0` / decodebin** — they open the hardware decoder
   per file; ~10 files in a row wedged the VideoCore codec firmware (`timed out waiting for sync
   completion`, `failed to create component … (Not enough GPU mem?)`, reload of `bcm2835_codec` failed
   and removed `/dev/video10`, reboot needed) [C, at `gpu_mem=76M`]. **Parse-only probe**:
   `filesrc ! parsebin ! fakesink` → codec, profile, size, fps, colorimetry, rotation; ~76 ms per file,
   0.12 s even during a hardware decode, no decoder touched [C, F].
2. **Hardware decoder concurrency depends on `gpu_mem`** [F]: at 76M two decoders work only for 720p+720p;
   a second decoder next to a 1080p stream gets 0 frames silently or wedges the firmware. At 128M and
   256M all tested pairs (up to 1080p60 + 1080p30) run. SIGKILL of decoding processes and 15 open/close
   cycles leave the decoder healthy at 128M. **Throughput is shared**: a free-running second 1080p
   decode slowed a real-time 1080p60 stream to ~60 %, and two processes with a decoder serialised
   completely at 76M → keep all decoding in one process and let a prerolled item stop after its first
   frame.
3. **Hardware decode speed**: 1920x720 ~110–120 fps, 1080p ~46–70 fps (16–20 Mbit/s) [C, F]. A stream
   **without** a colour description decodes; an **incomplete** one (`colorimetry=0:3:0:0`, which is what
   ffmpeg's `-colorspace bt709` produces without x264's own primaries/transfer — the *common* case [F])
   fails `not-negotiated` and decodes 300/300 frames once the caps are normalised (`capssetter
   caps="video/x-h264,colorimetry=(string)bt709"` between parser and decoder) [C].
4. **1080p hardware frames were not importable** in the existing player (`cannot import frame as DRM
   framebuffer`): the decoder pads to 1920x1088 and, without `GstVideoMeta` support announced in the
   ALLOCATION query, hands appsink a *copied* system-memory buffer. Adding the meta in an allocation-query
   probe on the appsink pad fixes it (1080p30 mirrored: 598/600 frames held exactly 2 vblanks) [F].
5. **Software decode** (`avdec_*`, all four cores): H.264 3840x1440@25 45 fps and HEVC 1080p25
   near-static 82 fps [C] — but **HEVC at phone-like bitrates is below real time**: 1080p30 8.6 Mbit/s
   21.8 fps, 12 Mbit/s 18.8 fps, Main10 16.3 fps, 1080p60 20 Mbit/s 19.7 fps, 2160p30 ~5 fps; H.264
   2160p30 13 fps; H.264 1080p60 27 Mbit/s 87 fps [F]. Software frames on screen (`kmssink
   can-scale=true`, copy + plane downscale): 3840x1440 H.264 298/0 dropped at ~27 % CPU, HEVC 1080p
   300/0 at ~23 % [C]. SoC 52 → 61 °C within minutes of software decode [F].
6. **Images**: 12 MP JPEG → 1080p 0.38 s (progressive 0.57 s), **50 MP 2.5–2.9 s** (almost all
   `jpegdec`); `videoscale` before `videoflip`/`videoconvert` saves ~0.5 s [F]. `jpegdec` alone ignores
   EXIF orientation; `jpegparse ! jpegdec ! videoflip video-direction=auto` applies it [C].
7. **Time to first frame** (minus ~50 ms process start): hardware ~90 ms, software HEVC ~460 ms,
   software H.264 3840x1440 ~580 ms [C].
8. **Mirror on two outputs** (one decoded framebuffer on an overlay plane of each CRTC, one atomic
   commit) [F]: 1080p30 master A-1 60.07 Hz → 598× 2 vblanks, 1× 3, clean on both; 720p25 → regular
   cadence; **1080p50/60 ~1 % late frames** on two unsynchronised outputs (each commit waits for both
   flips). `modetest` accepted a 3840x2160 NV12 plane scaled to 1920x1080 on both CRTCs, also with a
   second XRGB plane each (crossfade) — accepted only, not checked visually.
9. **Planes**: `COLOR_ENCODING` (601/709/2020) and `COLOR_RANGE` (limited/full), default 709 limited;
   the decoder's output caps say `bt601` even for BT.709 streams → take matrix and range from the
   **parser** caps. Rotation only `rotate-0/180` + reflect, no 90° [F].
10. **Packages**: `parsebin`, `capssetter`, `jpegparse`, `jpegdec`, `pngdec`, `videoflip`, `imagefreeze`
    and `libjson-glib-1.0-0` are in the image; **`gstreamer1.0-libav` must be added** (libavcodec59 etc.
    are already there, so it is small) [C, F].
11. **Stick**: NTFS, mounted rw by udisks (`ntfs3`). What an unclean NTFS volume does at the next mount
    is untested [F].

## `gpu_mem`

- **128**, not 256 (128 passed every two-decoder case; 256 costs 2 GB units another 128 MB). On a
  simulated 2 GB board (`total_mem=2048`): 1798 MB visible, ~1430 MB available at idle, CMA ~450 MB free,
  two 1080p decoders fine [F].
- **Where**: the root `config.txt` on the boot partition is rendered by the A/B slot selector from the
  slot's own `config.txt` (`misc-tools/packages/pi-ab-update/ab-slot-selector render-normal`), so a hand
  edit (as on the rig now: `[all]`/`gpu_mem=128`, backup `config.txt.fable-bak`) disappears at the next
  update. **All `config.txt` changes go through the micropanel repo's `scripts/pi-config-txt.sh`**
  (owner, 2026-10-03), which renders from `configs/config-base.txt.in` and, on A/B images (split boot
  configuration, `/etc/default/micropanel` → `MICROPANEL_BOOT_CONFIG`), writes a **device-owned display
  file** that the release-owned `config.txt` includes. Work out with that script which side `gpu_mem`
  belongs to (it must travel with the release, i.e. the slot `config.txt`, not stay behind in the
  device file), update its test (`tests/test_pi_config_txt.sh`), the golden copies
  (`tests/golden/pi-config-txt/*.config.txt`) and the misc-tools users (`micropanel-appliance-hook.sh`,
  `ab-assertions.sh`, `tests/test_ab_layout_static.sh`, `PERSISTENCE.md`).
  It then travels with the `.mpupdate`, and a rollback restores old config + old player together. Add an
  assertion so the line cannot drop silently (`ab-assertions.sh`, `test_ab_layout_static.sh`).
- **Player safety net**: read `vcgencmd get_mem gpu` at start; below 128 use **one hardware decoder at a
  time** (tear down, then start the next, holding the last frame — a short freeze instead of a wedge).
  Plus a **decoder watchdog**: an item with no frame within 5 s of PLAYING is failed and skipped (the
  76M failure mode is silence).
- **Regression checks before shipping** (need eyes on the panels): Kodi video + slideshow incl. the
  HDMI-2 mirror, Dual Video, the Qt Quick apps, disp-tester patterns, cluster demo; boot + 30 min playlist
  on a real 2 GB unit (`free -m`, `get_throttled`); `.mpupdate` install → `get_mem gpu` 128 on the
  candidate, forced rollback → old value; all display variants (the setting is in the shared base block).

## Architecture

```
qt-demo-launcher ── "USB Media" (id usb-media) ──> usb-media.sh   (the launcher's tracked child)
      │  available_command: usb-media.sh --check        loop:
      │                                                   usb-media-app            exit 0 → leave
      │                                                                            exit 10 → play
      │                                                   dual-video-player --playlist <stick>/micropanel-playlist.json --mirror
      │                                                   → back to the app with the player's exit reason
      │                                                 trap TERM → forward to the current child
boot: usb-media-autostart.service ── polls :8081 ── start-app usb-media-autostart (visible:false entry)
                                                    = usb-media.sh --autostart: countdown in the app → play → app
```

Why: the launcher refuses `start-app` while an app runs and cannot start an `enabled: false` entry
(`startApp()` → `findButton(appId, config, true)`); `visible: false` is the existing "off the grid but
startable" switch [F, code — verify]. The wrapper needs no second entry for normal use, no TCP round
trip, no launcher flash between app and player, and EXIT returns to the playlist editor.

### Player (`package/micropanel-media-player`, binary `dual-video-player`)

Order of work: (a) the 1080p import fix in place, own commit; (b) **pure `git mv`** to the new package,
own commit; (c) split the 1287-line file into modules (drm/presenter, item pipelines, input/popup,
playlist, probe) without behaviour change; (d) the extension.

- **CLI**: existing `VIDEO1 VIDEO2` mode unchanged. New: `--probe FILE…` (one line per file: kind,
  codec, profile, size, fps, PAR, duration, bitrate, rotation, path = hw/sw/unsupported + reason; the app
  runs this via QProcess — no GStreamer in the Qt app); `--playlist FILE --mirror`; test seams `--list`
  (print the resolved plan per item, no DRM), `--image-duration S` (any value), `--max-loops N`, a final
  machine-readable stats line.
- **Exit codes**: 0 finished (no loop) / user stop / SIGTERM; 1 nothing playable / setup error; 2 usage;
  3 stick removed.
- **Pipelines** built from elements with `g_object_set(location)` (stick filenames are arbitrary), never
  `gst_parse_launch` strings:
  - hw: `filesrc ! qtdemux|matroskademux ! h264parse ! [colorimetry probe] ! v4l2h264dec ! appsink`
    with the **VideoMeta allocation probe** on the appsink pad (finding 4);
  - sw: `… ! h265parse|h264parse ! avdec_* ! appsink` (I420) → copied into a fixed **ring of 3–4 dumb
    buffers**, scaled by the planes;
  - image: `filesrc ! jpegparse ! jpegdec` (PNG: `pngdec`) `! videoscale ! videoflip
    video-direction=auto ! videoconvert ! appsink` (scale first, to fit the larger display) → one dumb
    buffer per shown image, freed after it leaves the screen.
  - The colorimetry probe rewrites the decoder-sink CAPS event when the colorimetry is unknown or
    partial (`bt709` for HD, `bt601` for SD).
- **Presenter** (one thread, one atomic commit per frame for both outputs, black primaries, EXIT popup,
  input grab — as today), changed for playlists:
  - **Pacing from buffer PTS per item**: `target = item_start_vblank + round(pts / period)`, the vblank
    grid running continuously across items — mixed rates and variable frame rate with one rule; late
    frames hold the previous one and are counted.
  - **Wake-ups** (eventfd/condition): popup changes and stop must act while a still is shown (no commits
    for 10 s otherwise).
  - **Item switch**: commit the new item's first frame → wait for the flip → only then release the old
    sample and drop the old pipeline (removing a framebuffer that is on a plane turns the plane off →
    black flash). Teardown and the next item's build/preroll happen **off the presenter thread**, with
    `ASYNC_DONE` instead of a blocking `get_state`.
  - **Prefetch**: start preparing the next item as soon as the current one starts (preroll stops at the
    first frame: PAUSED, appsink `max-buffers` small) — no duration bookkeeping; needs `gpu_mem ≥ 128`,
    else one decoder at a time.
  - **Scaling**: fit, keep aspect, up and down, honouring the stream's **pixel aspect ratio**, per output.
  - **Colour**: set the planes' `COLOR_ENCODING`/`COLOR_RANGE` per item from the parser caps (after
    normalisation).
  - **No leaks in a forever loop**: free image and software-ring framebuffers after they leave the
    screen (today `dumb_fb()` keeps its framebuffers until the fd closes).
  - **Stick removal**: poll `/proc/self/mountinfo` (a still does no I/O) → exit 3; a read error must not
    walk "skip bad item" through the whole list.
  - **Stats per item**, images skipped, plus the boundary ("item 3→4: last frame held N vblanks" — the
    proof of "no black gaps").
- **JSON**: parse the playlist with json-glib (`libjson-glib-1.0-0` runtime, `libjson-glib-dev` build).

### App (`package/usb-media-app`, Qt Quick)

Follow touch-gallery / System Manager (QML + C++ controllers), CMake target in the top-level
`CMakeLists.txt`, launcher visual language.

- Find/mount the stick (the logic of `kodi-usb-common.sh`: removable `/sys/block/sd*`, existing mount,
  else `udisksctl mount`, else `sudo -n mount`), scan files (Defaults), classify them with
  `dual-video-player --probe` (QProcess; images too, for size), load/save the playlist.
- UI (touch, one screen): file list (checkbox, type, name, folder, duration/resolution or image size;
  greyed + reason when unsupported; "may play slowly" / "plays unrotated" notes), Select all / None,
  Up / Down for the checked items, Image duration, **Loop**, **Autostart on boot** (disabled + reason
  on a read-only stick), Save, Play, Back.
- **Save safely** (NTFS/exFAT/FAT sticks are pulled right after saving): write a temp file, `fsync`,
  rename, `fsync` the directory / `syncfs`.
- Exit codes to the wrapper: 0 leave, 10 play (playlist saved or passed). `--countdown N`: the autostart
  countdown screen, exit 10 = play, 0 = cancelled. `--message TEXT`: show the player's exit reason
  when returning ("Stick removed", "Nothing playable").
- Caches (phase 3 thumbnails, probe results) only under `/data/usb-media/` with a size cap — never in `/`.

### Launcher integration

- `qt-demo-launcher-pios.json`: button `usb-media` (page 2, row 3, column 2), program `usb-media.sh`,
  `available_command: usb-media.sh --check`; phase 2 adds the `visible: false` entry
  `usb-media-autostart` (program `usb-media.sh --autostart`).
- `update-config-paths.sh`: one sed line per new program path.
- `usb-media.sh`: `--check` (exit 0 when a stick is mounted, else one line of reason), default = the
  app↔player loop, `--autostart` (phase 2). `trap TERM` forwards to the current child so `stop-app`
  works within the launcher's timeout.

### Autostart (phase 2)

- `usb-media-autostart.service` (oneshot): poll port 8081 until the launcher answers (ordering alone
  is not enough: the launcher sleeps 4 s in `ExecStartPre`), wait up to 15 s for a stick, and if
  `micropanel-playlist.json` is valid and `"autostart": true` → `start-app usb-media-autostart`.
  Anything else → nothing; the launcher stays.

## Interfaces

**Playlist file** (`micropanel-playlist.json`, stick root):

```json
{
  "version": 1,
  "image_duration_s": 10,
  "loop": true,
  "autostart": false,
  "items": ["Videos/intro.mp4", "Pictures/slide-01.jpg", "Videos/demo.mkv"]
}
```

Relative paths, `/` separators, order = play order. Unknown keys ignored; a missing `autostart` means
false. Missing items are skipped at play time.

## Phases

### Phase 1 — playlist playback + app + button

**Status (2026-10-04): implemented, first image 2.05.** Player playlist mode (br-wrapper `cfec40c`,
`efd5f13`, `--root`), `usb-media.sh`, `usb-media-app`, the launcher button, misc-tools deps and
assertion, `gpu_mem=128` in micropanel's base template. Bench results on `.170` (gpu_mem=128):
1080p30 hw 598x2 + 1x3 vblanks, 0 late; boundaries held exactly as due (no black gaps); HEVC sw 4 late
in 300; 1080 item switches with framebuffers back to 1, CMA bounded, RSS flat 58-131 MB over 538
switches; EXIT during a 30 s still stops at once; stick pulled during a still and a video → exit 3 in
~0.76 s; `stop-app` during preroll 0.24 s and during playback (wrapper → player) 0.39 s; app → Play →
playback → back to the app → Back verified on the panel. **Open before shipping**: the `gpu_mem`
regression checks above (eyes on the panels), the 10-minute mixed loop and the 30-minute software
soak with `get_throttled` on a 2 GB unit, EXIT returning to the app pressed by hand.

1. Player: VideoMeta allocation fix (own commit) → `git mv` to `package/micropanel-media-player` (own
   commit) → module split (no behaviour change) → `--probe`, `--list`, `--playlist --mirror` with
   everything under "Player".
2. `usb-media.sh` (`--check`, app↔player loop, TERM forwarding).
3. `usb-media-app` (scan, classify, list, select, reorder, duration, loop, autostart checkbox stored in
   the playlist, safe save, play, return messages).
4. Launcher button + path rewrites; CMake/install; READMEs.
5. Board config: `gstreamer1.0-libav` and `libjson-glib-1.0-0` in misc-tools `runtime-deps*.txt`,
   `libjson-glib-dev` in the br-wrapper hook's build deps; `gpu_mem=128` in the micropanel repo base
   config (+ golden files, + assertion), with the regression checks above before it ships.

**Done when** (owner decisions 10/12/13):
- a mixed list (≥ 2 videos incl. a 1080p and a 720p item back to back, ≥ 3 images, ≥ 1 software item)
  plays mirrored on both HDMIs, **no display mode change**, no black gaps (boundary stats), and loops
  ≥ 10 min;
- the hold pattern per hardware item is the regular cadence for its frame rate at the display's
  refresh (30 fps on 60 Hz: 2; 25 fps: 2/3), late frames ≤ ~1 % for ≤ 60 fps items; software items
  are exempt from the late-frame limit but must not stall or grow memory;
- leak check: 1000 item switches (`--image-duration` small, `--max-loops`) without growth in
  framebuffers (`/sys/kernel/debug/dri/1/framebuffer`), `CmaFree` or RSS;
- 30-minute software-path soak with `vcgencmd get_throttled`;
- EXIT (also during a still) returns to the app; the stick pulled during a still and during a video
  → exit 3 → "Stick removed"; `stop-app` during preroll works;
- the USB Media tile is dimmed without a stick.

### Phase 2 — autostart on boot
The `autostart` key + checkbox (stored in the playlist), `usb-media-autostart.service`, the
`visible: false` entry, the countdown in the app. **Done when**: a cold boot with the prepared stick ends
in playback after the countdown; a tap cancels (this boot only); without the stick or with
`autostart: false` the launcher stays.

### Phase 3 — nice to have
Thumbnails (cached under `/data/usb-media/`, size-capped), crossfade transitions (two planes, accepted by
the planes, finding 8), per-item duration, shuffle, portrait videos via software `videoflip`, a hardware
HEVC path if GStreamer gains `v4l2slh265dec` for `rpi-hevc-dec`, libjpeg DCT-domain downscaling for
50 MP images.

## Lessons from dual-video-player (do not relearn these)

- **One atomic commit for both displays.** A commit on one CRTC waits for the other CRTC's pending flip
  (firmware KMS on this rig); two presenters stutter on both outputs.
- **Vblank grid from page-flip *timestamps***: vc4's page-flip sequence numbers are wrong for one CRTC;
  `drmWaitVBlank` relative-0 queries return a zero timestamp while vblank IRQs are idle.
- **Submit just after a vblank**, leaving the most room before the next (see `submit_time()`).
- **Never touch the FPGA's I2C** from the player (0x1E pointer race; legacy register 0x29 locks the FPGA —
  br-wrapper `eee1d32`, RTL handover `tmp-docs/blocked-i2c-issue.md`).
- The launcher runs program paths verbatim after `update-config-paths.sh`; every new program needs its
  own sed line.

## Bench notes (rig `pi@192.168.1.170`)

- **Prefer a power cycle over `sudo reboot`**: Tasmota socket `192.168.1.232`
  (`curl "http://192.168.1.232/cm?cmnd=Power%20Off"`, then `Power%20On`). A wedged codec firmware can hang
  a soft reboot; after soft reboots the DS90UB983 on HDMI-1 sometimes stops answering (`hh983-serializer
  1-0018: Failed to write reg 0x07: -5`) and the panel stays dark while HDMI-A-1 still reads connected.
  After every boot: `sudo dmesg | grep hh983`.
- `/boot/firmware` is mounted ro; the rig currently has `gpu_mem=128` hand-added (backup
  `config.txt.fable-bak`).
- Root is a tmpfs overlay: live patches vanish on reboot; `apt-get update` is needed before *every*
  `apt-get install`. Building on the unit works on the 4 GB rig only (on 2 GB it eats half the overlay).
- Launcher TCP API on 8081 via bash `/dev/tcp`: `start-app`, `stop-app`, `get-running-app`, `screen 2`,
  `get-screen`, `reload-config`.
- Screenshots: `/dev/fb0` (RGB565 1920x1080) → PIL `Image.frombytes('RGB',(1920,1080),raw,'raw','BGR;16')`;
  while a DRM app runs, `kmsprint`.
- Fake touch: a `uinput` device with ABS_X/ABS_Y 0..4095 + BTN_TOUCH (the player grabs it).
- USB unplug simulation via sysfs `authorized`: target the stick (`1-1.4`), not the hub `1-1`.
- `pkill -f <pattern>` over ssh matches the ssh command line itself; kill by PID or `pkill -x`.
