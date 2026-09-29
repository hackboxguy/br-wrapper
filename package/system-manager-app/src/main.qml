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
    // Nothing may be left while an update runs. An image install ends in the
    // engine's reboot; only a dry run's stand-in leaves the app on "arming".
    readonly property bool busy: updater.state === "updating" || imageUpdate.state === "installing"
                                 || (imageUpdate.state === "arming" && !imageUpdate.dryRun)
                                 || fpgaUpdate.state === "updating" || fpgaUpdate.state === "activating"
                                 || (fpgaUpdate.state === "rebooting" && !fpgaUpdate.dryRun)
    // "firmware", "image" or "fpga"; main.cpp opens the image section when it has news
    property string section: initialSection
    // The FPGA section exists only where the probe found an FPGA with the
    // update interface (0x1E); asked for it on a system without, show firmware
    Connections {
        target: fpgaUpdate
        function onStateChanged() { if (fpgaUpdate.state === "absent" && win.section === "fpga") win.section = "firmware" }
    }

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

    // Rough time per board, from the rigs: flash + reset + wait for the new
    // application to confirm. 983HH ~50 KiB at ~30 KiB/s + ~10 s confirm;
    // the display controller is slower on its bus (~4 KiB/s measured).
    readonly property var boardSeconds: ({ "983": 25 })
    readonly property int displaySeconds: 45
    readonly property int estimateSeconds: {
        var total = 0
        var list = updater.components
        for (var i = 0; i < list.length; ++i) {
            if (list[i].status !== "outdated") continue
            total += boardSeconds[list[i].board] !== undefined ? boardSeconds[list[i].board] : displaySeconds
        }
        return total
    }
    // Frozen when the update starts: the statuses change while it runs
    property int runEstimate: 0
    // The panel goes dark when a board restarts and needs a power cycle to
    // come back (the video link recovers, the panel does not); this is how
    // long to wait before that power cycle: well past the estimate, so the
    // script has finished or failed by then
    readonly property int darkWaitMinutes: Math.max(2, Math.ceil(2 * estimateSeconds / 60))
    function aboutText(sec) {
        return sec < 50 ? "about " + (Math.ceil(sec / 10) * 10) + " seconds"
             : sec < 90 ? "about a minute"
             : "about " + Math.ceil(sec / 60) + " minutes"
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

    // Hold to confirm: a stray tap must not start something that writes or restarts
    component HoldButton: Rectangle {
        id: hb
        property string label
        property color tint: t.accent
        signal done()
        height: 88 * s; radius: 22 * s
        color: withAlpha(tint, 0.18)
        border.color: tint; border.width: 2
        clip: true
        property real progress: 0
        Rectangle {
            width: parent.width * parent.progress; height: parent.height
            radius: parent.radius
            color: hb.tint
        }
        Text {
            anchors.centerIn: parent
            text: hbArea.pressed ? "Keep holding…" : hb.label
            color: hb.progress > 0.5 ? "#081018" : t.text
            font.family: t.font; font.pixelSize: 26 * s; font.weight: Font.Bold
        }
        NumberAnimation {
            id: hbAnim
            target: hb; property: "progress"
            from: 0; to: 1; duration: 1500
            onFinished: if (hb.progress >= 1) { hb.progress = 0; hb.done() }
        }
        MouseArea {
            id: hbArea
            anchors.fill: parent
            onPressed: hbAnim.restart()
            onReleased: if (hb.progress < 1) { hbAnim.stop(); hb.progress = 0 }
            onCanceled: { hbAnim.stop(); hb.progress = 0 }
        }
    }

    // Same shape as ComponentCard, with free text: the image section's cards
    component ImageCard: Rectangle {
        id: ic
        property string icon: "sdcard"
        property string title
        property string subtitle
        property string note
        property color noteTone: t.sub
        property string warning          // amber, e.g. the install preflight
        property string pillLabel
        property color tone: t.sub
        width: parent ? parent.width : 0
        height: 150 * s
        radius: 18 * s
        color: t.card
        border.color: t.border

        Rectangle {   // accent stripe
            x: 0; y: 0; width: 6 * s; height: parent.height; radius: 3 * s
            color: ic.tone
        }
        Rectangle {   // icon badge
            id: icBadge
            x: 36 * s; anchors.verticalCenter: parent.verticalCenter
            width: 84 * s; height: width; radius: 22 * s
            color: withAlpha(ic.tone, 0.16)
            border.color: withAlpha(ic.tone, 0.45)
            Image {
                anchors.centerIn: parent
                width: parent.width * 0.6; height: width
                sourceSize: Qt.size(width, height)
                source: "qrc:/icons/" + ic.icon + ".svg"
            }
        }
        Column {
            anchors.left: icBadge.right; anchors.leftMargin: 28 * s
            anchors.right: icPill.left; anchors.rightMargin: 20 * s
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6 * s
            Text {
                width: parent.width; elide: Text.ElideRight
                text: ic.title
                color: t.text
                font.family: t.font; font.pixelSize: 28 * s; font.weight: Font.DemiBold
            }
            Text {
                width: parent.width; elide: Text.ElideMiddle
                visible: text !== ""
                text: ic.subtitle
                color: t.sub
                font.family: t.font; font.pixelSize: 18 * s
            }
            Text {
                width: parent.width; elide: Text.ElideRight
                visible: text !== ""
                text: ic.note
                color: ic.noteTone
                font.family: t.font; font.pixelSize: 18 * s
            }
            Text {
                width: parent.width; elide: Text.ElideRight
                visible: text !== ""
                text: ic.warning
                color: t.warn
                font.family: t.font; font.pixelSize: 18 * s; font.weight: Font.DemiBold
            }
        }
        Pill {
            id: icPill
            anchors.right: parent.right; anchors.rightMargin: 28 * s
            anchors.verticalCenter: parent.verticalCenter
            label: ic.pillLabel
            tint: ic.tone
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
                MouseArea { id: backArea; anchors.fill: parent; enabled: !win.busy; onClicked: if (!win.busy) updater.quitApp() }
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
                    text: "Home  ›  System Manager  ›  " + (win.section === "image" ? "System image"
                                                         : win.section === "fpga" ? "Display FPGA" : "Firmware")
                    color: t.sub
                    font.family: t.font; font.pixelSize: 19 * s
                }
            }
            // Sections: fixed while any update runs. Display FPGA appears only
            // where an FPGA with the update interface answered the probe.
            Rectangle {
                id: sectionSwitch
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                width: sectionRow.width + 12 * s; height: 60 * s
                radius: height / 2
                color: t.card
                border.color: t.border
                opacity: win.busy ? 0.4 : 1.0
                property real selX: 0
                property real selW: 0
                Rectangle {   // the selection, sliding between the tabs
                    y: 6 * s; height: parent.height - 12 * s; radius: height / 2
                    x: 6 * s + sectionSwitch.selX
                    width: sectionSwitch.selW
                    color: withAlpha(t.accent, 0.22)
                    border.color: withAlpha(t.accent, 0.7)
                    Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                    Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                }
                Row {
                    id: sectionRow
                    x: 6 * s
                    anchors.verticalCenter: parent.verticalCenter
                    Repeater {
                        model: fpgaUpdate.present
                               ? [{ key: "firmware", label: "Firmware" }, { key: "image", label: "System image" },
                                  { key: "fpga", label: "Display FPGA" }]
                               : [{ key: "firmware", label: "Firmware" }, { key: "image", label: "System image" }]
                        Item {
                            id: tab
                            readonly property bool selected: win.section === modelData.key
                            width: tabText.implicitWidth + 56 * s; height: 48 * s
                            function place() { if (selected) { sectionSwitch.selX = x; sectionSwitch.selW = width } }
                            onSelectedChanged: place()
                            onXChanged: place()
                            onWidthChanged: place()
                            Component.onCompleted: place()
                            Text {
                                id: tabText
                                anchors.centerIn: parent
                                text: modelData.label
                                color: tab.selected ? t.text : t.sub
                                font.family: t.font; font.pixelSize: 20 * s; font.weight: Font.DemiBold
                            }
                            // other sections flag news on their tab
                            Rectangle {
                                readonly property bool imageNews: modelData.key === "image"
                                    && (imageUpdate.scanState === "ready" || imageUpdate.lastOutcome === "fallback")
                                readonly property bool fpgaNews: modelData.key === "fpga"
                                    && (fpgaUpdate.updateAvailable || fpgaUpdate.state === "written")
                                visible: !tab.selected && (imageNews || fpgaNews)
                                anchors.right: parent.right; anchors.rightMargin: 14 * s
                                anchors.verticalCenter: parent.verticalCenter
                                width: 10 * s; height: width; radius: width / 2
                                color: imageUpdate.lastOutcome === "fallback" && imageNews ? t.bad : t.warn
                            }
                            MouseArea {
                                anchors.fill: parent
                                enabled: !win.busy
                                onClicked: win.section = modelData.key
                            }
                        }
                    }
                }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14 * s
                Pill { visible: updater.dryRun; label: "DRY RUN"; tint: t.info }
                Pill {
                    visible: win.section === "image"
                    label: !imageUpdate.supported ? "Not available"
                           : imageUpdate.state === "installing" ? "Installing"
                           : imageUpdate.state === "arming" ? "Rebooting"
                           : imageUpdate.state === "failed" ? "Update failed"
                           : imageUpdate.lastOutcome === "fallback" ? "Rolled back"
                           : imageUpdate.lastOutcome === "candidate-armed" ? "Verifying"
                           : imageUpdate.scanState === "ready" ? "Update on USB"
                           : "Image " + imageUpdate.runningVersion
                    tint: !imageUpdate.supported ? t.sub
                          : imageUpdate.state === "installing" || imageUpdate.state === "arming" ? t.info
                          : imageUpdate.state === "failed" || imageUpdate.lastOutcome === "fallback" ? t.bad
                          : imageUpdate.lastOutcome === "candidate-armed" ? t.info
                          : imageUpdate.scanState === "ready" ? t.warn : t.ok
                }
                Pill {
                    visible: win.section === "fpga"
                    label: fpgaUpdate.state === "probing" || fpgaUpdate.state === "checking" ? "Checking"
                           : fpgaUpdate.state === "updating" ? "Updating"
                           : fpgaUpdate.state === "activating" ? "Restarting FPGA"
                           : fpgaUpdate.state === "rebooting" ? "Restarting"
                           : fpgaUpdate.state === "written" ? "Restart required"
                           : fpgaUpdate.state === "failed" ? "Needs attention"
                           : fpgaUpdate.status === "current" ? "Up to date"
                           : fpgaUpdate.status === "outdated" ? "Update available"
                           : fpgaUpdate.status === "blocked" ? "Blocked" : "Not checked"
                    tint: fpgaUpdate.state === "updating" || fpgaUpdate.state === "activating"
                          || fpgaUpdate.state === "rebooting" ? t.info
                          : fpgaUpdate.state === "written" ? t.warn
                          : fpgaUpdate.state === "failed" ? t.bad
                          : fpgaUpdate.status === "current" ? t.ok
                          : fpgaUpdate.status === "outdated" ? t.warn
                          : fpgaUpdate.status === "blocked" ? t.bad : t.sub
                }
                Pill {
                    visible: win.section === "firmware"
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

        // ==== Display FPGA section (update-fpga.sh) ============================
        Item {
            id: fpgaSection
            visible: win.section === "fpga"
            anchors.top: strip.bottom; anchors.topMargin: 26 * s
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: parent.bottom

            readonly property string st: fpgaUpdate.state
            readonly property string status: fpgaUpdate.status
            readonly property bool checkingNow: st === "probing" || st === "checking"
            readonly property bool firmwareFirst: status === "blocked"
                && /firmware|983_manager|IOC/i.test(fpgaUpdate.reason)

            // ---- left: what runs, and what this system ships -----------------
            Column {
                id: fpgaLeft
                width: parent.width * 0.58
                spacing: 18 * s

                Text {
                    text: "DISPLAY FPGA"
                    color: t.sub
                    font.family: t.font; font.pixelSize: 16 * s; font.weight: Font.DemiBold
                    font.letterSpacing: 2 * s
                }
                ImageCard {
                    icon: "fpga"
                    title: "Display FPGA" + (fpgaUpdate.displayName ? "  (" + fpgaUpdate.displayName + ")" : "")
                    subtitle: fpgaUpdate.runningRelease
                              ? "Release " + fpgaUpdate.runningRelease + "   ·   build " + fpgaUpdate.runningBuild
                                + "   ·   " + (fpgaUpdate.runningSlot === "OTA" ? "from the update slot"
                                              : fpgaUpdate.runningSlot === "GOLDEN" ? "factory image (fallback)"
                                              : "slot unknown")
                              : fpgaSection.checkingNow ? "Reading the display FPGA…" : "Not read"
                    note: fpgaSection.st === "written" ? "The new image is in the update slot; it runs after activation"
                          : fpgaSection.status === "current" ? "Runs the image this system ships"
                          : fpgaSection.status === "outdated" ? "This system ships a different image"
                          : fpgaSection.status === "no-answer" ? "The FPGA does not answer"
                          : fpgaUpdate.reason
                    noteTone: fpgaSection.st === "written" ? t.warn
                              : fpgaSection.status === "current" ? t.ok
                              : fpgaSection.status === "outdated" ? t.warn : t.bad
                    pillLabel: fpgaSection.checkingNow ? "Checking"
                               : fpgaSection.st === "written" ? "Restart required"
                               : fpgaSection.status === "current" ? "Up to date"
                               : fpgaSection.status === "outdated" ? "Update available"
                               : fpgaSection.status === "blocked" ? "Blocked"
                               : fpgaSection.status === "no-answer" ? "No answer" : "Unknown"
                    tone: fpgaSection.checkingNow ? t.sub
                          : fpgaSection.st === "written" || fpgaSection.status === "outdated" ? t.warn
                          : fpgaSection.status === "current" ? t.ok : t.bad
                }
                ImageCard {
                    icon: "update"
                    title: fpgaUpdate.image || "Shipped image"
                    subtitle: "Written to the update slot; the factory image stays as the fallback"
                    note: ""
                    pillLabel: "Shipped"
                    tone: t.accent
                }
            }

            // ---- right: what can be done now ------------------------------------
            Rectangle {
                id: fpgaPanel
                anchors.top: fpgaLeft.top; anchors.topMargin: 34 * s
                anchors.right: parent.right
                anchors.left: fpgaLeft.right; anchors.leftMargin: 30 * s
                anchors.bottom: parent.bottom
                radius: 22 * s
                color: t.card
                border.color: t.border

                Item {
                    anchors.fill: parent
                    anchors.margins: 34 * s

                    // Checking, activating, restarting: a spinner and a line
                    Column {
                        visible: fpgaSection.checkingNow || fpgaSection.st === "activating" || fpgaSection.st === "rebooting"
                        anchors.centerIn: parent
                        width: parent.width
                        spacing: 22 * s
                        Spinner { anchors.horizontalCenter: parent.horizontalCenter }
                        Text {
                            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                            text: fpgaSection.st === "activating" ? "Restarting the display FPGA…"
                                  : fpgaSection.st === "rebooting" ? "Restarting the system…"
                                  : "Checking the display FPGA"
                            color: t.text
                            font.family: t.font; font.pixelSize: 28 * s; font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                            visible: text !== ""
                            text: fpgaSection.st === "activating" ? "The display goes dark for a few seconds."
                                  : fpgaSection.st === "rebooting" ? fpgaUpdate.outcomeDetail
                                  : "About ten seconds."
                            color: t.sub
                            font.family: t.font; font.pixelSize: 20 * s
                        }
                    }

                    // Ready: offer, up to date, or why not
                    Column {
                        id: fpgaReady
                        visible: fpgaSection.st === "ready"
                        width: parent.width
                        spacing: 18 * s
                        readonly property bool offer: fpgaSection.status === "outdated"
                        Row {
                            spacing: 22 * s
                            ResultIcon {
                                tint: fpgaReady.offer ? t.warn : fpgaSection.status === "current" ? t.ok : t.bad
                                glyph: fpgaReady.offer ? "update" : fpgaSection.status === "current" ? "check" : "bad"
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: fpgaReady.width - 100 * s
                                wrapMode: Text.WordWrap
                                text: fpgaReady.offer ? "FPGA update available"
                                      : fpgaSection.status === "current" ? "The display FPGA is up to date"
                                      : fpgaSection.firmwareFirst ? "Update the board firmware first"
                                      : fpgaSection.status === "blocked" ? "The update is blocked"
                                      : "The display FPGA could not be checked"
                                color: t.text
                                font.family: t.font; font.pixelSize: 30 * s; font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            visible: !fpgaReady.offer
                            width: parent.width; wrapMode: Text.WordWrap
                            text: fpgaSection.status === "current" ? "It runs the image this system ships."
                                  : fpgaSection.firmwareFirst ? fpgaUpdate.reason + ". The Firmware section installs it."
                                  : fpgaUpdate.reason
                            color: t.sub
                            font.family: t.font; font.pixelSize: 20 * s
                        }
                        Column {
                            visible: fpgaReady.offer
                            width: parent.width
                            spacing: 8 * s
                            Bullet { label: "Takes up to about 15 minutes. Keep the system switched on." }
                            Bullet { label: "The display may go dark briefly if its link has to be recovered." }
                            Bullet { label: "Then activate it: the display restarts and the system reboots." }
                        }
                    }
                    Column {
                        visible: fpgaSection.st === "ready"
                        anchors.bottom: parent.bottom
                        width: parent.width
                        spacing: 16 * s
                        HoldButton {
                            visible: fpgaReady.offer
                            width: parent.width
                            label: fpgaUpdate.dryRun ? "Hold to run a dry update" : "Hold to update the FPGA"
                            onDone: fpgaUpdate.startUpdate()
                        }
                        ActionButton {
                            visible: fpgaSection.firmwareFirst
                            width: parent.width
                            height: 72 * s
                            primary: true
                            label: "Go to Firmware"
                            onClicked: win.section = "firmware"
                        }
                        ActionButton {
                            width: parent.width
                            height: 72 * s
                            label: "Check again"
                            onClicked: fpgaUpdate.check()
                        }
                    }

                    // Updating: the engine's own progress
                    Column {
                        visible: fpgaSection.st === "updating"
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
                                    text: "Updating the display FPGA"
                                    color: t.text
                                    font.family: t.font; font.pixelSize: 30 * s; font.weight: Font.DemiBold
                                }
                                Text {
                                    text: fpgaUpdate.phaseText + "   ·   " + fpgaUpdate.percent + "%   ·   " + mmss(fpgaUpdate.elapsedSeconds)
                                    color: t.sub
                                    font.family: t.font; font.pixelSize: 19 * s
                                }
                            }
                        }
                        Rectangle {
                            width: parent.width; height: 14 * s; radius: height / 2
                            color: withAlpha(t.accent, 0.18)
                            Rectangle {
                                width: Math.max(parent.height, parent.width * fpgaUpdate.percent / 100)
                                height: parent.height; radius: height / 2
                                color: t.accent
                                Behavior on width { NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }
                            }
                        }
                        Pill { visible: !fpgaUpdate.dryRun; label: "Do not switch the system off"; tint: t.warn }
                    }

                    // Written: activate now (restarts the system) or later
                    Column {
                        visible: fpgaSection.st === "written"
                        width: parent.width
                        spacing: 18 * s
                        Row {
                            spacing: 22 * s
                            ResultIcon { tint: t.ok; glyph: "check" }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: fpgaPanel.width - 170 * s
                                wrapMode: Text.WordWrap
                                text: fpgaUpdate.outcomeTitle
                                color: t.text
                                font.family: t.font; font.pixelSize: 30 * s; font.weight: Font.DemiBold
                            }
                        }
                        Rectangle {
                            width: parent.width
                            height: writtenText.implicitHeight + 28 * s
                            radius: 16 * s
                            color: withAlpha(t.warn, 0.14)
                            border.color: withAlpha(t.warn, 0.6)
                            Text {
                                id: writtenText
                                anchors.fill: parent; anchors.margins: 14 * s
                                wrapMode: Text.WordWrap
                                text: "Activating restarts the display FPGA (the display goes dark for a few seconds) "
                                      + "and then the whole system. If the display stays dark, switch the system off and on."
                                color: t.warn
                                font.family: t.font; font.pixelSize: 19 * s; font.weight: Font.DemiBold
                            }
                        }
                    }
                    Column {
                        visible: fpgaSection.st === "written"
                        anchors.bottom: parent.bottom
                        width: parent.width
                        spacing: 16 * s
                        HoldButton {
                            width: parent.width
                            tint: t.warn
                            label: "Hold to activate and restart"
                            onDone: fpgaUpdate.activate()
                        }
                        ActionButton {
                            width: parent.width
                            height: 72 * s
                            label: "Later"
                            onClicked: updater.quitApp()
                        }
                    }

                    // Failed
                    Column {
                        visible: fpgaSection.st === "failed"
                        width: parent.width
                        spacing: 18 * s
                        Row {
                            spacing: 22 * s
                            ResultIcon {
                                tint: fpgaUpdate.outcomeKind === "info" ? t.info
                                      : fpgaUpdate.outcomeKind === "warning" ? t.warn : t.bad
                                glyph: fpgaUpdate.outcomeKind === "info" ? "info"
                                       : fpgaUpdate.outcomeKind === "warning" ? "warn" : "bad"
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: fpgaPanel.width - 170 * s
                                wrapMode: Text.WordWrap
                                text: fpgaUpdate.outcomeTitle
                                color: t.text
                                font.family: t.font; font.pixelSize: 30 * s; font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            visible: !fpgaUpdate.powerCycleRequired
                            width: parent.width; wrapMode: Text.WordWrap
                            text: fpgaUpdate.outcomeDetail
                            color: t.sub
                            font.family: t.font; font.pixelSize: 20 * s
                        }
                        Rectangle {
                            visible: fpgaUpdate.powerCycleRequired
                            width: parent.width
                            height: fpgaPowerText.implicitHeight + 36 * s
                            radius: 16 * s
                            color: withAlpha(t.warn, 0.14)
                            border.color: withAlpha(t.warn, 0.6)
                            Text {
                                id: fpgaPowerText
                                anchors.fill: parent; anchors.margins: 18 * s
                                wrapMode: Text.WordWrap
                                text: fpgaUpdate.outcomeDetail
                                color: t.warn
                                font.family: t.font; font.pixelSize: 21 * s; font.weight: Font.DemiBold
                            }
                        }
                    }
                    Column {
                        visible: fpgaSection.st === "failed"
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
                            height: 72 * s
                            label: fpgaUpdate.canRetry ? "Check again and retry" : "Check again"
                            onClicked: fpgaUpdate.check()
                        }
                    }
                }
            }
        }

        // ==== System image section (pi-ab-update) =============================
        // USB polling and scans run only while this section is on screen
        Binding { target: imageUpdate; property: "active"; value: win.section === "image" }

        Item {
            id: imageSection
            visible: win.section === "image"
            anchors.top: strip.bottom; anchors.topMargin: 26 * s
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: parent.bottom

            readonly property var offer: imageUpdate.offered
            readonly property string offerTitle: offer.version
                ? "Image " + offer.version + "  (" + offer.variant + ", " + offer.boards + ")" : ""

            // An image without the A/B engine: this, and nothing else
            Rectangle {
                visible: !imageUpdate.supported
                anchors.fill: parent; anchors.topMargin: 34 * s
                radius: 22 * s
                color: t.card; border.color: t.border
                Column {
                    anchors.centerIn: parent
                    spacing: 16 * s
                    ResultIcon { anchors.horizontalCenter: parent.horizontalCenter; tint: t.sub; glyph: "info" }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "This image does not support in-system updates"
                        color: t.text
                        font.family: t.font; font.pixelSize: 28 * s; font.weight: Font.DemiBold
                    }
                }
            }

            // ---- left: what runs, and what the stick offers -------------------
            Column {
                id: imageLeft
                visible: imageUpdate.supported
                width: parent.width * 0.58
                spacing: 18 * s

                Text {
                    text: "SYSTEM IMAGE"
                    color: t.sub
                    font.family: t.font; font.pixelSize: 16 * s; font.weight: Font.DemiBold
                    font.letterSpacing: 2 * s
                }

                ImageCard {
                    icon: "sdcard"
                    title: "Image " + (imageUpdate.runningVersion || "unknown")
                    subtitle: "Running from slot " + (imageUpdate.slot || "?")
                              + (imageUpdate.variant ? "   ·   " + imageUpdate.variant : "")
                    note: imageUpdate.lastOutcomeText
                    noteTone: imageUpdate.lastOutcome === "fallback" ? t.bad
                              : imageUpdate.lastOutcome === "candidate-armed" ? t.info : t.ok
                    pillLabel: imageUpdate.lastOutcome === "fallback" ? "Rolled back"
                               : imageUpdate.lastOutcome === "candidate-armed" ? "Verifying"
                               : imageUpdate.lastOutcome === "committed" ? "Committed" : "Running"
                    tone: imageUpdate.lastOutcome === "fallback" ? t.bad
                          : imageUpdate.lastOutcome === "candidate-armed" ? t.info : t.ok
                }

                ImageCard {
                    readonly property string st: imageUpdate.scanState
                    readonly property bool hasOffer: st === "ready" || st === "same-version" || st === "one"
                    icon: "usb"
                    title: hasOffer ? imageSection.offerTitle : "USB stick"
                    subtitle: hasOffer
                              ? imageSection.offer.size + "   ·   " + imageSection.offer.path + "  on  " + imageSection.offer.device
                              : st === "scanning" || st === "idle" ? "Looking at the USB stick…"
                              : st === "nostick" ? "No USB stick"
                              : st === "none" ? "No update bundle on the stick"
                              : st === "nested" ? "The bundle is inside a folder"
                              : st === "many" ? "More than one bundle on the stick"
                              : st === "unreadable" ? "The stick could not be read"
                              : "The stick could not be scanned"
                    note: st === "ready" || st === "same-version" ? "Signed with this device's release key"
                          : imageUpdate.scanDetail
                    noteTone: st === "ready" || st === "same-version" ? t.ok
                              : st === "one" || st === "error" || st === "unreadable" ? t.bad : t.sub
                    // preflight: the engine would not keep the new image
                    warning: st === "ready" ? imageUpdate.preflightWarning : ""
                    pillLabel: st === "ready" ? "Update available"
                               : st === "same-version" ? "Already running"
                               : st === "one" ? "Not installable"
                               : st === "scanning" || st === "idle" ? "Scanning"
                               : st === "nostick" ? "No stick"
                               : st === "many" ? "Refused"
                               : st === "error" || st === "unreadable" ? "Error" : "No bundle"
                    tone: st === "ready" ? t.warn
                          : st === "same-version" ? t.ok
                          : st === "one" || st === "many" || st === "error" || st === "unreadable" ? t.bad : t.sub
                }
            }

            // ---- right: what can be done now ------------------------------------
            Rectangle {
                id: imagePanel
                visible: imageUpdate.supported
                anchors.top: imageLeft.top; anchors.topMargin: 34 * s
                anchors.right: parent.right
                anchors.left: imageLeft.right; anchors.leftMargin: 30 * s
                anchors.bottom: parent.bottom
                radius: 22 * s
                color: t.card
                border.color: t.border

                readonly property string st: imageUpdate.scanState
                readonly property bool idle: imageUpdate.state === "idle"
                readonly property bool verifying: imageUpdate.lastOutcome === "candidate-armed"

                Item {
                    anchors.fill: parent
                    anchors.margins: 34 * s

                    // Looking at the stick
                    Column {
                        visible: imagePanel.idle && !imagePanel.verifying
                                 && (imagePanel.st === "scanning" || imagePanel.st === "idle")
                        anchors.centerIn: parent
                        width: parent.width
                        spacing: 22 * s
                        Spinner { anchors.horizontalCenter: parent.horizontalCenter }
                        Text {
                            width: parent.width; horizontalAlignment: Text.AlignHCenter
                            text: "Looking at the USB stick"
                            color: t.text
                            font.family: t.font; font.pixelSize: 28 * s; font.weight: Font.DemiBold
                        }
                    }

                    // Idle: the offer, or why there is none
                    Column {
                        id: imageIdleView
                        visible: imagePanel.idle && (imagePanel.verifying
                                 || (imagePanel.st !== "scanning" && imagePanel.st !== "idle"))
                        width: parent.width
                        spacing: 18 * s
                        readonly property bool offerReady: imagePanel.st === "ready" && !imagePanel.verifying

                        Row {
                            spacing: 22 * s
                            ResultIcon {
                                tint: imagePanel.verifying ? t.info
                                      : imageIdleView.offerReady ? t.warn
                                      : imagePanel.st === "same-version" ? t.ok
                                      : imagePanel.st === "one" || imagePanel.st === "many" || imagePanel.st === "error" || imagePanel.st === "unreadable" ? t.bad
                                      : t.sub
                                glyph: imagePanel.verifying ? "info"
                                       : imageIdleView.offerReady ? "update"
                                       : imagePanel.st === "same-version" ? "check"
                                       : imagePanel.st === "one" || imagePanel.st === "many" || imagePanel.st === "error" || imagePanel.st === "unreadable" ? "bad"
                                       : "info"
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: imageIdleView.width - 100 * s
                                wrapMode: Text.WordWrap
                                text: imagePanel.verifying ? "The previous update is being verified"
                                      : imageIdleView.offerReady ? "Install image " + imageSection.offer.version
                                      : imagePanel.st === "same-version" ? "Already running this version"
                                      : imagePanel.st === "nostick" ? "No USB stick"
                                      : imagePanel.st === "none" ? "No update bundle on the stick"
                                      : imagePanel.st === "nested" ? "Move the bundle to the top of the stick"
                                      : imagePanel.st === "many" ? "More than one bundle — leave exactly one on the stick"
                                      : imagePanel.st === "one" ? "This bundle cannot be installed"
                                      : imagePanel.st === "unreadable" ? "The USB stick could not be read"
                                      : "The stick could not be scanned"
                                color: t.text
                                font.family: t.font; font.pixelSize: 30 * s; font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            visible: !imageIdleView.offerReady
                            width: parent.width; wrapMode: Text.WordWrap
                            text: imagePanel.verifying ? "Wait a minute and come back. The new image commits itself once it has run healthy for 30 seconds."
                                  : imagePanel.st === "same-version" ? "The stick carries " + imageSection.offer.version + ", which this system already runs."
                                  : imagePanel.st === "nostick" ? "Plug in a USB stick (FAT32, exFAT or NTFS) that holds one .mpupdate bundle at its top level."
                                  : imagePanel.st === "none" ? "Copy one .mpupdate bundle to the top level of the stick."
                                  : imagePanel.st === "many" ? "The installer refuses to choose between bundles. Remove all but one, then scan again."
                                  : imageUpdate.scanDetail
                            color: t.sub
                            font.family: t.font; font.pixelSize: 20 * s
                        }
                        Column {
                            visible: imageIdleView.offerReady
                            width: parent.width
                            spacing: 8 * s
                            Bullet { label: "Replaces " + (imageUpdate.runningVersion || "the running image") + " in the other slot; takes about 5 minutes." }
                            Bullet { label: "Keep the power on: a power cut is safe, but means starting over." }
                            Bullet { label: "The device restarts by itself; the screen goes dark meanwhile." }
                        }
                    }
                    Column {   // idle: actions at the bottom
                        visible: imageIdleView.visible
                        anchors.bottom: parent.bottom
                        width: parent.width
                        spacing: 16 * s

                        // Hold to confirm: a stray tap must not start an image update
                        Rectangle {
                            id: imageHold
                            visible: imageUpdate.canInstall
                            width: parent.width; height: 88 * s; radius: 22 * s
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
                                text: imageHoldArea.pressed ? "Keep holding…"
                                      : (imageUpdate.dryRun ? "Hold to run a dry install" : "Hold to install")
                                color: imageHold.progress > 0.5 ? "#081018" : t.text
                                font.family: t.font; font.pixelSize: 26 * s; font.weight: Font.Bold
                            }
                            NumberAnimation {
                                id: imageHoldAnim
                                target: imageHold; property: "progress"
                                from: 0; to: 1; duration: 1500
                                onFinished: if (imageHold.progress >= 1) imageUpdate.startInstall()
                            }
                            MouseArea {
                                id: imageHoldArea
                                anchors.fill: parent
                                onPressed: imageHoldAnim.restart()
                                onReleased: if (imageHold.progress < 1) { imageHoldAnim.stop(); imageHold.progress = 0 }
                                onCanceled: { imageHoldAnim.stop(); imageHold.progress = 0 }
                            }
                            Connections {
                                target: imageUpdate
                                function onStateChanged() { imageHold.progress = 0 }
                            }
                        }
                        ActionButton {
                            width: parent.width
                            height: 72 * s
                            label: "Scan again"
                            onClicked: imageUpdate.rescan()
                        }
                    }

                    // Installing
                    Column {
                        visible: imageUpdate.state === "installing"
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
                                    text: "Installing image " + (imageSection.offer.version || "")
                                    color: t.text
                                    font.family: t.font; font.pixelSize: 30 * s; font.weight: Font.DemiBold
                                }
                                Text {
                                    text: imageUpdate.phaseText + "   ·   " + imageUpdate.percent + "%   ·   " + mmss(imageUpdate.elapsedSeconds)
                                    color: t.sub
                                    font.family: t.font; font.pixelSize: 19 * s
                                }
                            }
                        }
                        Rectangle {   // determinate: the engine's own progress
                            width: parent.width; height: 14 * s; radius: height / 2
                            color: withAlpha(t.accent, 0.18)
                            Rectangle {
                                width: Math.max(parent.height, parent.width * imageUpdate.percent / 100)
                                height: parent.height; radius: height / 2
                                color: t.accent
                                Behavior on width { NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }
                            }
                        }
                        Flow {
                            width: parent.width
                            spacing: 12 * s
                            Pill { label: "Keep the power on"; tint: t.warn }
                            Pill { label: "Leave the USB stick in"; tint: t.warn }
                        }
                    }

                    // Armed: the engine reboots by itself
                    Column {
                        visible: imageUpdate.state === "arming"
                        anchors.centerIn: parent
                        width: parent.width
                        spacing: 22 * s
                        Spinner { anchors.horizontalCenter: parent.horizontalCenter }
                        Text {
                            width: parent.width; horizontalAlignment: Text.AlignHCenter
                            text: "Rebooting into the new image…"
                            color: t.text
                            font.family: t.font; font.pixelSize: 30 * s; font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                            text: imageUpdate.dryRun ? "Dry run: a real device would restart now."
                                  : "The screen goes dark while the device restarts. It comes back by itself."
                            color: t.sub
                            font.family: t.font; font.pixelSize: 20 * s
                        }
                    }
                    ActionButton {
                        visible: imageUpdate.state === "arming" && imageUpdate.dryRun
                        anchors.bottom: parent.bottom
                        width: parent.width
                        label: "Back to home"
                        onClicked: updater.quitApp()
                    }

                    // Failed
                    Column {
                        visible: imageUpdate.state === "failed"
                        width: parent.width
                        spacing: 18 * s
                        Row {
                            spacing: 22 * s
                            ResultIcon { tint: imageUpdate.failureClass === "" ? t.info : t.bad; glyph: imageUpdate.failureClass === "" ? "info" : "bad" }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: imagePanel.width - 170 * s
                                wrapMode: Text.WordWrap
                                text: imageUpdate.outcomeTitle
                                color: t.text
                                font.family: t.font; font.pixelSize: 30 * s; font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            text: imageUpdate.outcomeDetail
                            color: t.sub
                            font.family: t.font; font.pixelSize: 20 * s
                        }
                    }
                    Column {
                        visible: imageUpdate.state === "failed"
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
                            label: imageUpdate.canRetry ? "Scan again and retry" : "Scan again"
                            onClicked: imageUpdate.acknowledgeFailure()
                        }
                    }
                }
            }
        }

        // ---- left: components --------------------------------------------
        Column {
            id: components
            visible: win.section === "firmware"
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
            visible: win.section === "firmware"
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
                        Bullet { label: "Takes " + aboutText(estimateSeconds) + ". Keep the system switched on." }
                        Bullet { label: "The screen goes dark during the update and stays dark. That is expected." }
                        Bullet { label: "Wait " + darkWaitMinutes + " minutes, then switch the system off and on to finish." }
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
                            onFinished: if (holdButton.progress >= 1) { runEstimate = estimateSeconds; updater.startUpdate() }
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
                                      + (runEstimate > 0 ? "  ·  usually done in " + aboutText(runEstimate) : "")
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
