import QtQuick 2.12

PatternBase {
    patternName: "white-text-black"
    backgroundColor: "black"
    
    // Large title text
    Text {
        id: titleText
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: parent.height * 0.1
        
        text: "AUTOMOTIVE DISPLAY TEST"
        color: "white"
        font.pixelSize: Math.min(parent.width, parent.height) / 20
        font.bold: true
    }
}
    // Subtitle rows simulating automotive UI
    Column {
        anchors.centerIn: parent
        spacing: parent.height * 0.05
        
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Speed: 120 km/h"
            color: "white"
            font.pixelSize: Math.min(parent.parent.width, parent.parent.height) / 25
        }
        
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Engine: 2500 RPM"
            color: "white" 
            font.pixelSize: Math.min(parent.parent.width, parent.parent.height) / 25
        }
        
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Fuel: 85% | Range: 420 km"
            color: "white"
            font.pixelSize: Math.min(parent.parent.width, parent.parent.height) / 25
        }
        
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Temperature: 21°C"
            color: "white"
            font.pixelSize: Math.min(parent.parent.width, parent.parent.height) / 25
        }
    }
    
    // Bottom status bar text
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: parent.height * 0.08
        color: "black"
        
        Row {
            anchors.centerIn: parent
            spacing: parent.parent.width * 0.1
            
            Text {
                text: "GPS"
                color: "white"
                font.pixelSize: Math.min(parent.parent.parent.width, parent.parent.parent.height) / 30
            }
            
            Text {
                text: "BLUETOOTH"
                color: "white"
                font.pixelSize: Math.min(parent.parent.parent.width, parent.parent.parent.height) / 30
            }
            
            Text {
                text: "12:34"
                color: "white"
                font.pixelSize: Math.min(parent.parent.parent.width, parent.parent.parent.height) / 30
                font.bold: true
            }
        }
    }
