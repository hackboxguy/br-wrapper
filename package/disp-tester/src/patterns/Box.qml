import QtQuick 2.12

// Measurement window: a square of one RGB colour covering a given share of the screen area (or a
// given side in pixels), on a background colour (sets the frame's APL - OLED ABL, local dimming),
// centred on the screen or on a pixel position (the sensor locator steps a small box across it).
// Network: pattern box <area-pct>|<N>px <r> <g> <b> [bg <r> <g> <b>] [at <x> <y>]
PatternBase {
    patternName: "box"

    property var params: patternController ? patternController.patternParams : ({})
    property real areaPct: params.boxAreaPct !== undefined ? params.boxAreaPct : 10
    property int sidePx: params.boxSidePx || 0
    property int cx: params.boxCx !== undefined && params.boxCx >= 0 ? params.boxCx : Math.round(parent.width / 2)
    property int cy: params.boxCy !== undefined && params.boxCy >= 0 ? params.boxCy : Math.round(parent.height / 2)
    backgroundColor: Qt.rgba((params.boxBgR || 0) / 255, (params.boxBgG || 0) / 255, (params.boxBgB || 0) / 255, 1)

    // side of a square with areaPct of the screen's area (or sidePx), at most the shorter screen side
    property int boxSide: Math.min(sidePx > 0 ? sidePx : Math.round(Math.sqrt(areaPct / 100.0 * parent.width * parent.height)),
                                   Math.min(parent.width, parent.height))

    Rectangle {
        width: boxSide
        height: boxSide
        x: cx - Math.round(boxSide / 2)
        y: cy - Math.round(boxSide / 2)
        color: Qt.rgba((params.boxR || 0) / 255, (params.boxG || 0) / 255, (params.boxB || 0) / 255, 1)
    }
}
