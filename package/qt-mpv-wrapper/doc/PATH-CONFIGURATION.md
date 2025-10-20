# Path Configuration for qt-mpv-wrapper

## Summary

**qt-mpv-wrapper has NO hardcoded paths** that would prevent deployment to different locations. All paths are configurable via command-line arguments.

## Default Behavior

If no video directory is specified, qt-mpv-wrapper uses a relative path:
- Default: `Videos` (relative to current working directory)

## Configuration via qt-demo-launcher.json

When launched from qt-demo-launcher, all paths are configured via the JSON file:

```json
{
  "apps": [
    {
      "name": "Video Player",
      "executable": "/path/to/qt-mpv-wrapper/src/qt-mpv-wrapper.sh",
      "arguments": [
        "/custom/path/to/Videos",
        "--port",
        "8082"
      ],
      "button_text": "Videos"
    }
  ]
}
```

## Command-Line Arguments

### qt-mpv-wrapper binary

**Usage:**
```bash
qt-mpv-wrapper [video_directory] [--port PORT]
```

**Arguments:**
- `video_directory` - Path to videos folder (absolute or relative)
- `--port PORT` - Network API port (default: 8082)

**Examples:**
```bash
# Use relative path (looks for ./Videos in current directory)
./qt-mpv-wrapper

# Use absolute path
./qt-mpv-wrapper /home/pi/videos

# Use custom path and port
./qt-mpv-wrapper /mnt/usb/media/videos --port 9000

# Use relative path with custom port
./qt-mpv-wrapper ../videos --port 8082
```

### qt-mpv-wrapper.sh wrapper script

**Usage:**
```bash
qt-mpv-wrapper.sh [video_directory] [--port PORT]
```

The wrapper script passes all arguments directly to the binary, so usage is identical.

**Examples:**
```bash
# Default (relative path)
./qt-mpv-wrapper.sh

# Absolute path
./qt-mpv-wrapper.sh /home/user/Videos --port 8082

# Relative path
./qt-mpv-wrapper.sh ../../media/videos
```

## Deployment Examples

### Example 1: Standard Deployment
```json
{
  "executable": "/home/pi/micropanel/qt-apps/qt-mpv-wrapper/src/qt-mpv-wrapper.sh",
  "arguments": ["/home/pi/micropanel/qt-apps/Videos", "--port", "8082"]
}
```

### Example 2: USB Drive
```json
{
  "executable": "/home/pi/apps/qt-mpv-wrapper/src/qt-mpv-wrapper.sh",
  "arguments": ["/mnt/usb/videos", "--port", "8082"]
}
```

### Example 3: Network Share
```json
{
  "executable": "/opt/apps/qt-mpv-wrapper/src/qt-mpv-wrapper.sh",
  "arguments": ["/mnt/nfs/media/videos", "--port", "8082"]
}
```

### Example 4: Relative Path
```json
{
  "executable": "/home/user/apps/qt-mpv-wrapper/src/qt-mpv-wrapper.sh",
  "arguments": ["Videos", "--port", "8082"]
}
```
In this case, videos are looked up relative to the working directory set by qt-demo-launcher.

## Internal Temporary Files

The following temporary files are used and are **not configurable** (always in /tmp):

### /tmp/qt-mpv-playback.txt
- **Purpose:** Stores playback request when Qt app exits
- **Format:** Two lines (video path + loop flag)
- **Lifecycle:** Created by Qt app, read by wrapper script, deleted after use
- **Permissions:** Created with user's default umask

### /tmp/mpv-socket
- **Purpose:** Unix domain socket for mpv IPC
- **Type:** Socket file
- **Lifecycle:** Created by mpv, used by input-monitor, deleted after mpv exits
- **Permissions:** Created by mpv with default permissions

**Note:** These temporary files must be in `/tmp/` because:
1. Standard location for temporary files
2. Guaranteed to be writable by all users
3. Cleaned up automatically on reboot
4. Fast (often tmpfs - in-memory filesystem)

If you need to use a different temp directory, you would need to:
1. Modify `VideoPlayer.cpp` (search for `/tmp/qt-mpv-playback.txt`)
2. Modify `qt-mpv-wrapper.sh` (search for `/tmp/mpv-socket`)
3. Ensure the directory is writable by the running user

## Script-Relative Paths

The wrapper script uses **script-relative paths** for finding binaries:

```bash
# Get script's own directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Find binaries relative to script location
QT_MPV_BINARY="$SCRIPT_DIR/qt-mpv-wrapper"
INPUT_MONITOR="$SCRIPT_DIR/input-monitor"
MPV_INPUT_CONF="$SCRIPT_DIR/mpv-input.conf"
```

This means:
- ✅ No hardcoded paths to binaries
- ✅ Works from any installation directory
- ✅ Can be moved as a unit (copy entire src/ folder)

## Input Device Paths

The input-monitor C program scans `/dev/input/` to find mouse/touch devices:

```c
// These paths are Linux kernel standard and cannot be changed
/dev/input/event*          // Input event devices
/dev/input/by-path/*       // Symlinks by device path
/dev/input/by-id/*         // Symlinks by device ID
/sys/class/input/*/device/name  // Device names
```

These are **Linux kernel standard paths** and are the same on all Linux systems.

## Configuration File Paths

### mpv-input.conf
**Location:** `src/mpv-input.conf` (relative to wrapper script)
**Purpose:** mpv keyboard shortcuts
**Configurable:** Yes, edit the file to change keybindings

**Passed to mpv as:**
```bash
mpv --input-conf=$SCRIPT_DIR/mpv-input.conf ...
```

## Environment Variables

qt-mpv-wrapper does **not** use any environment variables for configuration. All configuration is via command-line arguments.

Optional environment variables used by Qt (standard Qt behavior):
- `QT_QPA_PLATFORM` - Qt platform plugin (set by systemd service)
- `QT_QPA_EGLFS_INTEGRATION` - EGLFS integration (set by systemd service)

These are typically set by the launch environment (systemd service, shell, etc.), not by qt-mpv-wrapper itself.

## Working Directory

qt-mpv-wrapper's behavior depends on working directory only if using **relative paths** for video directory:

```bash
# Working directory: /home/pi/apps
./qt-mpv-wrapper/src/qt-mpv-wrapper.sh Videos
# Looks for videos in: /home/pi/apps/Videos

# Working directory: /mnt/usb
./qt-mpv-wrapper/src/qt-mpv-wrapper.sh Videos
# Looks for videos in: /mnt/usb/Videos
```

**Best practice:** Always use **absolute paths** when launching from qt-demo-launcher to avoid ambiguity.

## Verification

To verify no hardcoded paths exist in your deployment:

```bash
# Search for hardcoded paths in source code
cd /path/to/qt-mpv-wrapper/src
grep -r "/home/pi" . --include="*.cpp" --include="*.h" --include="*.c" --include="*.sh"

# Should only find default value in main.cpp:
# QString videoPath = "Videos"; // Default: relative path

# Check wrapper script
grep "^[^#]*/" qt-mpv-wrapper.sh | grep -v SCRIPT_DIR | grep -v "/tmp/"

# Should show no absolute paths except /tmp/ and $SCRIPT_DIR-based paths
```

## Migration Checklist

When deploying qt-mpv-wrapper to a new location:

- [ ] Copy entire `src/` directory to new location
- [ ] Update `qt-demo-launcher.json` with new paths:
  - [ ] Update `executable` path to point to new `qt-mpv-wrapper.sh` location
  - [ ] Update `arguments[0]` to point to videos directory
- [ ] Ensure videos directory exists and is readable
- [ ] Ensure user is in `input` group for touch/mouse access
- [ ] Test: `./qt-mpv-wrapper.sh /path/to/videos --port 8082`

## Summary of Configurable Paths

| Path Type | Configurable? | How to Configure |
|-----------|---------------|------------------|
| Video directory | ✅ Yes | Command-line arg #1 |
| Network port | ✅ Yes | `--port` argument |
| Executable location | ✅ Yes | Can be anywhere (uses $SCRIPT_DIR) |
| Input monitor | ✅ Yes | Relative to wrapper script |
| mpv-input.conf | ✅ Yes | Relative to wrapper script |
| Temp files | ❌ No | Always `/tmp/` (standard) |
| Input devices | ❌ No | Always `/dev/input/` (kernel) |

**Conclusion:** qt-mpv-wrapper is **fully portable** and has **no hardcoded deployment paths**. All user-configurable paths are passed via command-line arguments, typically from qt-demo-launcher.json.
