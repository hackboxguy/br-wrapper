# qt-mpv-wrapper Components

## Overview

qt-mpv-wrapper consists of multiple components working together to provide video playback on embedded Linux systems without X11/Wayland.

## Component List

### 1. qt-mpv-wrapper (Qt Application)
**File:** `qt-mpv-wrapper` (binary)
**Source:** `main.cpp`, `VideoPlayer.cpp`, `VideoController.cpp`, `NetworkInterface.cpp`, `main.qml`
**Language:** C++ / QML
**Purpose:**
- Video file browser UI
- Network API server (TCP port 8082)
- Saves playback requests and exits to release framebuffer

**Key Features:**
- Lists videos from configured directory
- Responds to network commands (list, play, play-loop, status)
- Writes playback request to `/tmp/qt-mpv-playback.txt`
- Calls `QCoreApplication::quit()` to exit cleanly

### 2. qt-mpv-wrapper.sh (Wrapper Script)
**File:** `qt-mpv-wrapper.sh`
**Language:** Bash
**Purpose:**
- Manages Qt app ↔ mpv lifecycle
- Launches mpv after Qt exits
- Restarts Qt after mpv finishes

**Workflow:**
```
Loop:
  1. Launch qt-mpv-wrapper (Qt app)
  2. Wait for Qt to exit
  3. If /tmp/qt-mpv-playback.txt exists:
     a. Read video path and loop flag
     b. Launch mpv with IPC socket
     c. Launch input-monitor in background
     d. Wait for mpv to finish
     e. Kill input-monitor
     f. Return to step 1
  4. Else: Exit (user quit without playing)
```

### 3. input-monitor (C Program)
**File:** `input-monitor` (binary)
**Source:** `input-monitor.c`
**Language:** C
**Purpose:**
- Monitors `/dev/input/eventX` for mouse clicks and touchscreen taps
- Sends quit command to mpv via IPC socket

**How It Works:**
1. Finds mouse or touch device in `/dev/input/`
2. Opens device file for reading
3. Reads `input_event` structures (24 bytes each)
4. Filters for `EV_KEY` events (button presses):
   - `BTN_LEFT` (272) - Left mouse button
   - `BTN_RIGHT` (273) - Right mouse button
   - `BTN_MIDDLE` (274) - Middle mouse button
   - `BTN_TOUCH` (330) - Touchscreen tap
5. Ignores `EV_REL` (mouse movement) and `EV_ABS` (position) events
6. On button press: Connects to mpv IPC socket and sends `{"command":["quit"]}`

**Advantages:**
- Native C code - very fast and lightweight (~10KB)
- No Python dependency
- Precise event filtering (clicks only, not movement)
- Uses standard Linux input API

### 4. mpv (Video Player)
**Binary:** `/usr/bin/mpv`
**Language:** C
**Purpose:** Actual video playback with GPU acceleration

**Invocation:**
```bash
mpv --vo=gpu \
    --gpu-context=drm \
    --fullscreen \
    --really-quiet \
    --msg-level=all=error \
    --input-ipc-server=/tmp/mpv-socket \
    --input-conf=mpv-input.conf \
    [--loop] \
    /path/to/video.mp4
```

**Configuration Files:**
- `mpv-input.conf` - Keyboard shortcuts (q=quit, Space=pause, etc.)

### 5. Supporting Files

#### mpv-input.conf
**Purpose:** mpv keyboard shortcuts
**Contents:**
```
q quit
ESC quit
SPACE cycle pause
LEFT seek -5
RIGHT seek +5
# etc.
```

#### rebuild.sh
**Purpose:** Quick rebuild script
**Actions:**
- Runs `qmake`
- Runs `make` (builds both Qt app and input-monitor)
- Shows usage instructions

## Build Process

The build is orchestrated by the Qt `.pro` file with a custom target:

**qt-mpv-wrapper.pro:**
```qmake
# Qt application (built by qmake/make normally)
SOURCES += main.cpp VideoPlayer.cpp ...

# Custom target for C program
input_monitor.target = input-monitor
input_monitor.commands = gcc -o input-monitor input-monitor.c -Wall -O2
input_monitor.depends = input-monitor.c
QMAKE_EXTRA_TARGETS += input_monitor
PRE_TARGETDEPS += input-monitor
```

**Build command:**
```bash
qmake && make
```

**Output:**
- `qt-mpv-wrapper` - Qt binary
- `input-monitor` - C binary

## File Protocol

### /tmp/qt-mpv-playback.txt
**Format:**
```
/full/path/to/video.mp4
true|false
```
- Line 1: Absolute path to video file
- Line 2: Loop flag (`true` or `false`)

**Lifecycle:**
1. Created by `VideoPlayer::startMpvDelayed()` when user selects video
2. Read by `qt-mpv-wrapper.sh` after Qt exits
3. Deleted by wrapper script after reading

### /tmp/mpv-socket
**Type:** Unix domain socket
**Purpose:** IPC communication with mpv
**Protocol:** JSON commands

**Example commands:**
```json
{"command": ["quit"]}
{"command": ["set_property", "pause", true]}
{"command": ["get_property", "time-pos"]}
```

**Used by:**
- `input-monitor` - Sends quit command on button press
- Can be used by external tools for advanced control

## Component Dependencies

```
qt-mpv-wrapper.sh
  ├─> qt-mpv-wrapper (Qt binary)
  │     └─> Network API (port 8082)
  │
  ├─> mpv (video player)
  │     ├─> --input-ipc-server=/tmp/mpv-socket
  │     └─> --input-conf=mpv-input.conf
  │
  └─> input-monitor (C binary)
        └─> Sends commands to /tmp/mpv-socket
```

## Runtime Flow

### User Selects Video in File Browser

```
1. User clicks video filename in Qt UI
2. VideoPlayer::play() called
3. Saves to /tmp/qt-mpv-playback.txt:
   /home/pi/.../video.mp4
   false
4. QCoreApplication::quit() - Qt exits
5. qt-mpv-wrapper.sh detects playback file
6. Wrapper launches mpv in background
7. Wrapper launches input-monitor in background
8. mpv plays video with full framebuffer access
```

### User Clicks During Playback

```
1. User clicks mouse
2. /dev/input/event5 receives EV_KEY event (code=272, value=1)
3. input-monitor reads event
4. input-monitor filters: EV_KEY + BTN_LEFT detected
5. input-monitor connects to /tmp/mpv-socket
6. input-monitor sends: {"command":["quit"]}
7. mpv receives command and exits gracefully
8. Wrapper script kills input-monitor
9. Wrapper script restarts qt-mpv-wrapper
10. File browser appears again
```

### Network API Command

```
1. External client: echo "play video.mp4" | nc localhost 8082
2. NetworkInterface receives command
3. VideoController::handleNetworkCommand() called
4. VideoController finds video file
5. VideoController calls VideoPlayer::play()
6. VideoPlayer saves to /tmp/qt-mpv-playback.txt
7. VideoPlayer calls QCoreApplication::quit()
8. TCP response: "OK: Playing video.mp4"
9. Qt exits, wrapper launches mpv (same as above)
```

## Permissions

### Input Device Access

The `input-monitor` needs read access to `/dev/input/eventX` devices.

**Solution:** Add user to `input` group:
```bash
sudo usermod -a -G input pi
# Then logout and login
```

**Check permissions:**
```bash
ls -l /dev/input/event*
# Should show: crw-rw---- 1 root input ...
```

**Test access:**
```bash
groups
# Should include: pi adm dialout cdrom sudo audio video plugdev games users input ...
```

## Troubleshooting

### input-monitor exits immediately
**Check:** User in `input` group? Run `groups` to verify.

### Mouse movement quits video
**Check:** Using `input-monitor` (C program)? Not shell script?
**Verify:** `ls -lh input-monitor` should show executable

### mpv doesn't quit on click
**Check:**
1. `socat` installed? `which socat`
2. IPC socket exists? `ls -l /tmp/mpv-socket`
3. input-monitor running? `ps aux | grep input-monitor`

### Qt app doesn't restart after video
**Check:**
1. `/tmp/qt-mpv-playback.txt` cleaned up?
2. Wrapper script while loop logic
3. mpv exit code (should be 0 for normal quit)

## Performance

**Startup:**
- Qt app launch: ~1-2 seconds
- mpv launch: ~500-1000ms
- input-monitor launch: <50ms

**Memory Usage:**
- qt-mpv-wrapper: ~25MB RAM
- mpv (during playback): ~30-50MB RAM
- input-monitor: <1MB RAM

**Binary Sizes:**
- qt-mpv-wrapper: ~150KB (stripped)
- input-monitor: ~10KB (stripped)

## Security Considerations

### Input Device Access
- Requires `input` group membership
- Can read all mouse/keyboard events (not just for mpv)
- Consider running in isolated user session for kiosk mode

### Network API
- No authentication
- Anyone on network can control playback
- Use firewall to restrict to localhost if needed:
  ```bash
  sudo iptables -A INPUT -p tcp --dport 8082 -s 127.0.0.1 -j ACCEPT
  sudo iptables -A INPUT -p tcp --dport 8082 -j DROP
  ```

### IPC Socket
- Unix domain socket in `/tmp/`
- World-readable but requires connection
- mpv accepts commands without authentication
- Consider setting socket permissions if needed

## Future Enhancements

### Possible Improvements

1. **Gesture Support**
   - Detect swipe gestures for seek forward/backward
   - Pinch to zoom (if using touchscreen)
   - Two-finger tap for pause

2. **On-Screen Display**
   - Show "Tap to quit" message briefly
   - Display playback position overlay
   - Show volume level when adjusting

3. **Multi-touch**
   - Support multiple simultaneous touch points
   - Different gestures for different actions

4. **Hardware Button Support**
   - Monitor GPIO pins for physical buttons
   - Alternative to touchscreen for headless operation

5. **Status Proxy**
   - Keep network API alive during mpv playback
   - Forward status queries to mpv IPC socket
   - Allow pause/seek via network during playback
