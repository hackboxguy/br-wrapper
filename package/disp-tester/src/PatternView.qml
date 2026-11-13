import QtQuick 2.12

// The pattern itself (solid colour or the current pattern), without any controls. Shared by the
// main window and MirrorWindow.qml, so both displays always show the same thing.
Item {
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
                case "colorbar":
                    return "qrc:/patterns/SmpteBars.qml"
                case "zone-boundary-grid":
                    return "qrc:/patterns/ZoneBoundaryGrid.qml"
                case "blooming-detection":
                    return "qrc:/patterns/BloomingDetection.qml"
                case "cross-dimming":
                    return "qrc:/patterns/CrossDimming.qml"
                case "whitebox":
                    return "qrc:/patterns/WhiteBox.qml"
                case "whiteboxmm":
                    return "qrc:/patterns/WhiteBoxMM.qml"
                case "box":
                    return "qrc:/patterns/Box.qml"
                case "checker":
                    return "qrc:/patterns/Checker.qml"
                case "moving-whitebox":
                    return "qrc:/patterns/MovingWhiteBox.qml"
                case "edge-ruler":
                    return "qrc:/patterns/EdgeRuler.qml"
                case "sensor-outline":
                    return "qrc:/patterns/SensorOutline.qml"
                default:
                    return "qrc:/patterns/GrayscaleRamp.qml"
            }
        }
    }
}
