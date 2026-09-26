import QtQuick 2.12
import QtQuick.Window 2.12

// System Manager, firmware section. Same visual language as qt-demo-launcher's "tiles" theme:
// navy grid backdrop, header with SMPTE bar strip, cards with an accent stripe,
// icon badge and status pill. Colors are RGB565 steps like the launcher's, so
// the 16bpp framebuffer shows them without dithering.
Window {
    id: win
    visible: true
    visibility: Window.FullScreen
    color: t.bg
    title: "System Manager"

    QtObject {
        id: t
        readonly property color bg: "#080C18"
        readonly property color grid: "#101828"
        readonly property color card: "#182030"
        readonly property color cardPressed: "#202C40"
        readonly property color border: "#283450"
        readonly property color text: "#F0F4F8"
        readonly property color sub: "#8894A8"
        readonly property color ok: "#34D399"
        readonly property color warn: "#FBBF24"
        readonly property color bad: "#F87171"
        readonly property color info: "#38BDF8"
        readonly property color accent: "#60A5FA"
        readonly property string font: uiFont
    }

    // Designed at 1920x720; 1080-line panels keep the same sizes
    readonly property real s: Math.min(width / 1920, height / 720)
    readonly property bool busy: updater.state === "updating"

    function tone(name) {
        switch (name) {
        case "ok": return t.ok
        case "warn": return t.warn
        case "bad": return t.bad
        case "info": return t.info
        default: return t.sub
        }
    }
    function outcomeTone(kind) {
        switch (kind) {
        case "success": return t.ok
        case "warning": return t.warn
        case "error": return t.bad
        default: return t.info
        }
    }
    function withAlpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    function mmss(sec) {
        var m = Math.floor(sec / 60), r = sec % 60
        return m + ":" + (r < 10 ? "0" : "") + r
    }

    Item {
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: if (!win.busy) updater.quitApp()
    }

    // ---- backdrop grid -----------------------------------------------------
    Repeater {
        model: Math.ceil(win.width / 48)
        Rectangle { x: index * 48 + (win.width % 48) / 2; width: 1; height: win.height; color: t.grid }
    }
    Repeater {
        model: Math.ceil(win.height / 48)
        Rectangle { y: index * 48 + (win.height % 48) / 2; height: 1; width: win.width; color: t.grid }
    }

    // ---- reusable pieces ---------------------------------------------------
    component Pill: Rectangle {
        property string label
        property color tint: t.sub
        height: 40 * s
        width: pillText.implicitWidth + 36 * s
        radius: height / 2
        color: withAlpha(tint, 0.16)
        border.color: withAlpha(tint, 0.45)
        Text {
            id: pillText
            anchors.centerIn: parent
            text: parent.label
            color: parent.tint
            font.family: t.font; font.pixelSize: 18 * s; font.weight: Font.DemiBold
        }
    }

    component Spinner: Item {
        width: 64 * s; height: width
        Repeater {
            model: 8
            Rectangle {
                width: parent.width * 0.16; height: width; radius: width / 2
                color: t.accent
                opacity: 0.25 + 0.75 * (index / 7)
                x: parent.width / 2 - width / 2 + Math.cos(index / 8 * 2 * Math.PI) * parent.width * 0.38
                y: parent.height / 2 - height / 2 + Math.sin(index / 8 * 2 * Math.PI) * parent.height * 0.38
            }
        }
        RotationAnimation on rotation { from: 0; to: 360; duration: 1100; loops: Animation.Infinite; running: parent.visible }
    }

    component ResultIcon: Rectangle {
        property color tint: t.ok
        property string glyph: "check"
        width: 76 * s; height: width; radius: width / 2
        color: withAlpha(tint, 0.18)
        border.color: withAlpha(tint, 0.6)
        Image {
            anchors.centerIn: parent
            width: parent.width * 0.55; height: width
            sourceSize: Qt.size(width, height)
            source: "qrc:/icons/" + parent.glyph + ".svg"
        }
    }

    component ActionButton: Rectangle {
        property string label
        property bool primary: false
        signal clicked()
        height: 84 * s
        radius: 20 * s
        color: primary ? (btnArea.pressed ? Qt.darker(t.accent, 1.2) : t.accent)
                       : (btnArea.pressed ? t.cardPressed : "transparent")
        border.color: primary ? t.accent : t.border
        border.width: 2
        Text {
            anchors.centerIn: parent
            text: parent.label
            color: parent.primary ? "#081018" : t.text
            font.family: t.font; font.pixelSize: 24 * s; font.weight: Font.DemiBold
        }
        MouseArea { id: btnArea; anchors.fill: parent; onClicked: parent.clicked() }
    }

    component Bullet: Row {
        property string label
        spacing: 14 * s
        width: parent ? parent.width : 0
        Rectangle { width: 8 * s; height: width; radius: width / 2; color: t.accent; anchors.top: parent.top; anchors.topMargin: 11 * s }
        Text {
            width: parent.width - 22 * s
            text: parent.label
            color: t.sub
            wrapMode: Text.WordWrap
            font.family: t.font; font.pixelSize: 20 * s
        }
    }

    component ComponentCard: Rectangle {
        property var info
        width: parent ? parent.width : 0
        height: 150 * s
        radius: 18 * s
        color: t.card
        border.color: t.border
        readonly property color toneColor: tone(info.tone)

        Rectangle {   // accent stripe
            x: 0; y: 0; width: 6 * s; height: parent.height; radius: 3 * s
            color: parent.toneColor
        }
        Rectangle {   // icon badge
            id: badge
            x: 36 * s; anchors.verticalCenter: parent.verticalCenter
            width: 84 * s; height: width; radius: 22 * s
            color: withAlpha(parent.toneColor, 0.16)
            border.color: withAlpha(parent.toneColor, 0.45)
            Image {
                anchors.centerIn: parent
                width: parent.width * 0.6; height: width
                sourceSize: Qt.size(width, height)
                source: "qrc:/icons/" + (info.icon || "board") + ".svg"
            }
        }
        Column {
            anchors.left: badge.right; anchors.leftMargin: 28 * s
            anchors.right: statusPill.left; anchors.rightMargin: 20 * s
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6 * s
            Text {
                width: parent.width; elide: Text.ElideRight
                text: info.name
                color: t.text
                font.family: t.font; font.pixelSize: 28 * s; font.weight: Font.DemiBold
            }
            Text {
                width: parent.width; elide: Text.ElideRight
                visible: info.installed !== "\u2014" || info.shipped !== "\u2014"
                text: "Installed " + info.installed + "   ·   Shipped " + info.shipped
                      + (info.image ? "   ·   " + info.image : "")
                color: t.sub
                font.family: t.font; font.pixelSize: 18 * s
            }
            Text {
                width: parent.width; elide: Text.ElideRight
                visible: text !== ""
                text: info.note || info.role
                color: info.note ? parent.parent.toneColor : t.sub
                font.family: t.font; font.pixelSize: 18 * s
            }
        }
        Pill {
            id: statusPill
            anchors.right: parent.right; anchors.rightMargin: 28 * s
            anchors.verticalCenter: parent.verticalCenter
            label: info.statusText
            tint: parent.toneColor
        }
    }

    // ---- page ----------------------------------------------------------------
    Item {
        id: page
        anchors.fill: parent
        anchors.leftMargin: 40 * s; anchors.rightMargin: 40 * s
        anchors.topMargin: 22 * s; anchors.bottomMargin: 30 * s

        // Header: back, title, summary
        Item {
            id: header
            width: parent.width
            height: 96 * s

            Rectangle {
                id: backButton
                width: 76 * s; height: width; radius: width / 2
                anchors.verticalCenter: parent.verticalCenter
                color: backArea.pressed ? t.cardPressed : t.card
                border.color: t.border
                opacity: win.busy ? 0.3 : 1.0
                Canvas {
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        ctx.strokeStyle = t.text
                        ctx.lineWidth = Math.max(2, width * 0.06)
                        ctx.lineCap = "round"; ctx.lineJoin = "round"
                        ctx.beginPath()
                        ctx.moveTo(width * 0.56, height * 0.32)
                        ctx.lineTo(width * 0.40, height * 0.5)
                        ctx.lineTo(width * 0.56, height * 0.68)
                        ctx.stroke()
                    }
                }
                MouseArea { id: backArea; anchors.fill: parent; enabled: !win.busy; onClicked: updater.quitApp() }
            }
            Column {
                anchors.left: backButton.right; anchors.leftMargin: 28 * s
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4 * s
                Text {
                    text: "System Manager"
                    color: t.text
                    font.family: t.font; font.pixelSize: 36 * s; font.weight: Font.Bold
                }
                Text {
                    text: "Home  ›  System Manager  ›  Firmware"
                    color: t.sub
                    font.family: t.font; font.pixelSize: 19 * s
                }
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14 * s
                Pill { visible: updater.dryRun; label: "DRY RUN"; tint: t.info }
                Pill {
                    label: updater.summary
                    tint: updater.state === "checking" ? t.sub
                          : updater.state === "updating" ? t.info
                          : updater.state === "done" ? (updater.powerCycleRequired ? t.warn
                                                        : updater.outcomeKind === "info" ? t.info : t.bad)
                          : updater.checkError !== "" ? t.bad
                          : updater.updatesAvailable > 0 ? t.warn : t.ok
                }
            }
        }

        // SMPTE 75% bars, as on the home screen
        Row {
            id: strip
            anchors.top: header.bottom; anchors.topMargin: 12 * s
            width: parent.width
            Repeater {
                model: ["#C0C0C0", "#C0C000", "#00C0C0", "#00C000", "#C000C0", "#C00000", "#0000C0"]
                Rectangle { width: strip.width / 7; height: 6 * s; color: modelData }
            }
        }

        // ---- left: components --------------------------------------------
        Column {
            id: components
            anchors.top: strip.bottom; anchors.topMargin: 26 * s
            anchors.left: parent.left
            width: parent.width * 0.58
            spacing: 18 * s

            Text {
                text: "INSTALLED FIRMWARE"
                color: t.sub
                font.family: t.font; font.pixelSize: 16 * s; font.weight: Font.DemiBold
                font.letterSpacing: 2 * s
            }
            Repeater {
                model: updater.components
                ComponentCard { info: modelData }
            }
            Rectangle {   // placeholder while the first check runs
                visible: updater.components.length === 0 && updater.state === "checking"
                width: parent.width; height: 150 * s; radius: 18 * s
                color: t.card; border.color: t.border
                Row {
                    anchors.centerIn: parent
                    spacing: 20 * s
                    Spinner { width: 44 * s }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Reading each board…"
                        color: t.sub
                        font.family: t.font; font.pixelSize: 22 * s
                    }
                }
            }
        }

        // ---- right: what can be done now ---------------------------------
        Rectangle {
            id: panel
            anchors.top: components.top; anchors.topMargin: 34 * s
            anchors.right: parent.right
            anchors.left: components.right; anchors.leftMargin: 30 * s
            anchors.bottom: parent.bottom
            radius: 22 * s
            color: t.card
            border.color: t.border

            Item {
                anchors.fill: parent
                anchors.margins: 34 * s

                // Checking
                Column {
                    visible: updater.state === "checking"
                    anchors.centerIn: parent
                    width: parent.width
                    spacing: 22 * s
                    Spinner { anchors.horizontalCenter: parent.horizontalCenter }
                    Text {
                        width: parent.width; horizontalAlignment: Text.AlignHCenter
                        text: "Checking installed firmware"
                        color: t.text
                        font.family: t.font; font.pixelSize: 28 * s; font.weight: Font.DemiBold
                    }
                }

                // Ready
                Column {
                    id: readyView
                    visible: updater.state === "ready"
                    width: parent.width
                    spacing: 18 * s
                    readonly property bool hasUpdate: updater.updatesAvailable > 0 && updater.checkError === ""

                    Row {
                        spacing: 22 * s
                        ResultIcon {
                            tint: updater.checkError !== "" ? t.bad : readyView.hasUpdate ? t.warn : t.ok
                            glyph: updater.checkError !== "" ? "bad" : readyView.hasUpdate ? "update" : "check"
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: readyView.width - 100 * s
                            wrapMode: Text.WordWrap
                            text: updater.checkError !== "" ? "Could not check the firmware"
                                  : readyView.hasUpdate ? "Update available"
                                  : "Everything is up to date"
                            color: t.text
                            font.family: t.font; font.pixelSize: 30 * s; font.weight: Font.DemiBold
                        }
                    }
                    Text {
                        visible: !readyView.hasUpdate
                        width: parent.width; wrapMode: Text.WordWrap
                        text: updater.checkError !== "" ? updater.checkError
                              : "Each board runs the firmware this system ships."
                        color: t.sub
                        font.family: t.font; font.pixelSize: 20 * s
                    }
                    Column {
                        visible: readyView.hasUpdate
                        width: parent.width
                        spacing: 8 * s
                        Bullet { label: "Takes about half a minute." }
                        Bullet { label: "Keep the system switched on until it finishes." }
                        Bullet { label: "The screen may flicker while a board restarts." }
                        Bullet { label: "Afterwards, switch the system off and on once." }
                    }
                }
                Column {   // ready: actions at the bottom
                    visible: updater.state === "ready"
                    anchors.bottom: parent.bottom
                    width: parent.width
                    spacing: 16 * s

                    // Hold to confirm: a stray tap must not start a firmware update
                    Rectangle {
                        id: holdButton
                        visible: readyView.hasUpdate
                        width: parent.width; height: 96 * s; radius: 22 * s
                        color: withAlpha(t.accent, 0.18)
                        border.color: t.accent; border.width: 2
                        clip: true
                        property real progress: 0
                        Rectangle {
                            width: parent.width * parent.progress; height: parent.height
                            radius: parent.radius
                            color: t.accent
                        }
                        Text {
                            anchors.centerIn: parent
                            text: holdArea.pressed ? "Keep holding…"
                                  : (updater.dryRun ? "Hold to run a dry run" : "Hold to update")
                            color: holdButton.progress > 0.5 ? "#081018" : t.text
                            font.family: t.font; font.pixelSize: 26 * s; font.weight: Font.Bold
                        }
                        NumberAnimation {
                            id: holdAnim
                            target: holdButton; property: "progress"
                            from: 0; to: 1; duration: 1500
                            onFinished: if (holdButton.progress >= 1) updater.startUpdate()
                        }
                        MouseArea {
                            id: holdArea
                            anchors.fill: parent
                            onPressed: holdAnim.restart()
                            onReleased: if (holdButton.progress < 1) { holdAnim.stop(); holdButton.progress = 0 }
                            onCanceled: { holdAnim.stop(); holdButton.progress = 0 }
                        }
                        Connections {
                            target: updater
                            function onStateChanged() { holdButton.progress = 0 }
                        }
                    }
                    ActionButton {
                        width: parent.width
                        label: "Check again"
                        onClicked: updater.check()
                    }
                }

                // Updating
                Column {
                    visible: updater.state === "updating"
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    spacing: 22 * s
                    Row {
                        spacing: 22 * s
                        Spinner {}
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4 * s
                            Text {
                                text: updater.dryRun ? "Dry run in progress" : "Updating firmware"
                                color: t.text
                                font.family: t.font; font.pixelSize: 30 * s; font.weight: Font.DemiBold
                            }
                            Text {
                                text: "Elapsed " + mmss(updater.elapsedSeconds)
                                color: t.sub
                                font.family: t.font; font.pixelSize: 19 * s
                            }
                        }
                    }
                    Rectangle {   // indeterminate progress
                        width: parent.width; height: 10 * s; radius: height / 2
                        color: withAlpha(t.accent, 0.18)
                        clip: true
                        Rectangle {
                            id: runner
                            width: parent.width * 0.3; height: parent.height; radius: height / 2
                            color: t.accent
                            SequentialAnimation on x {
                                loops: Animation.Infinite
                                running: updater.state === "updating"
                                NumberAnimation { from: -runner.width; to: runner.parent.width; duration: 1400; easing.type: Easing.InOutQuad }
                            }
                        }
                    }
                    Text {
                        width: parent.width; wrapMode: Text.WordWrap
                        maximumLineCount: 2; elide: Text.ElideRight
                        text: updater.activity
                        color: t.sub
                        font.family: t.font; font.pixelSize: 19 * s
                    }
                    Pill {
                        visible: !updater.dryRun
                        label: "Do not switch the system off"
                        tint: t.warn
                    }
                }

                // Done
                Column {
                    visible: updater.state === "done"
                    width: parent.width
                    spacing: 18 * s
                    Row {
                        spacing: 22 * s
                        ResultIcon {
                            tint: outcomeTone(updater.outcomeKind)
                            glyph: updater.outcomeKind === "success" ? "check"
                                   : updater.outcomeKind === "info" ? "info"
                                   : updater.outcomeKind === "warning" ? "warn" : "bad"
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: panel.width - 170 * s
                            wrapMode: Text.WordWrap
                            text: updater.outcomeTitle
                            color: t.text
                            font.family: t.font; font.pixelSize: 30 * s; font.weight: Font.DemiBold
                        }
                    }
                    Text {   // the power-cycle box below says it more directly
                        visible: !updater.powerCycleRequired
                        width: parent.width; wrapMode: Text.WordWrap
                        text: updater.outcomeDetail
                        color: t.sub
                        font.family: t.font; font.pixelSize: 20 * s
                    }
                    Rectangle {   // the one instruction that matters after an update
                        visible: updater.powerCycleRequired
                        width: parent.width
                        height: powerText.implicitHeight + 36 * s
                        radius: 16 * s
                        color: withAlpha(t.warn, 0.14)
                        border.color: withAlpha(t.warn, 0.6)
                        Text {
                            id: powerText
                            anchors.fill: parent; anchors.margins: 18 * s
                            wrapMode: Text.WordWrap
                            text: "Power cycle required: switch the system off, wait 5 seconds, switch it on."
                            color: t.warn
                            font.family: t.font; font.pixelSize: 21 * s; font.weight: Font.DemiBold
                        }
                    }
                }
                Column {
                    visible: updater.state === "done"
                    anchors.bottom: parent.bottom
                    width: parent.width
                    spacing: 16 * s
                    ActionButton {
                        width: parent.width
                        primary: true
                        label: "Back to home"
                        onClicked: updater.quitApp()
                    }
                    ActionButton {
                        width: parent.width
                        label: updater.canRetry ? "Check again and retry" : "Check again"
                        onClicked: updater.check()
                    }
                }
            }
        }
    }
}
