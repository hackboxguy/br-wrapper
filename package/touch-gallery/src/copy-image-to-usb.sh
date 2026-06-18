#!/bin/sh

# Copy one image/report to a USB stick.
# Compatible with /bin/sh on Buildroot and Raspberry Pi OS.

USB_MOUNT_POINT="${USB_MOUNT_POINT:-/tmp/micropanel-usb}"
USB_REPORT_DIR="${USB_REPORT_DIR:-display-reports}"
USB_MOUNTED_BY_SCRIPT=0

SUDO_CMD=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO_CMD="sudo"
    fi
fi

print_info() {
    printf "%s\n" "$1"
}

print_error() {
    printf "%s\n" "$1"
}

detect_usb_stick() {
    for block_dev in /sys/block/sd*; do
        [ -e "$block_dev" ] || continue

        dev_name=$(basename "$block_dev")
        removable=$(cat "$block_dev/removable" 2>/dev/null || echo "0")
        [ "$removable" = "1" ] || continue

        for part in "$block_dev"/"$dev_name"*; do
            if [ -e "$part" ]; then
                part_name=$(basename "$part")
                if [ "$part_name" != "$dev_name" ]; then
                    echo "/dev/$part_name"
                    return 0
                fi
            fi
        done

        echo "/dev/$dev_name"
        return 0
    done

    return 1
}

mounted_path_for_device() {
    device="$1"
    awk -v dev="$device" '$1 == dev { print $2; exit }' /proc/mounts 2>/dev/null
}

detect_filesystem() {
    device="$1"

    if command -v blkid >/dev/null 2>&1; then
        fstype=$($SUDO_CMD blkid -s TYPE -o value "$device" 2>/dev/null | head -n1)
        if [ -n "$fstype" ] && [ "$fstype" != "$device"* ]; then
            echo "$fstype"
            return 0
        fi
    fi

    echo "vfat"
    return 0
}

mount_usb_stick() {
    device="$1"

    existing_mount=$(mounted_path_for_device "$device")
    if [ -n "$existing_mount" ]; then
        echo "$existing_mount"
        return 0
    fi

    if mount | grep -q " $USB_MOUNT_POINT "; then
        echo "$USB_MOUNT_POINT"
        return 0
    fi

    if [ ! -d "$USB_MOUNT_POINT" ]; then
        if ! $SUDO_CMD mkdir -p "$USB_MOUNT_POINT" 2>/dev/null; then
            return 1
        fi
    fi

    fstype=$(detect_filesystem "$device")
    if $SUDO_CMD mount -t "$fstype" "$device" "$USB_MOUNT_POINT" 2>/dev/null; then
        USB_MOUNTED_BY_SCRIPT=1
        echo "$USB_MOUNT_POINT"
        return 0
    fi

    if $SUDO_CMD mount "$device" "$USB_MOUNT_POINT" 2>/dev/null; then
        USB_MOUNTED_BY_SCRIPT=1
        echo "$USB_MOUNT_POINT"
        return 0
    fi

    return 1
}

unmount_usb_stick() {
    if [ "$USB_MOUNTED_BY_SCRIPT" = "1" ] && [ -d "$USB_MOUNT_POINT" ]; then
        sync
        $SUDO_CMD umount "$USB_MOUNT_POINT" 2>/dev/null || return 1
    fi

    return 0
}

main() {
    image_path="${1:-}"

    if [ -z "$image_path" ]; then
        print_error "No image selected"
        return 1
    fi

    if [ ! -f "$image_path" ]; then
        print_error "Image not found"
        return 1
    fi

    if ! usb_device=$(detect_usb_stick); then
        print_error "Connect USB disk"
        return 1
    fi

    if ! mount_point=$(mount_usb_stick "$usb_device"); then
        print_error "Failed to mount USB"
        return 1
    fi

    dest_dir="$mount_point/$USB_REPORT_DIR"
    if ! $SUDO_CMD mkdir -p "$dest_dir" 2>/dev/null; then
        print_error "USB write failed"
        unmount_usb_stick
        return 1
    fi

    image_name=$(basename "$image_path")
    dest_path="$dest_dir/$image_name"
    if ! $SUDO_CMD cp -f "$image_path" "$dest_path" 2>/dev/null; then
        print_error "USB copy failed"
        unmount_usb_stick
        return 1
    fi

    sync
    if ! unmount_usb_stick; then
        print_error "USB unmount failed"
        return 1
    fi

    print_info "Copied to USB"
    if [ "$USB_MOUNTED_BY_SCRIPT" = "1" ]; then
        print_info "Safe to remove"
    fi
    return 0
}

main "$@"
exit $?
