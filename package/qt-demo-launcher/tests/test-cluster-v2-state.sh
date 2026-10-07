#!/bin/sh
# shellcheck disable=SC2016 # the checks are eval'd strings, expanded when run
# Host test for cluster-v2.sh's remembered states: the DMS panel and the map.
# Runs the script against a fake install whose "binary" records its
# arguments, with systemctl and sudo stubbed and the data directory and log
# moved into a temporary directory. Run: sh tests/test-cluster-v2-state.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SCRIPT=$HERE/../src/cluster-v2.sh
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail=0 pass=0
check() { if eval "$2"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAILED: $1"; echo "  args: $(cat "$T/args" 2>/dev/null)"; fi; }

mkdir -p "$T/home/bin" "$T/home/systemd" "$T/data" "$T/stub"
printf '#!/bin/sh\nexit 0\n' > "$T/stub/systemctl"
printf '#!/bin/sh\nexit 1\n' > "$T/stub/sudo"
chmod +x "$T/stub/"*
echo 'CLUSTER_ARGS=--source=proxy --map-backdrop=on' > "$T/home/systemd/qt-cluster-demo.env"

# fake_binary <knows the options?>: echoes its arguments into $T/args
fake_binary() {
    {
        echo '#!/bin/sh'
        echo "echo \"\$*\" > '$T/args'; echo \"\${QT_QPA_EGLFS_KMS_CONFIG:-}\" > '$T/kmsenv'"
        [ "$1" = yes ] && echo '# map-backdrop-state dms-video-view-state on, off or none'
    } > "$T/home/bin/qt-cluster-demo"
    chmod +x "$T/home/bin/qt-cluster-demo"
}
run() { rm -f "$T/args" "$T/kmsenv"; PATH="$T/stub:$PATH" CLUSTER_V2_HOME="$T/home" CLUSTER_V2_DATA_DIR="$T/data" \
            CLUSTER_V2_LOG="$T/log" CLUSTER_V2_DRM="$T/drm" sh "$SCRIPT" --theme=analog; }

fake_binary yes
run
check "no map state file: the env's choice, the state file passed" \
    'grep -q -- "--map-backdrop=on --theme=analog --map-backdrop-state=$T/data/map-backdrop.state$" "$T/args"'

echo off > "$T/data/map-backdrop.state"; run
check "map off remembered: --map-backdrop=off after the env's" \
    'grep -q -- "--map-backdrop-state=$T/data/map-backdrop.state --map-backdrop=off$" "$T/args"'
check "the log says so" 'grep -q "map: off (last choice)" "$T/log"'

echo on > "$T/data/map-backdrop.state"; run
check "map on remembered" 'grep -q -- "--map-backdrop=on$" "$T/args"'

echo junk > "$T/data/map-backdrop.state"; run
check "junk in the file: ignored, the env's choice stays" \
    'grep -q -- "--map-backdrop-state=$T/data/map-backdrop.state$" "$T/args"'

echo off > "$T/data/map-backdrop.state"; fake_binary no; run
check "a binary without the option gets neither" '! grep -q -- "map-backdrop-state\|--map-backdrop=off" "$T/args"'

fake_binary yes; chmod a-w "$T/data"; run; chmod u+w "$T/data"
if [ "$(id -u)" != 0 ]; then
    check "a read-only data directory: nothing passed" '! grep -q -- "map-backdrop-state" "$T/args"'
fi

# The DMS panel's state (only with DMS_ENABLED=1) next to the map's
echo 'DMS_ENABLED=1' >> "$T/home/systemd/qt-cluster-demo.env"
echo none > "$T/data/dms-video-view.state"; echo off > "$T/data/map-backdrop.state"; run
check "both remembered" 'grep -q -- "--dms-video-view=none" "$T/args" && grep -q -- "--map-backdrop=off" "$T/args"'

# Two displays: the second mirrors the first; one: nothing
mkdir -p "$T/drm/card1-HDMI-A-1" "$T/drm/card1-HDMI-A-2"
echo connected > "$T/drm/card1-HDMI-A-1/status"; echo disconnected > "$T/drm/card1-HDMI-A-2/status"
run
check "one display: no KMS configuration" '[ ! -s "$T/kmsenv" ] || [ "$(cat "$T/kmsenv")" = "" ]'
echo connected > "$T/drm/card1-HDMI-A-2/status"; run
check "two displays: HDMI2 clones HDMI1" 'grep -q "\"name\": \"HDMI2\", \"clones\": \"HDMI1\"" "$(cat "$T/kmsenv")"'
check "the configuration is JSON" 'python3 -c "import json,sys; json.load(open(sys.argv[1]))" "$(cat "$T/kmsenv")"'
check "the log says so" 'grep -q "displays: HDMI1, mirrored on HDMI2" "$T/log"'
echo 'KMS_MIRROR=0' >> "$T/home/systemd/qt-cluster-demo.env"; run
check "KMS_MIRROR=0: no mirror" '[ "$(cat "$T/kmsenv")" = "" ]'
sed -i '/KMS_MIRROR/d' "$T/home/systemd/qt-cluster-demo.env"
QT_QPA_EGLFS_KMS_CONFIG=/etc/own.json run
check "an operator's own KMS configuration is kept" '[ "$(cat "$T/kmsenv")" = /etc/own.json ]'

echo "test-cluster-v2-state: $pass passed, $fail failed"
[ "$fail" = 0 ]
