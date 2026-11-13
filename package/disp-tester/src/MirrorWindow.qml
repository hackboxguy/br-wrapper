import QtQuick 2.12
import QtQuick.Window 2.12

// Pattern-only window for a second display (e.g. the Pi's HDMI2). main.cpp puts it on the
// other screen and shows it full screen; the controls stay on the main window.
Window {
    id: mirrorWindow
    visible: false
    color: "black"
    title: "Display Pattern Tester (second display)"

    PatternView {
        anchors.fill: parent
    }
}
