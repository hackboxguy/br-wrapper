import QtQuick 2.12

// A white box moving on black, for local-dimming halo tracking: the box crosses zone boundaries at a
// steady speed, so the backlight's lag (the halo trailing or leading the box) and zone stepping show.
// Network: pattern moving-whitebox size <mm> | speed <mm/s> | direction horizontal|vertical|diagonal |
//          level <0-255> | pause | resume
// Sized and moved in mm: px per mm per axis from the panel's active area (--panel-size-mm), so the box
// is square and the speed the same on any panel; without it 6 px/mm (~0.17 mm pitch: the 12.3"-15.6"
// panels are 5.9-7.4 px/mm). Horizontal sweeps the middle row, vertical the middle column, diagonal
// bounces off all four edges. Driven by the animation clock (vsync), not a timer: no frame-rate jitter.
PatternBase {
    id: root
    patternName: "moving-whitebox"
    backgroundColor: "black"

    property var params: patternController ? patternController.patternParams : ({})
    property real sizeMM: params.movingBoxSizeMM !== undefined ? params.movingBoxSizeMM : 20
    property real speedMM: params.movingBoxSpeedMM !== undefined ? params.movingBoxSpeedMM : 34
    property string direction: params.movingBoxDirection !== undefined ? params.movingBoxDirection : "horizontal"
    property int level: params.movingBoxLevel !== undefined ? params.movingBoxLevel : 255
    property bool paused: params.movingBoxPaused !== undefined ? params.movingBoxPaused : false

    readonly property real fallbackPxPerMM: 6
    property real pxPerMM_W: params.panelWidthMM > 0 ? width / params.panelWidthMM : fallbackPxPerMM
    property real pxPerMM_H: params.panelHeightMM > 0 ? height / params.panelHeightMM : fallbackPxPerMM

    property real boxW: Math.min(width, Math.round(sizeMM * pxPerMM_W))
    property real boxH: Math.min(height, Math.round(sizeMM * pxPerMM_H))
    property real rangeX: Math.max(0, width - boxW)
    property real rangeY: Math.max(0, height - boxH)
    property bool moveX: direction !== "vertical"
    property bool moveY: direction !== "horizontal"
    // diagonal: the speed along the path, sqrt(2) per axis
    property real axisSpeedMM: direction === "diagonal" ? speedMM / Math.SQRT2 : speedMM
    property int durX: Math.round(rangeX / Math.max(1e-6, axisSpeedMM * pxPerMM_W) * 1000)
    property int durY: Math.round(rangeY / Math.max(1e-6, axisSpeedMM * pxPerMM_H) * 1000)

    Rectangle {
        id: box
        width: root.boxW
        height: root.boxH
        color: Qt.rgba(root.level / 255, root.level / 255, root.level / 255, 1)
    }

    SequentialAnimation {
        id: animX
        loops: Animation.Infinite
        NumberAnimation { target: box; property: "x"; from: 0; to: root.rangeX; duration: root.durX }
        NumberAnimation { target: box; property: "x"; from: root.rangeX; to: 0; duration: root.durX }
    }

    SequentialAnimation {
        id: animY
        loops: Animation.Infinite
        NumberAnimation { target: box; property: "y"; from: 0; to: root.rangeY; duration: root.durY }
        NumberAnimation { target: box; property: "y"; from: root.rangeY; to: 0; duration: root.durY }
    }

    // An animation takes new durations only when started: restart on every geometry/speed change,
    // from the left/top edge (a still axis stays centred)
    function restart() {
        animX.stop();
        animY.stop();
        box.x = moveX ? 0 : rangeX / 2;
        box.y = moveY ? 0 : rangeY / 2;
        if (speedMM <= 0)
            return;
        if (moveX && rangeX > 0)
            animX.start();
        if (moveY && rangeY > 0)
            animY.start();
        animX.paused = paused && animX.running;
        animY.paused = paused && animY.running;
    }

    onPausedChanged: {
        if (animX.running) animX.paused = paused;
        if (animY.running) animY.paused = paused;
    }
    onDurXChanged: Qt.callLater(restart)
    onDurYChanged: Qt.callLater(restart)
    onRangeXChanged: Qt.callLater(restart)
    onRangeYChanged: Qt.callLater(restart)
    onDirectionChanged: Qt.callLater(restart)
    onSpeedMMChanged: Qt.callLater(restart)
    Component.onCompleted: Qt.callLater(restart)
}
