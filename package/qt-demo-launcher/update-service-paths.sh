#!/bin/bash
# Update qt-demo-launcher.service to use custom installation paths
#
# Usage: ./update-service-paths.sh [BASE_PATH]
#   BASE_PATH: Installation prefix (default: /home/pi/micropanel/qt-apps)
#
# Example: ./update-service-paths.sh /home/testpc/qt-apps

SERVICE_FILE="qt-demo-launcher.service"
BASE_PATH="${1:-/home/pi/micropanel/qt-apps}"

if [ ! -f "$SERVICE_FILE" ]; then
    echo "Error: $SERVICE_FILE not found!"
    exit 1
fi

echo "Updating paths in $SERVICE_FILE..."
echo "Base path: $BASE_PATH"

# Backup original
cp "$SERVICE_FILE" "$SERVICE_FILE.backup"
echo "Backup saved as $SERVICE_FILE.backup"

# Update Documentation path (matches to end of line only)
sed -i "s|^\(Documentation=file://\)[^[:space:]]*\(/README.md\).*|\1$BASE_PATH/share/qt-apps\2|" "$SERVICE_FILE"

# Update WorkingDirectory (matches to end of line only)
sed -i "s|^\(WorkingDirectory=\).*|\1$BASE_PATH/share/qt-apps|" "$SERVICE_FILE"

# Update ExecStartPre directory creation paths (specific path matching)
sed -i "s|\(ExecStartPre=/bin/mkdir -p \)[^[:space:]]*/Pictures|\1$BASE_PATH/share/qt-apps/Pictures|g" "$SERVICE_FILE"
sed -i "s|\(ExecStartPre=/bin/mkdir -p \)[^[:space:]]*/Patterns|\1$BASE_PATH/share/qt-apps/Patterns|g" "$SERVICE_FILE"
sed -i "s|\(ExecStartPre=/bin/mkdir -p \)[^[:space:]]*/Videos|\1$BASE_PATH/share/qt-apps/Videos|g" "$SERVICE_FILE"

# Update ExecStart - both binary path and config path (match only the path parts)
sed -i "s|\(^ExecStart=\)[^[:space:]]*/qt-demo-launcher\( --config \)[^[:space:]]*/qt-demo-launcher.json|\1$BASE_PATH/bin/qt-demo-launcher\2$BASE_PATH/share/qt-apps/qt-demo-launcher.json|" "$SERVICE_FILE"

echo ""
echo "✓ Updated Documentation path:"
grep -E '^Documentation=' "$SERVICE_FILE" | sed 's/^/  /'

echo ""
echo "✓ Updated WorkingDirectory:"
grep -E '^WorkingDirectory=' "$SERVICE_FILE" | sed 's/^/  /'

echo ""
echo "✓ Updated ExecStartPre paths:"
grep -E 'ExecStartPre=.*/(Pictures|Patterns|Videos)' "$SERVICE_FILE" | sed 's/^[[:space:]]*/  /'

echo ""
echo "✓ Updated ExecStart:"
grep -E '^ExecStart=' "$SERVICE_FILE" | sed 's/^/  /'

echo ""
echo "Done! Service file updated successfully."
