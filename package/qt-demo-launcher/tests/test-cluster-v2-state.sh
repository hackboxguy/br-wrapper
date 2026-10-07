#!/bin/sh
# shellcheck disable=SC2016 # the checks are eval'd strings, expanded when run
# Host test for cluster-v2.sh's remembered states (the DMS panel, MAP, T, B)
# and the mirror onto a second display.
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
        [ "$1" = yes ] && echo '# map-backdrop-state telltale-min-dark-level-state info-bar-state dms-video-view-state on, off or none mirror-screen'
    } > "$T/home/bin/qt-cluster-demo"
    chmod +x "$T/home/bin/qt-cluster-demo"
}
run() { rm -f "$T/args" "$T/kmsenv"; PATH="$T/stub:$PATH" CLUSTER_V2_HOME="$T/home" CLUSTER_V2_DATA_DIR="$T/data" \
            CLUSTER_V2_LOG="$T/log" CLUSTER_V2_DRM="$T/drm" sh "$SCRIPT" --theme=analog; }

fake_binary yes
run
check "no map state file: the env's choice, the state file passed" \
    'grep -q -- "--map-backdrop=on --theme=analog --map-backdrop-state=$T/data/map-backdrop.state --telltale" "$T/args"'

echo off > "$T/data/map-backdrop.state"; run
check "map off remembered: --map-backdrop=off after the env's" \
    'grep -q -- "--map-backdrop-state=$T/data/map-backdrop.state --map-backdrop=off --telltale" "$T/args"'
check "the log says so" 'grep -q "map: off (last choice)" "$T/log"'

echo on > "$T/data/map-backdrop.state"; run
check "map on remembered" 'grep -q -- "--map-backdrop-state=$T/data/map-backdrop.state --map-backdrop=on " "$T/args"'

echo junk > "$T/data/map-backdrop.state"; run
check "junk in the file: ignored, the env's choice stays" \
    'grep -q -- "--map-backdrop-state=$T/data/map-backdrop.state --telltale" "$T/args"'

echo off > "$T/data/map-backdrop.state"; fake_binary no; run
check "a binary without the options gets none of them" '! grep -q -- "-state=\|--map-backdrop=off" "$T/args"'

fake_binary yes; chmod a-w "$T/data"; run; chmod u+w "$T/data"
if [ "$(id -u)" != 0 ]; then
    check "a read-only data directory: nothing passed" '! grep -q -- "-state=" "$T/args"'
fi

# T and B: their own files, passed back as their options
rm -f "$T/data/map-backdrop.state"
run
check "T and B: their state files passed" 'grep -q -- "--telltale-min-dark-level-state=$T/data/telltale-min-dark-level.state --info-bar-state=$T/data/info-bar.state$" "$T/args"'
echo on > "$T/data/telltale-min-dark-level.state"; echo off > "$T/data/info-bar.state"; run
check "T on and B off remembered" 'grep -q -- "--telltale-min-dark-level=on --info-bar-state=$T/data/info-bar.state --info-bar=off$" "$T/args"'
check "the log says so" 'grep -q "telltale ghost: on (last choice)" "$T/log" && grep -q "info bar: off (last choice)" "$T/log"'
rm -f "$T/data/telltale-min-dark-level.state" "$T/data/info-bar.state"

# The DMS panel's state (only with DMS_ENABLED=1) next to the map's
echo 'DMS_ENABLED=1' >> "$T/home/systemd/qt-cluster-demo.env"
echo none > "$T/data/dms-video-view.state"; echo off > "$T/data/map-backdrop.state"; run
check "both remembered" 'grep -q -- "--dms-video-view=none" "$T/args" && grep -q -- "--map-backdrop=off" "$T/args"'

# Two displays: the app's second window (KMS_MIRROR=auto); one: nothing
mkdir -p "$T/drm/card1-HDMI-A-1" "$T/drm/card1-HDMI-A-2"
echo connected > "$T/drm/card1-HDMI-A-1/status"; echo disconnected > "$T/drm/card1-HDMI-A-2/status"
run
check "one display: no mirror, no KMS configuration" '! grep -q -- "--mirror-screen" "$T/args" && [ "$(cat "$T/kmsenv")" = "" ]'
echo connected > "$T/drm/card1-HDMI-A-2/status"; run
check "two displays: a second window on HDMI2" 'grep -q -- "--mirror-screen=HDMI2" "$T/args" && [ "$(cat "$T/kmsenv")" = "" ]'
check "  the log says so" 'grep -q "displays: HDMI1, and a second window on HDMI2" "$T/log"'
fake_binary no; run; fake_binary yes
check "an older cluster: one display, no clone" '! grep -q -- "--mirror-screen" "$T/args" && [ "$(cat "$T/kmsenv")" = "" ] && grep -q "has no --mirror-screen" "$T/log"'
echo 'KMS_MIRROR=0' >> "$T/home/systemd/qt-cluster-demo.env"; run
check "KMS_MIRROR=0: one display" '! grep -q -- "--mirror-screen" "$T/args" && [ "$(cat "$T/kmsenv")" = "" ]'
sed -i '/KMS_MIRROR/d' "$T/home/systemd/qt-cluster-demo.env"

# KMS_MIRROR=clone: Qt's clone, watched. The fake app, run with the clone,
# reports the frozen clone's lock failures and keeps running; the script
# must stop it and start it again on one display, once, with one log line
echo 'KMS_MIRROR=clone' >> "$T/home/systemd/qt-cluster-demo.env"
cat > "$T/home/bin/qt-cluster-demo" <<EOF
#!/bin/sh
# mirror-screen (the option text the script looks for)
echo "\$*" > '$T/args'; echo "\${QT_QPA_EGLFS_KMS_CONFIG:-}" >> '$T/kmsenv'
if [ -n "\${QT_QPA_EGLFS_KMS_CONFIG:-}" ]; then
    grep -q '"name": "HDMI2", "clones": "HDMI1"' "\$QT_QPA_EGLFS_KMS_CONFIG" && echo clone-config-ok >> '$T/kmsenv'
    i=0; while [ \$i -lt 40 ]; do echo "Could not lock GBM surface front buffer!"; i=\$((i + 1)); sleep 0.05; done
    sleep 30
fi
EOF
chmod +x "$T/home/bin/qt-cluster-demo"
rm -f "$T/kmsenv"; CLUSTER_V2_WATCH_S=0.2 run
check "clone: Qt's clone configuration first" 'grep -q clone-config-ok "$T/kmsenv"'
check "  restarted once, on one display" '[ "$(sed -n 3p "$T/kmsenv")" = "" ] && [ "$(wc -l < "$T/kmsenv")" = 3 ]'
check "  one line says so" '[ "$(grep -c "mirror: Qt.s output clone froze" "$T/log")" = 1 ]'
check "  no clone option to the app" '! grep -q -- "--mirror-screen" "$T/args"'
sed -i '/KMS_MIRROR/d' "$T/home/systemd/qt-cluster-demo.env"; fake_binary yes
echo 'KMS_MIRROR=clone' >> "$T/home/systemd/qt-cluster-demo.env"
QT_QPA_EGLFS_KMS_CONFIG=/etc/own.json run
check "an operator's own KMS configuration is kept" '[ "$(cat "$T/kmsenv")" = /etc/own.json ]'
sed -i '/KMS_MIRROR/d' "$T/home/systemd/qt-cluster-demo.env"

echo "test-cluster-v2-state: $pass passed, $fail failed"
[ "$fail" = 0 ]
