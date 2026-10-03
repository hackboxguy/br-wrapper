# usb-media-app

The playlist editor behind qt-demo-launcher's **USB Media** button: pick
images and videos on the USB stick, set the order, the image duration and
Loop, then Play — the playlist plays mirrored on both HDMI outputs with
`dual-video-player --playlist` (`package/micropanel-media-player`).

```
usb-media.sh (launcher child)      loop until Back:
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
- **Autostart on boot**: the `autostart` key is read and kept, the checkbox is
  hidden until phase 2 (`usb-media-autostart.service`) ships.

Screenshots without a display:
`QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software usb-media-app --root DIR --screenshot out.png --window-size 1920x720`.
