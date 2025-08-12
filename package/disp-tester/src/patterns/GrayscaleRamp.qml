import QtQuick 2.12

PatternBase {
    patternName: "grayscale-ramp"
    
    // Horizontal grayscale gradient
    Rectangle {
        anchors.fill: parent
        
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#000000" }
            GradientStop { position: 0.125; color: "#202020" }
            GradientStop { position: 0.25; color: "#404040" }
            GradientStop { position: 0.375; color: "#606060" }
            GradientStop { position: 0.5; color: "#808080" }
            GradientStop { position: 0.625; color: "#a0a0a0" }
            GradientStop { position: 0.75; color: "#c0c0c0" }
            GradientStop { position: 0.875; color: "#e0e0e0" }
            GradientStop { position: 1.0; color: "#ffffff" }
        }
    }
    
    // Vertical grayscale bands for precise measurement
    Row {
        anchors.fill: parent
        
        Repeater {
            model: 16
            Rectangle {
                width: parent.width / 16
                height: parent.height
                color: Qt.rgba(index/15, index/15, index/15, 1.0)
            }
        }
    }
}
