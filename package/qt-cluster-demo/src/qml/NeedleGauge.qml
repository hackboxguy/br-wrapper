import QtQuick 2.12

Item {
    id: root

    // Data properties
    property real value: 0
    property real minValue: 0
    property real maxValue: 100
    property real redlineValue: -1  // -1 = no redline

    // Angle properties (degrees, 0 = top, clockwise positive)
    property real startAngle: -135
    property real endAngle: 135

    // Tick configuration
    property real majorTickInterval: 10
    property real minorTickInterval: 5
    property real labelDivisor: 1  // divide label values by this (e.g. 1000 for tach)
    property real labelScale: 1.0  // scale factor for label font size

    // Display
    property string label: ""
    property bool showDigitalValue: false
    property string digitalFormat: "%1"
    property color dialColor: "#0a0a0a"
    property color tickColor: "#ffffff"
    property color needleColor: "#ff2200"
    property color redlineColor: "#ff0000"
    property color labelColor: "#ffffff"

    // Computed needle rotation
    property real needleRotation: {
        var range = maxValue - minValue;
        if (range <= 0) return startAngle;
        var angleRange = endAngle - startAngle;
        var clamped = Math.max(minValue, Math.min(maxValue, value));
        return startAngle + ((clamped - minValue) / range) * angleRange;
    }

    // Smoothed needle angle
    property real smoothedRotation: needleRotation
    Behavior on smoothedRotation {
        SmoothedAnimation {
            velocity: 800  // degrees per second
        }
    }

    Canvas {
        id: dialCanvas
        anchors.fill: parent
        onPaint: drawDial()

        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    function drawDial() {
        var ctx = dialCanvas.getContext("2d");
        var w = dialCanvas.width;
        var h = dialCanvas.height;
        var cx = w / 2;
        var cy = h / 2;
        var radius = Math.min(cx, cy) * 0.9;

        ctx.clearRect(0, 0, w, h);

        // Dial background
        ctx.beginPath();
        ctx.arc(cx, cy, radius, 0, 2 * Math.PI);
        ctx.fillStyle = dialColor;
        ctx.fill();

        // Outer ring
        ctx.beginPath();
        ctx.arc(cx, cy, radius, 0, 2 * Math.PI);
        ctx.strokeStyle = "#888888";
        ctx.lineWidth = 3;
        ctx.stroke();

        var toRad = Math.PI / 180;
        var angleRange = endAngle - startAngle;
        var valueRange = maxValue - minValue;

        // Redline arc
        if (redlineValue >= 0 && redlineValue < maxValue) {
            var redlineStartAngle = startAngle + ((redlineValue - minValue) / valueRange) * angleRange;
            ctx.beginPath();
            ctx.arc(cx, cy, radius * 0.85,
                    (redlineStartAngle - 90) * toRad,
                    (endAngle - 90) * toRad);
            ctx.strokeStyle = redlineColor;
            ctx.lineWidth = radius * 0.08;
            ctx.stroke();
        }

        // Tick marks and labels
        var majorCount = Math.floor(valueRange / majorTickInterval);
        for (var i = 0; i <= majorCount; i++) {
            var val = minValue + i * majorTickInterval;
            var frac = (val - minValue) / valueRange;
            var angle = (startAngle + frac * angleRange - 90) * toRad;

            // Major tick
            var innerMajor = radius * 0.75;
            var outerMajor = radius * 0.85;
            ctx.beginPath();
            ctx.moveTo(cx + innerMajor * Math.cos(angle),
                       cy + innerMajor * Math.sin(angle));
            ctx.lineTo(cx + outerMajor * Math.cos(angle),
                       cy + outerMajor * Math.sin(angle));
            ctx.strokeStyle = (redlineValue >= 0 && val >= redlineValue) ? redlineColor : tickColor;
            ctx.lineWidth = 3.5;
            ctx.stroke();

            // Label
            var labelRadius = radius * 0.65;
            var labelVal = val / labelDivisor;
            var labelText = Number.isInteger(labelVal) ? labelVal.toString() : labelVal.toFixed(1);
            ctx.font = "bold " + Math.round(radius * 0.12 * labelScale) + "px sans-serif";
            ctx.fillStyle = (redlineValue >= 0 && val >= redlineValue) ? redlineColor : labelColor;
            ctx.textAlign = "center";
            ctx.textBaseline = "middle";
            ctx.fillText(labelText,
                         cx + labelRadius * Math.cos(angle),
                         cy + labelRadius * Math.sin(angle));
        }

        // Minor ticks
        if (minorTickInterval > 0 && minorTickInterval < majorTickInterval) {
            var minorCount = Math.floor(valueRange / minorTickInterval);
            for (var j = 0; j <= minorCount; j++) {
                var minorVal = minValue + j * minorTickInterval;
                // Skip positions where major ticks are
                var remainder = (minorVal - minValue) % majorTickInterval;
                if (Math.abs(remainder) < 0.01 || Math.abs(remainder - majorTickInterval) < 0.01)
                    continue;

                var minorFrac = (minorVal - minValue) / valueRange;
                var minorAngle = (startAngle + minorFrac * angleRange - 90) * toRad;
                var innerMinor = radius * 0.80;
                var outerMinor = radius * 0.85;

                ctx.beginPath();
                ctx.moveTo(cx + innerMinor * Math.cos(minorAngle),
                           cy + innerMinor * Math.sin(minorAngle));
                ctx.lineTo(cx + outerMinor * Math.cos(minorAngle),
                           cy + outerMinor * Math.sin(minorAngle));
                ctx.strokeStyle = (redlineValue >= 0 && minorVal >= redlineValue) ? redlineColor : "#999999";
                ctx.lineWidth = 1.5;
                ctx.stroke();
            }
        }

        // Unit label
        if (label.length > 0) {
            ctx.font = "bold " + Math.round(radius * 0.10) + "px sans-serif";
            ctx.fillStyle = "#bbbbbb";
            ctx.textAlign = "center";
            ctx.textBaseline = "middle";
            ctx.fillText(label, cx, cy + radius * 0.25);
        }

        // Center cap
        ctx.beginPath();
        ctx.arc(cx, cy, radius * 0.07, 0, 2 * Math.PI);
        ctx.fillStyle = "#999999";
        ctx.fill();
    }

    // Needle (separate item for smooth rotation)
    Rectangle {
        id: needle
        width: Math.min(parent.width, parent.height) * 0.9 * 0.03
        height: Math.min(parent.width, parent.height) * 0.9 * 0.42
        color: needleColor
        radius: width / 2
        antialiasing: true

        x: parent.width / 2 - width / 2
        y: parent.height / 2 - height

        transformOrigin: Item.Bottom
        rotation: root.smoothedRotation
    }

    // Digital readout
    Text {
        visible: showDigitalValue
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.68
        text: root.digitalFormat.arg(Math.round(root.value))
        color: labelColor
        font.pixelSize: Math.min(parent.width, parent.height) * 0.10
        font.bold: true
    }
}
