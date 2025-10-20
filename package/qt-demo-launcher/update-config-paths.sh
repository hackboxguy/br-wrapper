#!/bin/bash
# Update qt-demo-launcher.json to use /home/pi/micropanel/qt-apps/ paths

CONFIG_FILE="qt-demo-launcher.json"
BASE_PATH="/home/pi/micropanel/qt-apps"

if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: $CONFIG_FILE not found!"
    exit 1
fi

echo "Updating paths in $CONFIG_FILE..."
echo "Base path: $BASE_PATH"

# Backup original
cp "$CONFIG_FILE" "$CONFIG_FILE.backup"
echo "Backup saved as $CONFIG_FILE.backup"

# Update binary paths
sed -i "s|/usr/bin/touch-gallery|$BASE_PATH/touch-gallery|g" "$CONFIG_FILE"
sed -i "s|/usr/bin/disp-tester|$BASE_PATH/disp-tester|g" "$CONFIG_FILE"
sed -i "s|/usr/lib/qt/examples/widgets/touch/fingerpaint/fingerpaint|$BASE_PATH/fingerpaint|g" "$CONFIG_FILE"
sed -i "s|/usr/bin/qt-mpv-wrapper.sh|$BASE_PATH/qt-mpv-wrapper.sh|g" "$CONFIG_FILE"

# Update qt-mpv-wrapper path - handle both possible structures:
# Option 1: /path/to/qt-mpv-wrapper/src/qt-mpv-wrapper.sh (development)
# Option 2: /path/to/qt-mpv-wrapper.sh (flat deployment)
sed -i "s|\"program\": \".*/qt-mpv-wrapper/src/qt-mpv-wrapper.sh\"|\"program\": \"$BASE_PATH/qt-mpv-wrapper.sh\"|g" "$CONFIG_FILE"
sed -i "s|\"program\": \".*/qt-mpv-wrapper.sh\"|\"program\": \"$BASE_PATH/qt-mpv-wrapper.sh\"|g" "$CONFIG_FILE"

# Update directory paths
sed -i "s|\"arguments\": \\[\"/Pictures\"|\"arguments\": [\"$BASE_PATH/Pictures\"|g" "$CONFIG_FILE"
sed -i "s|\"arguments\": \\[\"/Patterns\"|\"arguments\": [\"$BASE_PATH/Patterns\"|g" "$CONFIG_FILE"
sed -i "s|\"arguments\": \\[\"/Videos\"|\"arguments\": [\"$BASE_PATH/Videos\"|g" "$CONFIG_FILE"
sed -i "s|\"working_directory\": \"/Pictures\"|\"working_directory\": \"$BASE_PATH/Pictures\"|g" "$CONFIG_FILE"
sed -i "s|\"working_directory\": \"/Patterns\"|\"working_directory\": \"$BASE_PATH/Patterns\"|g" "$CONFIG_FILE"

# Handle slideshow arguments (has multiple args)
sed -i "s|\"arguments\": \\[\"/Pictures\", \"slideshow\"|\"arguments\": [\"$BASE_PATH/Pictures\", \"slideshow\"|g" "$CONFIG_FILE"

# Handle qt-mpv-wrapper arguments - update Videos path if it's absolute
sed -i "s|\"arguments\": \\[\".*/Videos\"|\"arguments\": [\"$BASE_PATH/Videos\"|g" "$CONFIG_FILE"

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
