# usb-media-app

The playlist editor behind qt-demo-launcher's **USB Media** button: pick
images and videos on the USB stick, set the order, the image duration and
Loop, then Play — the playlist plays mirrored on both HDMI outputs with
`dual-video-player --playlist` (`package/micropanel-media-player`).

```
usb-media.sh (launcher child)      [--autostart: countdown -> play, retried while unattended]
                                   loop until Back:
  usb-media-app --root <stick> --player <dual-video-player> --temp-playlist /tmp/usb-media-playlist.json [--message TEXT]
      exit 10 = Play, exit 0 = Back
  dual-video-player --playlist <stick>/micropanel-playlist.json        (or the temp copy + --root on a read-only stick)
      exit 0 done/EXIT, 1 nothing playable, 3 stick removed -> back to the app with that message
```

- **Scan**: stick root + 3 folder levels; names starting with `.`/`$`,
  `LOST.DIR`, `System Volume Information` skipped. Videos `mp4 mkv mov m4v`,
  images `jpg jpeg png`; HEIC, AVI, WebM, GIF, ... are listed as unsupported.
- **Classification**: `dual-video-player --probe` (parse only, no decoder) in
  batches of 16; unsupported files are greyed with the reason, software-decoded
  ones say "may play slowly", rotated videos "plays unrotated".
- **Playlist**: `micropanel-playlist.json` in the stick root (format in
  `docs/dual-video-player-extension-plan.md`, "Interfaces"); unknown keys are
  kept on save. Saving writes a temp file, fsyncs, renames and fsyncs the
  directory, so the stick can be pulled right after.
- **Read-only stick**: Save is disabled; Play writes the playlist to
  `--temp-playlist` and the wrapper passes `--root=<stick>` to the player.
- **Classification cache**: `--probe-cache` (default `/tmp/usb-media-probe.cache`,
  RAM, per boot) keeps each file's probe result keyed by path + size + mtime,
  so coming back from playback does not classify the stick again.
- **A playlist item listed twice** (hand-written files) is shown once; saving
  from the app writes it once.
- **Autostart on boot**: the checkbox stores `"autostart": true` in the stick's
  playlist (the unit stores nothing). At boot `usb-media-autostart.service`
  (`usb-media-autostart.sh`: waits for the launcher's API and up to 15 s for
  the stick) asks the launcher to start the hidden `usb-media-autostart` entry
  (`usb-media.sh --autostart`): `usb-media-app --countdown 5` ("Starting the
  playlist 5... tap anywhere to cancel"; exit 10 = play, 0 = cancelled into the
  app, this boot only), then playback. Until someone touches the unit, a
  playback that ends with an error is retried 3 times, 5 s apart; EXIT (exit 0)
  and a removed stick (exit 3) return to the app as usual.

Screenshots without a display:
`QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software usb-media-app --root DIR --screenshot out.png --window-size 1920x720`.
