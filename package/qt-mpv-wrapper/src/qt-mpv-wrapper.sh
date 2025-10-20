#!/bin/bash
# Wrapper script for qt-mpv-wrapper that manages mpv playback
# This script works around the framebuffer conflict between Qt and mpv
# by exiting qt-mpv-wrapper before launching mpv

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Log file for debugging
LOGFILE="/tmp/qt-mpv-wrapper.log"
echo "=== qt-mpv-wrapper started at $(date) ===" >> "$LOGFILE"

# Path to the qt-mpv-wrapper binary (in same directory as this script)
QT_MPV_BINARY="$SCRIPT_DIR/qt-mpv-wrapper"

# Temporary file for playback requests
PLAYBACK_FILE="/tmp/qt-mpv-playback.txt"

# Check if binary exists
if [ ! -x "$QT_MPV_BINARY" ]; then
    echo "Error: qt-mpv-wrapper binary not found at $QT_MPV_BINARY"
    echo "Please run 'make' in the src directory first"
    exit 1
fi

# Parse command line arguments
ARGS=()
for arg in "$@"; do
    ARGS+=("$arg")
done

# Function to launch Qt app
launch_qt_app() {
    # Remove any stale playback request file
    rm -f "$PLAYBACK_FILE"

    # Launch qt-mpv-wrapper and wait for it to exit
    "$QT_MPV_BINARY" "${ARGS[@]}"
}

# Main loop
while true; do
    # Launch Qt app
    launch_qt_app

    # Check if there's a playback request
    if [ -f "$PLAYBACK_FILE" ]; then
        # Read playback parameters
        VIDEO_PATH=$(sed -n '1p' "$PLAYBACK_FILE")
        LOOP_FLAG=$(sed -n '2p' "$PLAYBACK_FILE")

        # Remove the playback file
        rm -f "$PLAYBACK_FILE"

        # IPC socket for controlling mpv
        MPV_SOCKET="/tmp/mpv-socket"
        rm -f "$MPV_SOCKET"

        # Build mpv command
        MPV_ARGS=(
            "--vo=gpu"
            "--gpu-context=drm"
            "--fullscreen"
            "--really-quiet"
            "--msg-level=all=error"
            # Enable IPC socket for external control
            "--input-ipc-server=$MPV_SOCKET"
            # Use custom input config for keyboard shortcuts
            "--input-conf=$SCRIPT_DIR/mpv-input.conf"
        )

        if [ "$LOOP_FLAG" = "true" ]; then
            MPV_ARGS+=("--loop")
        fi

        MPV_ARGS+=("$VIDEO_PATH")

        # Launch mpv with full framebuffer access in background
        echo "Launching mpv: ${MPV_ARGS[@]}"
        echo "Click/tap screen to stop playback, or press 'q'"
        mpv "${MPV_ARGS[@]}" &
        MPV_PID=$!

        # Wait for IPC socket to be created
        sleep 0.5

        # Launch touch/mouse monitor in background
        # Try C program first (best), fall back to Python, then shell script
        echo "=== Launching input monitor at $(date) ===" >> "$LOGFILE"
        echo "DEBUG: SCRIPT_DIR=$SCRIPT_DIR" >> "$LOGFILE"
        echo "DEBUG: MPV_SOCKET=$MPV_SOCKET" >> "$LOGFILE"
        echo "DEBUG: Checking for input-monitor at: $SCRIPT_DIR/input-monitor" >> "$LOGFILE"

        if [ -f "$SCRIPT_DIR/input-monitor" ]; then
            ls -la "$SCRIPT_DIR/input-monitor" >> "$LOGFILE" 2>&1
            if [ -x "$SCRIPT_DIR/input-monitor" ]; then
                echo "Found executable input-monitor, launching..." >> "$LOGFILE"
                "$SCRIPT_DIR/input-monitor" "$MPV_SOCKET" >> "$LOGFILE" 2>&1 &
                MONITOR_PID=$!
                echo "input-monitor started with PID: $MONITOR_PID" >> "$LOGFILE"

                # Check if it's still running after 0.5 seconds
                sleep 0.5
                if kill -0 $MONITOR_PID 2>/dev/null; then
                    echo "input-monitor is still running (PID $MONITOR_PID)" >> "$LOGFILE"
                else
                    echo "ERROR: input-monitor died immediately!" >> "$LOGFILE"
                    wait $MONITOR_PID 2>/dev/null
                    echo "input-monitor exit code: $?" >> "$LOGFILE"
                fi
            else
                echo "ERROR: input-monitor exists but is not executable!" >> "$LOGFILE"
            fi
        else
            echo "ERROR: input-monitor not found at $SCRIPT_DIR/input-monitor" >> "$LOGFILE"
        fi

        if [ -z "$MONITOR_PID" ]; then
            if command -v python3 >/dev/null 2>&1 && [ -f "$SCRIPT_DIR/input-monitor.py" ]; then
                echo "Falling back to Python input-monitor..." >> "$LOGFILE"
                python3 "$SCRIPT_DIR/input-monitor.py" "$MPV_SOCKET" >> "$LOGFILE" 2>&1 &
                MONITOR_PID=$!
            else
                echo "WARNING: No input monitor found - mouse clicks will not stop video" >> "$LOGFILE"
                if [ -f "$SCRIPT_DIR/input-monitor.sh" ]; then
                    "$SCRIPT_DIR/input-monitor.sh" "$MPV_SOCKET" >> "$LOGFILE" 2>&1 &
                    MONITOR_PID=$!
                fi
            fi
        fi

        # Wait for mpv to finish
        wait $MPV_PID
        MPV_EXIT_CODE=$?

        # Kill the monitor
        kill $MONITOR_PID 2>/dev/null
        wait $MONITOR_PID 2>/dev/null

        # Clean up socket
        rm -f "$MPV_SOCKET"

        # mpv exited, loop will restart Qt app
        echo "mpv exited, restarting qt-mpv-wrapper..."
        sleep 0.5
    else
        # No playback request, user quit the app
        echo "qt-mpv-wrapper exited without playback request"
        break
    fi
done
