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

    // Dark background for contrast
    Rectangle {
        anchors.fill: parent
        color: "#050505"
    }

    Row {
        anchors.centerIn: parent
        spacing: parent.width * 0.008

        Repeater {
            model: [
                { bit: root.bitEngine,   label: "ENG",   color: "#ffcc00", blink: false },
                { bit: root.bitOil,      label: "OIL",   color: "#ff2222", blink: false },
                { bit: root.bitBattery,  label: "BATT",  color: "#ff2222", blink: false },
                { bit: root.bitBrake,    label: "BRAKE", color: "#ff2222", blink: false },
                { bit: root.bitLeft,     label: "\u25C0", color: "#00ff44", blink: true },
                { bit: root.bitRight,    label: "\u25B6", color: "#00ff44", blink: true },
                { bit: root.bitHighbeam, label: "HIGH",  color: "#44aaff", blink: false },
                { bit: root.bitDoor,     label: "DOOR",  color: "#ffcc00", blink: false },
                { bit: root.bitSeatbelt, label: "BELT",  color: "#ff2222", blink: false },
                { bit: root.bitAbs,      label: "ABS",   color: "#ffcc00", blink: false },
                { bit: root.bitTraction, label: "TC",    color: "#ffcc00", blink: false },
                { bit: root.bitTpms,     label: "TPMS",  color: "#ffcc00", blink: false }
            ]

            delegate: Rectangle {
                property bool isOn: (root.telltales & modelData.bit) !== 0

                width: root.width * 0.07
                height: root.height * 0.78
                radius: 6
                color: isOn ? Qt.rgba(modelData.color.r, modelData.color.g,
                                      modelData.color.b, 0.25) : "#080808"
                border.color: isOn ? modelData.color : "#1a1a1a"
                border.width: isOn ? 2.5 : 1

                opacity: {
                    if (!isOn) return 0.35;
                    if (modelData.blink) return blinkVisible ? 1.0 : 0.0;
                    return 1.0;
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 6
                    text: modelData.label
                    color: parent.isOn ? modelData.color : "#363636"
                    font.pixelSize: modelData.blink ? parent.height * 0.65
                                    : Math.min(parent.height * 0.45, parent.width * 0.38)
                    font.bold: true
                    font.weight: parent.isOn ? Font.ExtraBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 8
                }
            }
        }
    }
}
