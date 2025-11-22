import QtQuick 2.12

PatternBase {
    patternName: "whitebox"
    backgroundColor: "black"

    // Get whitebox parameters
    property var params: patternController ? patternController.patternParams : ({})
    property string sizeMode: params.whiteboxMode !== undefined ? params.whiteboxMode : "percent"
    property int boxSizePercent: params.whiteboxSize !== undefined ? params.whiteboxSize : 10
    property int boxSizePixels: params.whiteboxPixels !== undefined ? params.whiteboxPixels : 100
    property real boxSizeMM: params.whiteboxMM !== undefined ? params.whiteboxMM : 50.0
    property real diagonalInch: params.whiteboxDiagonalInch !== undefined ? params.whiteboxDiagonalInch : 0.0

    // Screen dimensions
    property int screenMin: Math.min(parent.width, parent.height)
    property int screenWidth: parent.width
    property int screenHeight: parent.height

    // Calculate box size based on mode
    property int boxSize: {
        if (sizeMode === "pixels") {
            // Pixels mode: use absolute pixel value
            return boxSizePixels;
        } else if (sizeMode === "mm" && diagonalInch > 0) {
            // MM mode: calculate pixels from physical size and display diagonal
            // 1. Calculate display aspect ratio from resolution
            var aspectRatio = screenWidth / screenHeight;

            // 2. Calculate physical dimensions from diagonal (Pythagorean theorem)
            // diagonal² = width² + height²
            // width = aspect * height
            // diagonal² = (aspect * height)² + height²
            // height = diagonal / sqrt(aspect² + 1)
            var heightInch = diagonalInch / Math.sqrt(aspectRatio * aspectRatio + 1);
            var widthInch = heightInch * aspectRatio;

            // 3. Convert inches to mm (1 inch = 25.4mm)
            var heightMM = heightInch * 25.4;
            var widthMM = widthInch * 25.4;

            // 4. Calculate pixels per mm
            var pixelsPerMM_H = screenHeight / heightMM;
            var pixelsPerMM_W = screenWidth / widthMM;
            var pixelsPerMM = Math.min(pixelsPerMM_H, pixelsPerMM_W); // Use smaller for square box

            // 5. Convert requested mm to pixels
            return Math.floor(boxSizeMM * pixelsPerMM);
        } else {
            // Percent mode (default): percentage of smaller screen dimension
            return Math.floor(screenMin * boxSizePercent / 100.0);
        }
    }

    // White box centered on screen
    Rectangle {
        id: whiteBox
        width: boxSize
        height: boxSize
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        color: "white"
    }

    // Optional: Dim crosshair for precise positioning (can be commented out)
    // Show crosshair when box is small (< 200 pixels) regardless of sizing mode
    property bool showCrosshair: boxSize < 200

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
}
