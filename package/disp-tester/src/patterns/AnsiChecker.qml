import QtQuick 2.12

PatternBase {
    patternName: "ansi-checker"
    
    // ANSI checkerboard pattern
    Column {
        anchors.fill: parent
        
        property int checkerSize: Math.min(parent.width, parent.height) / 20
        
        Repeater {
            model: Math.ceil(parent.height / parent.checkerSize)
            
            Row {
                property int rowIndex: index
                
                Repeater {
                    model: Math.ceil(parent.parent.width / parent.parent.checkerSize)
                    
                    Rectangle {
                        width: parent.parent.checkerSize
                        height: parent.parent.checkerSize
                        color: ((parent.rowIndex + index) % 2 === 0) ? "white" : "black"
                    }
                }
            }
        }
    }
    
    // Alternative grid-based implementation for better performance
    Grid {
        anchors.fill: parent
        visible: false // Use column/row version above for now
        
        property int checkerSize: Math.min(parent.width, parent.height) / 16
        columns: Math.ceil(parent.width / checkerSize)
        rows: Math.ceil(parent.height / checkerSize)
        
        Repeater {
            model: parent.columns * parent.rows
            
            Rectangle {
                width: parent.checkerSize
                height: parent.checkerSize
                color: {
                    var col = index % parent.columns
                    var row = Math.floor(index / parent.columns)
                    return ((row + col) % 2 === 0) ? "white" : "black"
                }
            }
        }
    }
}
