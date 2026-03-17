import QtQuick 2.12

Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: "#111111"
        border.color: "#333333"
        border.width: 1

        Row {
            anchors.centerIn: parent
            spacing: parent.width * 0.05

            // Battery voltage
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "BATT: " + cluster.batteryVoltage.toFixed(1) + " V"
                color: cluster.batteryVoltage < 11.5 ? "#ff4444" : "#aaaaaa"
                font.pixelSize: root.height * 0.45
                font.family: "monospace"
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
                color: cluster.canConnected ? "#33cc33" : "#666666"
                font.pixelSize: root.height * 0.45
                font.family: "monospace"
            }
        }
    }
}
