import QtQuick 2.12

// Checkerboard of cols x rows tiles over the whole screen, the top-left tile in the first colour.
// An odd board has its centre tile in the first colour: "checker 3 3" on 1920x1080 is 640x360 tiles
// with the centre black and 4 of 9 white - the in-scene black of a local-dimming profile (the meter
// on the centre tile). Tile edges are rounded to whole pixels, so the tiles cover the screen exactly.
// Network: pattern checker <cols> <rows> [<r> <g> <b> <r> <g> <b>]
PatternBase {
    id: board
    patternName: "checker"

    property var params: patternController ? patternController.patternParams : ({})
    property int cols: params.checkerCols || 3
    property int rows: params.checkerRows || 3
    property color colourA: Qt.rgba((params.checkerAR || 0) / 255, (params.checkerAG || 0) / 255, (params.checkerAB || 0) / 255, 1)
    property color colourB: Qt.rgba((params.checkerBR !== undefined ? params.checkerBR : 255) / 255,
                                    (params.checkerBG !== undefined ? params.checkerBG : 255) / 255,
                                    (params.checkerBB !== undefined ? params.checkerBB : 255) / 255, 1)
    backgroundColor: colourA

    Repeater {
        model: board.cols * board.rows
        Rectangle {
            property int c: index % board.cols
            property int r: Math.floor(index / board.cols)
            x: Math.round(c * board.width / board.cols)
            y: Math.round(r * board.height / board.rows)
            width: Math.round((c + 1) * board.width / board.cols) - x
            height: Math.round((r + 1) * board.height / board.rows) - y
            color: (c + r) % 2 === 0 ? board.colourA : board.colourB
        }
    }
}
