import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt.labs.folderlistmodel 2.15

ApplicationWindow {
    id: window
    visible: true
    width: 2560
    height: 1440
    title: "Touch Gallery"

    visibility: ApplicationWindow.FullScreen

    property int currentImageIndex: 0
    property bool uiVisible: true
    property bool isSlideshow: slideshowMode || false
    property bool slideshowPaused: false
    property bool usbCopyMode: usbCopyEnabled || false
    property string usbCopyStatusText: ""

    FolderListModel {
        id: folderModel
        folder: (typeof galleryController !== 'undefined' && galleryController.picturesDirectory)
                ? "file://" + galleryController.picturesDirectory
                : (picturesPath || "file:///Pictures")
        nameFilters: ["*.jpg", "*.jpeg", "*.png", "*.bmp", "*.gif", "*.tiff", "*.JPG", "*.JPEG", "*.PNG"]
        showDirs: false
        sortField: FolderListModel.Name

        onCountChanged: {
            // Notify controller of image count changes
            if (typeof galleryController !== 'undefined') {
                galleryController.setImageCount(count)
            }
        }
    }

    // Connect to GalleryController signals
    Connections {
        target: typeof galleryController !== 'undefined' ? galleryController : null

        function onNavigateNext() {
            nextImage()
        }

        function onNavigatePrevious() {
            previousImage()
        }

        function onCurrentIndexChanged() {
            // Sync QML current index with controller
            currentImageIndex = galleryController.currentIndex
        }

        function onDisplayImageRequested(filePath) {
            // Find and display the requested image
            for (var i = 0; i < folderModel.count; i++) {
                if (folderModel.get(i, "filePath") === filePath) {
                    currentImageIndex = i
                    break
                }
            }
        }

        function onUsbCopyStatusChanged() {
            usbCopyStatusText = galleryController.usbCopyStatus
            if (usbCopyStatusText !== "") {
                uiVisible = true
                usbCopyStatusTimer.restart()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"

        Image {
            id: mainImage
            anchors.centerIn: parent
            width: parent.width
            height: parent.height
            fillMode: Image.PreserveAspectFit
            smooth: true
            source: folderModel.count > 0 ? folderModel.get(currentImageIndex, "fileURL") : ""

            onSourceChanged: {
                resetImageTransform()
            }

            function resetImageTransform() {
                scale = 1.0
                x = 0
                y = 0
            }

            PinchArea {
                id: pinchArea
                anchors.fill: parent
                pinch.target: mainImage
                pinch.minimumScale: 0.5
                pinch.maximumScale: 4.0
                enabled: !isSlideshow // Disable pinch in slideshow mode

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent

                    property real lastX: 0
                    property real lastY: 0
                    property real startX: 0
                    property real startY: 0
                    property bool isPanning: false
                    property bool hasMoved: false

                    onPressed: {
                        lastX = mouse.x
                        lastY = mouse.y
                        startX = mouse.x
                        startY = mouse.y
                        isPanning = false
                        hasMoved = false
                    }

                    onPositionChanged: {
                        if (!isSlideshow) {
                            var deltaX = mouse.x - lastX
                            var deltaY = mouse.y - lastY

                            if (Math.abs(deltaX) > 3 || Math.abs(deltaY) > 3) {
                                hasMoved = true

                                // Always allow panning when not at normal scale
                                if (mainImage.scale !== 1.0) {
                                    isPanning = true
                                    mainImage.x += deltaX
                                    mainImage.y += deltaY

                                    // Optional: Add bounds checking
                                    var imageWidth = mainImage.width * mainImage.scale
                                    var imageHeight = mainImage.height * mainImage.scale
                                    var maxX = Math.max(0, (imageWidth - parent.width) / 2)
                                    var maxY = Math.max(0, (imageHeight - parent.height) / 2)

                                    if (maxX > 0) {
                                        mainImage.x = Math.max(-maxX, Math.min(maxX, mainImage.x))
                                    }
                                    if (maxY > 0) {
                                        mainImage.y = Math.max(-maxY, Math.min(maxY, mainImage.y))
                                    }
                                }
                            }

                            lastX = mouse.x
                            lastY = mouse.y
                        }
                    }

                    onClicked: {
                        if (isSlideshow) {
                            // In slideshow mode, toggle pause/play
                            slideshowPaused = !slideshowPaused
                            if (slideshowPaused) {
                                slideshowTimer.stop()
                            } else {
                                slideshowTimer.start()
                            }
                            showUITemporarily()
                        } else {
                            // Normal gallery mode behavior
                            if (!isPanning && !hasMoved) {
                                if (mainImage.scale === 1.0) {
                                    // Navigation only works at normal scale
                                    if (mouse.x < parent.width / 4) {
                                        previousImage()
                                    } else if (mouse.x > parent.width * 3 / 4) {
                                        nextImage()
                                    } else {
                                        uiVisible = !uiVisible
                                        if (uiVisible) {
                                            uiHideTimer.restart()
                                        }
                                    }
                                } else {
                                    // When zoomed, only toggle UI
                                    uiVisible = !uiVisible
                                    if (uiVisible) {
                                        uiHideTimer.restart()
                                    }
                                }
                            }
                        }
                    }

                    onReleased: {
                        if (!isSlideshow) {
                            // Swipe navigation only at normal scale
                            if (mainImage.scale === 1.0 && hasMoved && !isPanning) {
                                var deltaX = mouse.x - startX
                                if (Math.abs(deltaX) > 100) {
                                    if (deltaX > 0) {
                                        previousImage()
                                    } else {
                                        nextImage()
                                    }
                                }
                            }
                        }

                        // Reset states
                        isPanning = false
                        hasMoved = false
                    }
                }
            }
        }

        Rectangle {
            id: uiOverlay
            anchors.fill: parent
            color: "transparent"
            visible: uiVisible

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 20
                width: 140
                height: 60
                color: "#C0000000"
                radius: 8
                border.color: "white"
                border.width: 1
                visible: folderModel.count > 0

                Text {
                    anchors.centerIn: parent
                    text: (currentImageIndex + 1) + "/" + folderModel.count
                    color: "white"
                    font.pixelSize: 28
                    font.family: "DejaVu Sans"
                    font.bold: true
                    renderType: Text.NativeRendering
                }
            }

            // Slideshow indicator
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.leftMargin: 180
                anchors.topMargin: 20
                width: 120
                height: 60
                color: slideshowPaused ? "#C0800000" : "#C0008000"
                radius: 8
                border.color: "white"
                border.width: 1
                visible: isSlideshow

                Text {
                    anchors.centerIn: parent
                    text: slideshowPaused ? "PAUSED" : "SLIDE"
                    color: "white"
                    font.pixelSize: 20
                    font.family: "DejaVu Sans"
                    font.bold: true
                    renderType: Text.NativeRendering
                }
            }

            Rectangle {
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.margins: 20
                width: 100
                height: 60
                color: "#C0004080"
                radius: 8
                border.color: "white"
                border.width: 1
                visible: mainImage.scale !== 1.0 && !isSlideshow

                Text {
                    anchors.centerIn: parent
                    text: "FIT"
                    color: "white"
                    font.pixelSize: 24
                    font.family: "DejaVu Sans"
                    font.bold: true
                    renderType: Text.NativeRendering
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        mainImage.resetImageTransform()
                    }
                }
            }

            Rectangle {
                id: exitButton
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 20
                width: 100
                height: 60
                color: "#C0800000"
                radius: 8
                border.color: "white"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "EXIT"
                    color: "white"
                    font.pixelSize: 24
                    font.family: "DejaVu Sans"
                    font.bold: true
                    renderType: Text.NativeRendering
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: Qt.quit()
                }
            }

            Rectangle {
                id: usbCopyButton
                anchors.top: parent.top
                anchors.right: exitButton.left
                anchors.topMargin: 20
                anchors.rightMargin: 12
                width: 150
                height: 60
                color: galleryController.usbCopyBusy ? "#C0444444" : "#C0004080"
                radius: 8
                border.color: galleryController.usbCopyBusy ? "#777777" : "white"
                border.width: 1
                opacity: galleryController.usbCopyBusy ? 0.65 : 1.0
                visible: usbCopyMode && folderModel.count > 0 && !isSlideshow

                Text {
                    anchors.centerIn: parent
                    text: "COPY USB"
                    color: "white"
                    font.pixelSize: 22
                    font.family: "DejaVu Sans"
                    font.bold: true
                    renderType: Text.NativeRendering
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: !galleryController.usbCopyBusy
                    onClicked: {
                        if (typeof galleryController !== 'undefined' && folderModel.count > 0) {
                            galleryController.setCurrentIndex(currentImageIndex)
                            galleryController.copyImageToUsb(folderModel.get(currentImageIndex, "filePath"))
                        }
                        showUITemporarily()
                    }
                }
            }

            // LD / PC overlay toggle buttons (top-right, below the exit button).
            // Shown only when the FPGA responds to register 0x2C / 0x2D with a valid
            // value (0x00 or 0x01). Styled as toggles: green when enabled, gray when off.
            Row {
                id: fpgaToggleRow
                anchors.top: exitButton.bottom
                anchors.right: parent.right
                anchors.topMargin: 12
                anchors.rightMargin: 20
                spacing: 12
                visible: fpga.localDimmingSupported || fpga.pixelCompSupported

                // Local Dimming toggle
                Rectangle {
                    width: 70
                    height: 60
                    radius: 8
                    visible: fpga.localDimmingSupported
                    color: fpga.localDimmingEnabled ? "#C016803A" : "#C0444444"
                    border.color: fpga.localDimmingEnabled ? "#33CC66" : "#777777"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "LD"
                        color: "white"
                        font.pixelSize: 24
                        font.family: "DejaVu Sans"
                        font.bold: true
                        renderType: Text.NativeRendering
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            fpga.setLocalDimming(!fpga.localDimmingEnabled)
                            showUITemporarily()
                        }
                    }
                }

                // Pixel Compensation toggle — only meaningful while local dimming is on
                Rectangle {
                    width: 70
                    height: 60
                    radius: 8
                    visible: fpga.pixelCompSupported
                    property bool pcInteractive: fpga.localDimmingEnabled
                    color: (fpga.pixelCompEnabled && pcInteractive) ? "#C016803A" : "#C0444444"
                    border.color: (fpga.pixelCompEnabled && pcInteractive) ? "#33CC66" : "#777777"
                    border.width: 1
                    opacity: pcInteractive ? 1.0 : 0.4

                    Text {
                        anchors.centerIn: parent
                        text: "PC"
                        color: "white"
                        font.pixelSize: 24
                        font.family: "DejaVu Sans"
                        font.bold: true
                        renderType: Text.NativeRendering
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: parent.pcInteractive
                        onClicked: {
                            fpga.setPixelCompensation(!fpga.pixelCompEnabled)
                            showUITemporarily()
                        }
                    }
                }
            }

            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 20
                width: Math.min(Math.max(usbCopyStatusTextLabel.implicitWidth + 28, 260), parent.width - 40)
                height: 58
                color: "#C0000000"
                radius: 8
                border.color: "white"
                border.width: 1
                visible: usbCopyMode && usbCopyStatusText !== ""

                Text {
                    id: usbCopyStatusTextLabel
                    anchors.centerIn: parent
                    text: usbCopyStatusText
                    color: "white"
                    font.pixelSize: 20
                    font.family: "DejaVu Sans"
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    width: parent.width - 24
                    renderType: Text.NativeRendering
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.margins: 20
                width: Math.max(instructionText.width + 20, 200)
                height: 50
                color: "#C0000000"
                radius: 8
                border.color: "white"
                border.width: 1
                visible: folderModel.count > 1

                Text {
                    id: instructionText
                    anchors.centerIn: parent
                    text: isSlideshow ? 
                          (slideshowPaused ? "Tap to resume slideshow" : "Tap to pause slideshow") :
                          (mainImage.scale !== 1.0 ? "Tap center to fit image" : "Swipe or tap edges to navigate")
                    color: "white"
                    font.pixelSize: 20
                    font.family: "DejaVu Sans"
                    renderType: Text.NativeRendering
                }
            }
        }

        Rectangle {
            anchors.centerIn: parent
            width: 400
            height: 120
            color: "#C0000000"
            radius: 10
            border.color: "white"
            border.width: 2
            visible: folderModel.count === 0

            Text {
                anchors.centerIn: parent
                text: "No images found in " + (picturesPath || "/Pictures")
                color: "white"
                font.pixelSize: 24
                font.family: "DejaVu Sans"
                horizontalAlignment: Text.AlignHCenter
                renderType: Text.NativeRendering
            }
        }

        Timer {
            id: uiHideTimer
            interval: 4000
            running: false
            onTriggered: {
                if (galleryController.usbCopyBusy) {
                    restart()
                } else {
                    uiVisible = false
                }
            }
        }

        Timer {
            id: usbCopyStatusTimer
            interval: 5000
            running: false
            onTriggered: {
                if (!galleryController.usbCopyBusy) {
                    usbCopyStatusText = ""
                }
            }
        }

        Timer {
            id: slideshowTimer
            interval: slideshowInterval || 5000 // Use passed interval or default 5 seconds
            running: isSlideshow && !slideshowPaused && folderModel.count > 1
            repeat: true
            onTriggered: nextImage()
        }
    }

    function nextImage() {
        if (folderModel.count > 0) {
            currentImageIndex = (currentImageIndex + 1) % folderModel.count
            if (!isSlideshow) {
                showUITemporarily()
            }
            // Update controller's current index
            if (typeof galleryController !== 'undefined') {
                galleryController.setCurrentIndex(currentImageIndex)
            }
        }
    }

    function previousImage() {
        if (folderModel.count > 0) {
            currentImageIndex = currentImageIndex > 0 ? currentImageIndex - 1 : folderModel.count - 1
            if (!isSlideshow) {
                showUITemporarily()
            }
            // Update controller's current index
            if (typeof galleryController !== 'undefined') {
                galleryController.setCurrentIndex(currentImageIndex)
            }
        }
    }

    function showUITemporarily() {
        uiVisible = true
        if (!isSlideshow) {
            uiHideTimer.restart()
        } else {
            // In slideshow mode, hide UI after 2 seconds instead of 4
            uiHideTimer.interval = 2000
            uiHideTimer.restart()
            uiHideTimer.interval = 4000 // Reset for normal mode
        }
    }

    Component.onCompleted: {
        showUITemporarily()
        if (isSlideshow && folderModel.count > 1) {
            console.log("Starting slideshow with", slideshowInterval/1000, "second interval")
        }
        // Initialize controller with current state
        if (typeof galleryController !== 'undefined') {
            galleryController.setImageCount(folderModel.count)
            galleryController.setCurrentIndex(currentImageIndex)
        }
    }

    // Handle keyboard shortcuts for slideshow
    Keys.onSpacePressed: {
        if (isSlideshow) {
            slideshowPaused = !slideshowPaused
            if (slideshowPaused) {
                slideshowTimer.stop()
            } else {
                slideshowTimer.start()
            }
            showUITemporarily()
        }
    }

    Keys.onEscapePressed: Qt.quit()
}
