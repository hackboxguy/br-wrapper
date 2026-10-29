# micropanel-media-player (dual-video-player)

`dual-video-player` plays video on the rig's HDMI displays with GStreamer's V4L2 hardware decoder
and libdrm atomic commits (frames presented on the display's vblank grid, no GStreamer sink).

- **Two-file mode** — `dual-video-player [--once] VIDEO1 VIDEO2`: VIDEO1 on the first display,
  VIDEO2 on the second, frame N of both in one commit (the launcher's Dual Video button).
- **Playlist mode** — `dual-video-player --playlist micropanel-playlist.json`: the same item on
  every display (images, H.264/H.265 videos), the next item prepared while one plays. The USB Media
  app hands over to it (`usb-media.sh`). `--max-loops=N`, `--list`, `--autostart-check`,
  `--root=DIR`; exit 0 done/EXIT, 1 nothing playable, 2 usage, 3 stick removed.
- **Playlist sync** — `--sync=…`: two players on two rigs show the same frame (below).

Sources: `dual-video-player.c` (options, setup), `mirror.c` (playlist presenter), `presenter.c`
(two-file presenter, commits, statistics), `item.c` (pipelines), `drm.c` (outputs, planes, vblank
grid), `probe.c`, `playlist.c`, `input.c` (touch, EXIT button), `sync.c` (network sync).

## Playlist sync between two rigs (`--sync`)

Two rigs side by side (the shootout: the HDR10+-processed clip on one, "SDR today" on the other)
show the same media frame within one frame of each other. Opt-in: without `--sync` the player runs
exactly as before (the synced presenter is a separate function, `play_item_synced()`).

### By hand, from ssh

Stop the launcher's app first (`launcher-client --srv=127.0.0.1:8081 --command=stop-app`), then
on the master rig and on the other one:

```sh
dual-video-player --playlist /media/pi/<stick>/micropanel-playlist.json --sync=master
dual-video-player --playlist /media/pi/<stick>/micropanel-playlist.json --sync=client
```

To leave a player running after the ssh session ends, start it as a plain command —
`nohup dual-video-player … > /tmp/dvp.log 2>&1 < /dev/null &` — not inside an `a && b &` list
(bash then waits in a subshell that holds the ssh session's pipes, and ssh does not return).

Start them within `--sync-wait` seconds (10) of each other: both show their first frame, the
client reports ready, and both start 1.5 s later on the same frame. A client started later (or
restarted) joins at the master's next pass start, holding its first frame until then. `--sync=auto`
on both: a master announced on the network → client; else the rig whose wired port is in
DHCP-server mode (NetworkManager shared mode, the Network app's **DHCP server**) is the master; else
the lower IPv4 address. The choice is logged (`sync: auto: master (…)`) after a 3-s look around, so
start auto rigs within 3 s of each other or name the roles.

| option | |
|---|---|
| `--sync=master\|client\|auto` | role (above) |
| `--sync-peer=ADDR` | client: connect to ADDR, no Avahi (no announcement either) |
| `--sync-iface=IFACE` | only peers found on IFACE (a rig on the LAN and on a direct cable sees its peer twice; without it the first one seen is kept) |
| `--sync-port=N` | TCP control and UDP clock port (8096) |
| `--sync-wait=S` | master: start alone if no client within S s; client: play free-running if no master's schedule within S s (10) |
| `--sync-log=FILE` | CSV, one line per shown frame: pass, item, due, latched (shared clock, ns), vblank, clock domain |
| `--sync-clock-test=S` | no display, no playlist: run the clock for S s; the client prints `CLOCK rtt_us err_us` per ping and `CLOCKSTATS` |

Both playlists must have the same items in count, kind, frame count and frame rate (images: the
same image duration); the names may differ (`…-hdr10plus.mp4` vs `…-sdr-today.mp4`). A client
whose playlist differs refuses with the reason (both logs) and plays free-running. A playlist that
cannot be scheduled (an unplayable item, a variable frame rate) plays without sync. Two-file mode
has no `--sync`.

### How it works

**A shared clock, not a shared trigger.** The master serves its `CLOCK_MONOTONIC` (GStreamer's
system clock) with a `GstNetTimeProvider` on UDP 8096; the client slaves a `GstNetClientClock` to
it (libgstnet, part of GStreamer core: no new package). The master picks `T0` (the start of pass 0,
1.5 s ahead) on that clock, and every frame of every pass has a due time:

    due = T0 + pass × pass_duration + offset(item) + PTS − PTS(first frame)

`pass_duration` is the sum of the items' durations (frames × frame period, images their duration).
Each player shows a frame on **its own master display's first vblank at or after the due time**
(the existing vblank-grid pacing, with the target vblank derived from the due time instead of the
previous frame). Nothing accumulates: every frame and every pass is anchored to `T0`; the panels'
different refresh rates only decide which vblank a frame lands on. A rig's second HDMI display keeps
following that rig's master display (one atomic commit), as without sync.

**Commit timing.** Without sync, a frame's commit is planned for a moment early in its vblank's
window (just after the previous vblank); a frame that is decoded after that moment goes to the next
vblank. With sync that would put it a vblank off the other rig's, so the synced path commits at once
when the planned moment has passed, as long as the target vblank is still more than `GUARD_NS`
(1.5 ms) + 0.3 ms away. On the 14.6" rigs this took a 2-min 59.94 fps pass from ~90 drops and a
few frames a vblank late to none. The presenter thread also asks for `SCHED_FIFO` 10, so a busy CPU
cannot hold it past a vblank; that needs `RLIMIT_RTPRIO` (`LimitRTPRIO=10` in
`qt-demo-launcher.service`, inherited by everything the launcher starts). Without it the player logs
"presenter not real-time" and runs as is.

- **Repeats** (`sync_repeats`): vblanks on which a frame stays up because the next one is not due
  yet — the panel refreshes faster than the video (59.94 fps on 60.07 Hz: one per ~460 frames,
  ~7.7 s, 16 per 2-min pass; on 60.00 Hz one per 1000 frames). Expected, on both rigs; they keep
  both panels on the shared time line.
- **Drops** (`sync_drops`): a frame that can no longer make its vblank (decoder late, a pass joined
  late) while the next frame is already due by the earliest reachable vblank is skipped, so the
  player catches up instead of running late. One that can still go up a vblank late, its successor
  not due yet, is shown late (`late`).
- **Missed** (`sync_missed`): a commit made in time for its vblank that still latched later; each is
  logged with its timing.
- **Offset** (`sync_offset_ms`): latched − due on the shared clock, per frame; by construction
  0 … one vblank (16.7 ms) when nothing is late. The client's figure includes its clock error.
- **Lost connection**: the client keeps the master's schedule on its clock's last estimate (the net
  clock keeps its calibration; drift is ppm) and reconnects every 2 s; a restarted master sends a new
  schedule, which applies from the client's next pass start. A client that never got a schedule
  plays free-running after `--sync-wait` and keeps looking.
- **Clock not synchronised** within 5 s of the master's `clock` message (UDP blocked): logged; the
  client is not ready and takes no schedule until it synchronises (after `--sync-wait` it plays
  free-running).

`STATS` with `--sync` adds `sync=<role> sync_passes sync_drops sync_repeats sync_missed sync_offset_ms
sync_offset_ms_min sync_offset_ms_avg` and, on the client, `clock_pings clock_err_us_avg
clock_err_us_max rtt_us_min` (the net clock checked against a ping/pong over the control
connection: master time at the round trip's midpoint vs the client's shared time; samples with a
round trip under twice the minimum). A `sync pass N` line per pass gives the same per pass.

### Protocol

**Discovery** (Avahi, through `avahi-publish -s` / `avahi-browse -p -r -k`, avahi-utils): service
`_micropanel-player._tcp`, name `dvp-<last 3 bytes of eth0's MAC>-<pid>` (unique per run, so a restarted player never collides
with its predecessor's record), port = the control port. TXT:

| key | value |
|---|---|
| `proto` | 1 |
| `role` | `auto` (not decided yet), `master`, `client` |
| `state` | `preparing` (first frame not up), `ready` (first frame up, waiting for the start), `playing` (has a schedule), `free` (client playing free-running) |
| `host` | hostname |
| `item`, `frames`, `fps` | the first item: file name (sanitised), frame count, rate (`60000/1001`) |
| `dhcp` | 1 if the rig's wired port serves DHCP |

The record is re-announced when a value changes, but a peer's running `avahi-browse` keeps the
record it resolved first (TXT changes of a known service are not reported), so `role` and `state`
are a hint: a client connects to a peer announced as `master`, else to the `auto` peer that wins
the auto choice — only a master listens on the control port, a wrong guess is refused and retried.
A record at one of this rig's own addresses and the control port is our own (also an earlier run's
that Avahi has not dropped yet) and is never a peer. The announcer dies with the player
(`PR_SET_PDEATHSIG`); if it ends on its own the player logs why and announces again (1 s, doubling
to 30 s).

**Control**: TCP to the master's port, one JSON object per line, `TCP_NODELAY`.

| from | message | meaning |
|---|---|---|
| client | `{"msg":"hello","name":"dvp-4f638b-1234","proto":1}` | first line after connecting |
| master | `{"msg":"clock","port":8096,"proto":1}` | the clock provider's UDP port (at the master's address) |
| master | `{"msg":"prepare","loop":true,"pass_ns":120003216666,"items":[{"item":"…mp4","kind":"video","frames":7193,"fps":"60000/1001","duration_ns":120003216666}]}` | the master's playlist |
| client | `{"msg":"ready"}` | items match, first frame on screen, clock synchronised |
| client | `{"msg":"refuse","reason":"item 1: the master's … has 7193 frames at 60000/1001 fps, ours … 7000 at …"}` | items differ; the client plays free-running |
| master | `{"msg":"start","t0":<ns>,"pass_ns":<ns>}` | the schedule (master's clock); also sent to every client that connects later |
| client | `{"msg":"ping","t1":<client ns>}` | every second, clock check |
| master | `{"msg":"pong","t1":<ns>,"t2":<master ns>}` | |

**Timeouts**: client connect 3 s, reconnect every 2 s; clock synchronised within 5 s (else logged);
master: clients not all ready 10 s after the first `prepare` → start anyway (late clients join at a
pass start); no client within `--sync-wait` → start alone; a pass that began less than 1 s ago is
joined (frames dropped to catch up), else the next one is waited for.

**State machine** (both roles; the presenter thread on the left, the network side on the right):

```
presenter                          master                          client
---------                          ------                          ------
first frame up, held               serve clock + control;          browse / --sync-peer, connect,
  |                                announce                        hello -> clock, prepare
  v                                  |                              check items -> refuse | ok
ask for the pass start  <----------  all clients ready (or          net clock synchronised
  (waits, EXIT works)                timeouts) -> T0 = now+1.5 s,    + first frame up -> ready
  |                                  start -> clients               start -> schedule
  v
pass n: frames at due times; at the end of the pass ask again (next pass = T0 + n*D)
```

### Tests

`--sync-clock-test` measures the clock without a display. For frame-level checks both players
write `--sync-log`; the same due time is the same frame, and latched(master) − latched(client) is
how far apart the panels show it (join the two CSVs on `due_ns`, compare `latched_ns`). Photos at a
1 ms exposure of a clip with a burned-in frame counter (ffmpeg `drawtext` `%{frame_num}`) show both
counters equal or one apart.
