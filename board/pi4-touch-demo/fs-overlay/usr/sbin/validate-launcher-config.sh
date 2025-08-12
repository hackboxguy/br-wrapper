#!/bin/sh
# JSON Configuration Validator for Touch Launcher
# Usage: ./validate_launcher_config.sh [config_file]
# POSIX compliant version for /bin/sh

CONFIG_FILE="${1:-/etc/launcher.json}"

echo "Validating launcher configuration: $CONFIG_FILE"
echo "================================================"

# Check if file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "[ERROR] Configuration file not found: $CONFIG_FILE"
    exit 1
fi

# Check if jq is available
if ! command -v jq >/dev/null 2>&1; then
    echo "[ERROR] jq is not installed. Please install jq to validate JSON files."
    exit 1
fi

# Validate JSON syntax
if ! jq empty "$CONFIG_FILE" 2>/dev/null; then
    echo "[ERROR] Invalid JSON syntax in $CONFIG_FILE"
    echo "Run: jq . '$CONFIG_FILE' to see detailed error"
    exit 1
fi

echo "[OK] JSON syntax is valid"

# Extract configuration values using jq
TITLE_TEXT=$(jq -r '.launcher.title.text // "N/A"' "$CONFIG_FILE")
TITLE_LOGO=$(jq -r '.launcher.title.logo // "N/A"' "$CONFIG_FILE")
WINDOW_WIDTH=$(jq -r '.launcher.window.width // "800"' "$CONFIG_FILE")
WINDOW_HEIGHT=$(jq -r '.launcher.window.height // "600"' "$CONFIG_FILE")
LAYOUT_TYPE=$(jq -r '.launcher.layout.type // "vertical"' "$CONFIG_FILE")

echo ""
echo "Configuration Summary:"
echo "====================="
echo "Title Text: $TITLE_TEXT"
echo "Title Logo: $TITLE_LOGO"
echo "Window Size: ${WINDOW_WIDTH}x${WINDOW_HEIGHT}"
echo "Layout Type: $LAYOUT_TYPE"

# Check logo file if specified
if [ "$TITLE_LOGO" != "N/A" ] && [ "$TITLE_LOGO" != "null" ]; then
    if [ -f "$TITLE_LOGO" ]; then
        echo "[OK] Logo file found: $TITLE_LOGO"
    else
        echo "[WARN] Logo file not found: $TITLE_LOGO"
    fi
fi

echo ""
echo "Button Configuration:"
echo "===================="

# Check each button
BUTTON_COUNT=$(jq '.launcher.buttons | length' "$CONFIG_FILE")
echo "Total buttons defined: $BUTTON_COUNT"

# Use while loop instead of for with seq (more portable)
i=0
while [ $i -lt "$BUTTON_COUNT" ]; do
    BUTTON_ID=$(jq -r ".launcher.buttons[$i].id" "$CONFIG_FILE")
    BUTTON_ENABLED=$(jq -r ".launcher.buttons[$i].enabled" "$CONFIG_FILE")
    BUTTON_TEXT=$(jq -r ".launcher.buttons[$i].text" "$CONFIG_FILE")
    BUTTON_PROGRAM=$(jq -r ".launcher.buttons[$i].program // \"N/A\"" "$CONFIG_FILE")
    BUTTON_ICON=$(jq -r ".launcher.buttons[$i].icon // \"N/A\"" "$CONFIG_FILE")
    BUTTON_ACTION=$(jq -r ".launcher.buttons[$i].action // \"N/A\"" "$CONFIG_FILE")
    
    echo ""
    echo "Button $((i+1)): $BUTTON_ID"
    echo "  Enabled: $BUTTON_ENABLED"
    echo "  Text: $BUTTON_TEXT"
    
    if [ "$BUTTON_ENABLED" = "true" ]; then
        # Check program file
        if [ "$BUTTON_PROGRAM" != "N/A" ] && [ "$BUTTON_PROGRAM" != "null" ]; then
            if [ -x "$BUTTON_PROGRAM" ]; then
                echo "  [OK] Program: $BUTTON_PROGRAM (executable)"
            elif [ -f "$BUTTON_PROGRAM" ]; then
                echo "  [WARN] Program: $BUTTON_PROGRAM (not executable)"
            else
                echo "  [ERROR] Program: $BUTTON_PROGRAM (not found)"
            fi
        elif [ "$BUTTON_ACTION" != "N/A" ] && [ "$BUTTON_ACTION" != "null" ]; then
            echo "  [OK] Action: $BUTTON_ACTION"
        else
            echo "  [ERROR] No program or action defined"
        fi
        
        # Check icon file
        if [ "$BUTTON_ICON" != "N/A" ] && [ "$BUTTON_ICON" != "null" ]; then
            if [ -f "$BUTTON_ICON" ]; then
                echo "  [OK] Icon: $BUTTON_ICON"
            else
                echo "  [WARN] Icon: $BUTTON_ICON (not found)"
            fi
        else
            echo "  [INFO] No icon specified"
        fi
    else
        echo "  [DISABLED] Button disabled"
    fi
    
    i=$((i + 1))
done

echo ""
echo "Validation complete!"

# Count enabled buttons
ENABLED_COUNT=$(jq '[.launcher.buttons[] | select(.enabled == true)] | length' "$CONFIG_FILE")
echo "Enabled buttons: $ENABLED_COUNT"

# Check if any buttons are enabled
if [ "$ENABLED_COUNT" -eq 0 ]; then
    echo "[WARN] No buttons are enabled!"
fi

echo ""
echo "To test the configuration:"
echo "========================="
echo "1. Copy your JSON to /etc/launcher.json"
echo "2. Run the launcher application"
echo "3. Check console output for any runtime errors"

echo ""
echo "JSON Structure Check:"
echo "===================="

# Verify required structure exists - using simpler approach for /bin/sh
if jq -e ".launcher" "$CONFIG_FILE" >/dev/null 2>&1; then
    echo "[OK] .launcher exists"
else
    echo "[ERROR] Missing required path: .launcher"
fi

if jq -e ".launcher.buttons" "$CONFIG_FILE" >/dev/null 2>&1; then
    echo "[OK] .launcher.buttons exists"
else
    echo "[ERROR] Missing required path: .launcher.buttons"
fi

# Additional helpful checks
echo ""
echo "Additional Checks:"
echo "=================="

# Check for common directories
if [ -d "/Pictures" ]; then
    echo "[OK] /Pictures directory exists"
else
    echo "[WARN] /Pictures directory does not exist"
    echo "       Run: mkdir -p /Pictures"
fi

if [ -d "/usr/share/icons" ]; then
    echo "[OK] /usr/share/icons directory exists"
else
    echo "[WARN] /usr/share/icons directory does not exist"
fi

# Check if launcher executable might exist
if [ -f "/usr/bin/qt-demo-launcher" ]; then
    echo "[OK] Launcher executable found at /usr/bin/qt-demo-launcher"
elif [ -f "./qt-demo-launcher" ]; then
    echo "[OK] Launcher executable found in current directory"
else
    echo "[INFO] Launcher executable location not automatically detected"
fi
