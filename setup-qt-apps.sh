#!/bin/bash
# Setup script for Qt applications deployment
# Usage: ./setup-qt-apps.sh --output=/path/to/output

set -e  # Exit on error

# Parse command line arguments
OUTPUT_DIR=""
for arg in "$@"; do
    case $arg in
        --output=*)
            OUTPUT_DIR="${arg#*=}"
            shift
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 --output=/path/to/output"
            exit 1
            ;;
    esac
done

if [ -z "$OUTPUT_DIR" ]; then
    echo "Error: --output parameter is required"
    echo "Usage: $0 --output=/path/to/output"
    exit 1
fi

# Get the script directory (where this script is located)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Get the real user (in case script is run with sudo)
REAL_USER="${SUDO_USER:-$USER}"
REAL_UID="${SUDO_UID:-$(id -u)}"
REAL_GID="${SUDO_GID:-$(id -g)}"

echo "Script directory: $SCRIPT_DIR"
echo "Output directory: $OUTPUT_DIR"
echo "Running as user: $REAL_USER (UID: $REAL_UID, GID: $REAL_GID)"
echo ""

# Step 1: Install Qt5 dependencies (requires sudo)
echo "=== Step 1: Installing Qt5 dependencies (requires sudo) ==="
if [ "$EUID" -ne 0 ]; then
    echo "Installing packages with sudo..."
    sudo apt-get update
    sudo apt-get install -y \
        qtbase5-dev \
        qtbase5-dev-tools \
        qt5-qmake \
        libqt5widgets5 \
        libqt5gui5 \
        libqt5core5a \
        libqt5network5 \
        fonts-dejavu
else
    echo "Running as root, installing packages directly..."
    apt-get update
    apt-get install -y \
        qtbase5-dev \
        qtbase5-dev-tools \
        qt5-qmake \
        libqt5widgets5 \
        libqt5gui5 \
        libqt5core5a \
        libqt5network5 \
        fonts-dejavu
fi

# Drop privileges for build and copy operations
run_as_user() {
    if [ "$EUID" -eq 0 ]; then
        # Running as root, drop to real user
        su -c "$*" "$REAL_USER"
    else
        # Already running as user
        bash -c "$*"
    fi
}

echo ""
echo "=== Step 2: Building qt-demo-launcher (as user $REAL_USER) ==="
run_as_user "cd '$SCRIPT_DIR/package/qt-demo-launcher/src' && \
    sed -i 's|^\([[:space:]]*\)#include \"NetworkInterface\.moc\"|\1//#include \"NetworkInterface.moc\"|' NetworkInterface.cpp && \
    qmake && make"

echo ""
echo "=== Step 3: Building disp-tester (as user $REAL_USER) ==="
run_as_user "cd '$SCRIPT_DIR/package/disp-tester/src' && qmake && make"

echo ""
echo "=== Step 4: Building touch-gallery (as user $REAL_USER) ==="
run_as_user "cd '$SCRIPT_DIR/package/touch-gallery/src' && qmake && make"

echo ""
echo "=== Step 5: Building qt-mpv-wrapper (as user $REAL_USER) ==="
run_as_user "cd '$SCRIPT_DIR/package/qt-mpv-wrapper/src' && qmake && make"

echo ""
echo "=== Step 6: Creating output directory structure (as user $REAL_USER) ==="
run_as_user "mkdir -p '$OUTPUT_DIR' && \
    mkdir -p '$OUTPUT_DIR/Patterns' && \
    mkdir -p '$OUTPUT_DIR/Pictures' && \
    mkdir -p '$OUTPUT_DIR/Videos'"

echo ""
echo "=== Step 7: Copying binaries and configs to $OUTPUT_DIR (as user $REAL_USER) ==="
run_as_user "cd '$SCRIPT_DIR' && \
    cp package/qt-demo-launcher/src/qt-demo-launcher '$OUTPUT_DIR/' && \
    cp package/qt-demo-launcher/src/qt-demo-launcher.json '$OUTPUT_DIR/' && \
    cp package/qt-demo-launcher/src/qt-demo-launcher.service '$OUTPUT_DIR/' && \
    cp package/qt-demo-launcher/update-config-paths.sh '$OUTPUT_DIR/' && \
    cp package/disp-tester/src/disp-tester '$OUTPUT_DIR/' && \
    cp package/touch-gallery/src/touch-gallery '$OUTPUT_DIR/' && \
    cp package/qt-mpv-wrapper/src/qt-mpv-wrapper '$OUTPUT_DIR/' && \
    cp package/qt-mpv-wrapper/src/input-monitor '$OUTPUT_DIR/' && \
    cp package/qt-mpv-wrapper/src/qt-mpv-wrapper.sh '$OUTPUT_DIR/' && \
    cp package/qt-mpv-wrapper/src/mpv-input.conf '$OUTPUT_DIR/' && \
    cp package/qt-demo-launcher/99-input-permissions.rules '$OUTPUT_DIR/'"

echo ""
echo "=== Step 8: Copying sample Patterns and Pictures (as user $REAL_USER) ==="
if [ -d "$SCRIPT_DIR/board/pi4-touch-demo/fs-overlay/Patterns" ]; then
    echo "Copying Patterns from board/pi4-touch-demo/fs-overlay/Patterns/..."
    run_as_user "cp -r '$SCRIPT_DIR/board/pi4-touch-demo/fs-overlay/Patterns/'* '$OUTPUT_DIR/Patterns/' 2>/dev/null || true"
else
    echo "Warning: Patterns directory not found at board/pi4-touch-demo/fs-overlay/Patterns/"
fi

if [ -d "$SCRIPT_DIR/board/pi4-touch-demo/fs-overlay/Pictures" ]; then
    echo "Copying Pictures from board/pi4-touch-demo/fs-overlay/Pictures/..."
    run_as_user "cp -r '$SCRIPT_DIR/board/pi4-touch-demo/fs-overlay/Pictures/'* '$OUTPUT_DIR/Pictures/' 2>/dev/null || true"
else
    echo "Warning: Pictures directory not found at board/pi4-touch-demo/fs-overlay/Pictures/"
fi

echo ""
echo "=== Step 9: Updating JSON configuration paths (as user $REAL_USER) ==="
run_as_user "cd '$OUTPUT_DIR' && \
    chmod +x update-config-paths.sh && \
    ./update-config-paths.sh"

echo ""
echo "=== Step 10: Setting permissions (as user $REAL_USER) ==="
run_as_user "cd '$OUTPUT_DIR' && \
    chmod +x qt-demo-launcher && \
    chmod +x disp-tester && \
    chmod +x touch-gallery && \
    chmod +x qt-mpv-wrapper && \
    chmod +x qt-mpv-wrapper.sh && \
    chmod +x input-monitor"

echo ""
echo "============================================"
echo "✓ Build and deployment complete!"
echo "============================================"
echo ""
echo "Output directory contents:"
ls -lh "$OUTPUT_DIR"
echo ""
echo "Directory structure:"
tree -L 2 "$OUTPUT_DIR" 2>/dev/null || find "$OUTPUT_DIR" -type f -o -type d | sort
echo ""
echo "Next steps:"
echo "1. Copy $OUTPUT_DIR/99-input-permissions.rules to /etc/udev/rules.d/"
echo "2. Copy $OUTPUT_DIR/qt-demo-launcher.service to /etc/systemd/system/"
echo "3. Run: sudo systemctl daemon-reload"
echo "4. Run: sudo systemctl enable qt-demo-launcher.service"
echo "5. Run: sudo systemctl start qt-demo-launcher.service"
echo "6. Reboot to ensure udev rules take effect"
echo ""
