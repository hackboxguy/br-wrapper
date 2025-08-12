import QtQuick 2.12

PatternBase {
    patternName: "blooming-detection"
    backgroundColor: "black"
    
    // Single bright white pixel in center
    Rectangle {
        id: bloomPixel
        x: (parent.width / 2) - 1  // Center the 2x2 pixel
        y: (parent.height / 2) - 1
        width: 2
        height: 2
        color: "white"
        
        // Animated pulsing for better visibility during setup
        SequentialAnimation {
            running: true
            loops: Animation.Infinite
            PropertyAnimation {
                target: bloomPixel
                property: "opacity"
                from: 1.0
                to: 0.7
                duration: 1000
            }
            PropertyAnimation {
                target: bloomPixel
                property: "opacity"
                from: 0.7
                to: 1.0
                duration: 1000
            }
        }
    }
    
    // Crosshair for precise positioning (very dim)
    Rectangle {
        x: (parent.width / 2) - 20
        y: (parent.height / 2) - 0.5
        width: 40
        height: 1
        color: "white"
        opacity: 0.1
    }
    
    Rectangle {
        x: (parent.width / 2) - 0.5
        y: (parent.height / 2) - 20
        width: 1
        height: 40
        color: "white"
        opacity: 0.1
    }
    
    // Position info overlay (bottom-right)
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        width: positionText.width + 20
        height: positionText.height + 10
        color: "black"
        opacity: 0.8
        
        Text {
            id: positionText
            x: 10
            y: 5
            color: "white"
            font.pixelSize: 14
            text: "Single Pixel Test\nCenter Position\nBlooming Detection"
        }
    }
}
