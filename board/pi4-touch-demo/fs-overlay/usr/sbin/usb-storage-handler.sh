#!/bin/sh
# USB storage handler script for qt-demo-launcher gallery button control
ACTION="$1"
DEVICE="$2"
DEVICE_PATH="/dev/$DEVICE"
LOG_FILE="/var/log/usb-storage.log"
LAUNCHER_PORT="8081"

# Logging function
log_msg() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') [USB-HANDLER] $1" >> "$LOG_FILE"
}

# Function to check if device has supported filesystem
check_filesystem() {
    local device="$1"

    # Wait a moment for device to be ready
    sleep 1

    # Use hexdump to check filesystem signature
    local fs_header=$(hexdump -C "$device" 2>/dev/null | head -1)

    if [ -z "$fs_header" ]; then
        log_msg "Cannot read device $device"
        return 1
    fi

    # Check for NTFS (contains "NTFS" at offset 3-6)
    if echo "$fs_header" | grep -q "NTFS"; then
        log_msg "NTFS filesystem detected on $device"
        return 0
    # Check for FAT32 (starts with EB xx 90 jump instruction)
    elif echo "$fs_header" | grep -qE "eb.*90"; then
        log_msg "FAT filesystem detected on $device"
        return 0
    # Check for exFAT (starts with EB 76 90)
    elif echo "$fs_header" | grep -q "eb 76 90"; then
        log_msg "exFAT filesystem detected on $device"
        return 0
    # Additional FAT signature check (some FAT drives have different patterns)
    elif echo "$fs_header" | grep -qE "(55 aa|aa 55)"; then
        # Get more data to verify it's actually FAT
        local more_data=$(hexdump -C "$device" 2>/dev/null | head -3)
        if echo "$more_data" | grep -qE "(FAT|fat)"; then
            log_msg "FAT filesystem detected on $device (secondary check)"
            return 0
        fi
    fi

    log_msg "Unsupported filesystem on $device. Header: $fs_header"
    return 1
}

send_launcher_command() {
    local cmd="$1"
    local trigger_file="/tmp/usb_gallery_trigger"

    echo "$cmd" > "$trigger_file"
    log_msg "Created trigger file for command: $cmd"
    return 0
}

mount_usb_device() {
    local device="$1"
    local mount_point="/mnt/usbdrive"
    
    # Create mount point
    mkdir -p "$mount_point"
    
    # Try mounting with common filesystem types
    if mount -t ntfs "$device" "$mount_point" 2>/dev/null; then
        log_msg "Mounted NTFS device $device to $mount_point"
        return 0
    elif mount -t vfat "$device" "$mount_point" 2>/dev/null; then
        log_msg "Mounted FAT device $device to $mount_point"
        return 0
    elif mount -t exfat "$device" "$mount_point" 2>/dev/null; then
        log_msg "Mounted exFAT device $device to $mount_point"
        return 0
    else
        log_msg "Failed to mount $device"
        rmdir "$mount_point" 2>/dev/null
        return 1
    fi
}

unmount_usb_device() {
    local mount_point="/mnt/usbdrive"
    
    if mountpoint -q "$mount_point" 2>/dev/null; then
        umount "$mount_point"
        if [ $? -eq 0 ]; then
            log_msg "Unmounted $mount_point"
            rmdir "$mount_point" 2>/dev/null
        else
            log_msg "Failed to unmount $mount_point"
            return 1
        fi
    fi
    return 0
}

# Main logic
case "$ACTION" in
    "add")
        log_msg "USB storage device added: $DEVICE_PATH"
        # Check if it's a partition (has number at end)
        if echo "$DEVICE" | grep -q '[0-9]$'; then
            log_msg "Processing partition: $DEVICE"
            # Check filesystem type
            if check_filesystem "$DEVICE_PATH"; then
                log_msg "Enabling gallery button for USB storage"
                send_launcher_command "set-button-enabled gallery true"
            else
                log_msg "Filesystem not supported, gallery button remains disabled"
            fi
        else
            log_msg "Ignoring whole device $DEVICE (waiting for partition)"
        fi
        ;;
    "remove")
        log_msg "USB storage device removed: $DEVICE_PATH"
        #unmount_usb_device
        # On any USB storage removal, disable gallery button
        # (assumes only one USB drive at a time)
        log_msg "Disabling gallery button due to USB removal"
        send_launcher_command "set-button-enabled gallery false"
        ;;

    *)
        log_msg "Unknown action: $ACTION"
        exit 1
        ;;
esac

exit 0
