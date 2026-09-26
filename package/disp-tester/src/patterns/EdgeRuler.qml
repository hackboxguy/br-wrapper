import QtQuick 2.12

// Edge ruler: finds active pixels lost (cropped or shifted) anywhere between
// the source and the glass.
//
// Frame: the outermost rows are white across the full width; the two
// outermost columns on each side are solid colors from row 1 to row h-2
// (left: red, green; right: blue, white). A macro photo of one edge shows
// which source columns landed there: on a correct panel the left edge reads
// red, green and the right edge blue, white. Red and green appearing after the
// white column on the right means the line wrapped; blue, white, blue, white
// means the last pixel pair was repeated.
//
// Staircases, inward of the frame: at the middle of each side, pixel column n
// (counted from that edge, starting at 2) carries a line whose length grows by
// one step per column, labelled n; top and bottom do the same for rows from 1.
// The shortest step still visible is the first surviving column/row beyond
// the frame, so a loss deeper than the frame can still be counted.
PatternBase {
    patternName: "edge-ruler"
    backgroundColor: "black"

    readonly property int lines: 16
    readonly property int step: 14
    readonly property int firstCol: 2     // side staircases start inside the colored columns
    readonly property int firstRow: 1     // top/bottom staircases start inside the white row
    readonly property int w: width
    readonly property int h: height
    readonly property int midX: Math.floor(w / 2)
    readonly property int midY: Math.floor(h / 2)

    // Frame on the outermost pixels
    Rectangle { x: 0;     y: 0;     width: w; height: 1;     color: "white" }
    Rectangle { x: 0;     y: h - 1; width: w; height: 1;     color: "white" }
    Rectangle { x: 0;     y: 1;     width: 1; height: h - 2; color: "#FF0000" }
    Rectangle { x: 1;     y: 1;     width: 1; height: h - 2; color: "#00FF00" }
    Rectangle { x: w - 2; y: 1;     width: 1; height: h - 2; color: "#0000FF" }
    Rectangle { x: w - 1; y: 1;     width: 1; height: h - 2; color: "white" }

    // Left edge: column firstCol+i, downward from the middle
    Repeater {
        model: lines
        Item {
            Rectangle { x: firstCol + index; y: midY; width: 1; height: (index + 1) * step; color: "white" }
            Text {
                x: firstCol + lines + 6; y: midY + (index + 1) * step - height / 2
                text: firstCol + index; color: "#FFD000"; font.pixelSize: 11
            }
        }
    }

    // Right edge: column w-1-(firstCol+i), downward from the middle
    Repeater {
        model: lines
        Item {
            Rectangle { x: w - 1 - firstCol - index; y: midY; width: 1; height: (index + 1) * step; color: "white" }
            Text {
                x: w - firstCol - lines - 6 - width; y: midY + (index + 1) * step - height / 2
                text: firstCol + index; color: "#FFD000"; font.pixelSize: 11
            }
        }
    }

    // Top edge: row firstRow+i, rightward from the middle
    Repeater {
        model: lines
        Item {
            Rectangle { x: midX; y: firstRow + index; width: (index + 1) * step; height: 1; color: "white" }
            Text {
                x: midX + (index + 1) * step - width / 2; y: firstRow + lines + 4
                text: firstRow + index; color: "#FFD000"; font.pixelSize: 11
                visible: (firstRow + index) % 2 === 1
            }
        }
    }

    // Bottom edge: row h-1-(firstRow+i), rightward from the middle
    Repeater {
        model: lines
        Item {
            Rectangle { x: midX; y: h - 1 - firstRow - index; width: (index + 1) * step; height: 1; color: "white" }
            Text {
                x: midX + (index + 1) * step - width / 2; y: h - firstRow - lines - 4 - height
                text: firstRow + index; color: "#FFD000"; font.pixelSize: 11
                visible: (firstRow + index) % 2 === 1
            }
        }
    }

    // Centre crosshair and readout
    Rectangle { x: midX - 40; y: midY; width: 81; height: 1; color: "#808080" }
    Rectangle { x: midX; y: midY - 40; width: 1; height: 81; color: "#808080" }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: midY + 56
        horizontalAlignment: Text.AlignHCenter
        color: "#A0A0A0"
        font.pixelSize: 20
        text: "EDGE RULER  " + w + " x " + h
              + "\nedges: red, green | blue, white columns, white top and bottom rows"
              + "\nshortest visible step = first column/row beyond the frame that reaches the glass"
    }
}
