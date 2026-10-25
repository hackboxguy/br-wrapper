import QtQuick 2.12

// Measurement window: a centred square of one RGB colour covering a given share of the screen
// area, on a background colour (sets the frame's APL - OLED ABL, local dimming).
// Network: pattern box <area-pct> <r> <g> <b> [bg <r> <g> <b>]
PatternBase {
    patternName: "box"

    property var params: patternController ? patternController.patternParams : ({})
    property real areaPct: params.boxAreaPct !== undefined ? params.boxAreaPct : 10
    backgroundColor: Qt.rgba((params.boxBgR || 0) / 255, (params.boxBgG || 0) / 255, (params.boxBgB || 0) / 255, 1)

    // side of a square with areaPct of the screen's area, at most the shorter screen side
    property int boxSide: Math.min(Math.round(Math.sqrt(areaPct / 100.0 * parent.width * parent.height)),
                                   Math.min(parent.width, parent.height))

    Rectangle {
        width: boxSide
        height: boxSide
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        color: Qt.rgba((params.boxR || 0) / 255, (params.boxG || 0) / 255, (params.boxB || 0) / 255, 1)
    }
}
