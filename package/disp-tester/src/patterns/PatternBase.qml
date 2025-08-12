import QtQuick 2.12

// Base pattern component
Item {
    id: root
    anchors.fill: parent
    
    property string patternName: "base"
    property alias backgroundColor: background.color
    
    // Default black background
    Rectangle {
        id: background
        anchors.fill: parent
        color: "black"
    }
}
