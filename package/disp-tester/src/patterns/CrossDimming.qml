import QtQuick 2.12

PatternBase {
    patternName: "cross-dimming"
    backgroundColor: "black"
    
    property var params: patternController.patternParams
    property int crossDimmingSpots: params.crossDimmingSpots || 4
    
    // Bright spots arranged to test cross-dimming interference
    Repeater {
        model: crossDimmingSpots
        
        Rectangle {
            id: brightSpot
            width: 60
            height: 60
            radius: 30
            color: "white"
            
            // Position spots in a pattern based on count
            Component.onCompleted: {
                var positions = calculatePositions(index, crossDimmingSpots, parent.width, parent.height)
                x = positions.x - width/2
                y = positions.y - height/2
            }
            
            // Smooth pulsing animation
            SequentialAnimation {
                running: true
                loops: Animation.Infinite
                PropertyAnimation {
                    target: brightSpot
                    property: "opacity"
                    from: 1.0
                    to: 0.8
                    duration: 1500 + (index * 200)  // Stagger timing
                }
                PropertyAnimation {
                    target: brightSpot
                    property: "opacity"
                    from: 0.8
                    to: 1.0
                    duration: 1500 + (index * 200)
                }
            }
        }
    }
    
    // Position calculation function
    function calculatePositions(index, total, screenWidth, screenHeight) {
        var margin = 100  // Keep spots away from edges
        var usableWidth = screenWidth - 2 * margin
        var usableHeight = screenHeight - 2 * margin
        
        var x, y
        
        if (total === 1) {
            // Single spot in center
            x = screenWidth / 2
            y = screenHeight / 2
        } else if (total === 2) {
            // Two spots horizontally
            x = margin + (index * usableWidth)
            y = screenHeight / 2
        } else if (total === 3) {
            // Triangle pattern
            if (index === 0) {
                x = screenWidth / 2
                y = margin + usableHeight * 0.2
            } else {
                x = margin + ((index - 1) * usableWidth)
                y = margin + usableHeight * 0.8
            }
        } else if (total === 4) {
            // Four corners
            x = margin + (index % 2) * usableWidth
            y = margin + Math.floor(index / 2) * usableHeight
        } else {
            // Distribute in a circle for higher counts
            var angle = (index / total) * 2 * Math.PI
            var radius = Math.min(usableWidth, usableHeight) * 0.4
            x = screenWidth / 2 + radius * Math.cos(angle)
            y = screenHeight / 2 + radius * Math.sin(angle)
        }
        
        return { x: x, y: y }
    }
    
    // Spots info overlay (bottom-right)
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        width: spotsText.width + 20
        height: spotsText.height + 10
        color: "black"
        opacity: 0.8
        
        Text {
            id: spotsText
            x: 10
            y: 5
            color: "white"
            font.pixelSize: 14
            text: "Bright Spots: " + crossDimmingSpots +
                  "\nPattern: " + (crossDimmingSpots <= 4 ? 
                      ["Single", "Horizontal", "Triangle", "Corners"][crossDimmingSpots - 1] : 
                      "Circle")
        }
    }
}
