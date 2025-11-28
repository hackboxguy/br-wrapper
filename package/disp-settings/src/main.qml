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
                font.pixelSize: 24
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
                font.pixelSize: 14
                color: "#888888"
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: 10
            }

            Button {
                id: exitButton
                text: "Exit"
                Layout.preferredWidth: 80
                Layout.preferredHeight: 40
                Layout.alignment: Qt.AlignVCenter

                background: Rectangle {
                    color: exitButton.pressed ? "#c0392b" : (exitButton.hovered ? "#e74c3c" : "#2c3e50")
                    radius: 5
                }

                contentItem: Text {
                    text: exitButton.text
                    font.pixelSize: 16
                    color: "#ffffff"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: {
                    console.log("Exit button clicked");
                    Qt.quit();
                }
            }
        }
    }

    // Main content area - fixed layout (no scrolling)
    Item {
        id: contentArea
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20

        ColumnLayout {
            anchors.fill: parent
            spacing: 15

            // Brightness Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 160
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
                            font.pixelSize: 18
                            font.bold: true
                            color: "#ffffff"
                        }

                        Item { Layout.fillWidth: true }

                        // Mode indicator
                        Rectangle {
                            width: modeText.width + 16
                            height: 24
                            radius: 12
                            color: alsDimmer.mode === "auto" ? "#27ae60" :
                                   alsDimmer.mode === "manual_temporary" ? "#f39c12" : "#3498db"

                            Text {
                                id: modeText
                                anchors.centerIn: parent
                                text: alsDimmer.mode === "auto" ? "AUTO" :
                                      alsDimmer.mode === "manual_temporary" ? "TEMP" : "MANUAL"
                                font.pixelSize: 11
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
                            font.pixelSize: 14
                            color: "#cccccc"
                            Layout.preferredWidth: 100
                        }

                        Slider {
                            id: brightnessSlider
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            value: alsDimmer.brightness
                            enabled: alsDimmer.connected
                            stepSize: 1

                            // Only sync from controller in auto mode (not manual)
                            // In manual mode, slider is "fire and forget"
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
                                height: 8
                                radius: 4
                                color: "#2c3e50"

                                Rectangle {
                                    width: brightnessSlider.visualPosition * parent.width
                                    height: parent.height
                                    color: brightnessSlider.enabled ? "#3498db" : "#555555"
                                    radius: 4
                                }
                            }

                            handle: Rectangle {
                                x: brightnessSlider.leftPadding + brightnessSlider.visualPosition * (brightnessSlider.availableWidth - width)
                                y: brightnessSlider.topPadding + brightnessSlider.availableHeight / 2 - height / 2
                                width: 24
                                height: 24
                                radius: 12
                                color: brightnessSlider.pressed ? "#2980b9" : (brightnessSlider.enabled ? "#3498db" : "#555555")
                                border.color: "#ffffff"
                                border.width: 2
                            }

                            onPressedChanged: {
                                userDraggingBrightness = pressed;
                                if (!pressed) {
                                    // Final update on release
                                    alsDimmer.setBrightness(Math.round(value));
                                }
                            }

                            onMoved: {
                                // Responsive updates while dragging
                                alsDimmer.setBrightness(Math.round(value));
                            }
                        }

                        Text {
                            text: Math.round(brightnessSlider.value) + "%"
                            font.pixelSize: 14
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
                            font.pixelSize: 14
                            color: "#cccccc"
                            Layout.preferredWidth: 100
                        }

                        Switch {
                            id: adaptiveSwitch
                            checked: alsDimmer.adaptiveEnabled
                            enabled: alsDimmer.connected

                            indicator: Rectangle {
                                implicitWidth: 50
                                implicitHeight: 26
                                x: adaptiveSwitch.leftPadding
                                y: parent.height / 2 - height / 2
                                radius: 13
                                color: adaptiveSwitch.checked ? "#27ae60" : "#2c3e50"
                                opacity: adaptiveSwitch.enabled ? 1.0 : 0.5

                                Rectangle {
                                    x: adaptiveSwitch.checked ? parent.width - width - 2 : 2
                                    y: 2
                                    width: 22
                                    height: 22
                                    radius: 11
                                    color: "#ffffff"

                                    Behavior on x {
                                        NumberAnimation { duration: 150 }
                                    }
                                }
                            }

                            onClicked: {
                                alsDimmer.setAdaptiveMode(checked);
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "ALS: " + (alsDimmer.connected ? alsDimmer.luxValue.toFixed(1) + " lux" : "---")
                            font.pixelSize: 14
                            color: "#888888"
                        }

                        Text {
                            text: "Zone: " + (alsDimmer.connected ? alsDimmer.zone : "---")
                            font.pixelSize: 14
                            color: "#888888"
                            Layout.leftMargin: 20
                        }
                    }
                }
            }

            // Temperature Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 130
                color: "#0f3460"
                radius: 10

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 8

                    Text {
                        text: "Temperature Sensors"
                        font.pixelSize: 18
                        font.bold: true
                        color: "#ffffff"
                    }

                    // Sensor 1 Row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15

                        Text {
                            text: "Sensor 1:"
                            font.pixelSize: 16
                            color: "#888888"
                            Layout.preferredWidth: 80
                        }

                        Text {
                            text: tempSensors.sensor1Available ? tempSensors.sensor1Temp.toFixed(1) + " °C" : "N/A"
                            font.pixelSize: 20
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
                            font.pixelSize: 12
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
                            font.pixelSize: 16
                            color: "#888888"
                            Layout.preferredWidth: 80
                        }

                        Text {
                            text: tempSensors.sensor2Available ? tempSensors.sensor2Temp.toFixed(1) + " °C" : "N/A"
                            font.pixelSize: 20
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
                            font.pixelSize: 12
                            color: "#555555"
                            visible: tempSensors.sensor2Available
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // FPGA Info Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 170
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
                            font.pixelSize: 18
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

                        Text { text: "Firmware Version:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: fpga.connected ? fpga.firmwareVersion : "N/A"; font.pixelSize: 14; color: "#ffffff" }

                        Text { text: "Build Date:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: fpga.connected ? fpga.buildDate : "N/A"; font.pixelSize: 14; color: "#ffffff" }

                        Text { text: "Firmware ID:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: fpga.connected ? fpga.firmwareId : "N/A"; font.pixelSize: 14; color: "#ffffff" }

                        Text { text: "Board Type:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: fpga.connected ? fpga.boardType : "N/A"; font.pixelSize: 14; color: "#ffffff" }

                        Text { text: "Display Size:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: fpga.connected ? fpga.displaySize : "N/A"; font.pixelSize: 14; color: "#ffffff" }

                        Text { text: "Display Resolution:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: fpga.connected ? fpga.displayResolution : "N/A"; font.pixelSize: 14; color: "#ffffff" }
                    }
                }
            }

            // FPGA Settings Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                color: "#0f3460"
                radius: 10

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 10

                    Text {
                        text: "FPGA Settings"
                        font.pixelSize: 18
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
                                font.pixelSize: 14
                                color: fpga.connected ? "#cccccc" : "#666666"
                            }
                            Switch {
                                id: privacySwitch
                                checked: fpga.privacyMode
                                enabled: fpga.connected

                                indicator: Rectangle {
                                    implicitWidth: 50
                                    implicitHeight: 26
                                    radius: 13
                                    color: privacySwitch.checked ? "#27ae60" : "#2c3e50"
                                    opacity: privacySwitch.enabled ? 1.0 : 0.5

                                    Rectangle {
                                        x: privacySwitch.checked ? parent.width - width - 2 : 2
                                        y: 2
                                        width: 22
                                        height: 22
                                        radius: 11
                                        color: "#ffffff"

                                        Behavior on x {
                                            NumberAnimation { duration: 150 }
                                        }
                                    }
                                }

                                onClicked: {
                                    fpga.setPrivacyMode(checked);
                                }
                            }
                        }

                        RowLayout {
                            spacing: 10
                            Text {
                                text: "Local Dimming:"
                                font.pixelSize: 14
                                color: "#666666"
                            }
                            Switch {
                                id: localDimmingSwitch
                                checked: false
                                enabled: false

                                indicator: Rectangle {
                                    implicitWidth: 50
                                    implicitHeight: 26
                                    radius: 13
                                    color: "#1a1a2e"
                                    opacity: 0.5

                                    Rectangle {
                                        x: 2
                                        y: 2
                                        width: 22
                                        height: 22
                                        radius: 11
                                        color: "#666666"
                                    }
                                }
                            }
                            Text {
                                text: "(N/A)"
                                font.pixelSize: 12
                                color: "#666666"
                            }
                        }
                    }
                }
            }

            // TDDI Info Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 160
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
                            font.pixelSize: 18
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

                        Text { text: "IC Type:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: tddi.available && tddi.icType ? tddi.icType : "N/A"; font.pixelSize: 14; color: "#ffffff" }

                        Text { text: "FW Version:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: tddi.available && tddi.fwVersion ? tddi.fwVersion : "N/A"; font.pixelSize: 14; color: "#ffffff" }

                        Text { text: "Display Config:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: tddi.available && tddi.displayConfig ? tddi.displayConfig : "N/A"; font.pixelSize: 14; color: "#ffffff" }

                        Text { text: "Touch Config:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: tddi.available && tddi.touchConfig ? tddi.touchConfig : "N/A"; font.pixelSize: 14; color: "#ffffff" }

                        Text { text: "Customer:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: tddi.available && tddi.customer ? tddi.customer : "N/A"; font.pixelSize: 14; color: "#ffffff" }

                        Text { text: "Project:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: tddi.available && tddi.project ? tddi.project : "N/A"; font.pixelSize: 14; color: "#ffffff" }

                        Text { text: "Panel Version:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: tddi.available && tddi.panelVersion ? tddi.panelVersion : "N/A"; font.pixelSize: 14; color: "#ffffff" }

                        Text { text: "Config Date:"; font.pixelSize: 14; color: "#888888" }
                        Text { text: tddi.available && tddi.configDate ? tddi.configDate : "N/A"; font.pixelSize: 14; color: "#ffffff" }
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
    }
}
