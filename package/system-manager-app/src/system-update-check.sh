#!/bin/sh
# system-update-check.sh - one line for the launcher's System Manager badge.
#
# Prints "Update available" when a board carries firmware other than the image
# this system ships (update-iocs.sh --check --image-dir, read-only: register
# reads and a file read, no reset, no effect on the display), and nothing
# otherwise. qt-demo-launcher runs it through the button's "badge_command"
# after start-up and each time an app exits, so it never runs while System
# Manager is updating; the lock file covers a manual run on top of that.
#
# Paths follow the install layout, like system-manager-app: the update tool
# beside this script, the images in ../share/sp6bins/firmware/bios-bin.
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
TOOL=${UPDATE_IOCS:-$HERE/update-iocs.sh}
DIR=${IMAGE_DIR:-$(dirname "$HERE")/share/sp6bins/firmware/bios-bin}

[ -e /tmp/system-update.lock ] && exit 0
[ -x "$TOOL" ] && [ -d "$DIR" ] || exit 0

if [ "$(id -u)" -eq 0 ]; then
    out=$(timeout 60 "$TOOL" --check --image-dir "$DIR" 2>/dev/null)
else
    out=$(sudo -n timeout 60 "$TOOL" --check --image-dir "$DIR" 2>/dev/null)
fi
echo "$out" | grep -q "^RESULT .*status=outdated" && echo "Update available"
exit 0
