#!/bin/sh
# kodi-usb-common.sh - Shared USB detection and mounting for Kodi launcher scripts
# Source this file from kodi-slideshow.sh and kodi-video.sh
#
# USB detection follows the same pattern as fpga-jtag-flasher scripts:
#   1. Scan /sys/block/sd* for removable=1
#   2. Check if already mounted (e.g., by udisks2 at /media/pi/<LABEL>)
#   3. If not mounted, mount at /tmp/micropanel-usb
#
# Exported functions:
#   detect_usb_media_path <subdir>  - Returns USB path to <subdir> if found
#                                     e.g., detect_usb_media_path "Pictures"
#   find_first_video <dir>          - Returns path to first video file in <dir>

USB_MOUNT_POINT="/tmp/micropanel-usb"

# Detect USB stick device (first removable /dev/sd* with a partition)
_detect_usb_device() {
    for block_dev in /sys/block/sd*; do
        [ -e "$block_dev" ] || continue

        dev_name=$(basename "$block_dev")
        removable=$(cat "$block_dev/removable" 2>/dev/null || echo "0")

        if [ "$removable" = "1" ]; then
            # Prefer first partition
            for part in "$block_dev"/"$dev_name"*; do
                if [ -e "$part" ]; then
                    part_name=$(basename "$part")
                    if [ "$part_name" != "$dev_name" ]; then
                        echo "/dev/$part_name"
                        return 0
                    fi
                fi
            done
            # No partitions, use device itself
            echo "/dev/$dev_name"
            return 0
        fi
    done
    return 1
}

# Get mount point of a USB device — checks existing mounts first, then mounts ourselves
_get_usb_mount() {
    local device="$1"

    # Check if already mounted (e.g., by udisks2 at /media/pi/<LABEL>)
    local existing
    existing=$(mount | grep "^$device " | awk '{print $3}' | head -1)
    if [ -n "$existing" ]; then
        echo "$existing"
        return 0
    fi

    # Not mounted — mount it ourselves
    [ -d "$USB_MOUNT_POINT" ] || mkdir -p "$USB_MOUNT_POINT" 2>/dev/null

    # Detect filesystem type
    local fstype="vfat"
    if command -v blkid >/dev/null 2>&1; then
        local detected
        detected=$(blkid -s TYPE -o value "$device" 2>/dev/null | head -n1)
        [ -n "$detected" ] && fstype="$detected"
    fi

    if mount -t "$fstype" "$device" "$USB_MOUNT_POINT" 2>/dev/null; then
        echo "$USB_MOUNT_POINT"
        return 0
    fi

    # Fallback to auto-detection
    if mount "$device" "$USB_MOUNT_POINT" 2>/dev/null; then
        echo "$USB_MOUNT_POINT"
        return 0
    fi

    return 1
}

# Detect USB media path for a given subdirectory (e.g., "Pictures" or "Videos")
# Returns the full path if found, exits with 1 if not.
# Retries up to 5 seconds to allow udisks2 automount to complete after boot.
detect_usb_media_path() {
    local subdir="$1"
    local usb_device
    local attempt

    usb_device=$(_detect_usb_device) || return 1

    # USB device exists — wait for it to be mounted (udisks2 may still be
    # processing after boot). Retry up to 5 times with 1-second intervals.
    for attempt in 1 2 3 4 5; do
        local mount_point
        mount_point=$(_get_usb_mount "$usb_device") || { sleep 1; continue; }

        if [ -d "$mount_point/$subdir" ]; then
            echo "$mount_point/$subdir"
            return 0
        fi

        # Mounted but no matching subdir — no point retrying
        return 1
    done

    return 1
}

# Find first video file in a directory
# Checks common video extensions: mkv, mp4, avi, mov, wmv, flv, webm, m4v, ts
find_first_video() {
    local dir="$1"
    local video

    for ext in mkv mp4 avi mov wmv flv webm m4v ts; do
        video=$(find "$dir" -maxdepth 1 -type f -iname "*.$ext" 2>/dev/null | head -1)
        if [ -n "$video" ]; then
            echo "$video"
            return 0
        fi
    done

    return 1
}
