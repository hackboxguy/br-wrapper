# qt-mpv-wrapper Network API

Complete reference for remote control of qt-mpv-wrapper via TCP network API.

## Connection

**Default Port:** 8082
**Protocol:** TCP
**Format:** Text commands (newline-terminated)

## Commands

### `list`
Lists all available video files in the configured video directory.

**Request:**
```bash
echo "list" | nc -q 0 localhost 8082
```

**Response (success):**
```
video1.mp4
video2.mkv
video3.avi
```

**Response (no videos):**
```
No videos found
```

---

### `play <filename>`
Plays the specified video file without looping.

**Request:**
```bash
echo "play myvideo.mp4" | nc -q 0 localhost 8082
```

**Response (success):**
```
OK: Playing myvideo.mp4
```

**Response (error):**
```
ERROR: Video not found: myvideo.mp4
```

**Notes:**
- Filename is case-insensitive
- Can include spaces (entire argument after "play" is treated as filename)
- Automatically stops any currently playing video

---

### `play-loop <filename>`
Plays the specified video file with looping enabled.

**Request:**
```bash
echo "play-loop demo.mkv" | nc -q 0 localhost 8082
```

**Response (success):**
```
OK: Playing (loop) demo.mkv
```

**Response (error):**
```
ERROR: Video not found: demo.mkv
```

---

### `stop`
Stops the currently playing video and returns to file browser.

**Request:**
```bash
echo "stop" | nc -q 0 localhost 8082
```

**Response (success):**
```
OK: Stopped
```

**Response (error):**
```
ERROR: Not playing
```

---

### `status` or `get-status`
Returns the current playback status.

**Request:**
```bash
echo "status" | nc -q 0 localhost 8082
```

**Response:**
```
playing
```
or
```
stopped
```

---

### `help`
Shows all available commands.

**Request:**
```bash
echo "help" | nc -q 0 localhost 8082
```

**Response:**
```
Available commands:
  list - List all video files
  play <filename> - Play video
  play-loop <filename> - Play video with looping
  stop - Stop playback
  status - Get playback status
  help - Show this help
```

## Usage Workflows

### Automated Video Playback

```bash
#!/bin/bash
# automated-playback.sh

HOST="localhost"
PORT="8082"

# Function to send command
send_cmd() {
    echo "$1" | nc -q 0 $HOST $PORT
}

# Get list of videos
echo "Available videos:"
send_cmd "list"

# Play first video
send_cmd "play video1.mp4"

# Wait 30 seconds
sleep 30

# Stop playback
send_cmd "stop"
```

### Integration with qt-demo-launcher

```bash
#!/bin/bash
# Start video player via launcher and control it

# 1. Start qt-mpv-wrapper via qt-demo-launcher
echo "start-app video-player" | nc -q 0 localhost 8081

# 2. Wait for app to start
sleep 2

# 3. Control video playback
echo "play presentation.mp4" | nc -q 0 localhost 8082

# 4. Video plays...
sleep 60

# 5. Stop video
echo "stop" | nc -q 0 localhost 8082

# 6. Exit video player (return to launcher)
echo "stop-app" | nc -q 0 localhost 8081
```

### Video Playlist

```bash
#!/bin/bash
# Play videos in sequence

VIDEOS=("intro.mp4" "main.mkv" "outro.mp4")

for video in "${VIDEOS[@]}"; do
    echo "Playing: $video"
    echo "play $video" | nc -q 0 localhost 8082

    # Wait for video to finish (poll status)
    while true; do
        STATUS=$(echo "status" | nc -q 0 localhost 8082)
        if [ "$STATUS" = "stopped" ]; then
            break
        fi
        sleep 1
    done

    echo "Finished: $video"
    sleep 2  # Pause between videos
done
```

### Looping Kiosk Display

```bash
#!/bin/bash
# Start looping video for kiosk mode

# Start wrapper
echo "start-app video-player" | nc -q 0 localhost 8081
sleep 2

# Play video in loop
echo "play-loop kiosk-video.mp4" | nc -q 0 localhost 8082

# Video loops indefinitely until stopped manually
```

## Error Handling

All error responses start with `ERROR:` prefix.

```bash
# Example error checking
RESPONSE=$(echo "play nonexistent.mp4" | nc -q 0 localhost 8082)

if [[ $RESPONSE == ERROR:* ]]; then
    echo "Command failed: $RESPONSE"
    exit 1
fi

echo "Success: $RESPONSE"
```

## Port Configuration

Change the network port using command-line argument:

```bash
# Start on custom port
./qt-mpv-wrapper /path/to/videos --port 9000

# Or in qt-demo-launcher.json
{
  "arguments": ["/home/pi/micropanel/qt-apps/Videos", "--port", "9000"]
}
```

## Security Considerations

- Network API listens on **all interfaces** (0.0.0.0)
- No authentication required
- **Production:** Use firewall to restrict access
- Single client connection at a time

## Testing

```bash
# Test connection
nc -zv localhost 8082

# Interactive mode
nc localhost 8082
# Then type commands interactively

# Verbose testing
echo "list" | nc -v localhost 8082
```

## Troubleshooting

### Port already in use
```bash
# Check what's using the port
sudo netstat -tulpn | grep 8082

# Use different port
./qt-mpv-wrapper --port 8083
```

### Connection refused
```bash
# Check if qt-mpv-wrapper is running
ps aux | grep qt-mpv-wrapper

# Check logs
journalctl -f | grep qt-mpv-wrapper

# Verify port in startup logs
# Should see: "Video controller network interface started on port 8082"
```

### Commands not working
```bash
# Ensure newline termination
echo -e "list\n" | nc localhost 8082

# Use -q flag with netcat
echo "list" | nc -q 0 localhost 8082

# Try different netcat variant
echo "list" | ncat localhost 8082
```

## API Comparison

| Port | Service | Purpose |
|------|---------|---------|
| 8081 | qt-demo-launcher | Launch/stop apps |
| 8082 | qt-mpv-wrapper | Video playback control |

**Workflow:**
1. Use port 8081 to start qt-mpv-wrapper
2. Use port 8082 to control video playback
3. Use port 8081 to stop qt-mpv-wrapper (or use EXIT button in UI)
