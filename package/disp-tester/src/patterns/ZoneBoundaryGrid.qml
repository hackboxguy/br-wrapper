import QtQuick 2.12

PatternBase {
    patternName: "zone-boundary-grid"
    backgroundColor: "black"
    
    // Calculate spacing for 16x9 grid
    property int actualSpacingX: parent.width / 16
    property int actualSpacingY: parent.height / 9
    
    // Vertical lines (including right edge)
    Repeater {
        model: 17  // 16 zones = 17 lines
        Rectangle {
            x: index < 16 ? index * actualSpacingX : parent.width - 2
            y: 0
            width: 2
            height: parent.height
            color: "white"
            opacity: 0.8
        }
    }
    
    // Horizontal lines (including bottom edge)
    Repeater {
        model: 10  // 9 zones = 10 lines
        Rectangle {
            x: 0
            y: index < 9 ? index * actualSpacingY : parent.height - 2
            width: parent.width
            height: 2
            color: "white"
            opacity: 0.8
        }
    }
    
    // Zone numbers
    Repeater {
        model: 144  // 16x9 = 144 zones
        Rectangle {
            property int zoneX: index % 16
            property int zoneY: Math.floor(index / 16)
            
            x: zoneX * actualSpacingX + 2
            y: zoneY * actualSpacingY + 2
            width: actualSpacingX - 4
            height: actualSpacingY - 4
            
            color: "transparent"
            
            // Zone number text
            Text {
                anchors.centerIn: parent
                text: index.toString()
                color: "white"
                font.pixelSize: Math.min(parent.width, parent.height) * 0.25
                visible: parent.width > 40 && parent.height > 25  // Only show if zone is large enough
            }
        }
    }
}
