#!/bin/bash
# Update qt-demo-launcher.json to use custom installation paths
#
# Usage: ./update-config-paths.sh [BASE_PATH]
#   BASE_PATH: Installation prefix (default: /home/pi/micropanel/qt-apps)
#
# Example: ./update-config-paths.sh /home/testpc/qt-apps

CONFIG_FILE="qt-demo-launcher.json"
BASE_PATH="${1:-/home/pi/micropanel/qt-apps}"

if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: $CONFIG_FILE not found!"
    exit 1
fi

echo "Updating paths in $CONFIG_FILE..."
echo "Base path: $BASE_PATH"

# Backup original
cp "$CONFIG_FILE" "$CONFIG_FILE.backup"
echo "Backup saved as $CONFIG_FILE.backup"

# Update binary paths in "program" fields (focus on program paths, not icons)
# Binaries are in ${BASE_PATH}/bin/
sed -i "s|/usr/bin/touch-gallery|$BASE_PATH/bin/touch-gallery|g" "$CONFIG_FILE"
sed -i "s|/usr/bin/disp-tester|$BASE_PATH/bin/disp-tester|g" "$CONFIG_FILE"
sed -i "s|/usr/bin/disp-settings|$BASE_PATH/bin/disp-settings|g" "$CONFIG_FILE"

# Update fingerpaint to system Qt examples location (not part of our build)
# Detect architecture and use appropriate path
if [ -f "/usr/lib/aarch64-linux-gnu/qt5/examples/widgets/touch/fingerpaint/fingerpaint" ]; then
    FINGERPAINT_PATH="/usr/lib/aarch64-linux-gnu/qt5/examples/widgets/touch/fingerpaint/fingerpaint"
elif [ -f "/usr/lib/arm-linux-gnueabihf/qt5/examples/widgets/touch/fingerpaint/fingerpaint" ]; then
    FINGERPAINT_PATH="/usr/lib/arm-linux-gnueabihf/qt5/examples/widgets/touch/fingerpaint/fingerpaint"
elif [ -f "/usr/lib/x86_64-linux-gnu/qt5/examples/widgets/touch/fingerpaint/fingerpaint" ]; then
    FINGERPAINT_PATH="/usr/lib/x86_64-linux-gnu/qt5/examples/widgets/touch/fingerpaint/fingerpaint"
else
    # Fallback - keep as is if fingerpaint not found
    FINGERPAINT_PATH="/usr/lib/qt/examples/widgets/touch/fingerpaint/fingerpaint"
fi
sed -i "s|/usr/lib/qt/examples/widgets/touch/fingerpaint/fingerpaint|$FINGERPAINT_PATH|g" "$CONFIG_FILE"

# kodi-launcher.sh, kodi-slideshow.sh and cluster-launcher.sh are in ${BASE_PATH}/share/qt-apps/
sed -i "s|/usr/bin/kodi-launcher.sh|$BASE_PATH/share/qt-apps/kodi-launcher.sh|g" "$CONFIG_FILE"
sed -i "s|/usr/share/qt-apps/kodi-launcher.sh|$BASE_PATH/share/qt-apps/kodi-launcher.sh|g" "$CONFIG_FILE"
sed -i "s|/usr/share/qt-apps/kodi-slideshow.sh|$BASE_PATH/share/qt-apps/kodi-slideshow.sh|g" "$CONFIG_FILE"
sed -i "s|/usr/share/qt-apps/kodi-video.sh|$BASE_PATH/share/qt-apps/kodi-video.sh|g" "$CONFIG_FILE"
sed -i "s|/usr/share/qt-apps/cluster-launcher.sh|$BASE_PATH/share/qt-apps/cluster-launcher.sh|g" "$CONFIG_FILE"
sed -i "s|/usr/share/qt-apps/display-analysis-report.sh|$BASE_PATH/share/qt-apps/display-analysis-report.sh|g" "$CONFIG_FILE"
sed -i "s|/usr/share/qt-apps/display-analysis-start-child.sh|$BASE_PATH/share/qt-apps/display-analysis-start-child.sh|g" "$CONFIG_FILE"

# Update directory paths in arguments (data is in ${BASE_PATH}/share/qt-apps/)
sed -i "s|\"arguments\": \\[\"/Pictures\"|\"arguments\": [\"$BASE_PATH/share/qt-apps/Pictures\"|g" "$CONFIG_FILE"
sed -i "s|\"arguments\": \\[\"/Patterns\"|\"arguments\": [\"$BASE_PATH/share/qt-apps/Patterns\"|g" "$CONFIG_FILE"
sed -i "s|\"working_directory\": \"/Pictures\"|\"working_directory\": \"$BASE_PATH/share/qt-apps/Pictures\"|g" "$CONFIG_FILE"
sed -i "s|\"working_directory\": \"/Patterns\"|\"working_directory\": \"$BASE_PATH/share/qt-apps/Patterns\"|g" "$CONFIG_FILE"

# Handle slideshow arguments (has multiple args)
sed -i "s|\"arguments\": \\[\"/Pictures\", \"slideshow\"|\"arguments\": [\"$BASE_PATH/share/qt-apps/Pictures\", \"slideshow\"|g" "$CONFIG_FILE"

# Handle qt-mpv-wrapper arguments - update Videos path if it's absolute
sed -i "s|\"arguments\": \\[\".*/Videos\"|\"arguments\": [\"$BASE_PATH/share/qt-apps/Videos\"|g" "$CONFIG_FILE"

echo ""
echo "✓ Updated program paths:"
grep -E '"program":' "$CONFIG_FILE" | sed 's/^[[:space:]]*/  /'

echo ""
echo "✓ Updated arguments:"
grep -E '"arguments":' "$CONFIG_FILE" | sed 's/^[[:space:]]*/  /'

echo ""
echo "✓ Updated working directories:"
grep -E '"working_directory":' "$CONFIG_FILE" | sed 's/^[[:space:]]*/  /'

echo ""
echo "Done! Configuration updated successfully."
