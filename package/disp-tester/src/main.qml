import QtQuick 2.12
import QtQuick.Window 2.12

Window {
    id: window
    visible: true
    width: Screen.width
    height: Screen.height
    title: "Display Pattern Tester"

    // Force fullscreen
    visibility: Window.FullScreen

    property bool uiVisible: true

    // Pattern navigation functions
    function nextPattern() {
        patternController.nextPattern()
        showUITemporarily()
    }

    function previousPattern() {
        patternController.previousPattern()
        showUITemporarily()
    }

    function showUITemporarily() {
        uiVisible = true
        uiHideTimer.restart()
    }

    // Custom color overlay (when RGB patch or solid color is active)
    Rectangle {
        anchors.fill: parent
        visible: patternController.showCustomColor
        color: patternController.customColor
    }

    // Pattern display area
    Loader {
        id: patternLoader
        anchors.fill: parent
        visible: !patternController.showCustomColor

        source: {
            switch(patternController.currentPattern) {
                case "grayscale-ramp":
                    return "qrc:/patterns/GrayscaleRamp.qml"
                case "ansi-checker":
                    return "qrc:/patterns/AnsiChecker.qml"
                case "zone-boundary-grid":
                    return "qrc:/patterns/ZoneBoundaryGrid.qml"
                case "blooming-detection":
                    return "qrc:/patterns/BloomingDetection.qml"
                case "cross-dimming":
                    return "qrc:/patterns/CrossDimming.qml"
                default:
                    return "qrc:/patterns/GrayscaleRamp.qml"
            }
        }
    }

    // Touch navigation overlay
    Rectangle {
        anchors.fill: parent
        color: "transparent"

        MouseArea {
            anchors.fill: parent

            property real startX: 0
            property real startY: 0
            property bool hasMoved: false

            onPressed: {
                startX = mouse.x
                startY = mouse.y
                hasMoved = false
            }

            onPositionChanged: {
                if (Math.abs(mouse.x - startX) > 10 || Math.abs(mouse.y - startY) > 10) {
                    hasMoved = true
                }
            }

            onClicked: {
                if (!hasMoved) {
                    // Touch areas for navigation
                    if (mouse.x < parent.width / 4) {
                        // Left 25% - Previous pattern
                        previousPattern()
                    } else if (mouse.x > parent.width * 3 / 4) {
                        // Right 25% - Next pattern
                        nextPattern()
                    } else {
                        // Center 50% - Toggle UI
                        uiVisible = !uiVisible
                        if (uiVisible) {
                            uiHideTimer.restart()
                        }
                    }
                }
            }

            onReleased: {
                // Swipe navigation
                if (hasMoved) {
                    var deltaX = mouse.x - startX
                    if (Math.abs(deltaX) > 100) {
                        if (deltaX > 0) {
                            // Swipe right - Previous pattern
                            previousPattern()
                        } else {
                            // Swipe left - Next pattern
                            nextPattern()
                        }
                    }
                }
                hasMoved = false
            }
        }
    }

    // UI Overlay
    Rectangle {
        id: uiOverlay
        anchors.fill: parent
        color: "transparent"
        visible: uiVisible

        // Pattern counter (top-left)
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 20
            width: 160
            height: 60
            color: "#C0000000"
            radius: 8
            border.color: "white"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: {
                    var patterns = ["grayscale-ramp", "ansi-checker", "white", "black",
                                   "red", "green", "blue", "cyan", "magenta", "yellow",
                                   "zone-boundary-grid", "blooming-detection", "cross-dimming"];
                    var currentIndex = patterns.indexOf(patternController.currentPattern) + 1;
                    var totalPatterns = patterns.length;
                    return currentIndex + "/" + totalPatterns;
                }
                color: "white"
                font.pixelSize: 28
                font.bold: true
            }
        }

        // Pattern name (top-center)
        Rectangle {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.margins: 20
            width: patternNameText.width + 40
            height: 60
            color: "#C0004080"
            radius: 8
            border.color: "white"
            border.width: 1

            Text {
                id: patternNameText
                anchors.centerIn: parent
                text: patternController.currentPattern.toUpperCase()
                color: "white"
                font.pixelSize: 24
                font.bold: true
            }
        }

        // Exit button (top-right)
        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 20
            width: 100
            height: 60
            color: "#C0800000"
            radius: 8
            border.color: "white"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "EXIT"
                color: "white"
                font.pixelSize: 24
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                onClicked: Qt.quit()
            }
        }

        // Instructions (bottom-center)
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.margins: 20
            width: Math.max(instructionText.width + 20, 300)
            height: 50
            color: "#C0000000"
            radius: 8
            border.color: "white"
            border.width: 1

            Text {
                id: instructionText
                anchors.centerIn: parent
                text: "Tap edges or swipe to navigate • Tap center to hide UI"
                color: "white"
                font.pixelSize: 18
            }
        }

        // Resolution info (bottom-left)
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.margins: 20
            width: resolutionText.width + 20
            height: 50
            color: "#C0000000"
            radius: 8
            border.color: "white"
            border.width: 1

            Text {
                id: resolutionText
                anchors.centerIn: parent
                text: Screen.width + "x" + Screen.height
                color: "white"
                font.pixelSize: 16
            }
        }

    }

    // Network info (bottom-right) - Outside uiOverlay for independent visibility
    Rectangle {
        id: networkInfoRect
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 20
        width: networkText.contentWidth + 20
        height: networkText.contentHeight + 20
        color: "#C0000000"
        radius: 8
        border.color: "white"
        border.width: 1

        // Visibility based on metadata status
        visible: {
            var status = patternController.metadataStatus;
            if (status === "disable") {
                return false;  // Always hidden
            } else if (status === "enable") {
                return true;   // Always visible
            } else { // "autohide"
                return uiVisible;  // Follow UI visibility
            }
        }

        Text {
            id: networkText
            anchors.centerIn: parent
            text: patternController.networkInfo
            color: "white"
            font.pixelSize: 16

            // Enable multiline text support
            wrapMode: Text.NoWrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // Auto-hide timer
    Timer {
        id: uiHideTimer
        interval: 4000
        running: false
        onTriggered: uiVisible = false
    }

    // Show UI initially
    Component.onCompleted: {
        showUITemporarily()
    }
}
