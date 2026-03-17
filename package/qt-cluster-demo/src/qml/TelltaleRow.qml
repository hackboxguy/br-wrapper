import QtQuick 2.12

Item {
    id: root
    property int telltales: cluster.telltales

    // Telltale bit definitions (must match CanReader/DemoSimulator)
    readonly property int bitEngine:   (1 << 0)
    readonly property int bitOil:      (1 << 1)
    readonly property int bitBattery:  (1 << 2)
    readonly property int bitBrake:    (1 << 3)
    readonly property int bitLeft:     (1 << 4)
    readonly property int bitRight:    (1 << 5)
    readonly property int bitHighbeam: (1 << 6)
    readonly property int bitDoor:     (1 << 7)
    readonly property int bitSeatbelt: (1 << 8)
    readonly property int bitAbs:      (1 << 9)
    readonly property int bitTraction: (1 << 10)
    readonly property int bitTpms:     (1 << 11)

    // Turn signal blink state (Option B: QML-driven blink)
    property bool leftActive: (telltales & bitLeft) !== 0
    property bool rightActive: (telltales & bitRight) !== 0
    property bool blinkVisible: true

    Timer {
        id: blinkTimer
        interval: 500
        repeat: true
        running: leftActive || rightActive
        onTriggered: blinkVisible = !blinkVisible
    }

    // Reset blink state when both turn signals go off
    onLeftActiveChanged: if (!leftActive && !rightActive) blinkVisible = true
    onRightActiveChanged: if (!leftActive && !rightActive) blinkVisible = true

    Row {
        anchors.centerIn: parent
        spacing: parent.width * 0.015

        // Helper component for a single telltale indicator
        Repeater {
            model: [
                { bit: root.bitEngine,   label: "ENG",   color: "#ffaa00", blink: false },
                { bit: root.bitOil,      label: "OIL",   color: "#ff3333", blink: false },
                { bit: root.bitBattery,  label: "BATT",  color: "#ff3333", blink: false },
                { bit: root.bitBrake,    label: "BRAKE", color: "#ff3333", blink: false },
                { bit: root.bitLeft,     label: "\u25C0", color: "#33cc33", blink: true },
                { bit: root.bitRight,    label: "\u25B6", color: "#33cc33", blink: true },
                { bit: root.bitHighbeam, label: "HIGH",  color: "#4488ff", blink: false },
                { bit: root.bitDoor,     label: "DOOR",  color: "#ffaa00", blink: false },
                { bit: root.bitSeatbelt, label: "BELT",  color: "#ff3333", blink: false },
                { bit: root.bitAbs,      label: "ABS",   color: "#ffaa00", blink: false },
                { bit: root.bitTraction, label: "TC",    color: "#ffaa00", blink: false },
                { bit: root.bitTpms,     label: "TPMS",  color: "#ffaa00", blink: false }
            ]

            delegate: Rectangle {
                width: root.width * 0.065
                height: root.height * 0.72
                radius: 4
                color: (root.telltales & modelData.bit) ? Qt.rgba(
                           modelData.color.r, modelData.color.g, modelData.color.b, 0.15)
                       : "transparent"
                border.color: (root.telltales & modelData.bit) ? modelData.color : "#333333"
                border.width: 1

                visible: true
                opacity: {
                    var isOn = (root.telltales & modelData.bit) !== 0;
                    if (!isOn) return 0.3;
                    if (modelData.blink) return blinkVisible ? 1.0 : 0.0;
                    return 1.0;
                }

                Text {
                    anchors.centerIn: parent
                    text: modelData.label
                    color: (root.telltales & modelData.bit) ? modelData.color : "#555555"
                    font.pixelSize: parent.height * 0.4
                    font.bold: true
                }
            }
        }
    }
}
