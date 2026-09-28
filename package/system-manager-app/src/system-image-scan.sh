#!/bin/bash
# system-image-scan.sh - what System Manager's "System image" section offers.
#
# Looks at attached USB media for a pi-ab-update bundle (*.mpupdate) the way
# the update engine (ab-system-update, discover_usb_bundle) will, so the user
# is told before pressing the button what the engine is going to find. It is
# read-only: every filesystem is mounted ro,nosuid,nodev,noexec in a private
# directory and unmounted again on every path. It never calls ab-update and
# never decides what is installable - the engine does that when it installs.
#
# Output, one line each (values never contain spaces):
#   BUNDLE device=<dev> path=<path on the stick> bytes=<file size>
#          version=<v> variant=<v> boards=<b> format=<n> signature=ok|bad|nokey|unreadable
#   NESTED device=<dev> path=<path>     a bundle below the top level: the engine
#                                       only looks at the top of each filesystem
#   UNMOUNTABLE device=<dev> fstype=<t> a USB filesystem that would not mount
#                                       read-only; with no bundle found the engine
#                                       fails such a stick as failed-source
#   SUMMARY sticks=<usb disks> filesystems=<vfat/exfat/ntfs on usb> bundles=<top-level>
#           nested=<n> unmountable=<n> running=<IMAGE_VERSION|-> layout=ab|single
# Exit 0 whenever it ran; the caller decides from the lines.
#
# The result is also cached in /run/system-manager/last-scan for the launcher
# badge (system-update-check.sh).
#
# Options / seams:
#   --dry-run                     print a canned result (desktop runs), touch nothing
#   SYSTEM_IMAGE_SCAN_FAKE=<file> print that file instead of scanning
#   SYSTEM_IMAGE_SCAN_LSBLK=<cmd> block-device inventory (default lsblk)
#   AB_UPDATE_CONFIG, AB_SIGNING_KEY, AB_IMAGE_MANIFEST as in the engine
set -uo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export LC_ALL=C

cache_dir=/run/system-manager
cache_file=$cache_dir/last-scan

publish() { # stdin -> stdout, and the cache when we may write it
    local out; out=$(cat)
    printf '%s\n' "$out"
    if [ "$(id -u)" -eq 0 ] && install -d -m0755 "$cache_dir" 2>/dev/null; then
        local tmp; tmp=$(mktemp "$cache_dir/.last-scan.XXXXXX") || return 0
        printf '%s\n' "$out" > "$tmp" && chmod 0644 "$tmp" && mv -f "$tmp" "$cache_file"
    fi
}

if [ -n "${SYSTEM_IMAGE_SCAN_FAKE:-}" ]; then
    cat "$SYSTEM_IMAGE_SCAN_FAKE"
    exit 0
fi
if [ "${1:-}" = --dry-run ]; then
    cat <<'EOF'
BUNDLE device=/dev/sda1 path=/micropanel-02.06.mpupdate bytes=812345344 version=02.06 variant=base boards=pi4 format=2 signature=ok
SUMMARY sticks=1 filesystems=1 bundles=1 nested=0 unmountable=0 running=02.05 layout=ab
EOF
    exit 0
fi

# --- the engine's own settings, parsed (never sourced) like the engine does --
ab_config=${AB_UPDATE_CONFIG:-/usr/lib/pi-ab-update/ab-update.conf}
ab_config_value() { # $1=key
    [ -f "$ab_config" ] && [ ! -L "$ab_config" ] || return 0
    awk -v key="$1" '
        /^[[:space:]]*#/ { next }
        index($0, key "=") == 1 { value = substr($0, length(key) + 2); if (value != "") { print value; found = 1 } }
        END { exit !found }' "$ab_config" 2>/dev/null || true
}
setting() { # $1=env, $2=key, $3=default
    if [ -n "$1" ]; then printf '%s\n' "$1"; return; fi
    local v; v=$(ab_config_value "$2")
    if [ -n "$v" ]; then printf '%s\n' "$v"; else printf '%s\n' "$3"; fi
}
signing_key=$(setting "${AB_SIGNING_KEY:-}" AB_SIGNING_KEY /usr/lib/pi-ab-update/update-signing-key.pub)
image_manifest=$(setting "${AB_IMAGE_MANIFEST:-}" AB_MANIFEST /usr/lib/pi-ab-update/image-manifest.env)
lsblk_command=${SYSTEM_IMAGE_SCAN_LSBLK:-lsblk}

manifest_value() { # $1=file $2=key; "-" when absent or not a plain token
    local v
    v=$(awk -F= -v key="$2" '$1 == key { print substr($0, length(key) + 2); exit }' "$1" 2>/dev/null)
    [[ $v =~ ^[A-Za-z0-9][A-Za-z0-9._,-]{0,63}$ ]] && printf '%s\n' "$v" || printf -- '-\n'
}
running=-; layout=single
if [ -f "$image_manifest" ]; then
    running=$(manifest_value "$image_manifest" IMAGE_VERSION)
    [ "$(manifest_value "$image_manifest" IMAGE_LAYOUT)" = ab ] && layout=ab
fi

# --- USB filesystems: the engine's rule (list_usb_filesystems) -----------------
# USB transport, vfat, exfat or ntfs, either a whole-disk filesystem or a
# partition of a USB disk. Removability is deliberately not part of it.
records=(); usb_disks=()
while IFS= read -r line; do
    [[ $line =~ ^PATH=\"([^\"]*)\"\ TYPE=\"([^\"]*)\"\ TRAN=\"([^\"]*)\"\ FSTYPE=\"([^\"]*)\"\ PKNAME=\"([^\"]*)\"$ ]] || continue
    records+=("${BASH_REMATCH[1]}|${BASH_REMATCH[2]}|${BASH_REMATCH[3]}|${BASH_REMATCH[4]}|${BASH_REMATCH[5]}")
    [ "${BASH_REMATCH[2]}" = disk ] && [ "${BASH_REMATCH[3]}" = usb ] && usb_disks+=("$(basename "${BASH_REMATCH[1]}")")
done < <("$lsblk_command" -P -o PATH,TYPE,TRAN,FSTYPE,PKNAME 2>/dev/null || true)
filesystems=(); fstypes=()
for record in ${records[@]+"${records[@]}"}; do
    IFS='|' read -r path type tran fstype pkname <<< "$record"
    case "$fstype" in vfat|exfat|ntfs) ;; *) continue ;; esac
    if [ "$type" = disk ] && [ "$tran" = usb ]; then filesystems+=("$path"); fstypes+=("$fstype"); continue; fi
    [ "$type" = part ] || continue
    for disk in ${usb_disks[@]+"${usb_disks[@]}"}; do
        [ "$pkname" = "$disk" ] && { filesystems+=("$path"); fstypes+=("$fstype"); break; }
    done
done

# --- scan each one, read-only, always unmounted --------------------------------
mnt=""; work=""
cleanup() {
    if [ -n "$mnt" ]; then
        mountpoint -q "$mnt" 2>/dev/null && umount "$mnt" 2>/dev/null
        rmdir "$mnt" 2>/dev/null
    fi
    [ -z "$work" ] || rm -rf -- "$work"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

token() { # a value that is safe to print in a key=value line
    local v=${1//[[:space:]]/_}; printf '%s\n' "${v:--}"
}

lines=(); bundles=0; nested=0; unmountable=0
if [ "${#filesystems[@]}" -gt 0 ]; then
    [ "$(id -u)" -eq 0 ] || { echo "SUMMARY sticks=${#usb_disks[@]} filesystems=${#filesystems[@]} bundles=0 nested=0 unmountable=0 running=$running layout=$layout error=needs-root"; exit 0; }
    install -d -m0700 /run/system-manager
    mnt=$(mktemp -d /run/system-manager/scan.XXXXXX)
    work=$(mktemp -d /run/system-manager/work.XXXXXX)
    for i in "${!filesystems[@]}"; do
        device=${filesystems[$i]}; fstype=${fstypes[$i]}
        [ -b "$device" ] || continue
        # The engine's driver order (mount_source_device): the in-kernel ntfs3
        # first for NTFS, then a plain mount, so ntfs-3g can serve too
        if ! { [ "$fstype" = ntfs ] && mount -t ntfs3 -o ro,nosuid,nodev,noexec -- "$device" "$mnt" 2>/dev/null; } \
           && ! mount -o ro,nosuid,nodev,noexec -- "$device" "$mnt" 2>/dev/null; then
            unmountable=$((unmountable + 1))
            lines+=("UNMOUNTABLE device=$device fstype=$fstype")
            continue
        fi
        # Top level only, regular files only - exactly what the engine globs
        shopt -s nullglob
        for bundle in "$mnt"/*.mpupdate; do
            [ -f "$bundle" ] && [ ! -L "$bundle" ] || continue
            bundles=$((bundles + 1))
            rm -f "$work/manifest" "$work/manifest.sig"
            # manifest and manifest.sig are the first two members; --occurrence
            # stops tar there instead of reading the 800 MB rootfs behind them
            tar --occurrence=1 -xOf "$bundle" manifest 2>/dev/null | head -c 65536 > "$work/manifest"
            tar --occurrence=1 -xOf "$bundle" manifest.sig 2>/dev/null | head -c 4096 > "$work/manifest.sig"
            if [ ! -s "$work/manifest" ]; then
                sig=unreadable
            elif [ ! -f "$signing_key" ]; then
                sig=nokey
            elif [ -s "$work/manifest.sig" ] && openssl pkeyutl -verify -pubin -inkey "$signing_key" -rawin \
                    -in "$work/manifest" -sigfile "$work/manifest.sig" >/dev/null 2>&1; then
                sig=ok
            else
                sig=bad
            fi
            lines+=("BUNDLE device=$device path=$(token "/${bundle#"$mnt"/}") bytes=$(stat -c %s "$bundle" 2>/dev/null || echo 0) version=$(manifest_value "$work/manifest" version) variant=$(manifest_value "$work/manifest" variant) boards=$(manifest_value "$work/manifest" boards) format=$(manifest_value "$work/manifest" format) signature=$sig")
        done
        shopt -u nullglob
        # Below the top level the engine will not look; say so rather than
        # letting the user wonder why "no bundle" when one is on the stick
        while IFS= read -r deeper; do
            nested=$((nested + 1))
            lines+=("NESTED device=$device path=$(token "/${deeper#"$mnt"/}")")
        done < <(find "$mnt" -mindepth 2 -maxdepth 3 -type f -name '*.mpupdate' 2>/dev/null)
        umount "$mnt" 2>/dev/null || true
    done
fi

{
    for l in ${lines[@]+"${lines[@]}"}; do printf '%s\n' "$l"; done
    echo "SUMMARY sticks=${#usb_disks[@]} filesystems=${#filesystems[@]} bundles=$bundles nested=$nested unmountable=$unmountable running=$running layout=$layout"
} | publish
exit 0
