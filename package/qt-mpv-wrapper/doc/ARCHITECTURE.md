# qt-mpv-wrapper Architecture

## Overview

qt-mpv-wrapper is a Qt-based video player that provides a file browser UI and uses mpv for video playback. Due to framebuffer constraints on embedded Linux systems (EGLFS/LinuxFB), the application uses a unique architecture where it exits before launching mpv, then restarts after playback finishes.

## The Framebuffer Problem

On embedded Linux systems without X11:
- Qt platform plugins (EGLFS, LinuxFB) take **exclusive ownership** of the GPU/framebuffer
- Only one process can own the framebuffer at a time
- When Qt owns the framebuffer, mpv cannot initialize any video output (`--vo=gpu`, `--vo=drm`, etc.)
- Even hiding Qt windows doesn't release framebuffer resources
- mpv exits with error code 2 when it cannot initialize video output

## Solution: Exit-Restart Architecture

Instead of trying to run Qt and mpv simultaneously, qt-mpv-wrapper:

1. **Runs the Qt file browser** - User selects a video
2. **Saves playback request** to `/tmp/qt-mpv-playback.txt`
3. **Exits completely** - Releases framebuffer
4. **Wrapper script launches mpv** - mpv gets exclusive framebuffer access
5. **mpv plays video** - Full GPU acceleration available
6. **mpv exits when done** - Releases framebuffer
7. **Wrapper script restarts Qt app** - Returns to file browser
8. **Loop repeats** - User can select another video

## Components

### 1. qt-mpv-wrapper (Qt Application)

**Location:** `src/qt-mpv-wrapper` (binary)

**Purpose:** Provides the file browser UI and network API

**Key behaviors:**
- Lists video files from configured directory
- Responds to network API commands (play, play-loop, stop, list, status, help)
- When user/API requests playback:
  - Writes video path and loop flag to `/tmp/qt-mpv-playback.txt`
  - Calls `QCoreApplication::quit()` to exit
- Does **NOT** launch mpv directly

**Source files:**
- [main.cpp](src/main.cpp) - Application entry point, command-line parsing
- [VideoPlayer.cpp](src/VideoPlayer.cpp) - Saves playback request and exits
- [VideoController.cpp](src/VideoController.cpp) - Network API handler
- [NetworkInterface.cpp](src/NetworkInterface.cpp) - TCP server (port 8082)
- [main.qml](src/main.qml) - File browser UI with overlay buttons

### 2. qt-mpv-wrapper.sh (Wrapper Script)

**Location:** `src/qt-mpv-wrapper.sh`

**Purpose:** Manages the Qt app ↔ mpv lifecycle

**Workflow:**
```bash
while true; do
    # 1. Launch Qt app
    ./qt-mpv-wrapper "${ARGS[@]}"

    # 2. Qt app exits - check for playback request
    if [ -f /tmp/qt-mpv-playback.txt ]; then
        # 3. Read video path and loop flag
        VIDEO_PATH=$(sed -n '1p' /tmp/qt-mpv-playback.txt)
        LOOP_FLAG=$(sed -n '2p' /tmp/qt-mpv-playback.txt)

        # 4. Launch mpv with full framebuffer access
        mpv --vo=gpu --gpu-context=drm --fullscreen "${VIDEO_PATH}"

        # 5. mpv exits, loop restarts Qt app
    else
        # No playback request - user quit, exit loop
        break
    fi
done
```

**Key features:**
- Passes all command-line arguments to qt-mpv-wrapper
- Removes playback file before launching Qt app (prevents stale requests)
- Removes playback file after reading (single-use)
- Exits cleanly when user quits without playing video

### 3. mpv (Video Player)

**Binary:** `/usr/bin/mpv`

**Purpose:** Actual video playback with GPU acceleration

**Invocation:**
```bash
mpv --vo=gpu --gpu-context=drm --fullscreen [--loop] /path/to/video.mp4
```

**Key behaviors:**
- Gets exclusive framebuffer access (Qt has exited)
- Uses GPU acceleration (`--vo=gpu --gpu-context=drm`)
- Exits when video finishes or user presses 'q'
- No interaction with Qt application (completely independent)

## File Protocol: /tmp/qt-mpv-playback.txt

**Format:**
```
/full/path/to/video.mp4
true|false
```

**Line 1:** Absolute path to video file
**Line 2:** Loop flag (`true` or `false`)

**Lifecycle:**
1. Removed by wrapper script before launching Qt app
2. Created by `VideoPlayer::startMpvDelayed()` when playback is requested
3. Read by wrapper script after Qt app exits
4. Removed by wrapper script after reading

**Why temporary file?**
- Qt application cannot pass data to wrapper script via exit code
- Environment variables don't persist across process boundaries
- Named pipes/FIFOs would require more complex synchronization
- Simple file is atomic and reliable

## Network API Flow

When external client sends `play video.mp4`:

1. **NetworkInterface** receives TCP command on port 8082
2. **VideoController** parses command and validates video file
3. **VideoController** calls `videoPlayer->play(videoPath, loop)`
4. **VideoPlayer** saves request to `/tmp/qt-mpv-playback.txt`
5. **VideoPlayer** calls `QCoreApplication::quit()`
6. **Qt app exits** - TCP connection closes with "OK: Playing video.mp4"
7. **Wrapper script** detects playback file, launches mpv
8. **mpv plays video**
9. **mpv exits**, wrapper script restarts Qt app
10. **Qt app starts**, network API available again

**Important:** Network API client sees immediate "OK" response, but playback starts after Qt app restarts. There's a ~500ms transition time.

## Deployment

### Standalone Execution (Recommended)

```bash
cd /home/pi/micropanel/qt-apps
./qt-mpv-wrapper.sh /home/pi/micropanel/qt-apps/Videos --port 8082
```

### Systemd Service

**File:** `/etc/systemd/system/qt-mpv-wrapper.service`

```ini
[Unit]
Description=Qt MPV Video Player Wrapper
After=network.target

[Service]
Type=simple
User=pi
Environment="QT_QPA_PLATFORM=eglfs"
Environment="QT_QPA_EGLFS_INTEGRATION=eglfs_kms"
WorkingDirectory=/home/pi/micropanel/qt-apps
ExecStart=/home/pi/micropanel/qt-apps/qt-mpv-wrapper.sh /home/pi/micropanel/qt-apps/Videos --port 8082
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

**Note:** Use the wrapper script as `ExecStart`, not the binary directly.

### Integration with qt-demo-launcher

**File:** `/home/pi/micropanel/qt-apps/qt-demo-launcher.json`

```json
{
  "name": "Video Player",
  "executable": "/home/pi/micropanel/qt-apps/qt-mpv-wrapper.sh",
  "arguments": [
    "/home/pi/micropanel/qt-apps/Videos",
    "--port",
    "8082"
  ],
  "button_text": "Videos"
}
```

**Important:** Use `qt-mpv-wrapper.sh` as the executable, not `qt-mpv-wrapper` binary.

## Timing and Synchronization

### Startup Time
- Qt app launch: ~1-2 seconds
- Network API ready: Immediately after UI appears
- Total time: ~2 seconds

### Playback Transition
1. User clicks video or API sends `play` command: **0ms**
2. VideoPlayer saves file and exits: **~100ms**
3. Wrapper script detects file and launches mpv: **~200ms**
4. mpv initializes and starts playback: **~500-1000ms**
5. **Total transition: ~800-1300ms**

### Return to File Browser
1. mpv exits (video finished or user presses 'q'): **0ms**
2. Wrapper script detects exit: **~50ms**
3. Wrapper script restarts Qt app: **~1-2 seconds**
4. UI appears: **~2 seconds**

## Limitations

### 1. No Real-Time Status During Playback
- While mpv is playing, Qt app is not running
- Network API is unavailable during playback
- Cannot query playback progress or position
- Cannot send commands to mpv

**Workaround:** Use mpv's own IPC interface (`--input-ipc-server=/tmp/mpv-socket`)

### 2. Transition Delay
- ~1 second delay between clicking video and playback starting
- ~2 second delay returning to file browser

**Mitigation:** Acceptable for kiosk/demo use cases

### 3. No Overlay Controls During Playback
- Qt overlay buttons (STOP, LOOP, EXIT) only visible in file browser
- During playback, use mpv keyboard shortcuts:
  - `q` - Quit and return to file browser
  - `Space` - Pause/Resume
  - `Left/Right` - Seek -/+ 5 seconds
  - `Up/Down` - Seek -/+ 60 seconds

**Alternative:** Use external GPIO buttons or touchscreen events to send signals to mpv

### 4. Loop Mode Limitation
- Loop mode set before playback starts (saved to playback file)
- Cannot toggle loop during playback via network API
- Must stop and restart video with `play-loop` command

## Advantages

### 1. Full GPU Acceleration
- mpv gets exclusive framebuffer access
- No framebuffer conflicts
- Best possible video performance

### 2. Clean Separation
- Qt handles UI only
- mpv handles video only
- Each optimized for its task

### 3. Reliability
- No complex inter-process communication
- No risk of deadlocks or race conditions
- Simple file-based protocol

### 4. Flexibility
- Can easily replace mpv with another player
- Can modify wrapper script for custom workflows
- Can run Qt app and mpv manually for debugging

## Debugging

### Check Playback File
```bash
# Watch for playback requests
watch -n 0.5 cat /tmp/qt-mpv-playback.txt
```

### Run Components Manually

**1. Test Qt app alone:**
```bash
./qt-mpv-wrapper /home/pi/micropanel/qt-apps/Videos --port 8082
# Click video - app should exit and create /tmp/qt-mpv-playback.txt
```

**2. Test mpv alone:**
```bash
cat /tmp/qt-mpv-playback.txt
# Copy video path from line 1
mpv --vo=gpu --gpu-context=drm --fullscreen /path/to/video.mp4
```

**3. Test wrapper script:**
```bash
./qt-mpv-wrapper.sh /home/pi/micropanel/qt-apps/Videos --port 8082
# Full workflow with automatic restart
```

### Common Issues

**mpv doesn't start after clicking video:**
- Check `/tmp/qt-mpv-playback.txt` was created
- Check wrapper script is executable: `chmod +x qt-mpv-wrapper.sh`
- Run wrapper script manually to see errors

**Qt app doesn't restart after mpv exits:**
- Check wrapper script while loop logic
- Ensure playback file is removed after reading
- Check for mpv crash (non-zero exit code)

**Network API not responding:**
- Qt app must be running (not during mpv playback)
- Check port: `netstat -tulpn | grep 8082`
- Check firewall: `sudo iptables -L`

## Future Enhancements

### Possible Improvements

1. **mpv IPC Integration**
   - Wrapper script could monitor mpv IPC socket
   - Provide status proxy while mpv is running
   - Forward commands to mpv

2. **Playlist Support**
   - Save multiple videos to playback file
   - Wrapper script plays them sequentially
   - Automatic transition between videos

3. **Progress Persistence**
   - Save playback position when mpv exits
   - Resume from saved position on next play

4. **Thumbnail Generation**
   - Pre-generate video thumbnails
   - Show in file browser for better UX

5. **Splash Screen**
   - Show "Loading video..." message during transition
   - Custom image displayed during mpv startup

## Comparison to Alternative Approaches

### Approach 1: Embedded libmpv (Not Used)
**Concept:** Link mpv library into Qt app, render to Qt surface

**Pros:**
- Single process
- No transition delay
- Full control from Qt

**Cons:**
- Complex integration
- Must use OpenGL rendering in Qt
- May still have framebuffer conflicts on EGLFS/LinuxFB
- Larger binary size

**Why not used:** Too complex for simple video playback needs

### Approach 2: X11/Wayland Compositor (Not Used)
**Concept:** Run X11 or Wayland, use proper window management

**Pros:**
- Multiple apps can render simultaneously
- Standard desktop environment

**Cons:**
- Huge overhead (~500MB RAM, ~1GB disk)
- Much slower on embedded systems
- Defeats purpose of lightweight EGLFS/LinuxFB setup

**Why not used:** User specifically wants OS Lite without desktop environment

### Approach 3: framebuffer Switching (Not Used)
**Concept:** Switch virtual terminals, run Qt on VT1 and mpv on VT2

**Pros:**
- Both apps stay running
- Faster switching

**Cons:**
- Requires VT setup and permissions
- Screen corruption possible
- Complex state management

**Why not used:** Exit-restart is simpler and more reliable

### Approach 4: Exit-Restart (SELECTED)
**Concept:** Qt app exits, mpv runs, Qt app restarts

**Pros:**
- Simple and reliable
- Clean separation of concerns
- Exclusive framebuffer access guaranteed
- Easy to debug

**Cons:**
- Transition delay (~1 second)
- No status during playback

**Why selected:** Best balance of simplicity and reliability for embedded use case
