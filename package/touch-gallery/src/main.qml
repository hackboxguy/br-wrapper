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
    
    FolderListModel {
        id: folderModel
        folder: picturesPath || "file:///Pictures"
        nameFilters: ["*.jpg", "*.jpeg", "*.png", "*.bmp", "*.gif", "*.tiff", "*.JPG", "*.JPEG", "*.PNG"]
        showDirs: false
        sortField: FolderListModel.Name
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
                    
                    onClicked: {
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
                    
                    onReleased: {
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
                visible: mainImage.scale !== 1.0
                
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
                    text: mainImage.scale !== 1.0 ? "Tap center to fit image" : "Swipe or tap edges to navigate"
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
            onTriggered: uiVisible = false
        }
    }
    
    function nextImage() {
        if (folderModel.count > 0) {
            currentImageIndex = (currentImageIndex + 1) % folderModel.count
            showUITemporarily()
        }
    }
    
    function previousImage() {
        if (folderModel.count > 0) {
            currentImageIndex = currentImageIndex > 0 ? currentImageIndex - 1 : folderModel.count - 1
            showUITemporarily()
        }
    }
    
    function showUITemporarily() {
        uiVisible = true
        uiHideTimer.restart()
    }
    
    Component.onCompleted: {
        showUITemporarily()
    }
}
