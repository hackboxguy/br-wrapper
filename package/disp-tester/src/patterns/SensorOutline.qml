import QtQuick 2.12

// The i1 Display Pro's outline at true size, for placing the sensor before a measurement: the
// operator lays the sensor on the drawing instead of guessing the centre of a white box.
// Network: pattern sensor-outline width-mm <W> height-mm <H> [at <x> <y>]
//   W x H = the panel's active area in mm (the framework config's physical.active_*_mm); px per mm per
//   axis as whiteboxmm, so the drawing is the sensor's size on any panel and resolution. The lens is
//   centred on the screen centre (or pixel x,y).
// Thin mid-grey lines on black, nothing bright: the prompt can sit for minutes without moving the
// OLED's ABL or the panel's warm-up.
//
// The sensor as it hangs on a display, cable end towards the top edge (owner, 2026-10-28). Body
// 37 x 65 mm, lens 20 mm (owner's dimensions, tmp-docs/i1-display-pro-outline.jpg); from the photo:
// flat ends 60 % of the width, full width around the lens, a bezel ring of ~30 mm, the cable boss at
// the cable end. The lens centre is 32.1 mm from the non-cable end: the sensor locator found the
// measuring aperture 3.8 mm nearer the cable end than the photo's 28.3 mm with the body on the
// outline (OLED-IB, twice), so the drawn circle is where the i1 measures.
PatternBase {
    patternName: "sensor-outline"
    backgroundColor: "black"

    property var params: patternController ? patternController.patternParams : ({})
    property real physWidthMM: params.outlineWidthMM || 0
    property real physHeightMM: params.outlineHeightMM || 0
    property real pxPerMM_W: physWidthMM > 0 ? width / physWidthMM : 0
    property real pxPerMM_H: physHeightMM > 0 ? height / physHeightMM : 0
    property real cx: params.outlineCx !== undefined && params.outlineCx >= 0 ? params.outlineCx : width / 2
    property real cy: params.outlineCy !== undefined && params.outlineCy >= 0 ? params.outlineCy : height / 2
    property bool valid: pxPerMM_W > 0 && pxPerMM_H > 0

    // sensor geometry in mm, relative to the lens centre (y down: the cable end is negative)
    readonly property real bodyHalfW: 37 / 2
    readonly property real endHalfW: 0.6 * 37 / 2
    readonly property real cableEndY: -(65 - 32.1)
    readonly property real farEndY: 32.1
    readonly property real shoulderTopY: -8.9      // full width from here ...
    readonly property real shoulderBottomY: 15.1   // ... to here (24 mm, as in the photo)
    readonly property real lensR: 20 / 2
    readonly property real bezelR: 30 / 2
    readonly property real bossHalfW: 5
    readonly property real bossH: 4

    property color lineColor: Qt.rgba(128 / 255, 128 / 255, 128 / 255, 1)
    property int lineWidth: 3

    onPxPerMM_WChanged: canvas.requestPaint()
    onPxPerMM_HChanged: canvas.requestPaint()
    onCxChanged: canvas.requestPaint()
    onCyChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        visible: valid

        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            if (!valid)
                return;
            var X = function (mm) { return cx + mm * pxPerMM_W; };
            var Y = function (mm) { return cy + mm * pxPerMM_H; };
            ctx.strokeStyle = lineColor;
            ctx.lineWidth = lineWidth;
            ctx.lineJoin = "round";

            // body: flat ends, slanted to the full width, straight sides level with the lens
            ctx.beginPath();
            ctx.moveTo(X(-endHalfW), Y(cableEndY));
            ctx.lineTo(X(endHalfW), Y(cableEndY));
            ctx.lineTo(X(bodyHalfW), Y(shoulderTopY));
            ctx.lineTo(X(bodyHalfW), Y(shoulderBottomY));
            ctx.lineTo(X(endHalfW), Y(farEndY));
            ctx.lineTo(X(-endHalfW), Y(farEndY));
            ctx.lineTo(X(-bodyHalfW), Y(shoulderBottomY));
            ctx.lineTo(X(-bodyHalfW), Y(shoulderTopY));
            ctx.closePath();
            ctx.stroke();

            // cable boss outside the cable end
            ctx.strokeRect(X(-bossHalfW), Y(cableEndY - bossH), 2 * bossHalfW * pxPerMM_W, bossH * pxPerMM_H);

            // bezel ring and lens
            ctx.beginPath();
            ctx.ellipse(X(-bezelR), Y(-bezelR), 2 * bezelR * pxPerMM_W, 2 * bezelR * pxPerMM_H);
            ctx.stroke();
            ctx.beginPath();
            ctx.ellipse(X(-lensR), Y(-lensR), 2 * lensR * pxPerMM_W, 2 * lensR * pxPerMM_H);
            ctx.stroke();

            // crosshair in the lens
            ctx.lineWidth = Math.max(1, lineWidth - 1);
            ctx.beginPath();
            ctx.moveTo(X(-0.7 * lensR), Y(0));
            ctx.lineTo(X(0.7 * lensR), Y(0));
            ctx.moveTo(X(0), Y(-0.7 * lensR));
            ctx.lineTo(X(0), Y(0.7 * lensR));
            ctx.stroke();
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !valid
        color: "#808080"
        font.pixelSize: 24
        text: "sensor-outline: set width-mm and height-mm"
    }
}
