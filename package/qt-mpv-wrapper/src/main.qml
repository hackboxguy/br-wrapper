import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt.labs.folderlistmodel 2.15

ApplicationWindow {
    id: window
    visible: true
    width: 2560
    height: 1440
    title: "Video Player"

    visibility: ApplicationWindow.FullScreen

    property bool videoPlaying: videoPlayer.isPlaying
    property string currentVideoPath: ""
    property bool loopEnabled: false

    // Video file model
    FolderListModel {
        id: videoModel
        folder: videoBasePath
        nameFilters: ["*.mp4", "*.mkv", "*.avi", "*.webm", "*.mov", "*.MP4", "*.MKV"]
        showDirs: false
        sortField: FolderListModel.Name
    }

    // Connect to VideoPlayer signals
    Connections {
        target: videoPlayer

        function onReadyToHideWindow() {
            console.log("Hiding Qt window - mpv will start in 500ms")
            window.hide()
        }

        function onPlaybackFinished() {
            console.log("Playback finished - showing Qt window")
            window.show()
            currentVideoPath = ""
        }

        function onPlaybackError(error) {
            console.log("Playback error:", error)
            window.show()
            currentVideoPath = ""
        }
    }

    // Video File Browser
    Rectangle {
        id: fileBrowser
        anchors.fill: parent
        color: "#1a1a1a"
        visible: !videoPlaying

        // Header
        Rectangle {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 100
            color: "#2a2a2a"

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 40
                anchors.verticalCenter: parent.verticalCenter
                text: "Video Player"
                color: "white"
                font.pixelSize: 36
                font.bold: true
            }

            // Video count
            Rectangle {
                anchors.right: exitButton.left
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                width: 120
                height: 60
                color: "#404040"
                radius: 8

                Text {
                    anchors.centerIn: parent
                    text: videoModel.count + " videos"
                    color: "white"
                    font.pixelSize: 20
                }
            }

            // Exit button (returns to launcher)
            Rectangle {
                id: exitButton
                anchors.right: parent.right
                anchors.rightMargin: 40
                anchors.verticalCenter: parent.verticalCenter
                width: 120
                height: 60
                color: "#C0800000"
                radius: 8
                border.color: "white"
                border.width: 2

                Text {
                    anchors.centerIn: parent
                    text: "EXIT"
                    color: "white"
                    font.pixelSize: 24
                    font.bold: true
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: Qt.quit() // Exit to launcher
                }
            }
        }

        // Video list
        ListView {
            id: videoList
            anchors.top: header.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 40
            spacing: 15
            clip: true

            model: videoModel

            delegate: Rectangle {
                width: videoList.width
                height: 100
                color: mouseArea.pressed ? "#505050" : "#303030"
                radius: 10
                border.color: "#505050"
                border.width: 2

                Row {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 20

                    // Video icon
                    Rectangle {
                        width: 60
                        height: 60
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#1a5080"
                        radius: 8

                        Text {
                            anchors.centerIn: parent
                            text: "▶"
                            color: "white"
                            font.pixelSize: 32
                        }
                    }

                    // Video info
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 5

                        Text {
                            text: fileName
                            color: "white"
                            font.pixelSize: 24
                            font.bold: true
                        }

                        Text {
                            text: {
                                var size = fileSize / (1024 * 1024)
                                return size.toFixed(1) + " MB"
                            }
                            color: "#aaaaaa"
                            font.pixelSize: 18
                        }
                    }
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    onClicked: {
                        console.log("Playing:", filePath)
                        currentVideoPath = filePath
                        videoPlayer.play(filePath, loopEnabled)
                    }
                }
            }

            // No videos message
            Text {
                anchors.centerIn: parent
                visible: videoModel.count === 0
                text: "No videos found in\n" + videoBasePath
                color: "#808080"
                font.pixelSize: 28
                horizontalAlignment: Text.AlignHCenter
            }
        }

        // Scroll indicator
        Rectangle {
            anchors.right: parent.right
            anchors.top: videoList.top
            anchors.bottom: videoList.bottom
            anchors.rightMargin: 10
            width: 8
            color: "#404040"
            radius: 4
            visible: videoList.contentHeight > videoList.height

            Rectangle {
                y: videoList.visibleArea.yPosition * parent.height
                width: parent.width
                height: videoList.visibleArea.heightRatio * parent.height
                color: "#808080"
                radius: 4
            }
        }
    }

    // Video playback overlay (shown when video is playing)
    Rectangle {
        id: playbackOverlay
        anchors.fill: parent
        color: "transparent"
        visible: videoPlaying

        property bool uiVisible: false

        // Touch area to show controls
        MouseArea {
            anchors.fill: parent
            onClicked: {
                playbackOverlay.uiVisible = !playbackOverlay.uiVisible
                if (playbackOverlay.uiVisible) {
                    uiHideTimer.restart()
                }
            }
        }

        // Control overlay
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            visible: playbackOverlay.uiVisible

            // Video filename (top-center)
            Rectangle {
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 20
                width: videoNameText.width + 40
                height: 60
                color: "#C0000000"
                radius: 8
                border.color: "white"
                border.width: 1

                Text {
                    id: videoNameText
                    anchors.centerIn: parent
                    text: {
                        var parts = currentVideoPath.split("/")
                        return parts[parts.length - 1]
                    }
                    color: "white"
                    font.pixelSize: 24
                }
            }

            // Loop toggle button (top-left)
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 20
                width: 120
                height: 60
                color: loopEnabled ? "#C0008000" : "#C0404040"
                radius: 8
                border.color: "white"
                border.width: 2

                Text {
                    anchors.centerIn: parent
                    text: loopEnabled ? "LOOP ON" : "LOOP OFF"
                    color: "white"
                    font.pixelSize: 20
                    font.bold: true
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        loopEnabled = !loopEnabled
                        // Note: Need to restart video for loop to take effect
                        uiHideTimer.restart()
                    }
                }
            }

            // Stop button (top-right) - returns to file browser
            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 20
                width: 120
                height: 60
                color: "#C0800000"
                radius: 8
                border.color: "white"
                border.width: 2

                Text {
                    anchors.centerIn: parent
                    text: "◀ STOP"
                    color: "white"
                    font.pixelSize: 24
                    font.bold: true
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        videoPlayer.stop()
                        playbackOverlay.uiVisible = false
                    }
                }
            }

            // Instructions (bottom-center)
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: 20
                width: instructionText.width + 40
                height: 50
                color: "#C0000000"
                radius: 8
                border.color: "white"
                border.width: 1

                Text {
                    id: instructionText
                    anchors.centerIn: parent
                    text: "Tap screen to hide controls"
                    color: "white"
                    font.pixelSize: 20
                }
            }
        }

        // Auto-hide timer for controls
        Timer {
            id: uiHideTimer
            interval: 5000
            running: false
            onTriggered: playbackOverlay.uiVisible = false
        }

        // Show controls initially when video starts
        Component.onCompleted: {
            if (videoPlaying) {
                playbackOverlay.uiVisible = true
                uiHideTimer.start()
            }
        }
    }

}
