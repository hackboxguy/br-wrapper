# Testing Guide for qt-mpv-wrapper

## Prerequisites

On Raspberry Pi:
```bash
# Ensure mpv is installed
sudo apt install -y mpv

# Ensure Qt runtime is available (should already be installed if qt-demo-launcher works)
# Qt5 Quick modules should already be installed from previous setup
```

## Build on Raspberry Pi

```bash
# Navigate to source directory
cd ~/micropanel/qt-apps/qt-mpv-wrapper/src

# Run rebuild script
./rebuild.sh

# Or manually:
qmake
make -j4
```

## Test 1: Verify Build

```bash
# Check binary exists
ls -lh qt-mpv-wrapper
# Should show executable with recent timestamp

# Check wrapper script is executable
ls -lh qt-mpv-wrapper.sh
# Should show executable permissions (rwxr-xr-x)
```

## Test 2: Verify Video Directory

```bash
# Create video directory if it doesn't exist
mkdir -p /home/pi/micropanel/qt-apps/Videos

# Add test videos
# Copy some .mp4 or .mkv files to this directory
# For testing, you can use any video file

# Verify videos are present
ls -lh /home/pi/micropanel/qt-apps/Videos/
```

## Test 3: Run with Wrapper Script

```bash
# Make sure you're running from the src directory
cd /home/pi/micropanel/qt-apps/qt-mpv-wrapper/src

# Run with wrapper script
./qt-mpv-wrapper.sh /home/pi/micropanel/qt-apps/Videos --port 8082
```

**Expected behavior:**
1. Qt file browser window appears
2. Video list shows .mp4, .mkv, .avi, .webm, .mov files
3. EXIT button visible at top-right

## Test 4: File Browser UI

**Actions:**
- Scroll through video list (if more than fits on screen)
- Tap EXIT button - should quit cleanly

**Expected:**
- List displays video filenames
- Scrolling works smoothly
- EXIT button quits application
- Wrapper script exits (no restart since no playback request)

## Test 5: Video Playback

```bash
# Run wrapper script again
./qt-mpv-wrapper.sh /home/pi/micropanel/qt-apps/Videos --port 8082
```

**Actions:**
1. Tap on a video in the list
2. Watch for transition
3. Video should start playing
4. Press 'q' to quit video

**Expected behavior:**
1. You tap video filename
2. Qt window disappears (~100ms)
3. Screen goes black briefly (~500ms)
4. mpv starts playing video
5. Video plays in fullscreen
6. You press 'q'
7. mpv exits
8. Qt file browser reappears (~2 seconds)

**Terminal output (on SSH session):**
```
Preparing to play video: "/home/pi/micropanel/qt-apps/Videos/test.mp4" Loop: false
Saving playback request and exiting Qt app: "/home/pi/micropanel/qt-apps/Videos/test.mp4" Loop: false
Playback request saved, exiting application...
Launching mpv: --vo=gpu --gpu-context=drm --fullscreen /home/pi/micropanel/qt-apps/Videos/test.mp4
[mpv plays video]
mpv exited, restarting qt-mpv-wrapper...
[Qt app restarts]
```

## Test 6: Playback File Mechanism

```bash
# In one terminal, watch the playback file
watch -n 0.5 cat /tmp/qt-mpv-playback.txt

# In another terminal, run qt-mpv-wrapper.sh
# Click a video and watch the file appear then disappear
```

**Expected:**
- File doesn't exist initially
- File appears when you click video (shows path and loop flag)
- File disappears when wrapper script reads it
- File doesn't exist after mpv exits

## Test 7: Network API - List Videos

```bash
# In terminal 1: Run qt-mpv-wrapper
./qt-mpv-wrapper.sh /home/pi/micropanel/qt-apps/Videos --port 8082

# In terminal 2: Test list command
echo "list" | nc -q 0 localhost 8082
```

**Expected response:**
```
video1.mp4
video2.mkv
video3.avi
```
(or "No videos found" if directory is empty)

## Test 8: Network API - Play Video

```bash
# Terminal 1: qt-mpv-wrapper running
./qt-mpv-wrapper.sh /home/pi/micropanel/qt-apps/Videos --port 8082

# Terminal 2: Send play command (use actual filename from your Videos directory)
echo "play test.mp4" | nc -q 0 localhost 8082
```

**Expected:**
1. Response: `OK: Playing test.mp4`
2. TCP connection closes
3. Qt window disappears
4. mpv starts playing video
5. Video plays
6. Press 'q' to quit
7. Qt app restarts
8. Network API available again

## Test 9: Network API - Play with Loop

```bash
# Send play-loop command
echo "play-loop test.mp4" | nc -q 0 localhost 8082
```

**Expected:**
- Response: `OK: Playing (loop) test.mp4`
- Video plays and loops infinitely
- Press 'q' to quit and return to file browser

## Test 10: Network API - Status Check

```bash
# While Qt app is showing file browser (not playing video)
echo "status" | nc -q 0 localhost 8082
# Expected: "stopped"

# NOTE: Cannot check status while mpv is playing because Qt app has exited
```

## Test 11: Network API - Help

```bash
echo "help" | nc -q 0 localhost 8082
```

**Expected response:**
```
Available commands:
  list - List all video files
  play <filename> - Play video
  play-loop <filename> - Play video with looping
  stop - Stop playback
  status - Get playback status
  help - Show this help
```

## Test 12: Error Handling - Invalid Video

```bash
echo "play nonexistent.mp4" | nc -q 0 localhost 8082
```

**Expected response:**
```
ERROR: Video not found: nonexistent.mp4
```

(App should NOT exit, stays at file browser)

## Test 13: Case-Insensitive Filenames

```bash
# If you have TEST.MP4, try:
echo "play test.mp4" | nc -q 0 localhost 8082
# Should work (case-insensitive match)
```

## Test 14: Filenames with Spaces

```bash
# If you have "my video.mp4", try:
echo "play my video.mp4" | nc -q 0 localhost 8082
# Should work (entire argument after "play" is treated as filename)
```

## Test 15: Integration with qt-demo-launcher

```bash
# Update qt-demo-launcher.json to include:
{
  "apps": [
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
  ]
}

# Start qt-demo-launcher
# Click "Videos" button
# qt-mpv-wrapper should launch
# Select and play video
# Press 'q' to return to file browser
# Click EXIT to return to launcher
```

## Test 16: Multiple Play/Quit Cycles

**Test workflow:**
1. Launch qt-mpv-wrapper.sh
2. Play video 1
3. Press 'q' after 5 seconds
4. Wait for Qt to restart
5. Play video 2
6. Press 'q' after 5 seconds
7. Wait for Qt to restart
8. Repeat 5 times

**Purpose:** Verify no memory leaks or state corruption across cycles

## Test 17: Quick Exit (No Playback)

```bash
./qt-mpv-wrapper.sh /home/pi/micropanel/qt-apps/Videos --port 8082
# Immediately click EXIT button
```

**Expected:**
- Qt app exits
- Wrapper script detects no playback file
- Wrapper script exits cleanly
- No mpv launch
- No restart

## Test 18: mpv Keyboard Controls During Playback

**Play a video and test these mpv shortcuts:**
- `q` - Quit (returns to file browser)
- `Space` - Pause/Resume
- `Left Arrow` - Seek backward 5 seconds
- `Right Arrow` - Seek forward 5 seconds
- `Up Arrow` - Seek forward 60 seconds
- `Down Arrow` - Seek backward 60 seconds
- `9` - Volume down
- `0` - Volume up

All should work normally.

## Test 19: Stress Test - Rapid API Commands

```bash
# Send multiple commands quickly
for i in {1..10}; do
    echo "status" | nc -q 0 localhost 8082
    sleep 0.1
done
```

**Expected:** All commands should complete successfully

## Test 20: Verify Clean Shutdown

```bash
# Run qt-mpv-wrapper
./qt-mpv-wrapper.sh /home/pi/micropanel/qt-apps/Videos

# Click EXIT button
# Check process list
ps aux | grep qt-mpv-wrapper
# Should be empty (no zombie processes)

# Check temp file cleaned up
ls -la /tmp/qt-mpv-playback.txt
# Should not exist
```

## Common Issues and Solutions

### Issue: "qmake: command not found"
**Solution:**
```bash
sudo apt install -y qt5-qmake qtbase5-dev
```

### Issue: "module Qt.labs.folderlistmodel is not installed"
**Solution:**
```bash
sudo apt install -y qml-module-qt-labs-folderlistmodel
```

### Issue: Wrapper script not executable
**Solution:**
```bash
chmod +x qt-mpv-wrapper.sh
```

### Issue: mpv not found
**Solution:**
```bash
sudo apt install -y mpv
```

### Issue: Videos don't appear in list
**Check:**
1. Video directory exists: `ls -la /home/pi/micropanel/qt-apps/Videos`
2. Videos have correct extensions (mp4, mkv, avi, webm, mov)
3. File permissions allow reading

### Issue: Qt app exits but mpv doesn't start
**Debug:**
```bash
# Check playback file was created
cat /tmp/qt-mpv-playback.txt

# Try launching mpv manually with that path
mpv --vo=gpu --gpu-context=drm --fullscreen /path/from/file

# Check wrapper script for errors
bash -x ./qt-mpv-wrapper.sh /path/to/videos
```

### Issue: mpv plays but Qt doesn't restart
**Debug:**
```bash
# Check wrapper script while loop
# Ensure it's not exiting prematurely
# Run with debug output:
bash -x ./qt-mpv-wrapper.sh /path/to/videos
```

### Issue: Network API not responding
**Check:**
1. Qt app is running (not during mpv playback)
2. Port is listening: `netstat -tulpn | grep 8082`
3. No firewall blocking: `sudo iptables -L`
4. Correct port number in command

## Performance Benchmarks

Measure these timings on your Raspberry Pi:

**Qt Startup:**
```bash
time ./qt-mpv-wrapper /home/pi/micropanel/qt-apps/Videos
# Click EXIT immediately
# Note the time
```

**Play Transition:**
```bash
# Measure from clicking video to mpv playback starting
# Typical: 800-1300ms on Pi4
```

**Return Transition:**
```bash
# Measure from pressing 'q' in mpv to Qt UI appearing
# Typical: 2000-3000ms on Pi4
```

## Success Criteria

All tests should pass with these results:

- ✅ Binary compiles without errors
- ✅ File browser displays video list
- ✅ Videos play with mpv
- ✅ Return to file browser after video
- ✅ Network API responds to all commands
- ✅ Case-insensitive filename matching works
- ✅ Filenames with spaces work
- ✅ Loop mode works
- ✅ No zombie processes after exit
- ✅ No leftover temp files after clean exit
- ✅ Multiple play/quit cycles work reliably
- ✅ Integration with qt-demo-launcher works

If all tests pass, qt-mpv-wrapper is ready for deployment!
