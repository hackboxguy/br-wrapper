import QtQuick 2.12

PatternBase {
    patternName: "zone-boundary-grid"
    backgroundColor: "black"
    
    // Perfect square zones aligned to 42x24 LED backlight
    property int zoneSize: 60  // 60x60 pixel squares
    property int gridWidth: 42 * zoneSize   // 2520 pixels
    property int gridHeight: 24 * zoneSize  // 1440 pixels
    property int offsetX: (parent.width - gridWidth) / 2  // Center horizontally
    property int offsetY: (parent.height - gridHeight) / 2  // Center vertically
    
    // Vertical lines (42 zones = 43 lines)
    Repeater {
        model: 43
        Rectangle {
            x: offsetX + (index * zoneSize)
            y: offsetY
            width: 2
            height: gridHeight
            color: "white"
            opacity: 0.8
        }
    }
    
    // Horizontal lines (24 zones = 25 lines, including bottom edge)
    Repeater {
        model: 25
        Rectangle {
            x: offsetX
            y: offsetY + (index * zoneSize)
            width: gridWidth
            height: 2
            color: "white"
            opacity: 0.8
            
            // Ensure the last line (index 24) is positioned correctly
            Component.onCompleted: {
                if (index === 24) {
                    // Position the bottom line at the very bottom of the grid
                    y = offsetY + gridHeight - 2
                }
            }
        }
    }
    
    // Zone numbers (1008 zones, each exactly 60x60 pixels)
    Repeater {
        model: 1008
        Rectangle {
            property int zoneX: index % 42
            property int zoneY: Math.floor(index / 42)
            
            x: offsetX + (zoneX * zoneSize) + 2
            y: offsetY + (zoneY * zoneSize) + 2
            width: zoneSize - 4
            height: zoneSize - 4
            
            color: "transparent"
            
            // Zone number text
            Text {
                anchors.centerIn: parent
                text: index.toString()
                color: "white"
                font.pixelSize: 12  // Fixed size for 60x60 zones
                visible: true  // Always show numbers in 60x60 zones
            }
        }
    }
}
