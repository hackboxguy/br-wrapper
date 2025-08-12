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
    
    // Handle touch/mouse events for pattern cycling
    MouseArea {
        anchors.fill: parent
        onClicked: {
            patternController.nextPattern()
        }
    }
    
    // Custom color overlay (when RGB patch is active)
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
                case "white-text-black":
                    return "qrc:/patterns/WhiteTextBlack.qml"
                default:
                    return "qrc:/patterns/GrayscaleRamp.qml"
            }
        }
    }
    
    // Debug info overlay (top-left corner)
    Rectangle {
        x: 10
        y: 10
        width: debugText.width + 20
        height: debugText.height + 10
        color: "black"
        opacity: 0.7
        
        Text {
            id: debugText
            x: 10
            y: 5
            color: "white"
            font.pixelSize: 16
            text: "Pattern: " + patternController.currentPattern + 
                  "\nResolution: " + Screen.width + "x" + Screen.height +
                  "\nTouch to cycle patterns"
        }
    }
}
