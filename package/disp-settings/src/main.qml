import QtQuick 2.12
import QtQuick.Window 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

Window {
    id: window
    visible: true
    width: Screen.width
    height: Screen.height
    title: "Display Settings"
    visibility: Window.FullScreen
    color: "#1a1a2e"

    // Track if user is dragging the brightness slider
    property bool userDraggingBrightness: false
    // Cooldown period after releasing slider - ignore brightness updates during this time
    property bool brightnessSetCooldown: false

    // Timer to clear cooldown after user releases slider
    Timer {
        id: brightnessCooldownTimer
        interval: 500  // 500ms cooldown
        onTriggered: brightnessSetCooldown = false
    }

    // Track user's preference for adaptive mode (separate from actual als-dimmer mode)
    // This allows the switch to stay ON even when user temporarily adjusts brightness
    property bool userPreferAdaptive: true
    property bool initialSyncDone: false

    // Adaptive layout: use two columns on wide screens (aspect ratio > 2.0)
    property bool wideScreen: Screen.width / Screen.height > 2.0

    // Sync user preference when we first receive the actual mode from als-dimmer
    Connections {
        target: alsDimmer
        function onConnectedChanged() {
            if (!alsDimmer.connected) {
                // Reset sync flag on disconnect so we re-sync on reconnect
                initialSyncDone = false;
            }
        }
        function onModeChanged() {
            // On first real mode update, sync user preference and slider from actual values
            if (!initialSyncDone && alsDimmer.mode !== "unknown") {
                userPreferAdaptive = (alsDimmer.mode === "auto");
                brightnessSlider.value = alsDimmer.brightness;  // Sync slider value on connect
                initialSyncDone = true;
                console.log("Initial sync: userPreferAdaptive =", userPreferAdaptive,
                            "brightness =", alsDimmer.brightness, "from mode:", alsDimmer.mode);
            }
            // If als-dimmer switches to full auto (e.g., user toggled switch), update preference
            else if (alsDimmer.mode === "auto") {
                userPreferAdaptive = true;
            }
            // When switching to manual mode, sync slider to actual brightness
            // als-dimmer may restore a different manual brightness value
            else if ((alsDimmer.mode === "manual" || alsDimmer.mode === "manual_temporary") && !userDraggingBrightness) {
                brightnessSlider.value = alsDimmer.brightness;
                console.log("Manual mode sync (mode): slider =", alsDimmer.brightness);
            }
        }
        function onBrightnessChanged() {
            // Sync slider when brightness changes in manual/manual_temporary mode
            // This catches external brightness changes (e.g., als-dimmer-client)
            // Skip during cooldown period (right after user released slider)
            if ((alsDimmer.mode === "manual" || alsDimmer.mode === "manual_temporary") && !userDraggingBrightness && !brightnessSetCooldown && initialSyncDone) {
                brightnessSlider.value = alsDimmer.brightness;
                console.log("Manual mode sync (brightness): slider =", alsDimmer.brightness);
            }
        }
    }

    // Header with title and exit button
    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 60
        color: "#16213e"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20

            Text {
                text: "Display Settings"
                font.pixelSize: 28
                font.bold: true
                color: "#ffffff"
                Layout.alignment: Qt.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            // Connection status indicator
            Rectangle {
                width: 12
                height: 12
                radius: 6
                color: alsDimmer.connected ? "#27ae60" : "#e74c3c"
                Layout.alignment: Qt.AlignVCenter

                ToolTip.visible: statusMouseArea.containsMouse
                ToolTip.text: alsDimmer.connected ? "Connected to als-dimmer" : "Disconnected from als-dimmer"

                MouseArea {
                    id: statusMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (!alsDimmer.connected) {
                            alsDimmer.reconnect();
                        }
                    }
                }
            }

            Text {
                text: "v" + appVersion
                font.pixelSize: 22
                color: "#888888"
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: 10
            }

            Rectangle {
                id: exitButton
                Layout.preferredWidth: 80
                Layout.preferredHeight: 40
                Layout.alignment: Qt.AlignVCenter
                radius: 5
                color: exitMouseArea.pressed ? "#c0392b" : "#2c3e50"

                Text {
                    anchors.centerIn: parent
                    text: "Exit"
                    font.pixelSize: 24
                    color: "#ffffff"
                }

                MouseArea {
                    id: exitMouseArea
                    anchors.fill: parent
                    // Trigger quit on press for 100% reliability (no release event needed)
                    onPressed: {
                        console.log("Exit button pressed - quitting");
                        Qt.quit();
                    }
                }
            }
        }
    }

    // Main content area - adaptive layout based on screen aspect ratio
    Item {
        id: contentArea
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20

        // Two-column layout for wide screens (aspect ratio > 2.0)
        RowLayout {
            anchors.fill: parent
            spacing: 20
            visible: wideScreen

            // Left column: Brightness + Temperature
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 15

                // Brightness Section
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 180
                    color: "#0f3460"
                    radius: 10

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "Brightness Control"
                                font.pixelSize: 22
                                font.bold: true
                                color: "#ffffff"
                            }

                            Item { Layout.fillWidth: true }

                            // Mode indicator
                            Rectangle {
                                width: modeTextWide.width + 20
                                height: 32
                                radius: 16
                                color: alsDimmer.mode === "auto" ? "#27ae60" :
                                       alsDimmer.mode === "manual_temporary" ? "#f39c12" : "#3498db"

                                Text {
                                    id: modeTextWide
                                    anchors.centerIn: parent
                                    text: alsDimmer.mode === "auto" ? "AUTO" :
                                          alsDimmer.mode === "manual_temporary" ? "TEMP" : "MANUAL"
                                    font.pixelSize: 16
                                    font.bold: true
                                    color: "#ffffff"
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 15

                            Text {
                                text: "Brightness:"
                                font.pixelSize: 22
                                color: "#cccccc"
                                Layout.preferredWidth: 140
                            }

                            Slider {
                                id: brightnessSliderWide
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                from: 2
                                to: 100
                                value: brightnessSlider.value
                                enabled: alsDimmer.connected
                                stepSize: 1

                                Binding {
                                    target: brightnessSliderWide
                                    property: "value"
                                    value: alsDimmer.brightness
                                    when: !userDraggingBrightness && alsDimmer.mode === "auto"
                                }

                                background: Rectangle {
                                    x: brightnessSliderWide.leftPadding
                                    y: brightnessSliderWide.topPadding + brightnessSliderWide.availableHeight / 2 - height / 2
                                    width: brightnessSliderWide.availableWidth
                                    height: 12
                                    radius: 6
                                    color: "#2c3e50"

                                    Rectangle {
                                        width: brightnessSliderWide.visualPosition * parent.width
                                        height: parent.height
                                        color: brightnessSliderWide.enabled ? "#3498db" : "#555555"
                                        radius: 6
                                    }
                                }

                                handle: Rectangle {
                                    x: brightnessSliderWide.leftPadding + brightnessSliderWide.visualPosition * (brightnessSliderWide.availableWidth - width)
                                    y: brightnessSliderWide.topPadding + brightnessSliderWide.availableHeight / 2 - height / 2
                                    width: 40
                                    height: 40
                                    radius: 20
                                    color: brightnessSliderWide.pressed ? "#2980b9" : (brightnessSliderWide.enabled ? "#3498db" : "#555555")
                                    border.color: "#ffffff"
                                    border.width: 3
                                }

                                onPressedChanged: {
                                    userDraggingBrightness = pressed;
                                    if (!pressed) {
                                        alsDimmer.setBrightness(Math.round(value));
                                        brightnessSetCooldown = true;
                                        brightnessCooldownTimer.restart();
                                    }
                                    brightnessSlider.value = value;
                                }

                                onMoved: {
                                    alsDimmer.setBrightness(Math.round(value));
                                    brightnessSlider.value = value;
                                }
                            }

                            Text {
                                text: Math.round(brightnessSliderWide.value) + "%"
                                font.pixelSize: 22
                                font.bold: true
                                color: "#ffffff"
                                Layout.preferredWidth: 50
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 15

                            Text {
                                text: "Adaptive Mode:"
                                font.pixelSize: 22
                                color: "#cccccc"
                                Layout.preferredWidth: 170
                            }

                            Switch {
                                id: adaptiveSwitchWide
                                checked: userPreferAdaptive
                                enabled: alsDimmer.connected

                                indicator: Rectangle {
                                    implicitWidth: 60
                                    implicitHeight: 32
                                    x: adaptiveSwitchWide.leftPadding
                                    y: parent.height / 2 - height / 2
                                    radius: 16
                                    color: adaptiveSwitchWide.checked ? "#27ae60" : "#2c3e50"
                                    opacity: adaptiveSwitchWide.enabled ? 1.0 : 0.5

                                    Rectangle {
                                        x: adaptiveSwitchWide.checked ? parent.width - width - 2 : 2
                                        y: 2
                                        width: 28
                                        height: 28
                                        radius: 14
                                        color: "#ffffff"

                                        Behavior on x {
                                            NumberAnimation { duration: 150 }
                                        }
                                    }
                                }

                                onClicked: {
                                    userPreferAdaptive = checked;
                                    alsDimmer.setAdaptiveMode(checked);
                                }
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: "ALS: " + (alsDimmer.connected ? alsDimmer.luxValue.toFixed(1) + " lux" : "---")
                                font.pixelSize: 22
                                color: "#888888"
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Item { Layout.fillWidth: true }

                            Text {
                                text: "Zone: " + (alsDimmer.connected ? alsDimmer.zone : "---")
                                font.pixelSize: 22
                                color: "#888888"
                            }
                        }
                    }
                }

                // Temperature Section
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 150
                    color: "#0f3460"
                    radius: 10

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 8

                        Text {
                            text: "Temperature Sensors"
                            font.pixelSize: 22
                            font.bold: true
                            color: "#ffffff"
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 15

                            Text {
                                text: "Sensor 1:"
                                font.pixelSize: 24
                                color: "#888888"
                                Layout.preferredWidth: 110
                            }

                            Text {
                                text: tempSensors.sensor1Available ? tempSensors.sensor1Temp.toFixed(1) + " °C" : "N/A"
                                font.pixelSize: 24
                                font.bold: true
                                color: tempSensors.sensor1Available && tempSensors.sensor1Healthy ? "#ffffff" : "#666666"
                                Layout.preferredWidth: 100
                            }

                            Rectangle {
                                width: 14
                                height: 14
                                radius: 7
                                color: !tempSensors.sensor1Available ? "#555555" :
                                       tempSensors.sensor1Healthy ? "#27ae60" : "#e74c3c"

                                ToolTip.visible: sensor1MouseAreaWide.containsMouse
                                ToolTip.text: !tempSensors.sensor1Available ? "Sensor not detected" :
                                              tempSensors.sensor1Healthy ? "Sensor healthy" : "Sensor error"

                                MouseArea {
                                    id: sensor1MouseAreaWide
                                    anchors.fill: parent
                                    hoverEnabled: true
                                }
                            }

                            Text {
                                text: tempSensors.sensor1Id ? "(" + tempSensors.sensor1Id + ")" : ""
                                font.pixelSize: 16
                                color: "#555555"
                                visible: tempSensors.sensor1Available
                            }

                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 15

                            Text {
                                text: "Sensor 2:"
                                font.pixelSize: 24
                                color: "#888888"
                                Layout.preferredWidth: 110
                            }

                            Text {
                                text: tempSensors.sensor2Available ? tempSensors.sensor2Temp.toFixed(1) + " °C" : "N/A"
                                font.pixelSize: 24
                                font.bold: true
                                color: tempSensors.sensor2Available && tempSensors.sensor2Healthy ? "#ffffff" : "#666666"
                                Layout.preferredWidth: 100
                            }

                            Rectangle {
                                width: 14
                                height: 14
                                radius: 7
                                color: !tempSensors.sensor2Available ? "#555555" :
                                       tempSensors.sensor2Healthy ? "#27ae60" : "#e74c3c"

                                ToolTip.visible: sensor2MouseAreaWide.containsMouse
                                ToolTip.text: !tempSensors.sensor2Available ? "Sensor not detected" :
                                              tempSensors.sensor2Healthy ? "Sensor healthy" : "Sensor error"

                                MouseArea {
                                    id: sensor2MouseAreaWide
                                    anchors.fill: parent
                                    hoverEnabled: true
                                }
                            }

                            Text {
                                text: tempSensors.sensor2Id ? "(" + tempSensors.sensor2Id + ")" : ""
                                font.pixelSize: 16
                                color: "#555555"
                                visible: tempSensors.sensor2Available
                            }

                            Item { Layout.fillWidth: true }
                        }

                        // Backlight Temperature Row (MCU 0x66)
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 15
                            visible: mcu.available

                            Text {
                                text: "Backlight:"
                                font.pixelSize: 24
                                color: "#888888"
                                Layout.preferredWidth: 110
                            }

                            Text {
                                text: mcu.backlightTempValid ? mcu.backlightTemp.toFixed(1) + " °C" : "N/A"
                                font.pixelSize: 24
                                font.bold: true
                                color: mcu.backlightTempValid ? "#ffffff" : "#666666"
                                Layout.preferredWidth: 100
                            }

                            Rectangle {
                                width: 14
                                height: 14
                                radius: 7
                                color: mcu.backlightTempValid ? "#27ae60" : "#555555"

                                ToolTip.visible: blTempMouseAreaWide.containsMouse
                                ToolTip.text: mcu.backlightTempValid ? "MCU backlight NTC" : "MCU not available"

                                MouseArea {
                                    id: blTempMouseAreaWide
                                    anchors.fill: parent
                                    hoverEnabled: true
                                }
                            }

                            Text {
                                text: "(MCU NTC)"
                                font.pixelSize: 16
                                color: "#555555"
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }

            // Right column: FPGA + TDDI
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 15

                // FPGA Info Section
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 160
                    color: "#0f3460"
                    radius: 10

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 5

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "FPGA Information"
                                font.pixelSize: 20
                                font.bold: true
                                color: "#ffffff"
                            }

                            Item { Layout.fillWidth: true }

                            Rectangle {
                                width: 12
                                height: 12
                                radius: 6
                                color: fpga.connected ? "#27ae60" : "#e74c3c"

                                ToolTip.visible: fpgaStatusMouseAreaWide.containsMouse
                                ToolTip.text: fpga.connected ? "FPGA I2C connected" : "FPGA I2C disconnected"

                                MouseArea {
                                    id: fpgaStatusMouseAreaWide
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: fpga.refresh()
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 15
                            rowSpacing: 3

                            Text { text: "Firmware Version:"; font.pixelSize: 18; color: "#888888" }
                            Text { text: fpga.connected ? fpga.firmwareVersion : "N/A"; font.pixelSize: 18; color: "#ffffff" }

                            Text { text: "Build Date:"; font.pixelSize: 18; color: "#888888" }
                            Text { text: fpga.connected ? fpga.buildDate : "N/A"; font.pixelSize: 18; color: "#ffffff" }

                            Text { text: "Firmware ID:"; font.pixelSize: 18; color: "#888888" }
                            Text { text: fpga.connected ? fpga.firmwareId : "N/A"; font.pixelSize: 18; color: "#ffffff" }

                            Text { text: "Board Type:"; font.pixelSize: 18; color: "#888888" }
                            Text { text: fpga.connected ? fpga.boardType : "N/A"; font.pixelSize: 18; color: "#ffffff" }

                            Text { text: "Display:"; font.pixelSize: 18; color: "#888888" }
                            Text { text: fpga.connected ? fpga.displaySize + " " + fpga.displayResolution : "N/A"; font.pixelSize: 18; color: "#ffffff" }
                        }
                    }
                }

                // FPGA Settings Section (compact for wide layout)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 50
                    color: "#0f3460"
                    radius: 10

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Text {
                            text: "FPGA:"
                            font.pixelSize: 16
                            font.bold: true
                            color: "#ffffff"
                        }

                        Text {
                            text: "Privacy"
                            font.pixelSize: 14
                            color: "#666666"
                        }
                        Switch {
                            checked: false
                            enabled: false
                            scale: 0.7
                        }

                        Text {
                            text: "Dimming"
                            font.pixelSize: 14
                            color: "#666666"
                        }
                        Switch {
                            checked: true
                            enabled: false
                            scale: 0.7
                        }

                        Text {
                            text: "PixelComp"
                            font.pixelSize: 14
                            color: "#666666"
                        }
                        Switch {
                            checked: false
                            enabled: false
                            scale: 0.7
                        }

                        Text {
                            text: "VisionBoost"
                            font.pixelSize: 14
                            color: "#666666"
                        }
                        Switch {
                            checked: false
                            enabled: false
                            scale: 0.7
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                // Version Info Row (compact for wide layout)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(36, Screen.height * 0.035)
                    color: "#0f3460"
                    radius: 8

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: Screen.width * 0.01

                        Text { text: "OS:"; font.pixelSize: Math.max(12, Screen.height * 0.018); font.bold: true; color: "#888888" }
                        Text { text: osVersion; font.pixelSize: Math.max(12, Screen.height * 0.018); color: "#ffffff" }
                        Text { text: "(" + osBuildDate + ")"; font.pixelSize: Math.max(10, Screen.height * 0.015); color: "#666666" }

                        Rectangle { width: 1; Layout.fillHeight: true; Layout.topMargin: 6; Layout.bottomMargin: 6; color: "#333333" }

                        Text { text: "App:"; font.pixelSize: Math.max(12, Screen.height * 0.018); font.bold: true; color: "#888888" }
                        Text { text: swVersion; font.pixelSize: Math.max(12, Screen.height * 0.018); color: "#ffffff" }
                        Text { text: "(" + swBuildDate + ")"; font.pixelSize: Math.max(10, Screen.height * 0.015); color: "#666666" }

                        Rectangle { width: 1; Layout.fillHeight: true; Layout.topMargin: 6; Layout.bottomMargin: 6; color: "#333333"; visible: mcu.available }

                        Text { text: "IOC:"; font.pixelSize: Math.max(12, Screen.height * 0.018); font.bold: true; color: "#888888"; visible: mcu.available }
                        Text { text: mcu.firmwareVersion; font.pixelSize: Math.max(12, Screen.height * 0.018); color: "#ffffff"; visible: mcu.available }
                        Text { text: "(" + mcu.buildDateTime + ")"; font.pixelSize: Math.max(10, Screen.height * 0.015); color: "#666666"; visible: mcu.available }

                        Item { Layout.fillWidth: true }
                    }
                }

                // TDDI Info Section
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true  // Take remaining space
                    color: "#0f3460"
                    radius: 10

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 5

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "Touch Controller (TDDI)"
                                font.pixelSize: 20
                                font.bold: true
                                color: "#ffffff"
                            }

                            Item { Layout.fillWidth: true }

                            Rectangle {
                                width: 12
                                height: 12
                                radius: 6
                                color: tddi.available ? "#27ae60" : "#e74c3c"

                                ToolTip.visible: tddiStatusMouseAreaWide.containsMouse
                                ToolTip.text: tddi.available ? "TDDI info available" : "TDDI info not available"

                                MouseArea {
                                    id: tddiStatusMouseAreaWide
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: tddi.refresh()
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 15
                            rowSpacing: 3

                            Text { text: "IC Type:"; font.pixelSize: 18; color: "#888888" }
                            Text { text: tddi.available && tddi.icType ? tddi.icType : "N/A"; font.pixelSize: 18; color: "#ffffff" }

                            Text { text: "FW Version:"; font.pixelSize: 18; color: "#888888" }
                            Text { text: tddi.available && tddi.fwVersion ? tddi.fwVersion : "N/A"; font.pixelSize: 18; color: "#ffffff" }

                            Text { text: "Display Config:"; font.pixelSize: 18; color: "#888888" }
                            Text { text: tddi.available && tddi.displayConfig ? tddi.displayConfig : "N/A"; font.pixelSize: 18; color: "#ffffff" }

                            Text { text: "Touch Config:"; font.pixelSize: 18; color: "#888888" }
                            Text { text: tddi.available && tddi.touchConfig ? tddi.touchConfig : "N/A"; font.pixelSize: 18; color: "#ffffff" }

                            Text { text: "Customer:"; font.pixelSize: 18; color: "#888888" }
                            Text { text: tddi.available && tddi.customer ? tddi.customer : "N/A"; font.pixelSize: 18; color: "#ffffff" }

                            Text { text: "Project:"; font.pixelSize: 18; color: "#888888" }
                            Text { text: tddi.available && tddi.project ? tddi.project : "N/A"; font.pixelSize: 18; color: "#ffffff" }
                        }
                    }
                }
            }
        }

        // Single-column layout for normal screens (original layout)
        ColumnLayout {
            anchors.fill: parent
            spacing: 15
            visible: !wideScreen

            // Brightness Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                color: "#0f3460"
                radius: 10

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "Brightness Control"
                            font.pixelSize: 22
                            font.bold: true
                            color: "#ffffff"
                        }

                        Item { Layout.fillWidth: true }

                        // Mode indicator
                        Rectangle {
                            width: modeText.width + 20
                            height: 32
                            radius: 16
                            color: alsDimmer.mode === "auto" ? "#27ae60" :
                                   alsDimmer.mode === "manual_temporary" ? "#f39c12" : "#3498db"

                            Text {
                                id: modeText
                                anchors.centerIn: parent
                                text: alsDimmer.mode === "auto" ? "AUTO" :
                                      alsDimmer.mode === "manual_temporary" ? "TEMP" : "MANUAL"
                                font.pixelSize: 16
                                font.bold: true
                                color: "#ffffff"
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15

                        Text {
                            text: "Brightness:"
                            font.pixelSize: 22
                            color: "#cccccc"
                            Layout.preferredWidth: 140
                        }

                        Slider {
                            id: brightnessSlider
                            Layout.fillWidth: true
                            Layout.preferredHeight: 48  // Touch-friendly height
                            from: 2    // Minimum 2% to prevent completely black screen
                            to: 100
                            value: 50  // Initial default, will be set on connect
                            enabled: alsDimmer.connected
                            stepSize: 1

                            // Only sync from controller in auto mode
                            // In manual mode, slider stays where user put it (no binding)
                            Binding {
                                target: brightnessSlider
                                property: "value"
                                value: alsDimmer.brightness
                                when: !userDraggingBrightness && alsDimmer.mode === "auto"
                            }

                            background: Rectangle {
                                x: brightnessSlider.leftPadding
                                y: brightnessSlider.topPadding + brightnessSlider.availableHeight / 2 - height / 2
                                width: brightnessSlider.availableWidth
                                height: 12
                                radius: 6
                                color: "#2c3e50"

                                Rectangle {
                                    width: brightnessSlider.visualPosition * parent.width
                                    height: parent.height
                                    color: brightnessSlider.enabled ? "#3498db" : "#555555"
                                    radius: 6
                                }
                            }

                            handle: Rectangle {
                                x: brightnessSlider.leftPadding + brightnessSlider.visualPosition * (brightnessSlider.availableWidth - width)
                                y: brightnessSlider.topPadding + brightnessSlider.availableHeight / 2 - height / 2
                                width: 40
                                height: 40
                                radius: 20
                                color: brightnessSlider.pressed ? "#2980b9" : (brightnessSlider.enabled ? "#3498db" : "#555555")
                                border.color: "#ffffff"
                                border.width: 3
                            }

                            onPressedChanged: {
                                userDraggingBrightness = pressed;
                                if (!pressed) {
                                    // Final update on release
                                    alsDimmer.setBrightness(Math.round(value));
                                    // Start cooldown to ignore brightness feedback briefly
                                    brightnessSetCooldown = true;
                                    brightnessCooldownTimer.restart();
                                }
                            }

                            onMoved: {
                                // Responsive updates while dragging
                                alsDimmer.setBrightness(Math.round(value));
                            }
                        }

                        Text {
                            text: Math.round(brightnessSlider.value) + "%"
                            font.pixelSize: 22
                            font.bold: true
                            color: "#ffffff"
                            Layout.preferredWidth: 50
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15

                        Text {
                            text: "Adaptive Mode:"
                            font.pixelSize: 22
                            color: "#cccccc"
                            Layout.preferredWidth: 170
                        }

                        Switch {
                            id: adaptiveSwitch
                            checked: userPreferAdaptive
                            enabled: alsDimmer.connected

                            indicator: Rectangle {
                                implicitWidth: 60
                                implicitHeight: 32
                                x: adaptiveSwitch.leftPadding
                                y: parent.height / 2 - height / 2
                                radius: 16
                                color: adaptiveSwitch.checked ? "#27ae60" : "#2c3e50"
                                opacity: adaptiveSwitch.enabled ? 1.0 : 0.5

                                Rectangle {
                                    x: adaptiveSwitch.checked ? parent.width - width - 2 : 2
                                    y: 2
                                    width: 28
                                    height: 28
                                    radius: 14
                                    color: "#ffffff"

                                    Behavior on x {
                                        NumberAnimation { duration: 150 }
                                    }
                                }
                            }

                            onClicked: {
                                userPreferAdaptive = checked;
                                alsDimmer.setAdaptiveMode(checked);
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "ALS: " + (alsDimmer.connected ? alsDimmer.luxValue.toFixed(1) + " lux" : "---")
                            font.pixelSize: 22
                            color: "#888888"
                        }

                        Text {
                            text: "Zone: " + (alsDimmer.connected ? alsDimmer.zone : "---")
                            font.pixelSize: 22
                            color: "#888888"
                            Layout.leftMargin: 20
                        }
                    }
                }
            }

            // Temperature Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: mcu.available ? 190 : 150
                color: "#0f3460"
                radius: 10

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 8

                    Text {
                        text: "Temperature Sensors"
                        font.pixelSize: 22
                        font.bold: true
                        color: "#ffffff"
                    }

                    // Sensor 1 Row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15

                        Text {
                            text: "Sensor 1:"
                            font.pixelSize: 24
                            color: "#888888"
                            Layout.preferredWidth: 110
                        }

                        Text {
                            text: tempSensors.sensor1Available ? tempSensors.sensor1Temp.toFixed(1) + " °C" : "N/A"
                            font.pixelSize: 24
                            font.bold: true
                            color: tempSensors.sensor1Available && tempSensors.sensor1Healthy ? "#ffffff" : "#666666"
                            Layout.preferredWidth: 100
                        }

                        // Sensor 1 Health LED
                        Rectangle {
                            width: 14
                            height: 14
                            radius: 7
                            color: !tempSensors.sensor1Available ? "#555555" :
                                   tempSensors.sensor1Healthy ? "#27ae60" : "#e74c3c"

                            ToolTip.visible: sensor1MouseArea.containsMouse
                            ToolTip.text: !tempSensors.sensor1Available ? "Sensor not detected" :
                                          tempSensors.sensor1Healthy ? "Sensor healthy" : "Sensor error (CRC fail or comm issue)"

                            MouseArea {
                                id: sensor1MouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                            }
                        }

                        Text {
                            text: tempSensors.sensor1Id ? "(" + tempSensors.sensor1Id + ")" : ""
                            font.pixelSize: 16
                            color: "#555555"
                            visible: tempSensors.sensor1Available
                        }

                        Item { Layout.fillWidth: true }
                    }

                    // Sensor 2 Row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15

                        Text {
                            text: "Sensor 2:"
                            font.pixelSize: 24
                            color: "#888888"
                            Layout.preferredWidth: 110
                        }

                        Text {
                            text: tempSensors.sensor2Available ? tempSensors.sensor2Temp.toFixed(1) + " °C" : "N/A"
                            font.pixelSize: 24
                            font.bold: true
                            color: tempSensors.sensor2Available && tempSensors.sensor2Healthy ? "#ffffff" : "#666666"
                            Layout.preferredWidth: 100
                        }

                        // Sensor 2 Health LED
                        Rectangle {
                            width: 14
                            height: 14
                            radius: 7
                            color: !tempSensors.sensor2Available ? "#555555" :
                                   tempSensors.sensor2Healthy ? "#27ae60" : "#e74c3c"

                            ToolTip.visible: sensor2MouseArea.containsMouse
                            ToolTip.text: !tempSensors.sensor2Available ? "Sensor not detected" :
                                          tempSensors.sensor2Healthy ? "Sensor healthy" : "Sensor error (CRC fail or comm issue)"

                            MouseArea {
                                id: sensor2MouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                            }
                        }

                        Text {
                            text: tempSensors.sensor2Id ? "(" + tempSensors.sensor2Id + ")" : ""
                            font.pixelSize: 16
                            color: "#555555"
                            visible: tempSensors.sensor2Available
                        }

                        Item { Layout.fillWidth: true }
                    }

                    // Backlight Temperature Row (MCU 0x66)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15
                        visible: mcu.available

                        Text {
                            text: "Backlight:"
                            font.pixelSize: 24
                            color: "#888888"
                            Layout.preferredWidth: 110
                        }

                        Text {
                            text: mcu.backlightTempValid ? mcu.backlightTemp.toFixed(1) + " °C" : "N/A"
                            font.pixelSize: 24
                            font.bold: true
                            color: mcu.backlightTempValid ? "#ffffff" : "#666666"
                            Layout.preferredWidth: 100
                        }

                        Rectangle {
                            width: 14
                            height: 14
                            radius: 7
                            color: mcu.backlightTempValid ? "#27ae60" : "#555555"

                            ToolTip.visible: blTempMouseArea.containsMouse
                            ToolTip.text: mcu.backlightTempValid ? "MCU backlight NTC" : "MCU not available"

                            MouseArea {
                                id: blTempMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                            }
                        }

                        Text {
                            text: "(MCU NTC)"
                            font.pixelSize: 16
                            color: "#555555"
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // FPGA Info Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                color: "#0f3460"
                radius: 10

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "FPGA Information"
                            font.pixelSize: 22
                            font.bold: true
                            color: "#ffffff"
                        }

                        Item { Layout.fillWidth: true }

                        // FPGA connection status
                        Rectangle {
                            width: 12
                            height: 12
                            radius: 6
                            color: fpga.connected ? "#27ae60" : "#e74c3c"

                            ToolTip.visible: fpgaStatusMouseArea.containsMouse
                            ToolTip.text: fpga.connected ? "FPGA I2C connected" : "FPGA I2C disconnected"

                            MouseArea {
                                id: fpgaStatusMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: fpga.refresh()
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: 30
                        rowSpacing: 5

                        Text { text: "Firmware Version:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: fpga.connected ? fpga.firmwareVersion : "N/A"; font.pixelSize: 22; color: "#ffffff" }

                        Text { text: "Build Date:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: fpga.connected ? fpga.buildDate : "N/A"; font.pixelSize: 22; color: "#ffffff" }

                        Text { text: "Firmware ID:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: fpga.connected ? fpga.firmwareId : "N/A"; font.pixelSize: 22; color: "#ffffff" }

                        Text { text: "Board Type:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: fpga.connected ? fpga.boardType : "N/A"; font.pixelSize: 22; color: "#ffffff" }

                        Text { text: "Display Size:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: fpga.connected ? fpga.displaySize : "N/A"; font.pixelSize: 22; color: "#ffffff" }

                        Text { text: "Display Resolution:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: fpga.connected ? fpga.displayResolution : "N/A"; font.pixelSize: 22; color: "#ffffff" }
                    }
                }
            }

            // FPGA Settings Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                color: "#0f3460"
                radius: 10

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 8

                    Text {
                        text: "FPGA Settings"
                        font.pixelSize: 22
                        font.bold: true
                        color: "#ffffff"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 30

                        RowLayout {
                            spacing: 10
                            Text {
                                text: "Privacy Mode:"
                                font.pixelSize: 22
                                // TODO: Re-enable after testing: fpga.connected ? "#cccccc" : "#666666"
                                color: "#666666"
                            }
                            Switch {
                                id: privacySwitch
                                // TODO: Re-enable after testing: fpga.privacyMode
                                checked: false
                                // TODO: Re-enable after testing: fpga.connected
                                enabled: false

                                indicator: Rectangle {
                                    implicitWidth: 60
                                    implicitHeight: 32
                                    radius: 16
                                    // TODO: Re-enable after testing: privacySwitch.checked ? "#27ae60" : "#2c3e50"
                                    color: "#1a1a2e"
                                    opacity: 0.5

                                    Rectangle {
                                        x: 2
                                        y: 2
                                        width: 28
                                        height: 28
                                        radius: 14
                                        color: "#666666"
                                    }
                                }

                                onClicked: {
                                    // TODO: Re-enable after testing
                                    // fpga.setPrivacyMode(checked);
                                }
                            }
                            Text {
                                text: "(Disabled)"
                                font.pixelSize: 16
                                color: "#666666"
                            }
                        }

                        RowLayout {
                            spacing: 10
                            Text {
                                text: "Local Dimming:"
                                font.pixelSize: 22
                                color: "#666666"
                            }
                            Switch {
                                id: localDimmingSwitch
                                checked: true  // Show as ON (but disabled)
                                enabled: false

                                indicator: Rectangle {
                                    implicitWidth: 60
                                    implicitHeight: 32
                                    radius: 16
                                    color: "#1a5c3a"  // Dimmed green to indicate ON but disabled
                                    opacity: 0.5

                                    Rectangle {
                                        x: parent.width - width - 2  // ON position
                                        y: 2
                                        width: 28
                                        height: 28
                                        radius: 14
                                        color: "#666666"
                                    }
                                }
                            }
                            Text {
                                text: "(N/A)"
                                font.pixelSize: 16
                                color: "#666666"
                            }
                        }

                        RowLayout {
                            spacing: 10
                            Text {
                                text: "Pixel Compensation:"
                                font.pixelSize: 22
                                color: "#666666"
                            }
                            Switch {
                                id: pixelCompSwitch
                                checked: false
                                enabled: false

                                indicator: Rectangle {
                                    implicitWidth: 60
                                    implicitHeight: 32
                                    radius: 16
                                    color: "#1a1a2e"
                                    opacity: 0.5

                                    Rectangle {
                                        x: 2
                                        y: 2
                                        width: 28
                                        height: 28
                                        radius: 14
                                        color: "#666666"
                                    }
                                }
                            }
                            Text {
                                text: "(Disabled)"
                                font.pixelSize: 16
                                color: "#666666"
                            }
                        }

                        RowLayout {
                            spacing: 10
                            Text {
                                text: "Vision Booster:"
                                font.pixelSize: 22
                                color: "#666666"
                            }
                            Switch {
                                id: visionBoostSwitch
                                checked: false
                                enabled: false

                                indicator: Rectangle {
                                    implicitWidth: 60
                                    implicitHeight: 32
                                    radius: 16
                                    color: "#1a1a2e"
                                    opacity: 0.5

                                    Rectangle {
                                        x: 2
                                        y: 2
                                        width: 28
                                        height: 28
                                        radius: 14
                                        color: "#666666"
                                    }
                                }
                            }
                            Text {
                                text: "(Disabled)"
                                font.pixelSize: 16
                                color: "#666666"
                            }
                        }
                    }
                }
            }

            // Version Info Row
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(44, Screen.height * 0.04)
                color: "#0f3460"
                radius: 8

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 15
                    anchors.rightMargin: 15
                    spacing: Screen.width * 0.012

                    Text { text: "OS:"; font.pixelSize: Math.max(14, Screen.height * 0.02); font.bold: true; color: "#888888" }
                    Text { text: osVersion; font.pixelSize: Math.max(14, Screen.height * 0.02); color: "#ffffff" }
                    Text { text: "(" + osBuildDate + ")"; font.pixelSize: Math.max(12, Screen.height * 0.016); color: "#666666" }

                    Rectangle { width: 1; Layout.fillHeight: true; Layout.topMargin: 8; Layout.bottomMargin: 8; color: "#333333" }

                    Text { text: "App:"; font.pixelSize: Math.max(14, Screen.height * 0.02); font.bold: true; color: "#888888" }
                    Text { text: swVersion; font.pixelSize: Math.max(14, Screen.height * 0.02); color: "#ffffff" }
                    Text { text: "(" + swBuildDate + ")"; font.pixelSize: Math.max(12, Screen.height * 0.016); color: "#666666" }

                    Rectangle { width: 1; Layout.fillHeight: true; Layout.topMargin: 8; Layout.bottomMargin: 8; color: "#333333"; visible: mcu.available }

                    Text { text: "IOC:"; font.pixelSize: Math.max(14, Screen.height * 0.02); font.bold: true; color: "#888888"; visible: mcu.available }
                    Text { text: mcu.firmwareVersion; font.pixelSize: Math.max(14, Screen.height * 0.02); color: "#ffffff"; visible: mcu.available }
                    Text { text: "(" + mcu.buildDateTime + ")"; font.pixelSize: Math.max(12, Screen.height * 0.016); color: "#666666"; visible: mcu.available }

                    Item { Layout.fillWidth: true }
                }
            }

            // TDDI Info Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 220
                color: "#0f3460"
                radius: 10

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "Touch Controller (TDDI)"
                            font.pixelSize: 22
                            font.bold: true
                            color: "#ffffff"
                        }

                        Item { Layout.fillWidth: true }

                        // TDDI availability status
                        Rectangle {
                            width: 12
                            height: 12
                            radius: 6
                            color: tddi.available ? "#27ae60" : "#e74c3c"

                            ToolTip.visible: tddiStatusMouseArea.containsMouse
                            ToolTip.text: tddi.available ? "TDDI info available" : "TDDI info not available"

                            MouseArea {
                                id: tddiStatusMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: tddi.refresh()
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: 30
                        rowSpacing: 5

                        Text { text: "IC Type:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: tddi.available && tddi.icType ? tddi.icType : "N/A"; font.pixelSize: 22; color: "#ffffff" }

                        Text { text: "FW Version:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: tddi.available && tddi.fwVersion ? tddi.fwVersion : "N/A"; font.pixelSize: 22; color: "#ffffff" }

                        Text { text: "Display Config:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: tddi.available && tddi.displayConfig ? tddi.displayConfig : "N/A"; font.pixelSize: 22; color: "#ffffff" }

                        Text { text: "Touch Config:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: tddi.available && tddi.touchConfig ? tddi.touchConfig : "N/A"; font.pixelSize: 22; color: "#ffffff" }

                        Text { text: "Customer:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: tddi.available && tddi.customer ? tddi.customer : "N/A"; font.pixelSize: 22; color: "#ffffff" }

                        Text { text: "Project:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: tddi.available && tddi.project ? tddi.project : "N/A"; font.pixelSize: 22; color: "#ffffff" }

                        Text { text: "Panel Version:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: tddi.available && tddi.panelVersion ? tddi.panelVersion : "N/A"; font.pixelSize: 22; color: "#ffffff" }

                        Text { text: "Config Date:"; font.pixelSize: 22; color: "#888888" }
                        Text { text: tddi.available && tddi.configDate ? tddi.configDate : "N/A"; font.pixelSize: 22; color: "#ffffff" }
                    }
                }
            }

            // Spacer to fill remaining space
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }

    Component.onCompleted: {
        console.log("disp-settings UI loaded");
        console.log("Screen size:", Screen.width, "x", Screen.height);
        console.log("Aspect ratio:", (Screen.width / Screen.height).toFixed(2), "- using", wideScreen ? "two-column" : "single-column", "layout");
    }
}
