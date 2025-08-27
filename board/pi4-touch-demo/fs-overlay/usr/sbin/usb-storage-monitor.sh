#!/bin/sh
# /usr/bin/usb-storage-monitor.sh

TRIGGER_FILE="/tmp/usb_gallery_trigger"
LAUNCHER_PORT="8081"

while true; do
    if [ -f "$TRIGGER_FILE" ]; then
        CMD=$(cat "$TRIGGER_FILE")
        rm -f "$TRIGGER_FILE"

        echo "$CMD" | nc localhost $LAUNCHER_PORT
        logger "USB Gallery Monitor: Executed $CMD"
    fi
    sleep 0.5
done
