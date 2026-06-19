#!/bin/sh

# Copy one image/report to a USB stick.
# Compatible with /bin/sh on Buildroot and Raspberry Pi OS.

USB_MOUNT_POINT="${USB_MOUNT_POINT:-/tmp/micropanel-usb}"
USB_REPORT_DIR="${USB_REPORT_DIR:-display-reports}"
USB_MOUNTED_BY_SCRIPT=0
USB_MOUNT_PATH=""

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

is_device_mounted() {
    device="$1"
    awk -v dev="$device" '$1 == dev { found=1 } END { exit found ? 0 : 1 }' /proc/mounts 2>/dev/null
}

is_path_mounted() {
    mount_point="$1"
    awk -v mp="$mount_point" '$2 == mp { found=1 } END { exit found ? 0 : 1 }' /proc/mounts 2>/dev/null
}

unmount_if_mounted() {
    target="$1"

    sync
    $SUDO_CMD umount "$target" 2>/dev/null
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

find_companion_data_dir() {
    image_path="$1"
    image_dir=$(dirname "$image_path")
    image_name=$(basename "$image_path")
    image_stem="${image_name%.*}"

    for candidate in \
        "$image_dir/$image_stem-data" \
        "$image_dir/$image_stem"
    do
        if [ -d "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done

    case "$image_stem" in
        *-local-dimming-apl)
            run_id="${image_stem%-local-dimming-apl}"
            suite_dir="local-dimming-apl"
            ;;
        *-color-gamut)
            run_id="${image_stem%-color-gamut}"
            suite_dir="color-gamut"
            ;;
        *)
            return 1
            ;;
    esac

    for candidate in \
        "$image_dir/$suite_dir/$run_id-data" \
        "$image_dir/$suite_dir/$run_id"
    do
        if [ -d "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done

    return 1
}

mount_usb_stick() {
    device="$1"
    USB_MOUNT_PATH=""

    # Own the mount lifecycle for report transfers. Reusing desktop/udisks
    # automounts can leave us with a stale or read-only mount after the stick
    # is removed and reinserted.
    if is_device_mounted "$device"; then
        if ! unmount_if_mounted "$device"; then
            return 1
        fi
    fi

    if is_path_mounted "$USB_MOUNT_POINT"; then
        if ! unmount_if_mounted "$USB_MOUNT_POINT"; then
            return 1
        fi
    fi

    if [ ! -d "$USB_MOUNT_POINT" ]; then
        if ! $SUDO_CMD mkdir -p "$USB_MOUNT_POINT" 2>/dev/null; then
            return 1
        fi
    fi

    fstype=$(detect_filesystem "$device")
    if $SUDO_CMD mount -t "$fstype" "$device" "$USB_MOUNT_POINT" 2>/dev/null; then
        USB_MOUNTED_BY_SCRIPT=1
        USB_MOUNT_PATH="$USB_MOUNT_POINT"
        return 0
    fi

    if $SUDO_CMD mount "$device" "$USB_MOUNT_POINT" 2>/dev/null; then
        USB_MOUNTED_BY_SCRIPT=1
        USB_MOUNT_PATH="$USB_MOUNT_POINT"
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

    if ! mount_usb_stick "$usb_device"; then
        print_error "Failed to mount USB"
        return 1
    fi
    mount_point="$USB_MOUNT_PATH"

    dest_dir="$mount_point/$USB_REPORT_DIR"
    if ! $SUDO_CMD mkdir -p "$dest_dir" 2>/dev/null; then
        print_error "USB write failed"
        unmount_usb_stick
        return 1
    fi

    image_name=$(basename "$image_path")
    image_stem="${image_name%.*}"
    dest_path="$dest_dir/$image_name"
    if ! $SUDO_CMD cp -f "$image_path" "$dest_path" 2>/dev/null; then
        print_error "USB copy failed"
        unmount_usb_stick
        return 1
    fi

    data_copied=0
    if data_dir=$(find_companion_data_dir "$image_path"); then
        data_dest="$dest_dir/$image_stem-data"
        if ! $SUDO_CMD mkdir -p "$data_dest" 2>/dev/null; then
            print_error "USB data copy failed"
            unmount_usb_stick
            return 1
        fi

        if ! $SUDO_CMD cp -rf "$data_dir/." "$data_dest/" 2>/dev/null; then
            print_error "USB data copy failed"
            unmount_usb_stick
            return 1
        fi
        data_copied=1
    fi

    sync
    if ! unmount_usb_stick; then
        print_error "USB unmount failed"
        return 1
    fi

    if [ "$data_copied" = "1" ]; then
        print_info "Copied image and data to USB"
    else
        print_info "Copied image only"
        print_info "Data not found"
    fi
    if [ "$USB_MOUNTED_BY_SCRIPT" = "1" ]; then
        print_info "Safe to remove"
    fi
    return 0
}

main "$@"
exit $?
