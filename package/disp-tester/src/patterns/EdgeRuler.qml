import QtQuick 2.12

// Edge ruler: finds active pixels lost (cropped or shifted) anywhere between
// the source and the glass.
//
// A 1 px white frame sits on the outermost rows/columns. At the middle of each
// edge, pixel column/row n (counted inward from that edge) carries a line
// (n + 1) * step pixels long, labelled n. The shortest step still visible on
// the panel is the first surviving column/row, so e.g. a left staircase
// starting at "2" means columns 0 and 1 never reach the glass.
PatternBase {
    patternName: "edge-ruler"
    backgroundColor: "black"

    readonly property int lines: 16
    readonly property int step: 14
    readonly property int w: width
    readonly property int h: height
    readonly property int midX: Math.floor(w / 2)
    readonly property int midY: Math.floor(h / 2)

    // Frame on the outermost pixels. The two outermost columns on each side
    // get distinct colors (left: red, green; right: blue, white) so a macro
    // photo of one edge shows which source columns landed there, e.g. red and
    // green appearing after the white column on the right means the line
    // wrapped, white-white means the last pixel was repeated.
    Rectangle { x: 0;     y: 0;     width: w; height: 1; color: "white" }
    Rectangle { x: 0;     y: h - 1; width: w; height: 1; color: "white" }
    Rectangle { x: 0;     y: 0;     width: 1; height: h; color: "#FF0000" }
    Rectangle { x: 1;     y: 0;     width: 1; height: h; color: "#00FF00" }
    Rectangle { x: w - 2; y: 0;     width: 1; height: h; color: "#0000FF" }
    Rectangle { x: w - 1; y: 0;     width: 1; height: h; color: "white" }

    // Left edge: column n, downward from the middle
    Repeater {
        model: lines
        Item {
            Rectangle { x: index; y: midY; width: 1; height: (index + 1) * step; color: "white" }
            Text {
                x: lines + 6; y: midY + (index + 1) * step - height / 2
                text: index; color: "#FFD000"; font.pixelSize: 11
            }
        }
    }

    // Right edge: column w-1-n, downward from the middle
    Repeater {
        model: lines
        Item {
            Rectangle { x: w - 1 - index; y: midY; width: 1; height: (index + 1) * step; color: "white" }
            Text {
                x: w - lines - 6 - width; y: midY + (index + 1) * step - height / 2
                text: index; color: "#FFD000"; font.pixelSize: 11
            }
        }
    }

    // Top edge: row n, rightward from the middle
    Repeater {
        model: lines
        Item {
            Rectangle { x: midX; y: index; width: (index + 1) * step; height: 1; color: "white" }
            Text {
                x: midX + (index + 1) * step - width / 2; y: lines + 4
                text: index; color: "#FFD000"; font.pixelSize: 11
                visible: index % 2 === 0
            }
        }
    }

    // Bottom edge: row h-1-n, rightward from the middle
    Repeater {
        model: lines
        Item {
            Rectangle { x: midX; y: h - 1 - index; width: (index + 1) * step; height: 1; color: "white" }
            Text {
                x: midX + (index + 1) * step - width / 2; y: h - lines - 4 - height
                text: index; color: "#FFD000"; font.pixelSize: 11
                visible: index % 2 === 0
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
              + "\nshortest visible step = first column/row that reaches the glass"
    }
}
