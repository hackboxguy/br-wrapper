import QtQuick 2.12

Item {
    id: root
    property bool minDarkLevelEnabled: true
    property bool bottomBarEnabled: true

    signal minDarkLevelToggled()
    signal bottomBarToggled()

    readonly property real buttonSize: parent ? parent.height * 0.08 : 48
    readonly property real buttonMargin: parent ? parent.height * 0.02 : 12
    readonly property real buttonSpacing: parent ? parent.height * 0.015 : 8
    property bool controlsVisible: false

    // Full-screen invisible touch area
    MouseArea {
        anchors.fill: parent
        onPressed: {
            root.controlsVisible = true;
            hideTimer.restart();
            mouse.accepted = false;  // Pass through to gauges if needed
        }
    }

    // Auto-hide timer
    Timer {
        id: hideTimer
        interval: 3000
        onTriggered: root.controlsVisible = false
    }

    Row {
        id: controlRow
        visible: root.controlsVisible
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: root.buttonMargin
        spacing: root.buttonSpacing

        // FPGA Local Dimming toggle — left of T/B/X.
        // Visible only when the FPGA at 0x1D responds to register 0x2C with
        // a valid value (0x00 or 0x01). Existing T/B/X buttons keep their
        // positions because the Row is right-anchored.
        Rectangle {
            width: root.buttonSize
            height: width
            radius: width * 0.2
            visible: fpga.localDimmingSupported
            color: fpga.localDimmingEnabled ? "#cc16803a" : "#cc444444"
            border.color: fpga.localDimmingEnabled ? "#33cc66" : "#777777"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "LD"
                color: "#ffffff"
                font.pixelSize: parent.height * 0.4
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    fpga.setLocalDimming(!fpga.localDimmingEnabled);
                    root.controlsVisible = true;
                    hideTimer.restart();
                }
            }
        }

        // FPGA Pixel Compensation toggle — only meaningful while local dimming
        // is on, so it's dimmed and non-interactive when LD is disabled.
        Rectangle {
            width: root.buttonSize
            height: width
            radius: width * 0.2
            visible: fpga.pixelCompSupported
            property bool pcInteractive: fpga.localDimmingEnabled
            color: (fpga.pixelCompEnabled && pcInteractive) ? "#cc16803a" : "#cc444444"
            border.color: (fpga.pixelCompEnabled && pcInteractive) ? "#33cc66" : "#777777"
            border.width: 1
            opacity: pcInteractive ? 1.0 : 0.4

            Text {
                anchors.centerIn: parent
                text: "PC"
                color: "#ffffff"
                font.pixelSize: parent.height * 0.4
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                enabled: parent.pcInteractive
                onClicked: {
                    fpga.setPixelCompensation(!fpga.pixelCompEnabled);
                    root.controlsVisible = true;
                    hideTimer.restart();
                }
            }
        }

        Rectangle {
            width: root.buttonSize
            height: width
            radius: width * 0.2
            color: root.minDarkLevelEnabled ? "#cc16803a" : "#cc444444"
            border.color: root.minDarkLevelEnabled ? "#33cc66" : "#777777"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "T"
                color: "#ffffff"
                font.pixelSize: parent.height * 0.5
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    root.minDarkLevelToggled();
                    root.controlsVisible = true;
                    hideTimer.restart();
                }
            }
        }

        Rectangle {
            width: root.buttonSize
            height: width
            radius: width * 0.2
            color: root.bottomBarEnabled ? "#cc16803a" : "#cc444444"
            border.color: root.bottomBarEnabled ? "#33cc66" : "#777777"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "B"
                color: "#ffffff"
                font.pixelSize: parent.height * 0.5
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    root.bottomBarToggled();
                    root.controlsVisible = true;
                    hideTimer.restart();
                }
            }
        }

        // Exit button (top-right corner)
        Rectangle {
            id: exitBtn
            width: root.buttonSize
            height: width
            radius: width * 0.2
            color: "#cc000000"
            border.color: "#666666"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "\u2715"  // Unicode multiplication sign (X)
                color: "#ffffff"
                font.pixelSize: parent.height * 0.5
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                onClicked: Qt.quit()
            }
        }
    }
}
