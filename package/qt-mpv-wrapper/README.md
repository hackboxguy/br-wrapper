# Qt MPV Wrapper - Touch-Optimized Video Player

A Qt Quick/QML-based video player wrapper for mpv with touch-optimized controls.

## Features

- **Video File Browser** - Browse and select videos from directory
- **Click/Tap to Quit** - Any mouse click or touchscreen tap stops video playback
- **Keyboard Shortcuts** - Full mpv keyboard control (q, Space, arrows, etc.)
- **Loop Mode** - Toggle video looping via network API
- **Network API** - Remote control via TCP (port 8082)
- **Auto-restart** - Returns to file browser after video ends
- **Supported Formats** - MP4, MKV, AVI, WebM, MOV

## Dependencies

### Build Dependencies
- Qt5 Quick/QML development packages
- gcc compiler
- qmake
- socat (for mpv IPC communication)

### Runtime Dependencies
- Qt5 Quick/QML runtime
- mpv (video player)
- socat

## Building

### On Development Machine

```bash
cd ~/git-repos/br-wrapper/package/qt-mpv-wrapper/src

# Build
qmake qt-mpv-wrapper.pro
make

# Result: ./qt-mpv-wrapper
```

### On Target (Raspberry Pi OS Lite)

```bash
# Install runtime dependencies
sudo apt install -y mpv socat

# Qt runtime already installed (same as qt-demo-launcher/touch-gallery)

# Build the application
cd ~/micropanel/qt-apps/qt-mpv-wrapper/src
./rebuild.sh

# This builds:
# - qt-mpv-wrapper (Qt application)
# - input-monitor (C input event handler)
```

## Usage

**IMPORTANT:** Always use the wrapper script `qt-mpv-wrapper.sh` instead of running the binary directly. The wrapper script manages the Qt app ↔ mpv lifecycle to avoid framebuffer conflicts.

See [ARCHITECTURE.md](ARCHITECTURE.md) for technical details about why this is necessary.

```bash
# Default video directory and port
./qt-mpv-wrapper.sh

# Custom video directory
./qt-mpv-wrapper.sh /path/to/videos

# Custom network port
./qt-mpv-wrapper.sh /path/to/videos --port 9000

# Default directory with custom port
./qt-mpv-wrapper.sh --port 9000
```

**Default Settings:**
- Video directory: `/home/pi/micropanel/qt-apps/Videos`
- Network port: `8082`

### How It Works

1. **qt-mpv-wrapper.sh** launches the Qt file browser
2. When you select a video, Qt app **exits** and saves the request to `/tmp/qt-mpv-playback.txt`
3. Wrapper script launches **mpv** with exclusive framebuffer access
4. After video finishes, wrapper script **restarts** Qt app
5. You're back at the file browser

This architecture ensures mpv gets full GPU access without framebuffer conflicts.

## Controls

### File Browser
- **Tap video** - Play video
- **EXIT button** - Return to qt-demo-launcher

### During Playback

**Note:** Overlay controls are not available during playback due to the exit-restart architecture. Use mpv keyboard shortcuts:

- **q** - Quit video and return to file browser
- **Space** - Pause/Resume
- **Left/Right** - Seek backward/forward 5 seconds
- **Up/Down** - Seek backward/forward 60 seconds
- **9/0** - Volume down/up
- **f** - Toggle fullscreen (already fullscreen by default)

See [ARCHITECTURE.md](ARCHITECTURE.md) section "Limitations" for details.

## Network API

qt-mpv-wrapper provides a TCP-based API for remote control (default port: 8082).

### Available Commands

#### `list`
List all video files in the video directory.

```bash
echo "list" | nc -q 0 localhost 8082
# Returns: video1.mp4\nvideo2.mkv\nvideo3.avi
```

#### `play <filename>`
Play a specific video file.

```bash
echo "play video1.mp4" | nc -q 0 localhost 8082
# Returns: OK: Playing video1.mp4
```

#### `play-loop <filename>`
Play a video file with looping enabled.

```bash
echo "play-loop video2.mkv" | nc -q 0 localhost 8082
# Returns: OK: Playing (loop) video2.mkv
```

#### `stop`
Stop current playback.

```bash
echo "stop" | nc -q 0 localhost 8082
# Returns: OK: Stopped
```

#### `status` or `get-status`
Get current playback status.

```bash
echo "status" | nc -q 0 localhost 8082
# Returns: playing  (or: stopped)
```

#### `help`
Show available commands.

```bash
echo "help" | nc -q 0 localhost 8082
```

### Error Responses

- `ERROR: Video not found: <filename>` - Video file doesn't exist
- `ERROR: Not playing` - Tried to stop when nothing is playing
- `ERROR: Unknown command` - Invalid command
- `No videos found` - Video directory is empty

### Workflow Example

```bash
# 1. Launch qt-mpv-wrapper via qt-demo-launcher
echo "start-app video-player" | nc -q 0 localhost 8081

# 2. Wait a moment for it to start
sleep 1

# 3. List available videos
echo "list" | nc -q 0 localhost 8082

# 4. Play a specific video
echo "play myvideo.mp4" | nc -q 0 localhost 8082

# 5. Check status
echo "status" | nc -q 0 localhost 8082

# 6. Stop playback
echo "stop" | nc -q 0 localhost 8082
```

## Integration with qt-demo-launcher

Add to `qt-demo-launcher.json`:

```json
{
  "id": "video-player",
  "enabled": true,
  "text": "Video Player",
  "program": "/home/pi/micropanel/qt-apps/qt-mpv-wrapper",
  "arguments": ["/home/pi/micropanel/qt-apps/Videos", "--port", "8082"],
  "working_directory": "/home/pi/micropanel/qt-apps",
  "background_color": "#9B59B6",
  "hover_color": "#8E44AD"
}
```

## Directory Structure

```
/home/pi/micropanel/qt-apps/
├── qt-mpv-wrapper          # Binary
└── Videos/                 # Video files
    ├── video1.mp4
    ├── video2.mkv
    └── ...
```

## mpv Configuration

Default mpv options used:
- `--fullscreen` - Full screen playback
- `--no-terminal` - No terminal output
- `--really-quiet` - Minimal logging
- `--osd-level=1` - Show OSD messages
- `--loop` - Loop video (when loop enabled)

## Architecture

```
qt-mpv-wrapper (Qt Quick app)
├── File Browser (QML ListView + FolderListModel)
├── VideoPlayer (C++ QProcess wrapper)
│   └── Launches /usr/bin/mpv
└── Overlay Controls (QML, tap to show/hide)
    ├── Video filename
    ├── Loop toggle
    └── STOP button
```

## Troubleshooting

### mpv not found
```bash
# Install mpv
sudo apt install -y mpv

# Verify installation
which mpv
# Should show: /usr/bin/mpv
```

### No videos found
- Check video directory exists: `/home/pi/micropanel/qt-apps/Videos`
- Check file extensions: `.mp4`, `.mkv`, `.avi`, `.webm`, `.mov`
- Check file permissions (readable by user `pi`)

### Video plays but no controls
- Tap center of screen to show controls
- Controls auto-hide after 5 seconds
- Check logs: `journalctl -u qt-demo-launcher.service -f`

### Black screen
- mpv may be using wrong video output
- Check Qt platform plugin matches (linuxfb/eglfs)
- Test mpv manually: `mpv --fullscreen /path/to/video.mp4`

## File Formats

Supported video formats (tested):
- MP4 (H.264/H.265)
- MKV (Matroska)
- AVI
- WebM
- MOV

Audio formats (depends on mpv):
- AAC, MP3, Opus, Vorbis, etc.

## Performance

- Uses GPU hardware decoding when available (via mpv)
- Minimal CPU usage during playback
- Touch controls have negligible overhead
