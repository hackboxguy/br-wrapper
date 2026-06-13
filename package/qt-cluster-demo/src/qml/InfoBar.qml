import QtQuick 2.12

Item {
    id: root
    property bool barVisible: true

    Rectangle {
        anchors.fill: parent
        color: root.barVisible ? "#111111" : "#000000"
        border.color: root.barVisible ? "#333333" : "#000000"
        border.width: root.barVisible ? 1 : 0

        Row {
            visible: root.barVisible
            anchors.centerIn: parent
            spacing: parent.width * 0.05

            // Battery voltage
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "BATT: " + cluster.batteryVoltage.toFixed(1) + " V"
                color: cluster.batteryVoltage < 11.5 ? "#ff2222" : "#dddddd"
                font.pixelSize: root.height * 0.50
                font.family: "monospace"
                font.bold: true
            }

            // Separator
            Rectangle {
                width: 1
                height: root.height * 0.5
                anchors.verticalCenter: parent.verticalCenter
                color: "#333333"
            }

            // CAN connection status
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: cluster.canConnected ? "CAN: OK" : "CAN: --"
                color: cluster.canConnected ? "#00ff44" : "#666666"
                font.pixelSize: root.height * 0.50
                font.family: "monospace"
                font.bold: true
            }
        }
    }
}
