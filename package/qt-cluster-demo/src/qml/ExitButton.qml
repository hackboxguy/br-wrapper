import QtQuick 2.12

Item {
    id: root

    // Full-screen invisible touch area
    MouseArea {
        anchors.fill: parent
        onPressed: {
            exitBtn.visible = true;
            hideTimer.restart();
            mouse.accepted = false;  // Pass through to gauges if needed
        }
    }

    // Auto-hide timer
    Timer {
        id: hideTimer
        interval: 3000
        onTriggered: exitBtn.visible = false
    }

    // Exit button (top-right corner)
    Rectangle {
        id: exitBtn
        visible: false
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: parent.height * 0.02
        width: parent.height * 0.08
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
