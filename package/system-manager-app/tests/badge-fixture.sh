#!/bin/sh
# badge-fixture.sh - system-update-check.sh against fake engine status,
# last-install / acknowledged-fallback records and canned scans. No device.
set -u
here=$(cd "$(dirname "$0")" && pwd)
check=$here/../src/system-update-check.sh
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
mkdir -p "$work/run" "$work/data/logs/system-image-update"
printf '#!/bin/sh\nexit 0\n' > "$work/ab-update"; chmod +x "$work/ab-update"
printf 'AB_RUNTIME_DIR=%s\n' "$work/run" > "$work/ab.conf"
printf '#!/bin/sh\ncat %s/scan\n' "$work" > "$work/scan-tool"; chmod +x "$work/scan-tool"
nostick='SUMMARY sticks=0 filesystems=0 bundles=0 nested=0 running=02.03 layout=ab'
ready='BUNDLE device=/dev/sda1 path=/b.mpupdate bytes=1 version=02.05 variant=base boards=pi4 format=2 signature=ok
SUMMARY sticks=1 filesystems=1 bundles=1 nested=0 running=02.03 layout=ab'

t() { # <label>
    out=$(AB_UPDATE=$work/ab-update AB_UPDATE_CONFIG=$work/ab.conf SYSTEM_MANAGER_DATA=$work/data \
          UPDATE_IOCS=/nonexistent SYSTEM_IMAGE_SCAN=$work/scan-tool sh "$check")
    printf '%-62s [%s]\n' "$1" "$out"
}
status() { printf '%s\n' "$@" > "$work/run/status"; }
last()   { printf 'version=%s\nfrom=02.03\n' "$1" > "$work/data/logs/system-image-update/last-install"; }
ack()    { printf 'version=%s\n' "$1" > "$work/data/acknowledged-fallback"; }

echo "$nostick" > "$work/scan"
status state=fallback; last 02.04; rm -f "$work/data/acknowledged-fallback"
t "fallback of 02.04, not acknowledged"
ack 02.04
t "fallback of 02.04, acknowledged 02.04"
echo "$ready" > "$work/scan"
t "same, and an installable bundle on the stick"
echo "$nostick" > "$work/scan"
last 02.05
t "a later fallback (last-install 02.05), ack still 02.04"
status state=fallback version=02.06
t "engine publishes version=02.06, ack 02.04"
ack 02.06
t "engine publishes version=02.06, ack 02.06"
status state=fallback; rm -f "$work/data/logs/system-image-update/last-install"; ack -
t "no version anywhere, ack '-'"
rm -f "$work/data/acknowledged-fallback"
t "no version anywhere, no ack"
status state=committed
t "committed"
