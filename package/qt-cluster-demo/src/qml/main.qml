import QtQuick 2.12
import QtQuick.Window 2.12

Window {
    id: root
    visible: true
    width: Screen.width
    height: Screen.height
    flags: Qt.FramelessWindowHint
    color: "black"
    title: "Cluster Demo"

    // Gauge area dimensions (between telltale row and info bar)
    property real topBarHeight: height * 0.12
    property real bottomBarHeight: height * 0.06
    property real gaugeAreaHeight: height - topBarHeight - bottomBarHeight
    property real gaugeDiameter: gaugeAreaHeight * 0.95
    property real centerWidth: width - gaugeDiameter * 2

    // Telltale row (top)
    TelltaleRow {
        id: telltaleRow
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: topBarHeight
    }

    // Separator line
    Rectangle {
        id: topSep
        anchors.top: telltaleRow.bottom
        width: parent.width
        height: 1
        color: "#333333"
    }

    // Tachometer (left)
    Tachometer {
        id: tach
        anchors.verticalCenter: gaugeArea.verticalCenter
        x: centerColumn.x - gaugeDiameter + gaugeDiameter * 0.08
        width: gaugeDiameter
        height: gaugeDiameter
    }

    // Gauge area anchor helper
    Item {
        id: gaugeArea
        anchors.top: topSep.bottom
        anchors.bottom: bottomSep.top
        width: parent.width
    }

    // Center column (fuel + temp gauges)
    Item {
        id: centerColumn
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: topSep.bottom
        anchors.bottom: bottomSep.top
        width: centerWidth > 0 ? centerWidth : width * 0.2

        FuelGauge {
            anchors.top: parent.top
            anchors.topMargin: parent.height * 0.02
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width * 0.85, parent.height * 0.48)
            height: width
        }

        TempGauge {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: parent.height * 0.02
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width * 0.85, parent.height * 0.48)
            height: width
        }
    }

    // Speedometer (right)
    Speedometer {
        id: speedo
        anchors.verticalCenter: gaugeArea.verticalCenter
        x: centerColumn.x + centerColumn.width - gaugeDiameter * 0.08
        width: gaugeDiameter
        height: gaugeDiameter
    }

    // Separator line
    Rectangle {
        id: bottomSep
        anchors.bottom: infoBar.top
        width: parent.width
        height: 1
        color: "#333333"
    }

    // Info bar (bottom)
    InfoBar {
        id: infoBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: bottomBarHeight
    }

    // Exit button overlay (highest z-order)
    ExitButton {
        anchors.fill: parent
        z: 100
    }

    // Escape key to quit (for keyboard-attached setups)
    Shortcut {
        sequence: "Escape"
        onActivated: Qt.quit()
    }
}
