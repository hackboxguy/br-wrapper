import QtQuick 2.12

Item {
    id: root
    property int temperature: cluster.coolantTemp

    property real _smoothTemp: temperature
    Behavior on _smoothTemp {
        SmoothedAnimation { velocity: 30 }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: draw()

        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    onTemperatureChanged: canvas.requestPaint()
    on_SmoothTempChanged: canvas.requestPaint()

    function draw() {
        var ctx = canvas.getContext("2d");
        var w = canvas.width;
        var h = canvas.height;
        var cx = w / 2;
        var cy = h * 0.55;
        var radius = Math.min(w, h) * 0.40;
        var toRad = Math.PI / 180;

        ctx.clearRect(0, 0, w, h);

        var startDeg = 150;  // Cold position
        var sweepDeg = 120;
        var minTemp = 60;
        var maxTemp = 130;
        var dangerTemp = 110;

        // Background arc
        ctx.beginPath();
        ctx.arc(cx, cy, radius, startDeg * toRad, (startDeg - sweepDeg) * toRad, true);
        ctx.strokeStyle = "#333333";
        ctx.lineWidth = radius * 0.12;
        ctx.lineCap = "round";
        ctx.stroke();

        // Danger zone arc (110-130)
        var dangerFrac = (dangerTemp - minTemp) / (maxTemp - minTemp);
        var dangerStartDeg = startDeg - sweepDeg * dangerFrac;
        ctx.beginPath();
        ctx.arc(cx, cy, radius, dangerStartDeg * toRad, (startDeg - sweepDeg) * toRad, true);
        ctx.strokeStyle = "#ff444466";
        ctx.lineWidth = radius * 0.12;
        ctx.lineCap = "round";
        ctx.stroke();

        // Value arc
        var clamped = Math.max(minTemp, Math.min(maxTemp, _smoothTemp));
        var frac = (clamped - minTemp) / (maxTemp - minTemp);
        var endDeg = startDeg - sweepDeg * frac;

        var isHot = _smoothTemp >= dangerTemp;
        ctx.beginPath();
        ctx.arc(cx, cy, radius, startDeg * toRad, endDeg * toRad, true);
        ctx.strokeStyle = isHot ? "#ff4444" : "#44aaff";
        ctx.lineWidth = radius * 0.12;
        ctx.lineCap = "round";
        ctx.stroke();

        // "C" and "H" labels
        var labelRadius = radius * 1.35;
        ctx.font = Math.round(radius * 0.28) + "px sans-serif";
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";

        ctx.fillStyle = "#44aaff";
        ctx.fillText("C",
                     cx + labelRadius * Math.cos(startDeg * toRad),
                     cy + labelRadius * Math.sin(startDeg * toRad));
        ctx.fillStyle = "#ff4444";
        ctx.fillText("H",
                     cx + labelRadius * Math.cos((startDeg - sweepDeg) * toRad),
                     cy + labelRadius * Math.sin((startDeg - sweepDeg) * toRad));

        // Temperature label
        ctx.font = Math.round(radius * 0.22) + "px sans-serif";
        ctx.fillStyle = "#888888";
        ctx.textAlign = "center";
        ctx.fillText("TEMP", cx, cy - radius * 0.15);
    }

    // Overheat warning
    Rectangle {
        visible: root.temperature > 110
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
            text: "OVERHEAT"
            color: "#ff4444"
            font.pixelSize: parent.height * 0.7
            font.bold: true
        }

        SequentialAnimation on opacity {
            running: root.temperature > 110
            loops: Animation.Infinite
            NumberAnimation { to: 0.3; duration: 400 }
            NumberAnimation { to: 1.0; duration: 400 }
        }
    }
}
