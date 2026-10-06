import QtQuick 2.12
import QtQuick.Window 2.12

// USB Media: the files on the stick on the left (tap = in / out of the
// playlist, the number is the play order), the playlist settings and
// actions on the right. Same visual language as System Manager and the
// launcher's "tiles" theme; RGB565-friendly colours.
Window {
    id: win
    visible: true
    visibility: Window.FullScreen
    color: t.bg
    title: "USB Media"

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
        readonly property color video: "#A78BFA"
        readonly property color image: "#F59E0B"
        readonly property string font: uiFont
    }

    // Designed at 1920x720; 1080-line panels keep the same sizes
    readonly property real s: Math.min(width / 1920, height / 720)
    property int current: -1          // row the Up / Down buttons move
    function withAlpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    function tone(name) { return name === "ok" ? t.ok : name === "bad" ? t.bad : name === "info" ? t.info : t.warn }

    Item {
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: media.back()
    }

    // ---- autostart countdown (usb-media.sh --autostart), above everything ----
    property int countdownLeft: countdownSeconds
    Timer {
        running: countdownSeconds > 0 && win.countdownLeft > 0
        interval: 1000; repeat: true
        onTriggered: {
            win.countdownLeft--
            if (win.countdownLeft <= 0) media.playSaved()
        }
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
    component ActionButton: Rectangle {
        id: ab
        property string label
        property bool primary: false
        signal clicked()
        height: 72 * s
        radius: 18 * s
        opacity: enabled ? 1 : 0.35
        color: primary ? (abArea.pressed ? Qt.darker(t.accent, 1.2) : t.accent)
                       : (abArea.pressed ? t.cardPressed : t.card)
        border.color: primary ? t.accent : t.border
        border.width: 2
        Text {
            anchors.centerIn: parent
            text: ab.label
            color: ab.primary ? "#081018" : t.text
            font.family: t.font; font.pixelSize: 24 * s; font.weight: Font.DemiBold
        }
        MouseArea { id: abArea; anchors.fill: parent; enabled: ab.enabled; onClicked: ab.clicked() }
    }

    component Toggle: Rectangle {
        id: tg
        property string label
        property string sublabel
        property bool on: false
        signal toggled()
        height: 72 * s
        radius: 18 * s
        color: tgArea.pressed ? t.cardPressed : t.card
        border.color: t.border
        border.width: 2
        opacity: enabled ? 1 : 0.45
        Column {
            anchors.left: parent.left; anchors.leftMargin: 24 * s
            anchors.right: knob.left; anchors.rightMargin: 12 * s
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width; elide: Text.ElideRight
                text: tg.label; color: t.text
                font.family: t.font; font.pixelSize: 24 * s; font.weight: Font.DemiBold
            }
            Text {
                width: parent.width; elide: Text.ElideRight
                visible: text !== ""
                text: tg.sublabel; color: t.sub
                font.family: t.font; font.pixelSize: 16 * s
            }
        }
        Rectangle {
            id: knob
            anchors.right: parent.right; anchors.rightMargin: 20 * s
            anchors.verticalCenter: parent.verticalCenter
            width: 76 * s; height: 40 * s; radius: height / 2
            color: tg.on ? t.accent : t.border
            Rectangle {
                width: parent.height - 8 * s; height: width; radius: width / 2
                y: 4 * s
                x: tg.on ? parent.width - width - 4 * s : 4 * s
                color: tg.on ? "#081018" : t.sub
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
        MouseArea { id: tgArea; anchors.fill: parent; enabled: tg.enabled; onClicked: tg.toggled() }
    }

    component Spinner: Item {
        width: 40 * s; height: width
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

    // ---- header --------------------------------------------------------------
    Item {
        id: header
        x: 40 * s; y: 20 * s
        width: win.width - 80 * s; height: 76 * s

        // Back, top left: the same round button as the Network and System
        // Manager apps' headers (back to the launcher, as Escape)
        Rectangle {
            id: backButton
            width: 76 * s; height: width; radius: width / 2
            anchors.verticalCenter: parent.verticalCenter
            color: backArea.pressed ? t.cardPressed : t.card
            border.color: t.border
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
            MouseArea { id: backArea; anchors.fill: parent; onClicked: media.back() }
        }
        Column {
            anchors.left: backButton.right; anchors.leftMargin: 28 * s
            anchors.right: headerStatus.left; anchors.rightMargin: 24 * s
            anchors.verticalCenter: parent.verticalCenter
            Text {
                text: "USB Media"
                color: t.text
                font.family: t.font; font.pixelSize: 36 * s; font.weight: Font.Bold
            }
            Text {
                width: parent.width; elide: Text.ElideMiddle
                text: media.root + (media.readOnly ? "   ·   read-only" : "")
                color: t.sub
                font.family: t.font; font.pixelSize: 18 * s
            }
        }
        Row {
            id: headerStatus
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 16 * s
            Spinner { visible: media.state !== "ready"; anchors.verticalCenter: parent.verticalCenter }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: media.state === "ready"
                      ? media.mediaCount + (media.mediaCount === 1 ? " file" : " files") + "   ·   "
                        + media.checkedCount + " in the playlist"
                      : "Examining files… " + media.probedCount + " / " + media.mediaCount
                color: t.sub
                font.family: t.font; font.pixelSize: 22 * s
            }
        }
        // SMPTE strip, as in the launcher's header
        Row {
            anchors.top: parent.bottom; anchors.topMargin: 8 * s
            anchors.left: backButton.right; anchors.leftMargin: 28 * s
            Repeater {
                model: ["#C0C0C0", "#C0C000", "#00C0C0", "#00C000", "#C000C0", "#C00000", "#0000C0"]
                Rectangle { width: 36 * s; height: 4 * s; color: modelData }
            }
        }
    }

    // ---- message banner (why playback ended, save results) --------------------
    Rectangle {
        id: banner
        visible: media.message !== ""
        x: header.x; y: header.y + header.height + 22 * s
        width: list.width; height: visible ? 56 * s : 0
        radius: 14 * s
        color: withAlpha(tone(media.messageTone), 0.14)
        border.color: withAlpha(tone(media.messageTone), 0.5)
        Text {
            anchors.left: parent.left; anchors.leftMargin: 22 * s
            anchors.right: closeText.left; anchors.rightMargin: 12 * s
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: media.message
            color: tone(media.messageTone)
            font.family: t.font; font.pixelSize: 21 * s; font.weight: Font.DemiBold
        }
        Text {
            id: closeText
            anchors.right: parent.right; anchors.rightMargin: 22 * s
            anchors.verticalCenter: parent.verticalCenter
            text: "✕"; color: t.sub
            font.pixelSize: 22 * s
        }
        MouseArea { anchors.fill: parent; onClicked: media.message = "" }
    }

    // ---- file list -------------------------------------------------------------
    ListView {
        id: list
        x: header.x
        y: banner.visible ? banner.y + banner.height + 14 * s : header.y + header.height + 22 * s
        width: header.width - side.width - 32 * s
        height: win.height - y - 24 * s
        clip: true
        spacing: 8 * s
        model: media.files
        boundsBehavior: Flickable.StopAtBounds
        // Up / Down keep the moved row in view
        onCountChanged: if (win.current >= count) win.current = -1

        Text {
            anchors.centerIn: parent
            visible: list.count === 0 && media.state === "ready"
            text: "No pictures or videos on this stick\n(JPEG, PNG, MP4, MKV, MOV, M4V — in the root or in folders)"
            horizontalAlignment: Text.AlignHCenter
            color: t.sub
            font.family: t.font; font.pixelSize: 24 * s
        }

        delegate: Rectangle {
            id: row
            width: list.width
            height: 76 * s
            radius: 14 * s
            readonly property bool isCurrent: index === win.current
            color: rowArea.pressed ? t.cardPressed : t.card
            border.color: isCurrent ? t.accent : (model.checked ? withAlpha(t.accent, 0.45) : t.border)
            border.width: isCurrent ? 3 : 1.5
            opacity: model.probed && !model.playable ? 0.55 : 1

            // check box with the play order
            Rectangle {
                id: box
                x: 18 * s; anchors.verticalCenter: parent.verticalCenter
                width: 48 * s; height: width; radius: 10 * s
                color: model.checked ? t.accent : "transparent"
                border.color: model.checked ? t.accent : t.sub
                border.width: 2
                visible: model.playable || !model.probed
                Text {
                    anchors.centerIn: parent
                    text: model.order > 0 ? model.order : ""
                    color: "#081018"
                    font.family: t.font; font.pixelSize: 22 * s; font.weight: Font.Bold
                }
            }
            Text {
                anchors.centerIn: box
                visible: model.probed && !model.playable
                text: "✕"; color: t.bad
                font.pixelSize: 26 * s
            }
            // kind badge
            Rectangle {
                id: kindBadge
                anchors.left: box.right; anchors.leftMargin: 18 * s
                anchors.verticalCenter: parent.verticalCenter
                width: 92 * s; height: 34 * s; radius: height / 2
                readonly property color tint: model.kind === "video" ? t.video : t.image
                color: withAlpha(tint, 0.16)
                border.color: withAlpha(tint, 0.5)
                Text {
                    anchors.centerIn: parent
                    text: model.kind === "video" ? "VIDEO" : "IMAGE"
                    color: parent.tint
                    font.family: t.font; font.pixelSize: 15 * s; font.weight: Font.Bold
                }
            }
            Column {
                anchors.left: kindBadge.right; anchors.leftMargin: 18 * s
                anchors.right: parent.right; anchors.rightMargin: 20 * s
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2 * s
                Text {
                    width: parent.width; elide: Text.ElideMiddle
                    text: model.name
                    color: t.text
                    font.family: t.font; font.pixelSize: 23 * s; font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width; elide: Text.ElideRight
                    textFormat: Text.StyledText
                    readonly property var parts: {
                        var p = []
                        if (model.folder !== "") p.push(model.folder)
                        if (model.info !== "") p.push(model.info)
                        if (!model.probed) p.push("examining…")
                        return p
                    }
                    text: parts.join("  ·  ")
                          + (model.reason !== "" ? "  <font color='" + t.bad + "'>— " + model.reason + "</font>" : "")
                          + (model.note !== "" ? "  <font color='" + t.warn + "'>— " + model.note + "</font>" : "")
                    color: t.sub
                    font.family: t.font; font.pixelSize: 17 * s
                }
            }
            MouseArea {
                id: rowArea
                anchors.fill: parent
                onClicked: {
                    win.current = index
                    media.toggle(index)
                }
            }
        }
    }

    // ---- settings and actions ------------------------------------------------
    Column {
        id: side
        anchors.right: header.right
        y: header.y + header.height + 22 * s
        width: 520 * s
        spacing: 12 * s

        Row {
            spacing: 12 * s
            ActionButton { width: (side.width - 12 * s) / 2; label: "Select all"; onClicked: media.selectAll(true) }
            ActionButton { width: (side.width - 12 * s) / 2; label: "None"; onClicked: media.selectAll(false) }
        }
        Row {
            spacing: 12 * s
            ActionButton {
                width: (side.width - 12 * s) / 2; label: "▲  Up"
                enabled: win.current > 0
                onClicked: if (media.move(win.current, -1)) { win.current--; list.positionViewAtIndex(win.current, ListView.Contain) }
            }
            ActionButton {
                width: (side.width - 12 * s) / 2; label: "▼  Down"
                enabled: win.current >= 0 && win.current < list.count - 1
                onClicked: if (media.move(win.current, 1)) { win.current++; list.positionViewAtIndex(win.current, ListView.Contain) }
            }
        }
        // image duration: - value +
        Rectangle {
            width: side.width; height: 72 * s; radius: 18 * s
            color: t.card; border.color: t.border; border.width: 2
            Text {
                anchors.left: parent.left; anchors.leftMargin: 24 * s
                anchors.verticalCenter: parent.verticalCenter
                text: "Each image"
                color: t.text
                font.family: t.font; font.pixelSize: 24 * s; font.weight: Font.DemiBold
            }
            Row {
                anchors.right: parent.right; anchors.rightMargin: 10 * s
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8 * s
                ActionButton {
                    width: 64 * s; height: 56 * s; label: "−"
                    enabled: media.imageDuration > 2
                    onClicked: media.imageDuration = media.imageDuration - (media.imageDuration > 60 ? 30 : media.imageDuration > 10 ? 5 : 1)
                }
                Text {
                    width: 110 * s
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: media.imageDuration >= 60 && media.imageDuration % 60 === 0
                          ? (media.imageDuration / 60) + " min" : media.imageDuration + " s"
                    color: t.text
                    font.family: t.font; font.pixelSize: 26 * s; font.weight: Font.Bold
                }
                ActionButton {
                    width: 64 * s; height: 56 * s; label: "+"
                    enabled: media.imageDuration < 600
                    onClicked: media.imageDuration = media.imageDuration + (media.imageDuration >= 60 ? 30 : media.imageDuration >= 10 ? 5 : 1)
                }
            }
        }
        Toggle {
            width: side.width
            label: "Loop"
            sublabel: media.loop ? "Plays the list again and again" : "Back here after the last item"
            on: media.loop
            onToggled: media.loop = !media.loop
        }
        Toggle {
            width: side.width
            visible: autostartSupported
            label: "Autostart on boot"
            sublabel: media.readOnly ? "Read-only stick: cannot be stored" : "Plays this list when the unit starts"
            enabled: !media.readOnly
            on: media.autostart
            onToggled: media.autostart = !media.autostart
        }
        ActionButton {
            width: side.width
            label: media.readOnly ? "Read-only" : media.dirty ? "Save" : "Saved"
            enabled: !media.readOnly && media.dirty
            onClicked: media.save()
        }
        ActionButton {
            width: side.width; height: 96 * s
            primary: true
            label: media.checkedCount > 0 ? "▶  Play " + media.checkedCount + (media.checkedCount === 1 ? " item" : " items")
                                          : "Tap files to add them"
            enabled: media.checkedCount > 0 && media.state === "ready"
            onClicked: media.play()
        }
        Text {
            width: side.width
            wrapMode: Text.WordWrap
            text: "Plays on both displays. Tap the screen during playback for EXIT."
            color: t.sub
            font.family: t.font; font.pixelSize: 17 * s
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: countdownSeconds > 0
        z: 100
        color: t.bg
        MouseArea { anchors.fill: parent; onClicked: media.back() }   // cancel: this boot only
        Column {
            anchors.centerIn: parent
            spacing: 28 * s
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Starting the playlist"
                color: t.text
                font.family: t.font; font.pixelSize: 48 * s; font.weight: Font.Bold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: win.countdownLeft
                color: t.accent
                font.family: t.font; font.pixelSize: 160 * s; font.weight: Font.Bold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "from " + media.root
                color: t.sub
                font.family: t.font; font.pixelSize: 22 * s
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: cancelText.implicitWidth + 80 * s; height: 84 * s; radius: 20 * s
                color: "transparent"; border.color: t.border; border.width: 2
                Text {
                    id: cancelText
                    anchors.centerIn: parent
                    text: "Tap anywhere to cancel"
                    color: t.text
                    font.family: t.font; font.pixelSize: 26 * s; font.weight: Font.DemiBold
                }
            }
        }
    }
}
