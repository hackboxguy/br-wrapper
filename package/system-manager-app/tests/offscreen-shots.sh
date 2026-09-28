#!/bin/bash
# offscreen-shots.sh <system-manager-app binary> <out dir> [WxH]
#
# Renders System Manager's screens without hardware, the way a reviewer can
# repeat it: QT_QPA_PLATFORM=offscreen, the software scene graph, --dry-run,
# tests/fake-ab-update for the engine, tests/fake-systemctl for the preflight,
# canned scan results, a canned update-iocs.sh for the firmware section, and a
# private data directory. Nothing here needs root or touches a device.
set -euo pipefail
app=$(realpath "$1"); out=$(realpath -m "$2"); size=${3:-1920x720}
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
mkdir -p "$out" "$work/run" "$work/data/logs/system-image-update" "$work/cases"

printf 'IMAGE_VERSION=02.03\nIMAGE_LAYOUT=ab\nIMAGE_VARIANT=base\n' > "$work/manifest.env"
printf 'AB_RUNTIME_DIR=%s\nAB_HEALTH_UNITS=qt-demo-launcher.service micropanel.service\n' "$work/run" > "$work/ab.conf"
printf 'BUNDLE device=/dev/sda1 path=/micropanel-base-02.04.mpupdate bytes=798218240 version=02.04 variant=base boards=pi4 format=2 signature=ok\nSUMMARY sticks=1 filesystems=1 bundles=1 nested=0 running=02.03 layout=ab\n' > "$work/cases/ready"
printf 'SUMMARY sticks=0 filesystems=0 bundles=0 nested=0 running=02.03 layout=ab\n' > "$work/cases/nostick"
cat > "$work/update-iocs.sh" <<'EOF'
#!/bin/sh
echo "RESULT board=ots status=uptodate version=0114 image=REMOTE_DISP_OTS_display_manager_ota.bin file_version=01.14"
echo "RESULT board=983 status=outdated version=0108 image=983HH_983_manager_ota.bin file_version=01.09"
exit 10
EOF
chmod +x "$work/update-iocs.sh"
# The engine stand-in, with its runtime directory pinned to this run's
printf '#!/bin/sh\nexport AB_RUNTIME_DIR=%s FAKE_AB_STEP=${FAKE_AB_STEP:-1}\nexec %s "$@"\n' "$work/run" "$here/fake-ab-update" > "$work/ab-update"
chmod +x "$work/ab-update"

export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
export AB_UPDATE_CONFIG=$work/ab.conf SYSTEM_MANAGER_DATA=$work/data

shot() { # <name> <case> <extra app args...>
    local name=$1 case=$2; shift 2
    SYSTEM_IMAGE_SCAN_FAKE=$work/cases/$case "$app" --dry-run --window-size "$size" \
        --ab-update "$work/ab-update" --runtime-dir "$work/run" --image-manifest "$work/manifest.env" \
        --update-tool "$work/update-iocs.sh" --image-dir "$work" \
        --systemctl "$here/fake-systemctl" --screenshot "$out/$name.png" "$@" 2>&1 \
        | grep -E 'preflight|screenshot' | sed "s/^/  [$name] /"
}

rm -f "$work/run/status"
FAKE_DOWN= FAKE_RESTARTED= shot image-offer ready --section image
FAKE_DOWN=qt-demo-launcher.service shot image-offer-unit-down ready --section image
FAKE_RESTARTED=micropanel.service=2505 shot image-offer-unit-restarted ready --section image
FAKE_AB_STEP=1 shot image-installing ready --section image --auto-install --screenshot-delay 11000
echo state=candidate-armed > "$work/run/status"
shot image-verifying nostick --section image
echo state=fallback > "$work/run/status"
printf 'version=02.04\nfrom=02.03\n' > "$work/data/logs/system-image-update/last-install"
shot image-fallback nostick --section image
echo "  acknowledged-fallback after opening the section: $(cat "$work/data/acknowledged-fallback" 2>/dev/null || echo '(none)')"
rm -f "$work/run/status"
shot firmware ready --section firmware
ls -1 "$out"
