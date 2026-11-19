# qvue-warp

Realtime warp of the 3x QVue HDMI output (4500x298 = three 1500x298 panels)
on the Pi 4, so normal content shows up straight in the windshield reflection.
The plan behind it is `tmp-docs/3xqvue-online-wraping-plan.md` in the workspace.

```
linuxfb apps ──CPU──► /dev/fb0 (4500x298 XR24, primary plane, unchanged)
                          │ dmabuf, 2 EGLImages, blitted into tiled textures
                          ▼
                 qvue-warpd (GLES 3 on V3D)
                 3 warp meshes ─► one 1500x894 XR24 GBM buffer
                          │ one atomic commit per vblank
                          ▼
        3 overlay planes: src (0, k*298, 1500x298) ─► crtc (k*1500, 0)
```

- **ON:** the daemon is DRM master; its three opaque overlay planes cover fb0.
- **OFF:** the daemon holds no DRM resources; fb0 goes 1:1 to HDMI, as before.
- **SUSPENDED:** like OFF, while a DRM-master app (dual-video-player, Kodi)
  runs; `resume` returns to the warp setting (retrying for 10 s while the
  app still holds the card).

## Parts

| Part | What |
|---|---|
| `lib/` → `libqvuewarp.a` | calibration loading (`calib.c`), forward mesh with integer or fractional source mapping (`mesh.c`), trim model (`trim.c`), tuning overlay geometry (`overlay.c`), GL programs and draw with per-panel source formats - RGB, external OES, NV12, I420 with YUV→RGB in the shader (`render.c`), stacked output stage: card by driver name or the caller's DRM fd with excluded planes, GBM/EGL, atomic flip in event-loop steps (`output.c`), the shared loader of `warp.conf` layers, calibration and the state directory (`qw_config.c`). Used by `qvue-warpd` and by `dual-video-player` (micropanel-media-player), which warps its own video while the daemon is suspended |
| `src/` → `qvue-warpd` | fb0 dmabuf source and its per-frame copy (`fb0src.c`), built-in test grid (`gridsrc.c`), atomic file writes and directories (`persist.c`), states, control socket, frame statistics (`qvue-warpd.c`) |
| `src/` → `qvue-warpctl` | the socket client: `qvue-warpctl COMMAND [ARGS]`, exit 0 for `OK`, 1 for `ERROR`, 2 without a daemon |
| `data/` | factory calibration `3xqvue-panel{1,2,3}-21x9.txt`, default `warp.conf`, `qvue-warpd.service.in` |
| `tools/` | host-side tests: `warp_ref.py` (numpy reference renderer + golden test), `dot_test.py` |

Installed (prefix `/home/pi/micropanel`): `bin/qvue-warpd`, `bin/qvue-warpctl`,
`share/qvue-warp/{3xqvue-panel*-21x9.txt,warp.conf}`,
`lib/systemd/system/qvue-warpd.service`.

Build dependencies: `libdrm-dev libgbm-dev libegl-dev libgles-dev` (cmake
skips the package without them).

## Warp

Per panel: a `gw x gh` calibration grid of destination points (panel pixels)
for a regular source grid, evaluated with bicubic Catmull-Rom at `subdiv`
steps per cell (linear extrapolation past the edges), drawn as textured
triangles. Vertex = destination pixel centre, texture coordinate = source
pixel centre. Outside the mesh: black. Bilinear sampling.

Calibration files: `#` comments, then `gw*gh` lines `dst_x dst_y`, row-major,
top-left point first. The grid size comes from a `# grid 21x9` comment or the
file name. Files that start bottom-right (CES25) need `panelN.reverse = true`.

## Trim (fine tuning)

The factory calibration is never changed. On top of it, each panel has a 3x3
lattice of handles (`tl t tr / l c r / bl b br`), each a `(dx, dy)` in panel
pixels; the displacement of grid node `(u, v)` is the quadratic Lagrange
interpolation of the nine handles. A corner handle gives keystone, an edge
middle bow, the centre bulge. Selecting `panel` moves all nine handles of the
panel (a translation), `all` every handle of all three panels. The trim is
applied on the next frame; `save` writes `trim.json`.

## Control socket

`/run/qvue-warp/ctl.sock` (0660 pi:video), one text line per request, one
reply line `OK ...` or `ERROR ...`. `qvue-warpctl` sends one request.

| Command | Effect |
|---|---|
| `status` | `OK state=on enabled=1 resuming=0 tune=0 source=fb0 panel=1 handle=c step=1 unsaved=0 fps=59.98 frames=N late=N pipeline=0 copy=1 error="..."` |
| `on` / `off` / `toggle` | the warp setting, saved in `warp.conf` |
| `suspend` / `resume` | give the display to a DRM-master app and take it back |
| `tune on` / `tune off` | tuning overlay (grid of the selected panel, handle markers) |
| `tune source grid` / `tune source fb0` | built-in straight test grid instead of the apps (reset by `tune off`) |
| `select panel 1\|2\|3\|next\|prev` | the panel the arrows move |
| `select handle tl\|t\|tr\|l\|c\|r\|bl\|b\|br\|panel\|all`, `select next\|prev` | what the arrows move |
| `nudge DX DY` | move the selection by DX, DY steps (integers -100..100) |
| `step N` | step in pixels, 1..50 |
| `save` / `revert` / `reset` | write `trim.json` / reload it / zero the trim (unsaved) |
| `reload` | re-read the config and calibration and rebuild the warp |
| `dump /OUT.ppm [/FB0.ppm]` | read the next frame back (and fb0) as 4500x298 PPM; replies when written |
| `help` | the command list |

After every change the daemon rewrites `/run/qvue-warp/state.json`
(state, enabled, warping, resuming, tune, source, panel, handle, step,
trim_unsaved, fps, frames, late, error); fps and the counters are refreshed
every 5 s while ON. The Stream Deck keys (streamdeck-ctrl
`scripts/qvue-warp.sh`) watch it.

## Files and configuration

| Path | What |
|---|---|
| `<datadir>/qvue-warp/warp.conf` | defaults, documented there (read first) |
| `<datadir>/qvue-warp/3xqvue-panel{1,2,3}-21x9.txt` | factory calibration |
| `/data/qvue-warp/warp.conf` | the writable layer read on top: `on`/`off`/`toggle` set `enabled` here (atomic write) |
| `/data/qvue-warp/trim.json` | saved trim |
| `/run/qvue-warp/ctl.sock`, `state.json` | runtime (`RuntimeDirectory=` of the unit) |

The state directory is `/data/qvue-warp` when `/data` exists (the unit creates
it, owned by pi), otherwise `~/.config/qvue-warp` (`/home/pi/.config/qvue-warp`
under the unit) for single-slot images. `--state-dir`, `--run-dir` and
`--config` override them.

Config keys: `enabled`, `subdiv`, `rt_priority`, `stats_interval`,
`copy_source`, `pipeline`, and per panel `calib`, `reverse`, `flip_x`,
`flip_y`, `src`. `--on` / `--off` override `enabled` at start without saving
it. SIGHUP (`systemctl reload`) is the same as `reload`. SIGTERM / SIGINT
disable the planes and exit. If the warp cannot start (for example another
program is DRM master), the daemon logs it, stays OFF and keeps serving the
socket.

### Failures, watchdog, automatic restarts

A flip that is still pending after 1 s (a stuck display) counts as a failure.
After any failure while the warp is wanted (start, present, flip watchdog,
a resume that gave up), the daemon turns OFF (fb0 shows) and restarts itself
after 2 s, 10 s and 60 s; then it waits for a command (`on`, `resume`,
`reload`), which also refills the three restarts. Running ON for a minute
refills them too. `resume` retries for 10 s while another program still holds
the card (Kodi held it 2.2-4.7 s after its exit in tests). `--debug-lose-flip N`
ignores the flip events after frame N, to see the watchdog work.

### Launcher integration

qt-demo-launcher apps flagged `"display": "drm"` (Kodi, dual-video-player,
the eglfs cluster, disp-tester) take the display themselves: the launcher
sends `suspend` before starting one and `resume` after every app exit (and at
its own start). `usb-media.sh` suspends around its player child itself
(`qvue-warpctl`). Without the daemon the socket is absent and nothing is sent.
`dual-video-player` (quick-video, dual-video, usb-media playback) then warps
its own frames through this library when the setting is on (see its README).

### Why fb0 is copied (`copy_source = true`)

Sampling the raster (linear) fb0 dmabuf through the GPU's texture unit costs
~11 ms per frame for the three panels (one panel 4.1 ms, two panels on the
same texture 8.0 ms: per sampled pixel, no shadow copy), against 3.7 ms from
a tiled texture, and it made ~0.4% of the flips land a vblank late. A
`glBlitFramebuffer` of the two imports into tiled textures each frame costs
~3 ms; with it 1 of 10800 flips was late (2026-11-18). `pipeline = true`
(one frame queued) made no difference and is off. Only whole textures are
blitted: a blit of just the columns the panels use takes a slow shader path
(29-32 ms per frame, 2026-11-19); a texture no panel samples is skipped.

## Service

`qvue-warpd.service` runs as `pi` (groups `video`, `render`), SCHED_FIFO 10,
`Restart=on-failure`, and only on the `3x-qvue` display type (`ExecCondition`
on `/boot/firmware/micropanel-display.txt`). `ExecStartPre` (as root) creates
`/data/qvue-warp` when `/data` exists; `RuntimeDirectory=qvue-warp` gives
`/run/qvue-warp`. It is not enabled by the image build yet.

## Testing on the rig

```sh
qvue-warpd --identity --selfcheck --frames 120 --dump-at 30   # output == fb0 (max diff 1), status 3 if not
qvue-warpd --frames 600 --dump out.ppm --dump-src src.ppm --dump-at 30
```

Only one DRM master at a time: stop the service first, or use the socket's
`dump` while it runs. On the host:

```sh
tools/warp_ref.py --src src.ppm --compare out.ppm data/calib/3xqvue-panel{1,2,3}-21x9.txt
tools/warp_ref.py --src src.ppm --compare out.ppm --trim trim.json data/calib/3xqvue-panel{1,2,3}-21x9.txt
tools/dot_test.py out.ppm warping-images/qvue-warping-calibration-test-pattern.png   # gallery showing the straight grid
```

The journal gets a statistics line every `stats_interval` seconds and on exit:
fps, late frames (flip interval > 20 ms), split into *submit* (the commit was
issued after the vblank it should have made) and *flip* (issued in time, shown
a vblank later), missed vblanks, worst interval, render time (draw + copy +
swap; with `--finish` including the GPU). `--verbose` logs every late frame.
Timing experiments: `--finish`, `--only-panel 13`, `--source grid`,
`--no-copy-src`, `--pipeline`, `QW_COPY_COLUMNS=1` (column-range blit).
