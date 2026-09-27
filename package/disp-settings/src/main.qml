import QtQuick 2.12
import QtQuick.Window 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

// Display Settings. Same visual language as qt-demo-launcher's "tiles" theme and
// System Manager: navy grid backdrop, header with back button and SMPTE bar strip,
// cards with an accent stripe and icon badge. Colors are RGB565 steps like the
// launcher's, so the 16bpp framebuffer shows them without dithering.
//
// One instance of every card; only their geometry depends on the screen:
// 1920x720 (aspect > 2) gets three columns, 1920x1080 two columns with the
// extra height. Sizes scale with s, designed at 1920x720.
Window {
    id: window
    visible: true
    width: Screen.width
    height: Screen.height
    title: "Display Settings"
    visibility: Window.FullScreen
    color: t.bg

    QtObject {
        id: t
        readonly property color bg: "#080C18"
        readonly property color grid: "#101828"
        readonly property color card: "#182030"
        readonly property color tile: "#1C2638"
        readonly property color cardPressed: "#202C40"
        readonly property color border: "#283450"
        readonly property color text: "#F0F4F8"
        readonly property color sub: "#8894A8"
        readonly property color dim: "#58647A"
        readonly property color ok: "#34D399"
        readonly property color warn: "#FBBF24"
        readonly property color bad: "#F87171"
        readonly property color info: "#38BDF8"
        readonly property color accent: "#60A5FA"
        readonly property string font: uiFont
    }

    // Designed at 1920x720; 1080-line panels keep the same sizes and use the height
    readonly property real s: Math.min(width / 1920, height / 720)

    // Track if user is dragging the brightness slider
    property bool userDraggingBrightness: false
    // Cooldown period after releasing slider - ignore brightness updates during this time
    property bool brightnessSetCooldown: false

    // Timer to clear cooldown after user releases slider
    Timer {
        id: brightnessCooldownTimer
        interval: 500  // 500ms cooldown
        onTriggered: brightnessSetCooldown = false
    }

    Timer {
        id: dualDisplaySliderSyncTimer
        interval: 0
        repeat: false
        onTriggered: syncDualDisplaySlider()
    }

    // Track user's preference for adaptive mode (separate from actual als-dimmer mode)
    // This allows the switch to stay ON even when user temporarily adjusts brightness
    property bool userPreferAdaptive: true
    property bool initialSyncDone: false

    // Adaptive layout: three columns on wide screens (aspect ratio > 2.0)
    property bool wideScreen: Screen.width / Screen.height > 2.0

    function withAlpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    function absoluteNitsText() {
        if (dualDisplay.loadingSavedState || dualDisplay.busy) {
            return "Loading…";
        }
        if (!alsDimmer.connected) {
            return "— nits";
        }
        if (alsDimmer.absoluteBrightnessValid) {
            return alsDimmer.absoluteBrightnessNits.toFixed(1) + " nits";
        }
        return alsDimmer.absoluteBrightnessCalibrated ? "— nits" : "Not calibrated";
    }

    function absoluteNitsColor() {
        if (dualDisplay.loadingSavedState || dualDisplay.busy) {
            return t.sub;
        }
        if (!alsDimmer.connected) {
            return t.sub;
        }
        if (!alsDimmer.absoluteBrightnessValid && alsDimmer.absoluteBrightnessCalibrated) {
            return t.sub;
        }
        return alsDimmer.absoluteBrightnessCalibrated ? t.ok : t.bad;
    }

    function brightnessSliderFrom() {
        return dualDisplay.targetActive ? dualDisplay.minNits : 2;
    }

    function brightnessSliderTo() {
        return dualDisplay.targetActive ? Math.max(dualDisplay.maxNits, dualDisplay.nitsStep) : 100;
    }

    function brightnessSliderStep() {
        return dualDisplay.targetActive ? dualDisplay.nitsStep : 1;
    }

    function brightnessSliderText(value) {
        return dualDisplay.targetActive ? Math.round(value) + " nits" : Math.round(value) + "%";
    }

    function setBrightnessFromSlider(value) {
        if (dualDisplay.active) {
            dualDisplay.setAbsoluteBrightness(value);
        } else {
            alsDimmer.setBrightness(Math.round(value));
        }
    }

    function requestDualDisplaySliderSync() {
        if (dualDisplay.active && !userDraggingBrightness) {
            dualDisplaySliderSyncTimer.restart();
        }
    }

    function syncDualDisplaySlider() {
        if (!dualDisplay.active || userDraggingBrightness) {
            return;
        }
        brightnessSlider.value = dualDisplay.currentNits;
    }

    function setDualDisplayAbsoluteMode(enabled) {
        dualDisplay.setEnabled(enabled, alsDimmer.mode, alsDimmer.brightness);
        if (dualDisplay.targetActive) {
            userPreferAdaptive = false;
            requestDualDisplaySliderSync();
        } else if (!enabled) {
            brightnessSlider.value = alsDimmer.brightness;
        }
    }

    // Sync user preference when we first receive the actual mode from als-dimmer
    Connections {
        target: alsDimmer
        function onConnectedChanged() {
            if (!alsDimmer.connected) {
                // Reset sync flag on disconnect so we re-sync on reconnect
                initialSyncDone = false;
            }
        }
        function onModeChanged() {
            if (dualDisplay.targetActive) {
                return;
            }
            // On first real mode update, sync user preference and slider from actual values
            if (!initialSyncDone && alsDimmer.mode !== "unknown") {
                userPreferAdaptive = (alsDimmer.mode === "auto");
                brightnessSlider.value = alsDimmer.brightness;  // Sync slider value on connect
                initialSyncDone = true;
                console.log("Initial sync: userPreferAdaptive =", userPreferAdaptive,
                            "brightness =", alsDimmer.brightness, "from mode:", alsDimmer.mode);
            }
            // If als-dimmer switches to full auto (e.g., user toggled switch), update preference
            else if (alsDimmer.mode === "auto") {
                userPreferAdaptive = true;
            }
            // When switching to manual mode, sync slider to actual brightness
            // als-dimmer may restore a different manual brightness value
            else if ((alsDimmer.mode === "manual" || alsDimmer.mode === "manual_temporary") && !userDraggingBrightness) {
                brightnessSlider.value = alsDimmer.brightness;
                console.log("Manual mode sync (mode): slider =", alsDimmer.brightness);
            }
        }
        function onBrightnessChanged() {
            if (dualDisplay.targetActive) {
                return;
            }
            // Sync slider when brightness changes in manual/manual_temporary mode
            // This catches external brightness changes (e.g., als-dimmer-client)
            // Skip during cooldown period (right after user released slider)
            if ((alsDimmer.mode === "manual" || alsDimmer.mode === "manual_temporary") && !userDraggingBrightness && !brightnessSetCooldown && initialSyncDone) {
                brightnessSlider.value = alsDimmer.brightness;
                console.log("Manual mode sync (brightness): slider =", alsDimmer.brightness);
            }
        }
    }

    Connections {
        target: dualDisplay
        function onTargetActiveChanged() {
            if (dualDisplay.targetActive) {
                userPreferAdaptive = false;
                requestDualDisplaySliderSync();
            } else if (!userDraggingBrightness) {
                brightnessSlider.value = alsDimmer.brightness;
            }
        }
        function onActiveChanged() {
            if (dualDisplay.active) {
                userPreferAdaptive = false;
                requestDualDisplaySliderSync();
            } else if (!userDraggingBrightness) {
                brightnessSlider.value = alsDimmer.brightness;
            }
        }
        function onCurrentNitsChanged() {
            requestDualDisplaySliderSync();
        }
        function onRangeChanged() {
            requestDualDisplaySliderSync();
        }
    }

    Item {
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: Qt.quit()
    }

    // ---- backdrop grid -----------------------------------------------------
    Repeater {
        model: Math.ceil(window.width / 48)
        Rectangle { x: index * 48 + (window.width % 48) / 2; width: 1; height: window.height; color: t.grid }
    }
    Repeater {
        model: Math.ceil(window.height / 48)
        Rectangle { y: index * 48 + (window.height % 48) / 2; height: 1; width: window.width; color: t.grid }
    }

    // ---- reusable pieces ---------------------------------------------------
    component Pill: Rectangle {
        id: pill
        property string label
        property color tint: t.sub
        property bool dot: false
        signal clicked()
        height: 40 * s
        width: pillRow.implicitWidth + 32 * s
        radius: height / 2
        color: pillArea.pressed ? withAlpha(tint, 0.28) : withAlpha(tint, 0.16)
        border.color: withAlpha(tint, 0.45)
        Behavior on color { ColorAnimation { duration: 200 } }
        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: 10 * s
            Rectangle {
                visible: pill.dot
                anchors.verticalCenter: parent.verticalCenter
                width: 10 * s; height: width; radius: width / 2
                color: pill.tint
            }
            Text {
                text: pill.label
                color: pill.tint
                font.family: t.font; font.pixelSize: 18 * s; font.weight: Font.DemiBold
            }
        }
        MouseArea { id: pillArea; anchors.fill: parent; onClicked: pill.clicked() }
    }

    component ThemedSwitch: Switch {
        id: sw
        property color onColor: t.ok
        property real trackWidth: 66 * s
        property real trackHeight: 38 * s
        padding: 0
        implicitWidth: trackWidth
        implicitHeight: trackHeight
        indicator: Rectangle {
            x: 0
            y: (sw.height - height) / 2
            width: sw.trackWidth; height: sw.trackHeight; radius: height / 2
            color: sw.checked ? withAlpha(sw.onColor, 0.85) : "#2A3448"
            border.color: sw.checked ? sw.onColor : t.border
            opacity: sw.enabled ? 1.0 : 0.35
            Behavior on color { ColorAnimation { duration: 160 } }
            Rectangle {
                x: sw.checked ? parent.width - width - 5 * s : 5 * s
                y: 5 * s
                width: parent.height - 10 * s; height: width; radius: width / 2
                color: "#FFFFFF"
                Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            }
        }
        contentItem: Item {}
    }

    // A card: accent stripe, icon badge and title, then its content in a column.
    component Card: Rectangle {
        id: card
        property string title
        property string icon
        property color tint: t.accent
        property int order: 0
        property bool centerBody: false
        default property alias content: body.data
        property alias headerRight: rightSlot.data
        readonly property real pad: 20 * s
        readonly property real headH: 40 * s
        implicitHeight: pad + headH + 12 * s + body.implicitHeight + pad
        radius: 18 * s
        color: t.card
        border.color: t.border
        opacity: 0

        transform: Translate { id: shift; y: 16 * s }
        SequentialAnimation {
            running: true
            PauseAnimation { duration: 60 + card.order * 70 }
            ParallelAnimation {
                NumberAnimation { target: card; property: "opacity"; to: 1; duration: 320; easing.type: Easing.OutCubic }
                NumberAnimation { target: shift; property: "y"; to: 0; duration: 380; easing.type: Easing.OutCubic }
            }
        }

        Rectangle {   // accent stripe
            x: 0; y: 0; width: 6 * s; height: parent.height; radius: 3 * s
            color: card.tint
        }
        Rectangle {   // icon badge
            id: badge
            x: card.pad + 6 * s; y: card.pad
            width: card.headH; height: width; radius: 12 * s
            color: withAlpha(card.tint, 0.16)
            border.color: withAlpha(card.tint, 0.45)
            Image {
                anchors.centerIn: parent
                width: parent.width * 0.62; height: width
                sourceSize: Qt.size(width, height)
                source: "qrc:/icons/" + card.icon + ".svg"
            }
        }
        Text {
            anchors.left: badge.right; anchors.leftMargin: 16 * s
            anchors.right: rightSlot.left; anchors.rightMargin: 12 * s
            anchors.verticalCenter: badge.verticalCenter
            text: card.title
            elide: Text.ElideRight
            color: t.text
            font.family: t.font; font.pixelSize: 24 * s; font.weight: Font.DemiBold
        }
        Row {
            id: rightSlot
            anchors.right: parent.right; anchors.rightMargin: card.pad
            anchors.verticalCenter: badge.verticalCenter
            spacing: 10 * s
        }
        Column {
            id: body
            x: card.pad + 6 * s
            y: {
                var top = card.pad + card.headH + 12 * s
                if (!card.centerBody) return top
                return top + Math.max(0, (card.height - top - card.pad - implicitHeight) / 2)
            }
            width: card.width - x - card.pad
            spacing: 12 * s
        }
    }

    // Key/value pairs, one or two pairs per row
    component KeyValues: GridLayout {
        id: kv
        property var pairs: []
        property int pairColumns: 1
        property real keyWidth: 130 * s
        width: parent ? parent.width : 0
        columns: pairColumns * 2
        columnSpacing: 14 * s
        rowSpacing: 6 * s
        Repeater {
            model: kv.pairs.length * 2
            Text {
                readonly property var pair: kv.pairs[Math.floor(index / 2)]
                readonly property bool isKey: index % 2 === 0
                Layout.preferredWidth: isKey ? kv.keyWidth : -1
                Layout.fillWidth: !isKey
                Layout.minimumWidth: isKey ? kv.keyWidth : 40 * s
                text: pair ? (isKey ? pair.k : pair.v) : ""
                elide: Text.ElideRight
                color: isKey ? t.sub : (pair && pair.v === "N/A" ? t.dim : t.text)
                font.family: t.font
                font.pixelSize: (isKey ? 16 : 18) * s
                font.weight: isKey ? Font.Normal : Font.Medium
            }
        }
    }

    component SensorTile: Rectangle {
        id: st
        property string label
        property string value
        property string caption
        property bool valid: false
        property color tone: t.dim
        height: 84 * s
        radius: 14 * s
        color: t.tile
        border.color: t.border
        Column {
            anchors.left: parent.left; anchors.leftMargin: 16 * s
            anchors.right: parent.right; anchors.rightMargin: 12 * s
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2 * s
            Row {
                spacing: 8 * s
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10 * s; height: width; radius: width / 2
                    color: st.tone
                }
                Text {
                    // the caption only where the tile has room for it (1080-line layout)
                    text: st.label + (st.caption !== "" && st.width > 240 * s ? "  ·  " + st.caption : "")
                    color: t.sub
                    font.family: t.font; font.pixelSize: 16 * s
                }
            }
            Text {
                text: st.value
                color: st.valid ? t.text : t.dim
                font.family: t.font; font.pixelSize: 28 * s; font.weight: Font.Bold
            }
        }
    }

    component Chip: Rectangle {
        property string label
        property color tint: t.sub
        property bool lit: false
        height: 30 * s
        width: chipText.implicitWidth + 20 * s
        radius: 8 * s
        color: withAlpha(tint, lit ? 0.2 : 0.08)
        border.color: withAlpha(tint, lit ? 0.7 : 0.35)
        Text {
            id: chipText
            anchors.centerIn: parent
            text: parent.label
            color: parent.lit ? parent.tint : t.sub
            font.family: t.font; font.pixelSize: 14 * s; font.weight: Font.Bold
        }
    }

    component FeatureTile: Rectangle {
        id: tile
        property string title
        property string icon
        property color tint: t.accent
        property bool on: false
        property bool available: false
        property string note
        signal toggled(bool checked)
        Layout.fillWidth: true
        Layout.preferredHeight: 62 * s
        radius: 14 * s
        color: tileArea.pressed && available ? t.cardPressed : t.tile
        border.color: on && available ? withAlpha(tint, 0.6) : t.border
        Behavior on border.color { ColorAnimation { duration: 200 } }

        MouseArea {
            id: tileArea
            anchors.fill: parent
            onClicked: if (tile.available) tile.toggled(!tile.on)
        }
        Rectangle {
            id: tileBadge
            x: 10 * s; anchors.verticalCenter: parent.verticalCenter
            width: 38 * s; height: width; radius: 10 * s
            color: withAlpha(tile.tint, tile.available ? 0.18 : 0.07)
            Image {
                anchors.centerIn: parent
                width: parent.width * 0.62; height: width
                sourceSize: Qt.size(width, height)
                source: "qrc:/icons/" + tile.icon + ".svg"
                opacity: tile.available ? 1.0 : 0.35
            }
        }
        Column {
            anchors.left: tileBadge.right; anchors.leftMargin: 10 * s
            anchors.right: tileSwitch.visible ? tileSwitch.left : parent.right
            anchors.rightMargin: 8 * s
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1 * s
            Text {
                width: parent.width; elide: Text.ElideRight
                text: tile.title
                color: tile.available ? t.text : t.sub
                font.family: t.font; font.pixelSize: 16 * s; font.weight: Font.DemiBold
            }
            Text {
                width: parent.width; elide: Text.ElideRight
                text: tile.note !== "" ? tile.note : (tile.on ? "On" : "Off")
                color: tile.note !== "" ? t.dim : (tile.on ? tile.tint : t.sub)
                font.family: t.font; font.pixelSize: 14 * s
            }
        }
        ThemedSwitch {
            id: tileSwitch
            anchors.right: parent.right; anchors.rightMargin: 12 * s
            anchors.verticalCenter: parent.verticalCenter
            // A feature that cannot be switched shows why instead of a dead toggle
            visible: tile.available
            trackWidth: 58 * s; trackHeight: 34 * s
            onColor: tile.tint
            enabled: tile.available
            onClicked: tile.toggled(checked)
        }
        Binding {
            target: tileSwitch
            property: "checked"
            value: tile.on
        }
    }

    component VersionRow: Item {
        id: vr
        property string name
        property string version
        property string slot
        property string date
        property string serial
        property bool alert: false
        property string alertReason
        width: parent ? parent.width : 0
        height: 44 * s
        Column {
            anchors.left: parent.left
            anchors.right: verText.left; anchors.rightMargin: 12 * s
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1 * s
            Text {
                width: parent.width; elide: Text.ElideRight
                text: vr.name
                color: t.text
                font.family: t.font; font.pixelSize: 17 * s; font.weight: Font.Medium
            }
            Text {
                width: parent.width; elide: Text.ElideRight
                text: vr.alert && vr.alertReason !== "" ? vr.alertReason
                      : vr.date + (vr.serial !== "" ? "   ·   " + vr.serial : "")
                color: vr.alert ? t.bad : t.dim
                font.family: t.font; font.pixelSize: 14 * s
            }
        }
        Text {
            id: verText
            anchors.right: slotChip.visible ? slotChip.left : parent.right
            anchors.rightMargin: slotChip.visible ? 10 * s : 0
            anchors.verticalCenter: parent.verticalCenter
            text: vr.version !== "" ? vr.version : "—"
            color: vr.alert ? t.bad : t.text
            font.family: t.font; font.pixelSize: 22 * s; font.weight: Font.Bold
        }
        Chip {
            id: slotChip
            visible: vr.slot !== ""
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            label: "slot " + vr.slot
            tint: t.accent
            lit: true
        }
    }

    // ---- page ----------------------------------------------------------------
    Item {
        id: page
        anchors.fill: parent
        anchors.leftMargin: 40 * s; anchors.rightMargin: 40 * s
        anchors.topMargin: 22 * s; anchors.bottomMargin: 24 * s

        // Header: back, title, status
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
                MouseArea {
                    id: backArea
                    anchors.fill: parent
                    // Quit on press: a release event is not guaranteed on every touch panel
                    onPressed: {
                        console.log("Back pressed - quitting");
                        Qt.quit();
                    }
                }
            }
            Column {
                anchors.left: backButton.right; anchors.leftMargin: 28 * s
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4 * s
                Text {
                    text: "Display Settings"
                    color: t.text
                    font.family: t.font; font.pixelSize: 36 * s; font.weight: Font.Bold
                }
                Text {
                    text: "Home  ›  Display Settings"
                    color: t.sub
                    font.family: t.font; font.pixelSize: 19 * s
                }
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14 * s
                Pill {
                    dot: true
                    label: alsDimmer.connected ? "Dimming service" : "Dimming service offline"
                    tint: alsDimmer.connected ? t.ok : t.bad
                    onClicked: if (!alsDimmer.connected) alsDimmer.reconnect()
                }
                Pill { label: "v" + appVersion; tint: t.sub }
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

        // ---- cards -----------------------------------------------------------
        Item {
            id: board
            anchors.top: strip.bottom; anchors.topMargin: 20 * s
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: parent.bottom

            readonly property real gap: 18 * s
            // wide: three columns; tall: two
            readonly property real w0: wideScreen ? (width - 2 * gap) * 0.335 : (width - gap) * 0.53
            readonly property real w1: wideScreen ? (width - 2 * gap) * 0.35 : width - gap - w0
            readonly property real w2: wideScreen ? width - 2 * gap - w0 - w1 : 0
            readonly property real x1: w0 + gap
            readonly property real x2: w0 + w1 + 2 * gap

            // ---- Brightness ----------------------------------------------------
            Card {
                id: brightnessCard
                order: 0
                title: "Brightness"
                icon: "brightness"
                tint: t.warn
                centerBody: true
                x: 0; y: 0
                width: board.w0
                height: board.height - healthCard.height - board.gap
                        - (wideScreen ? 0 : fpgaCard.height + board.gap)

                headerRight: [
                    Pill {
                        label: alsDimmer.mode === "auto" ? "Auto" :
                               alsDimmer.mode === "manual_temporary" ? "Temporary" : "Manual"
                        tint: alsDimmer.mode === "auto" ? t.ok :
                              alsDimmer.mode === "manual_temporary" ? t.warn : t.accent
                    }
                ]

                // Readout
                Row {
                    spacing: 20 * s
                    Text {
                        text: brightnessSliderText(brightnessSlider.value)
                        color: t.text
                        font.family: t.font; font.pixelSize: 56 * s; font.weight: Font.Bold
                    }
                    Text {
                        anchors.baseline: parent.children[0].baseline
                        text: absoluteNitsText()
                        color: absoluteNitsColor()
                        font.family: t.font; font.pixelSize: 24 * s; font.weight: Font.DemiBold
                    }
                }

                Slider {
                    id: brightnessSlider
                    width: parent.width
                    height: 48 * s  // Touch-friendly height
                    from: brightnessSliderFrom()
                    to: brightnessSliderTo()
                    value: 50  // Initial default, will be set on connect
                    enabled: !dualDisplay.loadingSavedState && !dualDisplay.busy &&
                             (dualDisplay.targetActive || alsDimmer.connected)
                    stepSize: brightnessSliderStep()
                    padding: 0
                    leftPadding: 24 * s; rightPadding: 24 * s

                    Binding {
                        target: brightnessSlider
                        property: "value"
                        value: dualDisplay.currentNits
                        when: dualDisplay.active && !userDraggingBrightness
                    }

                    // Only sync from controller in auto mode
                    // In manual mode, slider stays where user put it (no binding)
                    Binding {
                        target: brightnessSlider
                        property: "value"
                        value: alsDimmer.brightness
                        when: !dualDisplay.targetActive && !userDraggingBrightness && alsDimmer.mode === "auto"
                    }

                    background: Rectangle {
                        x: brightnessSlider.leftPadding
                        y: brightnessSlider.topPadding + brightnessSlider.availableHeight / 2 - height / 2
                        width: brightnessSlider.availableWidth
                        height: 14 * s
                        radius: height / 2
                        color: "#2A3448"

                        Rectangle {
                            width: Math.max(parent.height, brightnessSlider.visualPosition * parent.width)
                            height: parent.height
                            radius: height / 2
                            opacity: brightnessSlider.enabled ? 1.0 : 0.35
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: "#B45309" }
                                GradientStop { position: 1.0; color: t.warn }
                            }
                        }
                    }

                    handle: Rectangle {
                        x: brightnessSlider.leftPadding + brightnessSlider.visualPosition * (brightnessSlider.availableWidth) - width / 2
                        y: brightnessSlider.topPadding + brightnessSlider.availableHeight / 2 - height / 2
                        width: 44 * s
                        height: width
                        radius: width / 2
                        color: "#FFFFFF"
                        border.color: brightnessSlider.enabled ? t.warn : t.dim
                        border.width: 4 * s
                        scale: brightnessSlider.pressed ? 1.12 : 1.0
                        Behavior on scale { NumberAnimation { duration: 120 } }
                    }

                    onPressedChanged: {
                        userDraggingBrightness = pressed;
                        if (!pressed) {
                            // Final update on release
                            setBrightnessFromSlider(value);
                            // Start cooldown to ignore brightness feedback briefly
                            if (!dualDisplay.targetActive) {
                                brightnessSetCooldown = true;
                                brightnessCooldownTimer.restart();
                            }
                        }
                    }

                    onMoved: {
                        // Responsive updates while dragging
                        setBrightnessFromSlider(value);
                    }
                }

                // Modes and ambient light
                Item {
                    width: parent.width
                    height: 48 * s

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 14 * s

                        ThemedSwitch {
                            id: adaptiveSwitch
                            anchors.verticalCenter: parent.verticalCenter
                            checked: userPreferAdaptive
                            enabled: alsDimmer.connected && !dualDisplay.loadingSavedState &&
                                     !dualDisplay.targetActive && !dualDisplay.busy
                            onClicked: {
                                userPreferAdaptive = checked;
                                alsDimmer.setAdaptiveMode(checked);
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Adaptive"
                            color: adaptiveSwitch.enabled ? t.text : t.sub
                            font.family: t.font; font.pixelSize: 19 * s; font.weight: Font.Medium
                        }

                        Item { width: 10 * s; height: 1; visible: dualAbsoluteSwitch.visible }

                        ThemedSwitch {
                            id: dualAbsoluteSwitch
                            anchors.verticalCenter: parent.verticalCenter
                            visible: dualDisplay.hardwareAvailable || dualDisplay.targetActive
                            checked: dualDisplay.targetActive
                            onColor: t.info
                            enabled: alsDimmer.connected && dualDisplay.hardwareAvailable &&
                                     !dualDisplay.loadingSavedState && !dualDisplay.busy
                            onClicked: {
                                setDualDisplayAbsoluteMode(checked);
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: dualAbsoluteSwitch.visible
                            text: "Dual display"
                            color: dualAbsoluteSwitch.enabled ? t.text : t.sub
                            font.family: t.font; font.pixelSize: 19 * s; font.weight: Font.Medium
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10 * s
                        Chip {
                            label: alsDimmer.connected ? alsDimmer.luxValue.toFixed(1) + " lux" : "— lux"
                            tint: t.warn
                            lit: alsDimmer.connected
                        }
                        Chip {
                            label: alsDimmer.connected ? alsDimmer.zone : "—"
                            tint: t.info
                            lit: alsDimmer.connected
                        }
                    }
                }
            }

            // ---- Temperatures and panel power --------------------------------
            Card {
                id: healthCard
                order: 1
                title: "Temperatures"
                icon: "health"
                tint: "#FB923C"
                x: 0
                y: brightnessCard.height + board.gap
                width: board.w0
                height: implicitHeight

                RowLayout {
                    width: parent.width
                    spacing: 12 * s
                    SensorTile {
                        Layout.fillWidth: true
                        label: "Sensor 1"
                        valid: tempSensors.sensor1Available
                        value: tempSensors.sensor1Available ? tempSensors.sensor1Temp.toFixed(1) + " °C" : "N/A"
                        caption: tempSensors.sensor1Available && tempSensors.sensor1Id ? tempSensors.sensor1Id : ""
                        tone: !tempSensors.sensor1Available ? t.dim :
                               tempSensors.sensor1Healthy ? t.ok : t.bad
                    }
                    SensorTile {
                        Layout.fillWidth: true
                        label: "Sensor 2"
                        valid: tempSensors.sensor2Available
                        value: tempSensors.sensor2Available ? tempSensors.sensor2Temp.toFixed(1) + " °C" : "N/A"
                        caption: tempSensors.sensor2Available && tempSensors.sensor2Id ? tempSensors.sensor2Id : ""
                        tone: !tempSensors.sensor2Available ? t.dim :
                               tempSensors.sensor2Healthy ? t.ok : t.bad
                    }
                    // Backlight temperature (MCU 0x66)
                    SensorTile {
                        Layout.fillWidth: true
                        visible: mcu.available
                        label: "Backlight"
                        valid: mcu.backlightTempValid
                        value: mcu.backlightTempValid ? mcu.backlightTemp.toFixed(1) + " °C" : "N/A"
                        caption: "MCU NTC"
                        tone: mcu.backlightTempValid ? t.ok : t.dim
                    }
                }

                // RTQ6749 PMIC (only when the IOC MCU is reachable)
                Item {
                    visible: pmic.available
                    width: parent.width
                    height: pmicFlow.height
                    Flow {
                        id: pmicFlow
                        width: parent.width
                        spacing: 6 * s
                        Text {
                            height: 30 * s
                            verticalAlignment: Text.AlignVCenter
                            text: "Panel power"
                            color: t.sub
                            font.family: t.font; font.pixelSize: 15 * s
                        }
                        Chip {
                            label: pmic.faultSummary
                            tint: pmic.statusOk ? t.ok : t.bad
                            lit: true
                        }
                        Repeater {
                            model: [
                                { label: "PAVDD", on: pmic.chPavdd, fault: pmic.faultPavdd },
                                { label: "NAVDD", on: pmic.chNavdd, fault: pmic.faultNavdd },
                                { label: "VGH",   on: pmic.chVgh,   fault: pmic.faultVgh },
                                { label: "VGL",   on: pmic.chVgl,   fault: pmic.faultVgl },
                                { label: "VCOM",  on: pmic.chVcom,  fault: false },
                                { label: "RESET", on: pmic.chReset, fault: false }
                            ]
                            delegate: Chip {
                                label: modelData.label
                                tint: modelData.fault ? t.bad : t.ok
                                lit: modelData.fault || modelData.on
                            }
                        }
                        Repeater {
                            model: [
                                { label: "OTP", on: pmic.protOtp },
                                { label: "UVP", on: pmic.protUvp },
                                { label: "SCP", on: pmic.protScp }
                            ]
                            delegate: Chip {
                                label: modelData.label
                                tint: modelData.on ? t.ok : t.warn
                                lit: true
                            }
                        }
                    }
                }
            }

            // ---- Panel features (FPGA) ---------------------------------------
            Card {
                id: featuresCard
                order: 2
                title: "Panel features"
                icon: "features"
                tint: "#A78BFA"
                x: board.x1; y: 0
                width: board.w1
                height: implicitHeight

                GridLayout {
                    width: parent.width
                    columns: 2
                    columnSpacing: 12 * s
                    rowSpacing: 12 * s

                    FeatureTile {
                        title: "Privacy"
                        icon: "privacy"
                        tint: "#A78BFA"
                        // TODO: Re-enable after testing: fpga.privacyMode / fpga.setPrivacyMode()
                        on: false
                        available: false
                        note: "Disabled"
                    }
                    FeatureTile {
                        title: "Local dimming"
                        icon: "dimming"
                        tint: t.ok
                        on: fpga.localDimmingEnabled
                        available: fpga.connected && fpga.localDimmingSupported
                        note: !fpga.connected ? "FPGA not connected"
                              : !fpga.localDimmingSupported ? "Not supported" : ""
                        onToggled: fpga.setLocalDimming(checked)
                    }
                    FeatureTile {
                        title: "Pixel compensation"
                        icon: "pixelcomp"
                        tint: t.info
                        on: fpga.pixelCompEnabled
                        // Pixel compensation only meaningful while local dimming is on
                        available: fpga.connected && fpga.pixelCompSupported && fpga.localDimmingEnabled
                        note: !fpga.connected ? "FPGA not connected"
                              : !fpga.pixelCompSupported ? "Not supported"
                              : !fpga.localDimmingEnabled ? "Needs local dimming" : ""
                        onToggled: fpga.setPixelCompensation(checked)
                    }
                    FeatureTile {
                        title: "Vision booster"
                        icon: "visionboost"
                        tint: t.warn
                        on: false
                        available: false
                        note: "Disabled"
                    }
                }
            }

            // ---- Firmware versions -----------------------------------------------
            Card {
                id: firmwareCard
                order: 3
                title: "Firmware"
                icon: "firmware"
                tint: t.accent
                x: board.x1
                y: featuresCard.height + board.gap
                width: board.w1
                height: wideScreen ? board.height - featuresCard.height - board.gap : implicitHeight

                VersionRow {
                    name: "OS image"
                    version: osVersion
                    date: osBuildDate.substring(0, 10)
                }
                VersionRow {
                    name: "Applications"
                    version: swVersion
                    date: swBuildDate.substring(0, 10)
                }
                VersionRow {
                    visible: mcu.available
                    name: "Display controller"
                    version: mcu.firmwareVersion
                    slot: mcu.activeSlot
                    date: mcu.buildDateTime.substring(0, 10)
                    serial: mcu.shortSerial
                    alert: mcu.versionAlert
                    alertReason: mcu.versionAlertReason
                }
                VersionRow {
                    visible: hh983.available
                    name: "983HH serializer"
                    version: hh983.firmwareVersion
                    slot: hh983.activeSlot
                    date: hh983.buildDateTime.substring(0, 10)
                    serial: hh983.shortSerial
                    alert: hh983.versionAlert
                    alertReason: hh983.versionAlertReason
                }
            }

            // ---- FPGA ------------------------------------------------------------
            Card {
                id: fpgaCard
                order: 4
                title: "FPGA"
                icon: "fpga"
                tint: t.ok
                x: wideScreen ? board.x2 : 0
                y: wideScreen ? 0 : healthCard.y + healthCard.height + board.gap
                width: wideScreen ? board.w2 : board.w0
                height: implicitHeight

                headerRight: [
                    Pill {
                        dot: true
                        label: fpga.connected ? "Connected" : "Not connected"
                        tint: fpga.connected ? t.ok : t.bad
                        onClicked: fpga.refresh()
                    }
                ]

                KeyValues {
                    keyWidth: 150 * s
                    pairColumns: 1
                    pairs: [
                        { k: "Firmware version", v: fpga.connected ? fpga.firmwareVersion : "N/A" },
                        { k: "Build date", v: !fpga.connected ? "N/A" : (fpga.buildTimeValid ? fpga.buildDateTime : fpga.buildDate) },
                        { k: "Firmware ID", v: fpga.connected ? fpga.firmwareId : "N/A" },
                        { k: "Board type", v: fpga.connected ? fpga.boardType : "N/A" },
                        { k: "Display", v: fpga.connected ? fpga.displaySize + "  " + fpga.displayResolution : "N/A" }
                    ]
                }
            }

            // ---- Touch controller ------------------------------------------------
            Card {
                id: touchCard
                order: 5
                title: "Touch controller"
                icon: "touch"
                tint: t.info
                x: wideScreen ? board.x2 : board.x1
                y: wideScreen ? fpgaCard.height + board.gap : firmwareCard.y + firmwareCard.height + board.gap
                width: wideScreen ? board.w2 : board.w1
                height: board.height - y

                headerRight: [
                    Pill {
                        dot: true
                        label: tddi.available ? "Available" : "Not available"
                        tint: tddi.available ? t.ok : t.bad
                        onClicked: tddi.refresh()
                    }
                ]

                KeyValues {
                    keyWidth: 104 * s
                    pairColumns: 2
                    pairs: [
                        { k: "IC type", v: tddi.available && tddi.icType ? tddi.icType : "N/A" },
                        { k: "Firmware", v: tddi.available && tddi.fwVersion ? tddi.fwVersion : "N/A" },
                        { k: "Display cfg", v: tddi.available && tddi.displayConfig ? tddi.displayConfig : "N/A" },
                        { k: "Touch cfg", v: tddi.available && tddi.touchConfig ? tddi.touchConfig : "N/A" },
                        { k: "Customer", v: tddi.available && tddi.customer ? tddi.customer : "N/A" },
                        { k: "Project", v: tddi.available && tddi.project ? tddi.project : "N/A" },
                        { k: "Panel", v: tddi.available && tddi.panelVersion ? tddi.panelVersion : "N/A" },
                        { k: "Config date", v: tddi.available && tddi.configDate ? tddi.configDate : "N/A" }
                    ]
                }
            }
        }
    }

    Component.onCompleted: {
        console.log("disp-settings UI loaded");
        console.log("Screen size:", Screen.width, "x", Screen.height);
        console.log("Aspect ratio:", (Screen.width / Screen.height).toFixed(2), "- using", wideScreen ? "three-column" : "two-column", "layout");
    }
}
