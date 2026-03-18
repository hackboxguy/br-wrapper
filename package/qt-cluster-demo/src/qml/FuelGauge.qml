import QtQuick 2.12

Item {
    id: root
    property int level: cluster.fuelLevel

    property real _smoothLevel: level
    Behavior on _smoothLevel {
        SmoothedAnimation { velocity: 50 }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: draw()

        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    // Repaint when value changes
    onLevelChanged: canvas.requestPaint()
    on_SmoothLevelChanged: canvas.requestPaint()

    function draw() {
        var ctx = canvas.getContext("2d");
        var w = canvas.width;
        var h = canvas.height;
        var cx = w / 2;
        var cy = h * 0.55;
        var radius = Math.min(w, h) * 0.40;
        var toRad = Math.PI / 180;

        ctx.clearRect(0, 0, w, h);

        var arcStart = -210;  // degrees (0 = right, CCW negative)
        var arcEnd = -150;
        // We draw the arc from 210 to 330 degrees (a 120-degree sweep at bottom-left quadrant)
        // Remap: startAngle = 210 deg, endAngle = 330 deg (clockwise)
        var sa = 150 * toRad;   // 150 degrees in canvas coords
        var ea = 30 * toRad;    // 30 degrees (wraps around top)

        // Background arc
        ctx.beginPath();
        ctx.arc(cx, cy, radius, sa, ea, true);  // counter-clockwise from 150 to 30
        ctx.strokeStyle = "#333333";
        ctx.lineWidth = radius * 0.12;
        ctx.lineCap = "round";
        ctx.stroke();

        // Value arc
        var frac = _smoothLevel / 100.0;
        var valAngle = sa - frac * (sa - ea + 2 * Math.PI);
        // Simpler: interpolate
        // Arc goes CCW from sa (150) to ea (30), total sweep = -120 deg (or +240 via CCW)
        // Actually let's use a simpler arc range
        var sweepDeg = 120;
        var startDeg = 150;  // Empty position
        var endDeg = startDeg - sweepDeg * frac;

        ctx.beginPath();
        ctx.arc(cx, cy, radius, startDeg * toRad, endDeg * toRad, true);
        ctx.strokeStyle = (_smoothLevel < 15) ? "#ff4444" : "#44aaff";
        ctx.lineWidth = radius * 0.12;
        ctx.lineCap = "round";
        ctx.stroke();

        // "E" and "F" labels
        var labelRadius = radius * 1.35;
        ctx.font = Math.round(radius * 0.28) + "px sans-serif";
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";

        // E at start (150 deg)
        ctx.fillStyle = "#ff4444";
        ctx.fillText("E",
                     cx + labelRadius * Math.cos(150 * toRad),
                     cy + labelRadius * Math.sin(150 * toRad));
        // F at end (30 deg)
        ctx.fillStyle = "#44aaff";
        ctx.fillText("F",
                     cx + labelRadius * Math.cos(30 * toRad),
                     cy + labelRadius * Math.sin(30 * toRad));

        // Fuel pump icon (simple text)
        ctx.font = Math.round(radius * 0.22) + "px sans-serif";
        ctx.fillStyle = "#888888";
        ctx.textAlign = "center";
        ctx.fillText("FUEL", cx, cy - radius * 0.15);
    }

    // Low fuel warning blink
    Rectangle {
        visible: root.level < 15
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.90
        width: parent.width * 0.5
        height: parent.height * 0.1
        radius: height / 2
        color: "transparent"
        border.color: "#ff4444"
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: "LOW FUEL"
            color: "#ff4444"
            font.pixelSize: parent.height * 0.7
            font.bold: true
        }

        SequentialAnimation on opacity {
            running: root.level < 15
            loops: Animation.Infinite
            NumberAnimation { to: 0.3; duration: 500 }
            NumberAnimation { to: 1.0; duration: 500 }
        }
    }
}
