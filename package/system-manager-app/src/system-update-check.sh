#!/bin/sh
# system-update-check.sh - one line for the launcher's System Manager badge.
#
# The launcher shows one line, so this prints the most important of:
#   Update rolled back    the last system image update fell back (pi-ab-update
#                         status says fallback)
#   Update available      a board carries firmware other than the image this
#                         system ships (update-iocs.sh --check, read-only)
#   FPGA update available the display FPGA's update slot carries another image than
#                         the one this system ships (update-fpga.sh --probe, then
#                         --check: a read-only slot scan, about 6 s; its result is
#                         cached for the boot until System Manager runs the tool)
#   Image update on USB   a stick carries exactly one signed image bundle of a
#                         version other than the running one (system-image-scan.sh,
#                         read-only; it also refreshes /run/system-manager/last-scan)
# and nothing otherwise. qt-demo-launcher runs it through the button's
# "badge_command" after start-up and each time an app exits, so it never runs
# while System Manager is updating; the lock file covers a manual run on top.
#
# Paths follow the install layout, like system-manager-app: the tools beside
# this script, the images in ../share/sp6bins/firmware/bios-bin.
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
TOOL=${UPDATE_IOCS:-$HERE/update-iocs.sh}
DIR=${IMAGE_DIR:-$(dirname "$HERE")/share/sp6bins/firmware/bios-bin}
SCAN=${SYSTEM_IMAGE_SCAN:-$HERE/system-image-scan.sh}
FPGA_TOOL=${UPDATE_FPGA:-$HERE/update-fpga.sh}
FPGA_DIR=${FPGA_IMAGE_DIR:-$(dirname "$HERE")/fpga/bitbin}
AB_UPDATE=${AB_UPDATE:-/usr/local/bin/ab-update}
AB_CONF=${AB_UPDATE_CONFIG:-/usr/lib/pi-ab-update/ab-update.conf}
LOCK=/tmp/system-update.lock

# An update is running - unless the process that took the lock is gone (an
# image update ends in a reboot, which can leave the lock on a persistent /tmp)
if [ -e "$LOCK" ]; then
    pid=$(cat "$LOCK" 2>/dev/null)
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null && exit 0
fi

as_root() {
    if [ "$(id -u)" -eq 0 ]; then "$@"; else sudo -n "$@"; fi
}

# 1. A system image update that fell back - unless the user has already seen
# it: System Manager records acknowledged-fallback when its System image section
# is opened after a fallback, naming the install it refers to. A later fallback
# of another install shows the badge again. The reference is the candidate's
# version: from the engine's public status once it publishes one, else from the
# app's own last-install record, else "-" (the same rule the app uses).
if [ -x "$AB_UPDATE" ]; then
    rundir=$(awk 'index($0, "AB_RUNTIME_DIR=") == 1 { v = substr($0, 16) } END { print v }' "$AB_CONF" 2>/dev/null)
    [ -n "$rundir" ] || rundir=/run/ab-update
    if grep -qx 'state=fallback' "$rundir/status" 2>/dev/null; then
        data=${SYSTEM_MANAGER_DATA:-/data/system-manager}
        [ -d "$data" ] || data=$(dirname "$HERE")/usr
        ref=$(awk -F= '$1 == "version" { print substr($0, 9); exit }' "$rundir/status" 2>/dev/null)
        [ -n "$ref" ] || ref=$(awk -F= '$1 == "version" { print substr($0, 9); exit }' \
                                   "$data/logs/system-image-update/last-install" 2>/dev/null)
        [ -n "$ref" ] || ref=-
        ack=$(awk -F= '$1 == "version" { print substr($0, 9); exit }' "$data/acknowledged-fallback" 2>/dev/null)
        if [ "$ack" != "$ref" ]; then
            echo "Update rolled back"
            exit 0
        fi
    fi
fi

# 2. Board firmware
if [ -x "$TOOL" ] && [ -d "$DIR" ]; then
    out=$(as_root timeout 60 "$TOOL" --check --image-dir "$DIR" 2>/dev/null)
    if echo "$out" | grep -q "^RESULT .*status=outdated"; then
        echo "Update available"
        exit 0
    fi
fi

# 3. The display FPGA - only where one with the update interface answers
# (its per-run logs go to /tmp: this runs after every app exit, and a check changes nothing).
# The slot scan is cached: the slot only changes when System Manager runs update-fpga.sh, which
# leaves a log in its log directory. Scanning after every app exit kept the display's I2C port busy
# just as the next app started (the scan moves the FPGA's 0x1E register pointer; an app that read
# the FPGA in two transactions then saw another page and fell back to legacy writes). A conclusive
# result (current/outdated) is reused for this boot until such a log or a new image appears.
FPGA_CACHE=/tmp/system-update-check.fpga
FPGA_LOGS=${SYSTEM_MANAGER_DATA:-/data/system-manager}
[ -d "$FPGA_LOGS" ] || FPGA_LOGS=$(dirname "$HERE")/usr
FPGA_LOGS=$FPGA_LOGS/logs/system-update
BOOT_ID=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)
fpga_cached() {
    [ -f "$FPGA_CACHE" ] && grep -qx "boot=$BOOT_ID" "$FPGA_CACHE" || return 1
    [ -z "$(find "$FPGA_DIR" -newer "$FPGA_CACHE" 2>/dev/null | head -n 1)" ] || return 1
    [ -z "$(find "$FPGA_LOGS" -name 'update-fpga-*' -newer "$FPGA_CACHE" 2>/dev/null | head -n 1)" ] || return 1
    sed -n 's/^rc=//p' "$FPGA_CACHE"
}
if [ -x "$FPGA_TOOL" ] && [ -d "$FPGA_DIR" ]; then
    rc=$(fpga_cached)
    if [ -z "$rc" ] && as_root env LOG_DIR=/tmp/system-update-check "$FPGA_TOOL" --probe >/dev/null 2>&1; then
        as_root timeout 90 env LOG_DIR=/tmp/system-update-check "$FPGA_TOOL" --check --image-dir "$FPGA_DIR" >/dev/null 2>&1
        rc=$?
        case $rc in 0|10) printf 'boot=%s\nrc=%s\n' "$BOOT_ID" "$rc" > "$FPGA_CACHE" 2>/dev/null ;; esac
    fi
    if [ "$rc" = 10 ]; then
        echo "FPGA update available"
        exit 0
    fi
fi

# 4. A system image on a USB stick
if [ -x "$AB_UPDATE" ] && [ -x "$SCAN" ]; then
    out=$(as_root timeout 60 "$SCAN" 2>/dev/null)
    echo "$out" | awk '
        /^BUNDLE / { n++; for (i = 2; i <= NF; i++) { split($i, kv, "="); b[kv[1]] = kv[2] } }
        /^SUMMARY / { for (i = 2; i <= NF; i++) { split($i, kv, "="); s[kv[1]] = kv[2] } }
        END {
            if (n == 1 && s["layout"] == "ab" && b["signature"] == "ok" && b["version"] != s["running"])
                print "Image update on USB"
        }'
fi
exit 0
