import QtQuick 2.12

// Absolute physical-size white box.
//
// Unlike the legacy "whitebox" pattern (percentage of the smaller screen
// dimension), this renders a box of an exact physical size in millimetres.
// The active-area physical width and height are supplied explicitly (from the
// panel datasheet), so no diagonal/aspect/square-pixel assumptions are made:
// pixels-per-mm is computed independently for each axis, giving a box that is
// physically square even on panels with non-square pixels.
PatternBase {
    patternName: "whiteboxmm"
    backgroundColor: "black"

    property var params: patternController ? patternController.patternParams : ({})

    // Requested box side length in millimetres.
    property real boxSizeMM: params.whiteboxmmSize !== undefined ? params.whiteboxmmSize : 50.0
    // Active-area physical dimensions in millimetres (from the panel datasheet).
    property real physWidthMM: params.whiteboxmmPhysWidthMM !== undefined ? params.whiteboxmmPhysWidthMM : 0.0
    property real physHeightMM: params.whiteboxmmPhysHeightMM !== undefined ? params.whiteboxmmPhysHeightMM : 0.0

    // Screen dimensions (window fills the panel, so these are the native pixels
    // spanning the supplied physical dimensions).
    property int screenWidth: parent.width
    property int screenHeight: parent.height

    // Pixels-per-mm per axis. Guard against a missing/zero physical size.
    property real pxPerMM_W: physWidthMM > 0 ? screenWidth / physWidthMM : 0
    property real pxPerMM_H: physHeightMM > 0 ? screenHeight / physHeightMM : 0

    // Box dimensions in pixels (physically square: boxSizeMM on each axis).
    property int boxWidthPx: Math.round(boxSizeMM * pxPerMM_W)
    property int boxHeightPx: Math.round(boxSizeMM * pxPerMM_H)
    property bool valid: pxPerMM_W > 0 && pxPerMM_H > 0 && boxWidthPx > 0 && boxHeightPx > 0

    // White box centered on screen
    Rectangle {
        id: whiteBox
        width: boxWidthPx
        height: boxHeightPx
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        color: "white"
        visible: valid
    }

    // Dim crosshair for precise positioning when the box is small.
    property bool showCrosshair: valid && Math.min(boxWidthPx, boxHeightPx) < 200

    // Horizontal line
    Rectangle {
        x: (parent.width / 2) - 40
        y: (parent.height / 2) - 0.5
        width: 80
        height: 1
        color: "white"
        opacity: 0.05
        visible: showCrosshair
    }

    // Vertical line
    Rectangle {
        x: (parent.width / 2) - 0.5
        y: (parent.height / 2) - 40
        width: 1
        height: 80
        color: "white"
        opacity: 0.05
        visible: showCrosshair
    }

    // Dim readout (top-left) so the applied size can be confirmed without a
    // ruler. Shown only when the command included the "verbose" argument, so a
    // plain measurement run has nothing lit outside the box.
    property bool showReadout: params.whiteboxmmVerbose === true
    Text {
        x: 16
        y: 16
        visible: showReadout
        color: "#404040"
        font.pixelSize: 18
        text: valid
              ? (boxSizeMM.toFixed(1) + " mm  ->  " + boxWidthPx + " x " + boxHeightPx + " px"
                 + "   (" + pxPerMM_W.toFixed(3) + " / " + pxPerMM_H.toFixed(3) + " px/mm)")
              : "whiteboxmm: set width-mm and height-mm"
    }
}
