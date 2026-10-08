#!/bin/sh
# shellcheck disable=SC2016 # the checks are eval'd strings, expanded when run
# Host test for kodi-usb-common.sh's find_usb_video: Videos/ first, then the
# stick's top level, no stick or no video -> nothing. The stick is a temp
# directory (the device detection and mount are stubbed).
# Run: sh tests/test-kodi-usb-video.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail=0 pass=0
check() { if eval "$2"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAILED: $1 (got '$got')"; fi; }
. "$HERE/../src/kodi-usb-common.sh"
_detect_usb_device() { [ -e "$T/plugged" ] && echo /dev/sdz1; }
_get_usb_mount() { echo "$T/stick"; }
mkdir -p "$T/stick"

got=$(find_usb_video); check "no stick: nothing" '[ -z "$got" ]'
: > "$T/plugged"
got=$(find_usb_video); check "an empty stick: nothing" '[ -z "$got" ]'
: > "$T/stick/sample-video.mp4"
got=$(find_usb_video); check "a clip at the top level" '[ "$got" = "$T/stick/sample-video.mp4" ]'
mkdir -p "$T/stick/Videos"
got=$(find_usb_video); check "an empty Videos/: the top-level clip" '[ "$got" = "$T/stick/sample-video.mp4" ]'
: > "$T/stick/Videos/demo.mp4"
got=$(find_usb_video); check "Videos/ wins over the top level" '[ "$got" = "$T/stick/Videos/demo.mp4" ]'
: > "$T/stick/notes.txt"; rm -f "$T/stick/Videos/demo.mp4" "$T/stick/sample-video.mp4"
got=$(find_usb_video); check "no video anywhere: nothing" '[ -z "$got" ]'

echo "test-kodi-usb-video: $pass passed, $fail failed"
[ "$fail" = 0 ]
