import QtQuick 2.12
import QtQuick.Window 2.12

// Network: what every interface is doing, WiFi, the wired ports, diagnostics.
// Same visual language as qt-demo-launcher's "tiles" theme and System
// Manager: navy grid backdrop, header with the SMPTE bar strip, cards with an
// accent stripe, icon badge and status pill. Background colours are RGB565
// steps, so the 16bpp framebuffer shows them without dithering.
Window {
    id: win
    visible: true
    visibility: Window.FullScreen
    color: t.bg
    title: "Network"

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
        readonly property color violet: "#A78BFA"
        readonly property string font: uiFont
    }

    // Designed at 1920x720; 1080-line panels keep the same sizes
    readonly property real s: Math.min(width / 1920, height / 720)
    // overview | wifi | wired | tools
    property string section: initialSection
    readonly property bool nmMissing: status.checked && !status.available
    readonly property var summary: status.summary
    readonly property string radio: summary.wifi || "absent"
    readonly property var wlan: {
        var list = status.interfaces
        for (var i = 0; i < list.length; ++i) if (list[i].type === "wifi") return list[i]
        return null
    }

    // The shown section has its data (main.cpp grabs a screenshot once
    // shotReady, i.e. this and any --open-sheet overlay)
    readonly property bool dataReady: {
        if (!status.checked) return false
        if (nmMissing) return true
        if (!status.loaded) return false
        if (section === "wifi") return radio !== "on" || wifi.scanned
        return true
    }
    readonly property bool shotReady: dataReady && (initialSheet === "" || sheetOpened)
                                      && (initialSheet !== "probe-warning" || (!!wport && !!wired.probes[wport.name]
                                                                               && wired.probes[wport.name].state === "done"))

    // A WiFi change runs: the app stays until it has finished or restored
    readonly property bool changing: wifi.busyState === "connecting" || wifi.busyState === "working"
    onSectionChanged: sectionHooks()
    Component.onCompleted: sectionHooks()
    function sectionHooks() {
        wifi.setActive(section === "wifi")
        wired.setActive(section === "wired")
        status.setLeasesWanted(section === "overview" || section === "wired")
    }

    // ---- Wired: the selected port and the settings being edited ("draft")
    property string wiredPort: ""
    readonly property var wiredPorts: {
        var out = [], list = status.interfaces
        for (var i = 0; i < list.length; ++i) if (list[i].type === "ethernet") out.push(list[i])
        return out
    }
    readonly property var wport: {
        for (var i = 0; i < wiredPorts.length; ++i) if (wiredPorts[i].name === wiredPort) return wiredPorts[i]
        return wiredPorts.length > 0 ? wiredPorts[0] : null
    }
    property var draft: ({ mode: "client", ip: "", prefix: "24", gateway: "", dns: "" })
    property bool draftDirty: false
    onWportChanged: if (wport && (!draftDirty || draft.iface !== wport.name) && wired.busyState !== "applying") resetDraft()

    function currentMode(p) { return !p ? "client" : p.mode === "legacy-server" ? "server" : p.mode === "off" ? "client" : p.mode }
    function resetDraft() {
        var p = wport
        if (!p) return
        var m = currentMode(p)
        draft = { iface: p.name, mode: m,
                  ip: m === "client" ? (p.ip || "") : (p.cfgip || p.ip || ""),
                  prefix: m === "client" ? (p.prefix || "24") : (p.cfgprefix || p.prefix || "24"),
                  gateway: m === "static" ? (p.cfggateway || "") : (p.gateway || ""),
                  dns: m === "static" ? (p.cfgdns || "") : (p.dns || "") }
        draftDirty = false
    }
    function setDraft(key, value) {
        var d = {}
        for (var k in draft) d[k] = draft[k]
        d[key] = value
        draft = d
        draftDirty = true
    }
    function setDraftMode(m) {
        var p = wport
        if (!p || draft.mode === m) return
        var d = { iface: p.name, mode: m, ip: draft.ip, prefix: draft.prefix, gateway: draft.gateway, dns: draft.dns }
        if (m === "server") {
            d.ip = currentMode(p) === "server" && p.cfgip ? p.cfgip : suggestServerIp(p)
            d.prefix = "24"; d.gateway = ""; d.dns = ""
        } else if (m === "static") {
            if (currentMode(p) === "static") { d.ip = p.cfgip; d.prefix = p.cfgprefix; d.gateway = p.cfggateway; d.dns = p.cfgdns }
            else { d.ip = p.ip || ""; d.prefix = p.prefix || "24"; d.gateway = p.gateway || ""; d.dns = p.dns || "" }
        }
        draft = d
        draftDirty = true
    }

    function ipNum(a) {
        var p = String(a).split(".")
        return ((parseInt(p[0]) * 256 + parseInt(p[1])) * 256 + parseInt(p[2])) * 256 + parseInt(p[3])
    }
    function ipOk(a) {
        if (!/^[0-9]{1,3}(\.[0-9]{1,3}){3}$/.test(a)) return false
        var p = a.split(".")
        for (var i = 0; i < 4; ++i) if (parseInt(p[i]) > 255) return false
        return true
    }
    function prefixOk(v) { return /^[0-9]{1,2}$/.test(v) && parseInt(v) >= 1 && parseInt(v) <= 30 }
    function dnsOk(v) {
        if (v === "") return true
        var parts = v.split(",")
        if (parts.length > 3) return false
        for (var i = 0; i < parts.length; ++i) if (!ipOk(parts[i])) return false
        return true
    }
    function maskText(prefix) {
        var n = parseInt(prefix), out = []
        for (var i = 0; i < 4; ++i) { var b = Math.max(0, Math.min(8, n - 8 * i)); out.push(256 - Math.pow(2, 8 - b)) }
        return out.join(".")
    }
    function sameNet(a, pa, b, pb) {
        var bits = Math.min(parseInt(pa), parseInt(pb)), div = Math.pow(2, 32 - bits)
        return Math.floor(ipNum(a) / div) === Math.floor(ipNum(b) / div)
    }
    // Another interface whose subnet overlaps a.b.c.d/n (plan 4.3), or null
    function overlapWith(iface, ip, prefix) {
        var list = status.interfaces
        for (var i = 0; i < list.length; ++i) {
            var f = list[i]
            if (f.name === iface || !f.ip || !f.prefix) continue
            if (sameNet(ip, prefix, f.ip, f.prefix)) return f
        }
        return null
    }
    // 192.168.50.1 for the first serving port, .51.1 for the next (plan 4.3)
    function suggestServerIp(p) {
        for (var k = 50; k < 100; ++k) {
            var ip = "192.168." + k + ".1", taken = overlapWith(p.name, ip, 24) !== null
            for (var i = 0; i < wiredPorts.length && !taken; ++i) {
                var o = wiredPorts[i]
                if (o.name !== p.name && currentMode(o) === "server" && o.cfgip && sameNet(o.cfgip, 24, ip, 24)) taken = true
            }
            if (!taken) return ip
        }
        return "192.168.50.1"
    }
    readonly property string draftError: {
        var d = draft, p = wport
        if (!p || d.mode === "client") return ""
        if (!ipOk(d.ip)) return "Enter the address, e.g. 192.168.50.1"
        var last = parseInt(d.ip.split(".")[3])
        if (last === 0 || last === 255) return d.ip + " is not a host address"
        if (d.mode === "static") {
            if (!prefixOk(d.prefix)) return "The prefix is 1 to 30"
            if (d.gateway !== "" && !ipOk(d.gateway)) return "The gateway is not an address"
            if (d.gateway !== "" && !sameNet(d.ip, d.prefix, d.gateway, d.prefix)) return "The gateway is not in " + d.ip + "/" + d.prefix
            if (!dnsOk(d.dns)) return "DNS: up to three addresses, separated by commas"
        }
        if (d.mode === "server") {
            var o = overlapWith(p.name, d.ip, 24)
            if (o) return d.ip + "/24 overlaps " + o.name + "'s " + o.ip + "/" + o.prefix + ": choose another subnet"
        }
        return ""
    }
    readonly property bool draftChanged: {
        var d = draft, p = wport
        if (!p) return false
        if (p.mode === "legacy-server") return true
        var m = currentMode(p)
        if (d.mode !== m) return true
        if (m === "static") return d.ip !== p.cfgip || String(d.prefix) !== String(p.cfgprefix)
                                   || d.gateway !== (p.cfggateway || "") || d.dns !== (p.cfgdns || "")
        if (m === "server") return d.ip !== p.cfgip
        return false
    }
    // The line above Apply: what happens, and the address that goes away
    function whatHappens() {
        var d = draft, p = wport
        if (!p) return ""
        if (!draftChanged) return "These are the settings " + p.name + " has. Nothing to apply."
        var t
        if (d.mode === "client") t = p.name + " gets its address from the network (DHCP client)."
        else if (d.mode === "static") t = p.name + " uses the fixed address " + d.ip + "/" + d.prefix
                                          + (d.gateway ? ", gateway " + d.gateway : ", no gateway") + "."
        else t = p.name + " serves addresses " + d.ip.split(".").slice(0, 3).join(".") + ".10–254 as " + d.ip
                 + " (one-hour leases, no gateway announced)."
        if (p.mode === "legacy-server") t += " The panel menu's DHCP server on this port stops."
        if (parseInt(p.carrier) !== 1) return t + " No cable: the settings are saved and used when one is plugged in."
        if (p.ip && (d.mode === "client" || p.ip !== d.ip))
            t += " This rig will no longer be reachable at " + p.ip + " on " + p.name + "."
        if (p["default"] === "1" && (d.mode === "server" || (d.mode === "static" && !d.gateway)))
            t += " It carries this rig's default route, which goes away."
        return t
    }
    // WiFi: is this rig reached over WiFi? (report v1, 6.13)
    readonly property string wifiReachNote: {
        var w = wlan
        if (!w || w.state !== "connected" || !w.ip) return ""
        var others = 0, list = status.interfaces
        for (var i = 0; i < list.length; ++i) if (list[i].name !== w.name && list[i].ip) ++others
        if (summary.defaultdev === w.name)
            return "This rig's default route uses WiFi (" + w.ip + "): Disconnect, Forget or WiFi off drop it."
        if (others === 0)
            return "WiFi is this rig's only address (" + w.ip + "): Disconnect, Forget or WiFi off cut it off."
        return ""
    }

    function withAlpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    function rateText(bits) {
        if (bits >= 1e7) return Math.round(bits / 1e6) + " Mbit/s"
        if (bits >= 1e6) return (bits / 1e6).toFixed(1) + " Mbit/s"
        if (bits >= 1e3) return Math.round(bits / 1e3) + " kbit/s"
        return Math.round(bits) + " bit/s"
    }
    function speedText(mbit) {
        var n = parseInt(mbit)
        if (!(n > 0)) return ""
        return n >= 1000 ? (n / 1000) + " Gbit/s link" : n + " Mbit/s link"
    }
    function durationText(sec) {
        var n = parseInt(sec)
        if (!(n > 0)) return ""
        if (n % 86400 === 0) return (n / 86400) + (n === 86400 ? " day" : " days")
        if (n >= 3600) return Math.round(n / 3600) + " h"
        return Math.round(n / 60) + " min"
    }
    function bandText(b) { return b ? b + " GHz" : "" }
    function securityText(sec) {
        switch (sec) {
        case "open": return "Open"
        case "wpa2": return "WPA2"
        case "wpa3": return "WPA3"
        case "enterprise": return "Enterprise"
        default: return "Other security"
        }
    }
    function modeText(m) {
        switch (m) {
        case "client": return "DHCP client"
        case "static": return "Fixed address"
        case "server": return "DHCP server"
        case "legacy-server": return "DHCP server (set from the panel menu)"
        default: return "Not configured"
        }
    }
    function carrierOf(f) {
        var r = status.rates[f.name]
        return r && r.carrier !== undefined ? r.carrier : parseInt(f.carrier)
    }
    // The role pill of an interface card: label and tint
    function role(f) {
        if (!f) return { label: "", tint: t.dim }
        // net-ctl.sh's own check per port (inet=yes|no); NetworkManager's word otherwise
        var internet = f.inet === "yes" || (f.inet === "" && summary.internet === "yes" && f["default"] === "1")
        var dead = f.inet === "no"
        if (f.type === "wifi") {
            if (radio !== "on") return { label: "Off", tint: t.dim }
            if (f.state === "connecting") return { label: "Connecting…", tint: t.info }
            if (f.state === "connected") return internet ? { label: "Internet", tint: t.ok }
                                              : dead ? { label: "Connected, no internet", tint: t.warn }
                                              : { label: "Connected", tint: t.info }
            return { label: "Not connected", tint: t.dim }
        }
        if (carrierOf(f) !== 1) return { label: "No cable", tint: t.dim }
        if (f.mode === "server" || f.mode === "legacy-server") return { label: "Serving addresses", tint: t.accent }
        if (f.state === "connecting") return { label: "Connecting…", tint: t.info }
        if (f.state === "connected") {
            if (internet) return { label: "Internet", tint: t.ok }
            if (dead) return { label: "Connected, no internet", tint: t.warn }
            if (f.mode === "static") return { label: "Fixed address", tint: t.sub }
            return { label: "Connected", tint: t.info }
        }
        if (f.state === "unmanaged") return { label: "Not managed", tint: t.dim }
        return { label: "Not connected", tint: t.dim }
    }
    function iconOf(f) { return f && f.type === "wifi" ? "wifi" : "ethernet" }

    Item {
        id: keyHandler
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: {
            if (sheet.mode !== "") sheet.close()
            else if (!win.changing) Qt.quit()
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
        property bool danger: false
        signal clicked()
        height: 84 * s
        width: btnText.implicitWidth + 64 * s
        radius: 20 * s
        opacity: enabled ? 1 : 0.4
        color: primary ? (btnArea.pressed ? Qt.darker(t.accent, 1.2) : t.accent)
                       : (btnArea.pressed ? t.cardPressed : "transparent")
        border.color: primary ? t.accent : danger ? withAlpha(t.bad, 0.7) : t.border
        border.width: 2
        Text {
            id: btnText
            anchors.centerIn: parent
            text: parent.label
            color: parent.primary ? "#081018" : parent.danger ? t.bad : t.text
            font.family: t.font; font.pixelSize: (parent.height < 70 * s ? 21 : 24) * s; font.weight: Font.DemiBold
        }
        MouseArea { id: btnArea; anchors.fill: parent; onClicked: parent.clicked() }
    }

    // Hold to confirm (1.5 s): a stray tap must not cut the rig off the network
    component HoldButton: Rectangle {
        id: hb
        property string label
        property color tint: t.accent
        signal done()
        height: 80 * s; radius: 22 * s
        opacity: enabled ? 1 : 0.4
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
            font.family: t.font; font.pixelSize: 24 * s; font.weight: Font.Bold
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
            enabled: hb.enabled
            onPressed: hbAnim.restart()
            onReleased: if (hb.progress < 1) { hbAnim.stop(); hb.progress = 0 }
            onCanceled: { hbAnim.stop(); hb.progress = 0 }
        }
    }

    // A tappable value of the Wired section: opens the numeric pad
    component FieldTile: Rectangle {
        id: ft
        property string label
        property string value
        property string note
        property bool editable: true
        property bool bad: false
        signal tapped()
        height: 92 * s
        radius: 16 * s
        color: ftArea.pressed && editable ? t.cardPressed : editable ? "#101828" : "transparent"
        border.color: bad ? withAlpha(t.bad, 0.8) : editable ? t.border : withAlpha(t.border, 0.6)
        Column {
            anchors.left: parent.left; anchors.leftMargin: 20 * s
            anchors.right: parent.right; anchors.rightMargin: 16 * s
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4 * s
            Text {
                text: ft.label
                color: t.sub
                font.family: t.font; font.pixelSize: 15 * s; font.weight: Font.DemiBold; font.letterSpacing: 1 * s
            }
            Row {
                spacing: 12 * s
                Text {
                    text: ft.value === "" ? (ft.editable ? "Tap to set" : "—") : ft.value
                    color: ft.value === "" ? t.dim : t.text
                    font.family: t.font; font.pixelSize: 24 * s; font.weight: Font.Medium
                }
                Text {
                    anchors.baseline: parent.children[0].baseline
                    visible: ft.note !== ""
                    text: ft.note
                    color: t.sub
                    font.family: t.font; font.pixelSize: 17 * s
                }
            }
        }
        MouseArea { id: ftArea; anchors.fill: parent; enabled: ft.editable; onClicked: ft.tapped() }
    }

    // A switch without Quick Controls (the package needs none)
    component Toggle: Rectangle {
        id: tg
        property bool checked: false
        signal toggled(bool value)
        width: 76 * s; height: 42 * s; radius: height / 2
        opacity: enabled ? 1 : 0.4
        color: checked ? withAlpha(t.ok, 0.85) : "#2A3448"
        border.color: checked ? t.ok : t.border
        Behavior on color { ColorAnimation { duration: 160 } }
        Rectangle {
            x: tg.checked ? parent.width - width - 5 * s : 5 * s
            y: 5 * s
            width: parent.height - 10 * s; height: width; radius: width / 2
            color: "#FFFFFF"
            Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        }
        MouseArea { anchors.fill: parent; anchors.margins: -14 * s; onClicked: tg.toggled(!tg.checked) }
    }

    component SignalBars: Item {
        property int strength: 0
        property color tint: t.text
        readonly property int lit: strength >= 75 ? 4 : strength >= 50 ? 3 : strength >= 25 ? 2 : strength > 0 ? 1 : 0
        width: 4 * 7 * s + 3 * 4 * s; height: 26 * s
        Repeater {
            model: 4
            Rectangle {
                x: index * 11 * s
                width: 7 * s; height: (8 + index * 6) * s
                y: parent.height - height
                radius: 2 * s
                color: index < parent.lit ? parent.tint : withAlpha(t.sub, 0.3)
            }
        }
    }

    // The last 60 s of traffic: down (info, filled) and up (violet)
    component Sparkline: Canvas {
        id: spark
        property var rx: []
        property var tx: []
        onRxChanged: requestPaint()
        onTxChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var max = 8000
            var i
            for (i = 0; i < rx.length; ++i) max = Math.max(max, rx[i])
            for (i = 0; i < tx.length; ++i) max = Math.max(max, tx[i])
            var step = width / 59
            var top = 4 * s, h = height - top - 2
            ctx.strokeStyle = withAlpha(t.border, 1)
            ctx.lineWidth = 1
            ctx.beginPath(); ctx.moveTo(0, height - 1); ctx.lineTo(width, height - 1); ctx.stroke()
            function line(data, color, fill) {
                if (data.length < 2) return
                var x0 = width - (data.length - 1) * step
                ctx.beginPath()
                for (var j = 0; j < data.length; ++j) {
                    var x = x0 + j * step, y = top + h - h * data[j] / max
                    if (j === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                }
                if (fill) {
                    ctx.lineTo(width, height - 1); ctx.lineTo(x0, height - 1); ctx.closePath()
                    ctx.fillStyle = withAlpha(color, 0.16); ctx.fill()
                    ctx.beginPath()
                    for (j = 0; j < data.length; ++j) {
                        var x2 = x0 + j * step, y2 = top + h - h * data[j] / max
                        if (j === 0) ctx.moveTo(x2, y2); else ctx.lineTo(x2, y2)
                    }
                }
                ctx.strokeStyle = color
                ctx.lineWidth = Math.max(1.5, 2.5 * s)
                ctx.lineJoin = "round"
                ctx.stroke()
            }
            line(rx, t.info, true)
            line(tx, t.violet, false)
        }
    }

    component KeyValue: Column {
        property string key
        property string value
        property color valueColor: t.text
        spacing: 4 * s
        Text {
            text: parent.key
            color: t.sub
            font.family: t.font; font.pixelSize: 16 * s; font.weight: Font.DemiBold; font.letterSpacing: 1 * s
        }
        Text {
            width: parent.width
            text: parent.value === "" ? "—" : parent.value
            color: parent.value === "" ? t.dim : parent.valueColor
            wrapMode: Text.WrapAnywhere; maximumLineCount: 3; elide: Text.ElideRight
            font.family: t.font; font.pixelSize: 21 * s; font.weight: Font.Medium
        }
    }

    component SectionLabel: Text {
        color: t.sub
        font.family: t.font; font.pixelSize: 16 * s; font.weight: Font.DemiBold
        font.letterSpacing: 2 * s
    }

    component CloseButton: Rectangle {
        signal clicked()
        width: 64 * s; height: width; radius: width / 2
        color: closeArea.pressed ? t.cardPressed : t.card
        border.color: t.border
        Canvas {
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = t.text
                ctx.lineWidth = Math.max(2, width * 0.06)
                ctx.lineCap = "round"
                ctx.beginPath()
                ctx.moveTo(width * 0.36, height * 0.36); ctx.lineTo(width * 0.64, height * 0.64)
                ctx.moveTo(width * 0.64, height * 0.36); ctx.lineTo(width * 0.36, height * 0.64)
                ctx.stroke()
            }
        }
        MouseArea { id: closeArea; anchors.fill: parent; anchors.margins: -10 * s; onClicked: parent.clicked() }
    }

    // A card that says why there is nothing here (yet)
    component EmptyState: Rectangle {
        property string glyph: "info"
        property color tint: t.info
        property string title
        property string detail
        default property alias extra: emptyExtra.data
        radius: 22 * s
        color: t.card
        border.color: t.border
        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - 80 * s, 900 * s)
            spacing: 20 * s
            ResultIcon { anchors.horizontalCenter: parent.horizontalCenter; tint: parent.parent.tint; glyph: parent.parent.glyph }
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                text: parent.parent.title
                color: t.text
                font.family: t.font; font.pixelSize: 30 * s; font.weight: Font.DemiBold
            }
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                visible: text !== ""
                text: parent.parent.detail
                color: t.sub
                font.family: t.font; font.pixelSize: 20 * s
            }
            Item {
                id: emptyExtra
                anchors.horizontalCenter: parent.horizontalCenter
                width: childrenRect.width; height: childrenRect.height
            }
        }
    }

    // A text field of the sheets: typed into by Keyboard.qml or a USB keyboard
    component Field: Rectangle {
        id: fld
        property alias input: fldInput
        property alias text: fldInput.text
        property bool secret: false
        property string placeholder
        height: 66 * s
        radius: 14 * s
        color: "#101828"
        border.color: fldInput.activeFocus ? t.accent : t.border
        border.width: fldInput.activeFocus ? 2 : 1
        TextInput {
            id: fldInput
            anchors.fill: parent
            anchors.leftMargin: 20 * s; anchors.rightMargin: 20 * s
            verticalAlignment: TextInput.AlignVCenter
            color: t.text
            selectionColor: withAlpha(t.accent, 0.5)
            font.family: t.font; font.pixelSize: 26 * s
            clip: true
            echoMode: fld.secret && !keyboard.revealed ? TextInput.Password : TextInput.Normal
            passwordCharacter: "•"
            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            Keys.onEscapePressed: sheet.close()
            Keys.onReturnPressed: sheet.join()
            Keys.onEnterPressed: sheet.join()
            Keys.onTabPressed: if (sheet.mode === "hidden")
                                   (fld === hiddenNameField ? hiddenPassword : hiddenName).forceActiveFocus()
        }
        Text {
            anchors.left: parent.left; anchors.leftMargin: 20 * s
            anchors.verticalCenter: parent.verticalCenter
            visible: fldInput.text === ""
            text: fld.placeholder
            color: t.dim
            font.family: t.font; font.pixelSize: 22 * s
        }
        MouseArea { anchors.fill: parent; onPressed: { fldInput.forceActiveFocus(); mouse.accepted = false } }
    }

    // One interface on the Overview
    component InterfaceCard: Rectangle {
        id: ic
        property var f
        readonly property var r: role(f)
        readonly property var rate: status.rates[f.name] || ({})
        radius: 18 * s
        color: icArea.pressed ? t.cardPressed : t.card
        border.color: t.border

        Rectangle { x: 0; y: 0; width: 6 * s; height: parent.height; radius: 3 * s; color: ic.r.tint }

        Item {
            anchors.fill: parent
            anchors.leftMargin: 34 * s; anchors.rightMargin: 28 * s
            anchors.topMargin: 26 * s; anchors.bottomMargin: 24 * s

            Rectangle {
                id: icBadge
                width: 72 * s; height: width; radius: 20 * s
                color: withAlpha(ic.r.tint, 0.16)
                border.color: withAlpha(ic.r.tint, 0.45)
                Image {
                    anchors.centerIn: parent
                    width: parent.width * 0.6; height: width
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/" + iconOf(ic.f) + ".svg"
                }
            }
            Column {
                anchors.left: icBadge.right; anchors.leftMargin: 20 * s
                anchors.right: icPill.left; anchors.rightMargin: 14 * s
                anchors.verticalCenter: icBadge.verticalCenter
                spacing: 2 * s
                Text {
                    width: parent.width; elide: Text.ElideRight
                    text: ic.f.friendly
                    color: t.text
                    font.family: t.font; font.pixelSize: 28 * s; font.weight: Font.DemiBold
                }
                Text {
                    text: ic.f.name
                    color: t.sub
                    font.family: t.font; font.pixelSize: 18 * s
                }
            }
            Pill {
                id: icPill
                anchors.right: parent.right
                anchors.verticalCenter: icBadge.verticalCenter
                label: ic.r.label
                tint: ic.r.tint
            }

            Column {
                id: icInfo
                anchors.top: icBadge.bottom; anchors.topMargin: 22 * s
                width: parent.width
                spacing: 8 * s
                Text {
                    width: parent.width; elide: Text.ElideRight
                    text: ic.f.ip ? ic.f.ip + "/" + ic.f.prefix : "No address"
                    color: ic.f.ip ? t.text : t.dim
                    font.family: t.font; font.pixelSize: (ic.f.ip ? 38 : 30) * s; font.weight: Font.Bold
                }
                Row {
                    visible: ic.f.type === "wifi" && ic.f.ssid !== ""
                    spacing: 12 * s
                    SignalBars { strength: parseInt(ic.f.signal) || 0; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, icInfo.width - 60 * s)
                        elide: Text.ElideRight
                        text: ic.f.ssid + "   ·   " + ic.f.signal + "%" + (ic.f.band ? "   ·   " + bandText(ic.f.band) : "")
                        color: t.text
                        font.family: t.font; font.pixelSize: 21 * s; font.weight: Font.Medium
                    }
                }
                Text {
                    width: parent.width; elide: Text.ElideRight
                    visible: text !== ""
                    text: {
                        if (ic.f.type === "wifi") {
                            if (radio !== "on") return "WiFi is switched off"
                            return ic.f.ssid === "" ? "Not connected to a network" : ""
                        }
                        if (carrierOf(ic.f) !== 1) return "Plug in a cable"
                        var parts = []
                        if (speedText(ic.f.speed)) parts.push(speedText(ic.f.speed))
                        if (ic.f.mode !== "off") parts.push(modeText(ic.f.mode))
                        return parts.join("   ·   ")
                    }
                    color: t.sub
                    font.family: t.font; font.pixelSize: 20 * s
                }
                Text {
                    width: parent.width; elide: Text.ElideRight
                    visible: text !== ""
                    text: ic.f.mode === "server" || ic.f.mode === "legacy-server"
                          ? (ic.f.clients !== undefined ? (ic.f.clients === 1 ? "1 client" : ic.f.clients + " clients") : "")
                          : ic.f.gateway ? "Gateway " + ic.f.gateway : ""
                    color: t.sub
                    font.family: t.font; font.pixelSize: 20 * s
                }
            }

            // Traffic: rates and the last minute
            Row {
                id: icRates
                anchors.bottom: icSpark.top; anchors.bottomMargin: 12 * s
                spacing: 34 * s
                Repeater {
                    model: [{ label: "DOWN", key: "rx", tint: t.info }, { label: "UP", key: "tx", tint: t.violet }]
                    Row {
                        spacing: 10 * s
                        Rectangle { width: 10 * s; height: width; radius: width / 2; color: modelData.tint; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: t.sub
                            font.family: t.font; font.pixelSize: 15 * s; font.weight: Font.DemiBold; font.letterSpacing: 1.5 * s
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: rateText(ic.rate[modelData.key] || 0)
                            color: t.text
                            font.family: t.font; font.pixelSize: 22 * s; font.weight: Font.Medium
                        }
                    }
                }
            }
            Sparkline {
                id: icSpark
                anchors.bottom: parent.bottom
                width: parent.width
                height: Math.max(40 * s, Math.min(150 * s, parent.height - icInfo.y - icInfo.height - 70 * s))
                rx: ic.rate.rxHistory || []
                tx: ic.rate.txHistory || []
            }
        }
        MouseArea { id: icArea; anchors.fill: parent; onClicked: sheet.openDetail(ic.f.name) }
    }

    // ---- page ----------------------------------------------------------------
    Item {
        id: page
        anchors.fill: parent
        anchors.leftMargin: 40 * s; anchors.rightMargin: 40 * s
        anchors.topMargin: 22 * s; anchors.bottomMargin: 30 * s

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
                opacity: win.changing ? 0.3 : 1.0
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
                MouseArea { id: backArea; anchors.fill: parent; enabled: !win.changing; onClicked: Qt.quit() }
            }
            Column {
                anchors.left: backButton.right; anchors.leftMargin: 28 * s
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4 * s
                Text {
                    text: "Network"
                    color: t.text
                    font.family: t.font; font.pixelSize: 36 * s; font.weight: Font.Bold
                }
                Text {
                    text: "Home  ›  Network  ›  " + (win.section === "wifi" ? "WiFi" : win.section === "wired" ? "Wired"
                                                    : win.section === "tools" ? "Tools" : "Overview")
                    color: t.sub
                    font.family: t.font; font.pixelSize: 19 * s
                }
            }
            Rectangle {
                id: sectionSwitch
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                width: sectionRow.width + 12 * s; height: 60 * s
                radius: height / 2
                color: t.card
                border.color: t.border
                opacity: win.nmMissing ? 0.4 : 1.0
                property real selX: 0
                property real selW: 0
                Rectangle {
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
                        model: [{ key: "overview", label: "Overview" }, { key: "wifi", label: "WiFi" },
                                { key: "wired", label: "Wired" }, { key: "tools", label: "Tools" }]
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
                            MouseArea {
                                anchors.fill: parent
                                enabled: !win.nmMissing
                                onClicked: { win.section = modelData.key; sheet.close() }
                            }
                        }
                    }
                }
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14 * s
                Pill { visible: status.dryRun; label: "DRY RUN"; tint: t.info }
                Pill {
                    visible: !win.nmMissing
                    label: !status.loaded ? "Checking…"
                           : summary.internet === "yes" ? "Internet via " + summary.via
                           : summary.internet === "unknown" && summary.defaultdev ? "Connected via " + summary.defaultdev
                           : "No internet"
                    tint: !status.loaded ? t.sub : summary.internet === "yes" ? t.ok
                          : summary.internet === "unknown" && summary.defaultdev ? t.info : t.dim
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

        Item {
            id: contentArea
            anchors.top: strip.bottom; anchors.topMargin: 26 * s
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: parent.bottom

            // ==== no NetworkManager =========================================
            EmptyState {
                anchors.fill: parent
                visible: win.nmMissing
                glyph: "warn"; tint: t.warn
                title: "NetworkManager is not available"
                detail: (status.unavailableReason ? status.unavailableReason + ". " : "")
                        + "This app works through NetworkManager, which the Pi OS image runs. "
                        + "Nothing here can be shown or changed without it."
            }

            // ==== loading ====================================================
            Spinner {
                anchors.centerIn: parent
                visible: !win.nmMissing && !status.loaded
            }

            // ==== Overview ===================================================
            Flickable {
                id: overview
                anchors.fill: parent
                visible: win.section === "overview" && status.loaded && !win.nmMissing
                contentWidth: cardRow.width
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                readonly property int count: status.interfaces.length
                readonly property real gap: 24 * s
                // three fit at 1920 wide; fewer share the row, one takes half
                readonly property real cardWidth: count >= 3 ? (width - 2 * gap) / 3
                                                  : count === 2 ? (width - gap) / 2 : width / 2
                Row {
                    id: cardRow
                    spacing: overview.gap
                    Repeater {
                        model: status.interfaces
                        InterfaceCard {
                            f: modelData
                            width: overview.cardWidth
                            height: Math.min(overview.height, 640 * s)
                        }
                    }
                }
                // More cards than fit: the right edge fades and says how many
                Rectangle {
                    visible: overview.contentWidth > overview.width + 1 && !overview.atXEnd
                    x: overview.contentX + overview.width - width
                    width: 160 * s; height: overview.height
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: withAlpha(t.bg, 0) }
                        GradientStop { position: 1; color: withAlpha(t.bg, 0.95) }
                    }
                    Pill {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        readonly property int hidden: Math.ceil((overview.contentWidth - overview.contentX - overview.width)
                                                                / (overview.cardWidth + overview.gap) - 0.05)
                        label: hidden + " more  ›"
                        tint: t.accent
                        color: withAlpha(t.bg, 0.9)
                    }
                }
                EmptyState {
                    visible: overview.count === 0
                    anchors.fill: parent
                    title: "No network interfaces"
                    detail: "NetworkManager lists no wired port and no WiFi adapter."
                }
            }

            // ==== WiFi =======================================================
            Item {
                id: wifiSection
                anchors.fill: parent
                visible: win.section === "wifi" && status.loaded && !win.nmMissing
                readonly property bool radioOn: win.radio === "on"
                readonly property bool busy: wifi.busyState === "connecting" || wifi.busyState === "working"
                readonly property bool volatileRoot: summary.volatile === "1"
                // On the A/B images the radio state is volatile (plan 4.2)
                readonly property string bootNote: !volatileRoot ? ""
                    : radioOn && summary.wifiboot !== "on" ? "This image starts with WiFi off: it is off again after the next start."
                    : !radioOn && summary.wifiboot === "on" ? "WiFi turns on again at the next start."
                    : ""

                EmptyState {
                    anchors.fill: parent
                    visible: win.radio === "absent"
                    glyph: "info"; tint: t.sub
                    title: "This system has no WiFi adapter"
                    detail: "NetworkManager lists no WiFi device."
                }
                EmptyState {
                    anchors.fill: parent
                    visible: win.radio === "off"
                    glyph: "info"; tint: t.sub
                    title: "WiFi is switched off"
                    detail: "Switch it on to see the networks in range."
                            + (wifiSection.bootNote ? " " + wifiSection.bootNote : "")
                    Row {
                        spacing: 20 * s
                        Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: false
                            enabled: !wifiSection.busy
                            onToggled: wifi.setRadio(value)
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: wifi.busyState === "working" ? "Switching on…" : "WiFi"
                            color: t.text
                            font.family: t.font; font.pixelSize: 24 * s; font.weight: Font.DemiBold
                        }
                    }
                }

                // ---- left: the radio, the connection, saved networks ---------
                Column {
                    id: wifiLeft
                    visible: wifiSection.radioOn
                    width: parent.width * 0.4
                    height: parent.height
                    spacing: 16 * s

                    Rectangle {
                        width: parent.width; height: 92 * s
                        radius: 18 * s
                        color: t.card; border.color: t.border
                        Column {
                            anchors.left: parent.left; anchors.leftMargin: 28 * s
                            anchors.right: radioToggle.left; anchors.rightMargin: 20 * s
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4 * s
                            Text {
                                text: "WiFi"
                                color: t.text
                                font.family: t.font; font.pixelSize: 26 * s; font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width
                                elide: Text.ElideRight
                                text: wifiSection.bootNote || ("On" + (summary.country && summary.country !== "00"
                                                                       ? "   ·   country " + summary.country : ""))
                                color: t.sub
                                font.family: t.font; font.pixelSize: 17 * s
                            }
                        }
                        Toggle {
                            id: radioToggle
                            anchors.right: parent.right; anchors.rightMargin: 28 * s
                            anchors.verticalCenter: parent.verticalCenter
                            checked: true
                            enabled: !wifiSection.busy
                            onToggled: wifi.setRadio(value)
                        }
                    }

                    // The connection now
                    Rectangle {
                        id: currentCard
                        width: parent.width; height: (win.wifiReachNote !== "" && up ? 172 : 138) * s
                        radius: 18 * s
                        color: t.card; border.color: t.border
                        readonly property bool joining: wifi.busyState === "connecting"
                        readonly property bool up: !!win.wlan && win.wlan.state === "connected" && !joining
                        readonly property color tone: joining ? t.info : up ? t.ok : t.dim
                        Rectangle { width: 6 * s; height: parent.height; radius: 3 * s; color: currentCard.tone }
                        Rectangle {
                            id: curBadge
                            x: 28 * s; anchors.verticalCenter: parent.verticalCenter
                            width: 72 * s; height: width; radius: 20 * s
                            color: withAlpha(currentCard.tone, 0.16)
                            border.color: withAlpha(currentCard.tone, 0.45)
                            Image {
                                visible: !currentCard.joining
                                anchors.centerIn: parent
                                width: parent.width * 0.6; height: width
                                sourceSize: Qt.size(width, height)
                                source: "qrc:/icons/wifi.svg"
                            }
                            Spinner { visible: currentCard.joining; anchors.centerIn: parent; width: 48 * s }
                        }
                        Column {
                            anchors.left: curBadge.right; anchors.leftMargin: 22 * s
                            anchors.right: disconnectButton.visible ? disconnectButton.left : parent.right
                            anchors.rightMargin: 20 * s
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6 * s
                            Text {
                                width: parent.width; elide: Text.ElideRight
                                text: currentCard.joining ? "Joining " + wifi.connectingSsid
                                      : currentCard.up ? win.wlan.ssid : "Not connected"
                                color: currentCard.up || currentCard.joining ? t.text : t.sub
                                font.family: t.font; font.pixelSize: 26 * s; font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width; elide: Text.ElideRight
                                text: currentCard.joining
                                      ? (wifi.phase === "associating" ? "Associating…"
                                         : wifi.phase === "authenticating" ? "Checking the password…"
                                         : wifi.phase === "address" ? "Getting an address…" : "Starting…")
                                      : currentCard.up
                                        ? (win.wlan.ip ? win.wlan.ip + "/" + win.wlan.prefix : "No address")
                                          + "   ·   " + win.wlan.signal + "%" + (win.wlan.band ? "   ·   " + bandText(win.wlan.band) : "")
                                        : "Choose a network on the right."
                                color: t.sub
                                font.family: t.font; font.pixelSize: 19 * s
                            }
                            Text {
                                width: parent.width
                                visible: win.wifiReachNote !== "" && currentCard.up
                                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                text: win.wifiReachNote
                                color: t.warn
                                font.family: t.font; font.pixelSize: 16 * s; font.weight: Font.DemiBold
                            }
                        }
                        ActionButton {
                            id: disconnectButton
                            visible: currentCard.up
                            enabled: !wifiSection.busy
                            anchors.right: parent.right; anchors.rightMargin: 24 * s
                            anchors.verticalCenter: parent.verticalCenter
                            height: 64 * s
                            label: "Disconnect"
                            onClicked: wifi.disconnectWifi()
                        }
                    }

                    SectionLabel { text: "SAVED NETWORKS" }

                    ListView {
                        id: savedList
                        width: parent.width
                        height: wifiLeft.height - y
                        clip: true
                        spacing: 10 * s
                        boundsBehavior: Flickable.StopAtBounds
                        model: wifi.saved
                        delegate: Rectangle {
                            id: savedRow
                            width: savedList.width; height: 76 * s
                            radius: 16 * s
                            color: t.card; border.color: t.border
                            property bool confirming: false
                            Timer { id: confirmTimer; interval: 4000; onTriggered: savedRow.confirming = false }
                            Column {
                                anchors.left: parent.left; anchors.leftMargin: 24 * s
                                anchors.right: autoLabel.left; anchors.rightMargin: 14 * s
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2 * s
                                Text {
                                    width: parent.width; elide: Text.ElideRight
                                    text: modelData.ssid
                                    color: t.text
                                    font.family: t.font; font.pixelSize: 22 * s; font.weight: Font.Medium
                                }
                                Text {
                                    text: modelData.active === "1" ? "Connected"
                                          : modelData.inrange === "1" ? "In range" : "Not in range"
                                    color: modelData.active === "1" ? t.ok : t.sub
                                    font.family: t.font; font.pixelSize: 16 * s
                                }
                            }
                            Text {
                                id: autoLabel
                                anchors.right: autoToggle.left; anchors.rightMargin: 10 * s
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Auto"
                                color: t.sub
                                font.family: t.font; font.pixelSize: 17 * s
                            }
                            Toggle {
                                id: autoToggle
                                anchors.right: forgetButton.left; anchors.rightMargin: 18 * s
                                anchors.verticalCenter: parent.verticalCenter
                                checked: modelData.autoconnect === "1"
                                enabled: !wifiSection.busy
                                onToggled: wifi.setAutoconnect(modelData.ssid, value)
                            }
                            // Forgetting the network in use drops the link: a second tap confirms
                            ActionButton {
                                id: forgetButton
                                anchors.right: parent.right; anchors.rightMargin: 14 * s
                                anchors.verticalCenter: parent.verticalCenter
                                height: 56 * s
                                width: 150 * s
                                danger: savedRow.confirming
                                enabled: !wifiSection.busy
                                label: savedRow.confirming ? "Sure?" : "Forget"
                                onClicked: {
                                    if (savedRow.confirming) { savedRow.confirming = false; wifi.forget(modelData.ssid) }
                                    else { savedRow.confirming = true; confirmTimer.restart() }
                                }
                            }
                        }
                        Text {
                            visible: savedList.count === 0
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: "None yet. A network you join is saved and joined again when it is in range."
                            color: t.dim
                            font.family: t.font; font.pixelSize: 18 * s
                        }
                    }
                }

                // ---- right: networks in range ---------------------------------
                Item {
                    id: wifiRight
                    visible: wifiSection.radioOn
                    anchors.left: wifiLeft.right; anchors.leftMargin: 30 * s
                    anchors.right: parent.right
                    height: parent.height

                    Item {
                        id: rightHead
                        width: parent.width; height: 56 * s
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 14 * s
                            SectionLabel { text: "NETWORKS IN RANGE"; anchors.verticalCenter: parent.verticalCenter }
                            Spinner {
                                visible: wifi.busyState === "scanning"
                                width: 30 * s
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 14 * s
                            ActionButton {
                                height: 56 * s
                                label: "Hidden network…"
                                enabled: !wifiSection.busy
                                onClicked: sheet.openHidden()
                            }
                            ActionButton {
                                height: 56 * s
                                label: "Scan again"
                                enabled: !wifiSection.busy && wifi.busyState !== "scanning"
                                onClicked: wifi.scan(true)
                            }
                        }
                    }

                    // The last change's outcome
                    Rectangle {
                        id: outcomeCard
                        readonly property var o: wifi.outcome
                        readonly property color tone: o.kind === "ok" ? t.ok : o.kind === "error" ? t.bad : t.info
                        visible: o.kind !== undefined && !(o.kind === "ok" && sheet.mode !== "")
                        anchors.top: rightHead.bottom; anchors.topMargin: 14 * s
                        width: parent.width
                        height: visible ? Math.max(84 * s, outcomeText.implicitHeight + 28 * s) : 0
                        radius: 16 * s
                        color: withAlpha(tone, 0.12)
                        border.color: withAlpha(tone, 0.55)
                        Rectangle { width: 6 * s; height: parent.height; radius: 3 * s; color: outcomeCard.tone }
                        Column {
                            id: outcomeText
                            anchors.left: parent.left; anchors.leftMargin: 28 * s
                            anchors.right: outcomeClose.left; anchors.rightMargin: 16 * s
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2 * s
                            Text {
                                text: outcomeCard.o.title || ""
                                color: outcomeCard.tone
                                font.family: t.font; font.pixelSize: 22 * s; font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                text: outcomeCard.o.detail || ""
                                color: t.text
                                font.family: t.font; font.pixelSize: 18 * s
                            }
                        }
                        CloseButton {
                            id: outcomeClose
                            width: 48 * s
                            anchors.right: parent.right; anchors.rightMargin: 16 * s
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: wifi.clearOutcome()
                        }
                    }

                    ListView {
                        id: apList
                        anchors.top: outcomeCard.visible ? outcomeCard.bottom : rightHead.bottom
                        anchors.topMargin: 14 * s
                        anchors.bottom: parent.bottom
                        width: parent.width
                        clip: true
                        spacing: 10 * s
                        boundsBehavior: Flickable.StopAtBounds
                        model: wifi.networks
                        delegate: Rectangle {
                            id: apRow
                            readonly property var n: modelData
                            readonly property bool unsupported: n.security === "enterprise" || n.security === "other"
                            readonly property bool joining: wifi.busyState === "connecting" && wifi.connectingSsid === n.ssid
                            readonly property bool current: n.active === "1" && !joining
                            width: apList.width; height: 76 * s
                            radius: 16 * s
                            color: apArea.pressed ? t.cardPressed : t.card
                            border.color: current ? withAlpha(t.ok, 0.6) : joining ? withAlpha(t.info, 0.6) : t.border
                            opacity: unsupported ? 0.5 : 1
                            SignalBars {
                                id: apBars
                                x: 24 * s; anchors.verticalCenter: parent.verticalCenter
                                strength: parseInt(apRow.n.signal) || 0
                                tint: apRow.current ? t.ok : t.text
                            }
                            Column {
                                anchors.left: apBars.right; anchors.leftMargin: 22 * s
                                anchors.right: apRight.left; anchors.rightMargin: 16 * s
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2 * s
                                Text {
                                    width: parent.width; elide: Text.ElideRight
                                    text: apRow.n.ssid
                                    color: t.text
                                    font.family: t.font; font.pixelSize: 23 * s; font.weight: Font.Medium
                                }
                                Text {
                                    width: parent.width; elide: Text.ElideRight
                                    text: securityText(apRow.n.security) + "   ·   " + bandText(apRow.n.band)
                                          + "   ·   " + apRow.n.signal + "%" + (apRow.n.saved === "1" ? "   ·   Saved" : "")
                                    color: t.sub
                                    font.family: t.font; font.pixelSize: 16 * s
                                }
                            }
                            Row {
                                id: apRight
                                anchors.right: parent.right; anchors.rightMargin: 22 * s
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 14 * s
                                Text {
                                    visible: apRow.joining
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: wifi.phase === "associating" ? "Associating…"
                                          : wifi.phase === "authenticating" ? "Checking the password…"
                                          : wifi.phase === "address" ? "Getting an address…" : "Starting…"
                                    color: t.info
                                    font.family: t.font; font.pixelSize: 18 * s; font.weight: Font.DemiBold
                                }
                                Spinner { visible: apRow.joining; width: 30 * s; anchors.verticalCenter: parent.verticalCenter }
                                Pill { visible: apRow.current; label: "Connected"; tint: t.ok; anchors.verticalCenter: parent.verticalCenter }
                                Text {
                                    visible: apRow.unsupported
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Not supported"
                                    color: t.sub
                                    font.family: t.font; font.pixelSize: 18 * s
                                }
                                Image {
                                    visible: apRow.n.security !== "open" && !apRow.joining
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 30 * s; height: width
                                    sourceSize: Qt.size(width, height)
                                    source: "qrc:/icons/lock.svg"
                                    opacity: 0.7
                                }
                            }
                            MouseArea {
                                id: apArea
                                anchors.fill: parent
                                enabled: !wifiSection.busy && !apRow.current
                                onClicked: wifi.choose(apRow.n.ssid)
                            }
                        }
                        Text {
                            visible: apList.count === 0 && wifi.scanned
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: "No networks found. Scan again, or join a hidden network by its name."
                            color: t.dim
                            font.family: t.font; font.pixelSize: 18 * s
                        }
                    }
                }
            }

            // ==== Wired =====================================================
            Item {
                id: wiredSection
                anchors.fill: parent
                visible: win.section === "wired" && status.loaded && !win.nmMissing
                readonly property var p: win.wport
                readonly property bool applying: wired.busyState === "applying"
                readonly property bool serving: !!p && (p.mode === "server" || p.mode === "legacy-server")
                // Server mode asked for on a port that does not serve yet: probe (plan 4.3)
                readonly property bool wantsProbe: !!p && win.draft.mode === "server" && !serving
                readonly property bool holdsLease: !!p && p.mode === "client" && !!p.dhcpserver && p.state === "connected"
                readonly property var probe: p ? (wired.probes[p.name] || null) : null
                onWantsProbeChanged: maybeProbe()
                onPChanged: maybeProbe()
                function maybeProbe() {
                    if (!visible || !p) return
                    if (wantsProbe && parseInt(p.carrier) === 1 && !holdsLease && !probe) wired.probe(p.name)
                    if (!wantsProbe && probe && !serving) wired.forgetProbe(p.name)
                }
                onVisibleChanged: maybeProbe()

                EmptyState {
                    anchors.fill: parent
                    visible: win.wiredPorts.length === 0
                    glyph: "info"; tint: t.sub
                    title: "No wired port"
                    detail: "NetworkManager lists no Ethernet port on this system."
                }

                // ---- left: the ports ------------------------------------------
                Column {
                    id: portList
                    visible: win.wiredPorts.length > 0
                    width: parent.width * 0.27
                    spacing: 14 * s
                    SectionLabel { text: "WIRED PORTS" }
                    Repeater {
                        model: win.wiredPorts
                        Rectangle {
                            id: portRow
                            readonly property var f: modelData
                            readonly property bool sel: win.wport && win.wport.name === f.name
                            readonly property var r: role(f)
                            width: portList.width; height: 112 * s
                            radius: 16 * s
                            color: prArea.pressed ? t.cardPressed : t.card
                            border.color: sel ? withAlpha(t.accent, 0.8) : t.border
                            border.width: sel ? 2 : 1
                            Rectangle { width: 6 * s; height: parent.height; radius: 3 * s; color: portRow.r.tint }
                            Column {
                                anchors.left: parent.left; anchors.leftMargin: 24 * s
                                anchors.right: parent.right; anchors.rightMargin: 16 * s
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3 * s
                                Text {
                                    width: parent.width; elide: Text.ElideRight
                                    text: portRow.f.friendly + "  ·  " + portRow.f.name
                                    color: t.text
                                    font.family: t.font; font.pixelSize: 21 * s; font.weight: Font.DemiBold
                                }
                                Text {
                                    width: parent.width; elide: Text.ElideRight
                                    text: modeText(portRow.f.mode)
                                    color: t.sub
                                    font.family: t.font; font.pixelSize: 17 * s
                                }
                                Text {
                                    width: parent.width; elide: Text.ElideRight
                                    text: parseInt(portRow.f.carrier) !== 1 ? "No cable"
                                          : portRow.f.ip ? portRow.f.ip + "/" + portRow.f.prefix + "   ·   " + portRow.r.label
                                          : portRow.r.label
                                    color: parseInt(portRow.f.carrier) !== 1 ? t.dim : portRow.r.tint
                                    font.family: t.font; font.pixelSize: 17 * s; font.weight: Font.Medium
                                }
                            }
                            MouseArea {
                                id: prArea
                                anchors.fill: parent
                                enabled: !wiredSection.applying
                                onClicked: { win.wiredPort = portRow.f.name; win.resetDraft(); wired.clearOutcome() }
                            }
                        }
                    }
                }

                // ---- right: the selected port --------------------------------------
                Rectangle {
                    id: portCard
                    visible: !!wiredSection.p
                    anchors.left: portList.right; anchors.leftMargin: 30 * s
                    anchors.right: parent.right
                    height: parent.height
                    radius: 22 * s
                    color: t.card
                    border.color: t.border
                    readonly property var p: wiredSection.p || ({})

                    Item {
                        anchors.fill: parent
                        anchors.margins: 26 * s

                        // title
                        Rectangle {
                            id: pcBadge
                            width: 60 * s; height: width; radius: 18 * s
                            color: withAlpha(role(portCard.p).tint, 0.16)
                            border.color: withAlpha(role(portCard.p).tint, 0.45)
                            Image {
                                anchors.centerIn: parent
                                width: parent.width * 0.6; height: width
                                sourceSize: Qt.size(width, height)
                                source: "qrc:/icons/ethernet.svg"
                            }
                        }
                        Column {
                            anchors.left: pcBadge.right; anchors.leftMargin: 18 * s
                            anchors.right: pcPill.left; anchors.rightMargin: 14 * s
                            anchors.verticalCenter: pcBadge.verticalCenter
                            spacing: 2 * s
                            Text {
                                width: parent.width; elide: Text.ElideRight
                                text: (portCard.p.friendly || "") + "  ·  " + (portCard.p.name || "")
                                color: t.text
                                font.family: t.font; font.pixelSize: 26 * s; font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width; elide: Text.ElideRight
                                text: (portCard.p.mac || "")
                                      + (portCard.p.speed && parseInt(portCard.p.carrier) === 1 ? "   ·   " + speedText(portCard.p.speed) : "")
                                      + (portCard.p.saved === "1" ? "   ·   saved, bound to its " + (portCard.p.binding === "mac" ? "MAC" : "name")
                                         : portCard.p.profileuuid ? "   ·   NetworkManager's automatic profile" : "   ·   no profile yet")
                                color: t.sub
                                font.family: t.font; font.pixelSize: 16 * s
                            }
                        }
                        Pill {
                            id: pcPill
                            anchors.right: parent.right
                            anchors.verticalCenter: pcBadge.verticalCenter
                            label: portCard.p.mode === "legacy-server" ? "DHCP server (panel menu)"
                                   : parseInt(portCard.p.carrier) !== 1 ? "No cable · " + modeText(portCard.p.mode)
                                   : modeText(portCard.p.mode)
                            tint: parseInt(portCard.p.carrier) !== 1 ? t.dim : role(portCard.p).tint
                        }

                        // the three modes
                        Rectangle {
                            id: modeSwitch
                            anchors.top: pcBadge.bottom; anchors.topMargin: 16 * s
                            width: parent.width; height: 60 * s
                            radius: height / 2
                            color: "#101828"
                            border.color: t.border
                            opacity: wiredSection.applying ? 0.5 : 1
                            Row {
                                x: 5 * s; anchors.verticalCenter: parent.verticalCenter
                                Repeater {
                                    model: [{ key: "client", label: "Automatic (DHCP client)" },
                                            { key: "static", label: "Fixed address" },
                                            { key: "server", label: "DHCP server" }]
                                    Rectangle {
                                        readonly property bool sel: win.draft.mode === modelData.key
                                        width: (modeSwitch.width - 10 * s) / 3; height: modeSwitch.height - 10 * s
                                        radius: height / 2
                                        color: sel ? withAlpha(t.accent, 0.22) : "transparent"
                                        border.color: sel ? withAlpha(t.accent, 0.7) : "transparent"
                                        Text {
                                            anchors.centerIn: parent
                                            text: modelData.label
                                            color: parent.sel ? t.text : t.sub
                                            font.family: t.font; font.pixelSize: 20 * s; font.weight: Font.DemiBold
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: !wiredSection.applying
                                            onClicked: { win.setDraftMode(modelData.key); wired.clearOutcome() }
                                        }
                                    }
                                }
                            }
                        }

                        // the mode's fields
                        Item {
                            id: fieldsArea
                            anchors.top: modeSwitch.bottom; anchors.topMargin: 16 * s
                            width: parent.width; height: 92 * s
                            readonly property real w4: (width - 3 * 14 * s) / 4

                            Text {
                                visible: win.draft.mode === "client"
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                text: "Gets its address, gateway and DNS from the network. "
                                      + (parseInt(portCard.p.carrier) !== 1 ? "No cable at the moment."
                                         : portCard.p.mode === "client" && portCard.p.ip
                                           ? "Now: " + portCard.p.ip + "/" + portCard.p.prefix
                                             + (portCard.p.dhcpserver ? " from " + portCard.p.dhcpserver : "")
                                             + (portCard.p.leasetime ? ", lease " + durationText(portCard.p.leasetime) : "")
                                             + (portCard.p.gateway ? ", gateway " + portCard.p.gateway : "")
                                           : "")
                                color: t.sub
                                font.family: t.font; font.pixelSize: 20 * s
                            }
                            Row {
                                visible: win.draft.mode === "static"
                                spacing: 14 * s
                                FieldTile {
                                    width: fieldsArea.w4; label: "ADDRESS"; value: win.draft.ip
                                    bad: !win.ipOk(win.draft.ip)
                                    enabled: !wiredSection.applying
                                    onTapped: sheet.openNumpad("ip")
                                }
                                FieldTile {
                                    width: fieldsArea.w4; label: "PREFIX"
                                    value: win.draft.prefix ? "/" + win.draft.prefix : ""
                                    note: win.prefixOk(win.draft.prefix) ? win.maskText(win.draft.prefix) : ""
                                    bad: !win.prefixOk(win.draft.prefix)
                                    enabled: !wiredSection.applying
                                    onTapped: sheet.openNumpad("prefix")
                                }
                                FieldTile {
                                    width: fieldsArea.w4; label: "GATEWAY (OPTIONAL)"; value: win.draft.gateway
                                    enabled: !wiredSection.applying
                                    onTapped: sheet.openNumpad("gateway")
                                }
                                FieldTile {
                                    width: fieldsArea.w4; label: "DNS (OPTIONAL)"; value: win.draft.dns.split(",").join("  ")
                                    enabled: !wiredSection.applying
                                    onTapped: sheet.openNumpad("dns")
                                }
                            }
                            Row {
                                visible: win.draft.mode === "server"
                                spacing: 14 * s
                                FieldTile {
                                    width: fieldsArea.w4; label: "THIS RIG'S ADDRESS"; value: win.draft.ip
                                    bad: win.draftError !== ""
                                    enabled: !wiredSection.applying
                                    onTapped: sheet.openNumpad("ip")
                                }
                                FieldTile { width: fieldsArea.w4; editable: false; label: "PREFIX"; value: "/24"; note: "255.255.255.0" }
                                FieldTile {
                                    width: fieldsArea.w4; editable: false; label: "ADDRESSES HANDED OUT"
                                    value: win.ipOk(win.draft.ip) ? "." + "10 – .254" : ""
                                    note: win.ipOk(win.draft.ip) ? win.draft.ip.split(".").slice(0, 3).join(".") + ".x" : ""
                                }
                                FieldTile { width: fieldsArea.w4; editable: false; label: "LEASES"; value: "1 hour"; note: "no gateway, no DNS" }
                            }
                        }

                        // probe, lease table or the last outcome
                        Item {
                            id: infoArea
                            anchors.top: fieldsArea.bottom; anchors.topMargin: 14 * s
                            anchors.bottom: applyRow.top; anchors.bottomMargin: 14 * s
                            width: parent.width
                            readonly property var o: wired.outcome
                            readonly property bool showOutcome: o.kind !== undefined && o.iface === portCard.p.name
                            readonly property var pr: wiredSection.probe
                            readonly property int found: pr && pr.state === "done" ? pr.servers.length : 0
                            // which card: outcome | lease-held | no-cable | probing | found | none | leases | ""
                            readonly property string what: showOutcome ? "outcome"
                                : wiredSection.wantsProbe && wiredSection.holdsLease ? "lease-held"
                                : wiredSection.wantsProbe && parseInt(portCard.p.carrier) !== 1 ? "no-cable"
                                : pr && pr.state === "running" && (wiredSection.wantsProbe || wiredSection.serving) ? "probing"
                                : pr && pr.state === "done" && found > 0 ? "found"
                                : wiredSection.wantsProbe && pr && pr.state === "done" ? "none"
                                : wiredSection.serving && win.draft.mode === "server" ? "leases"
                                : ""
                            readonly property color tone: what === "outcome" ? (o.kind === "ok" ? t.ok : o.kind === "error" ? t.bad : t.info)
                                : what === "found" || what === "lease-held" ? t.bad
                                : what === "none" ? t.ok
                                : what === "no-cable" ? t.warn : t.info

                            Rectangle {
                                visible: infoArea.what !== "" && infoArea.what !== "leases"
                                width: parent.width
                                height: Math.min(parent.height, Math.max(76 * s, infoText.implicitHeight + 28 * s))
                                radius: 16 * s
                                color: withAlpha(infoArea.tone, infoArea.what === "found" || infoArea.what === "lease-held" ? 0.18 : 0.12)
                                border.color: withAlpha(infoArea.tone, 0.6)
                                Rectangle { width: 6 * s; height: parent.height; radius: 3 * s; color: infoArea.tone }
                                Spinner {
                                    id: infoSpin
                                    visible: infoArea.what === "probing"
                                    x: 26 * s; anchors.verticalCenter: parent.verticalCenter
                                    width: 40 * s
                                }
                                Column {
                                    id: infoText
                                    anchors.left: parent.left; anchors.leftMargin: infoSpin.visible ? 84 * s : 28 * s
                                    anchors.right: infoClose.visible ? infoClose.left : parent.right
                                    anchors.rightMargin: 16 * s
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2 * s
                                    Text {
                                        width: parent.width; elide: Text.ElideRight
                                        text: {
                                            switch (infoArea.what) {
                                            case "outcome": return infoArea.o.title
                                            case "lease-held": return "Another DHCP server (" + portCard.p.dhcpserver + ") is already on this network"
                                            case "no-cable": return "No cable: the port could not be checked"
                                            case "probing": return "Checking for another DHCP server on " + portCard.p.name + "…"
                                            case "found":
                                                var names = []
                                                for (var i = 0; i < infoArea.pr.servers.length; ++i) names.push(infoArea.pr.servers[i].server)
                                                return (names.length === 1 ? "Another DHCP server (" + names[0] + ") is"
                                                                           : "Other DHCP servers (" + names.join(", ") + ") are")
                                                       + " already on this network"
                                            case "none": return "No other DHCP server found on this port"
                                            }
                                            return ""
                                        }
                                        color: infoArea.tone
                                        font.family: t.font; font.pixelSize: 22 * s; font.weight: Font.DemiBold
                                    }
                                    Text {
                                        width: parent.width
                                        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                        visible: text !== ""
                                        text: {
                                            switch (infoArea.what) {
                                            case "outcome": return infoArea.o.detail
                                            case "lease-held": return "This port holds a lease from it. Serving addresses here will disrupt other devices on it."
                                            case "no-cable": return "Plug in the other end first, or apply and check when it is connected."
                                            case "probing": return "One DHCP request, answers collected for about five seconds. Nothing is taken."
                                            case "found": return wiredSection.serving ? "This port serves addresses on a network that has its own DHCP server: other devices on it may get the wrong address. Switch it to another mode."
                                                                                      : "Serving addresses here will disrupt other devices on it. Hold to apply anyway."
                                            case "none": return "Hold to apply."
                                            }
                                            return ""
                                        }
                                        color: t.text
                                        font.family: t.font; font.pixelSize: 17 * s
                                    }
                                }
                                CloseButton {
                                    id: infoClose
                                    visible: infoArea.what === "outcome"
                                    width: 48 * s
                                    anchors.right: parent.right; anchors.rightMargin: 14 * s
                                    anchors.verticalCenter: parent.verticalCenter
                                    onClicked: wired.clearOutcome()
                                }
                            }

                            // the lease table of a serving port
                            Item {
                                visible: infoArea.what === "leases"
                                anchors.fill: parent
                                readonly property var rows: wired.leases[portCard.p.name] || []
                                Row {
                                    id: leaseHead
                                    spacing: 0
                                    Repeater {
                                        model: [{ t: "ADDRESS", w: 0.25 }, { t: "MAC", w: 0.3 }, { t: "HOST NAME", w: 0.27 }, { t: "LEASE ENDS", w: 0.18 }]
                                        Text {
                                            width: infoArea.width * modelData.w
                                            text: modelData.t
                                            color: t.sub
                                            font.family: t.font; font.pixelSize: 14 * s; font.weight: Font.DemiBold; font.letterSpacing: 1 * s
                                        }
                                    }
                                }
                                ListView {
                                    anchors.top: leaseHead.bottom; anchors.topMargin: 8 * s
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    clip: true
                                    boundsBehavior: Flickable.StopAtBounds
                                    model: parent.rows
                                    delegate: Row {
                                        height: 34 * s
                                        Repeater {
                                            model: [{ v: modelData.ip, w: 0.25 }, { v: modelData.mac, w: 0.3 },
                                                    { v: modelData.host || "—", w: 0.27 },
                                                    { v: Qt.formatTime(new Date(parseInt(modelData.expires) * 1000), "HH:mm"), w: 0.18 }]
                                            Text {
                                                width: infoArea.width * modelData.w
                                                elide: Text.ElideRight
                                                text: modelData.v
                                                color: t.text
                                                font.family: t.font; font.pixelSize: 18 * s
                                            }
                                        }
                                    }
                                    Text {
                                        visible: parent.count === 0
                                        text: "No client has an address from this port yet."
                                        color: t.dim
                                        font.family: t.font; font.pixelSize: 18 * s
                                    }
                                }
                            }
                        }

                        // what will happen, and Apply
                        Item {
                            id: applyRow
                            anchors.bottom: parent.bottom
                            width: parent.width; height: 80 * s
                            Text {
                                anchors.left: parent.left
                                anchors.right: applyButton.left; anchors.rightMargin: 24 * s
                                anchors.verticalCenter: parent.verticalCenter
                                wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                                text: wiredSection.applying
                                      ? (wired.phase === "checking" ? "Checking that " + wired.applyingIface + " works…"
                                         : "Applying to " + wired.applyingIface + "… The previous settings come back if the new ones do not work.")
                                      : win.draftError !== "" ? win.draftError : win.whatHappens()
                                color: wiredSection.applying ? t.info : win.draftError !== "" ? t.bad
                                       : win.draftChanged ? t.text : t.sub
                                font.family: t.font; font.pixelSize: 18 * s
                            }
                            HoldButton {
                                id: applyButton
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: 340 * s
                                readonly property bool risky: win.draftChanged
                                                              && (infoArea.what === "found" || infoArea.what === "lease-held")
                                tint: risky ? t.bad : t.accent
                                label: wiredSection.applying ? "" : wired.dryRun ? "Hold to apply (dry run)"
                                       : risky ? "Hold to apply anyway" : "Hold to apply"
                                enabled: win.draftChanged && win.draftError === "" && !wiredSection.applying
                                         && infoArea.what !== "probing"
                                onDone: wired.apply(portCard.p.name, win.draft.mode, win.draft.ip, String(win.draft.prefix),
                                                    win.draft.gateway, win.draft.dns)
                                Spinner {
                                    visible: wiredSection.applying
                                    anchors.centerIn: parent
                                    width: 44 * s
                                }
                            }
                        }
                    }
                }
                Connections {
                    target: wired
                    function onBusyChanged() { if (wired.busyState === "idle" && wired.outcome.kind === "ok") win.draftDirty = false }
                }
            }

            // ==== Tools (a later phase) ======================================
            EmptyState {
                anchors.fill: parent
                visible: win.section === "tools" && status.loaded && !win.nmMissing
                glyph: "info"; tint: t.sub
                title: "Network tools"
                detail: "Coming in a later version. Ping, internet check and iperf3 will be here."
            }
        }
    }

    // ---- sheets: interface detail, password, hidden network -----------------
    Item {
        id: sheet
        anchors.fill: parent
        // "" | detail | password | hidden | numpad
        property string mode: ""
        property string detailName: ""
        property string ssid: ""
        property string security: "wpa2"
        property string errorText: ""
        readonly property var detail: {
            var list = status.interfaces
            for (var i = 0; i < list.length; ++i) if (list[i].name === detailName) return list[i]
            return null
        }
        visible: mode !== "" || panel.y < height

        function close() {
            mode = ""
            keyHandler.forceActiveFocus()
        }
        function openDetail(name) {
            detailName = name
            mode = "detail"
        }
        function openPassword(ssidName, sec, keep) {
            ssid = ssidName
            security = sec || "wpa2"
            if (!keep) { passwordInput.text = ""; errorText = "" }
            keyboard.revealed = false
            keyboard.keyLayer = "abc"
            mode = "password"
            passwordInput.forceActiveFocus()
        }
        function openHidden(keep) {
            if (!keep) { hiddenName.text = ""; hiddenPassword.text = ""; security = "wpa2"; errorText = "" }
            keyboard.revealed = false
            keyboard.keyLayer = "abc"
            mode = "hidden"
            hiddenName.forceActiveFocus()
        }
        // ---- numeric pad: one field of the Wired draft at a time
        property string numField: "ip"
        readonly property var numFields: win.draft.mode === "server" ? ["ip"] : ["ip", "prefix", "gateway", "dns"]
        readonly property var numLabels: ({ ip: "Address", prefix: "Prefix", gateway: "Gateway (optional)",
                                            dns: "DNS servers (optional, up to three, separated by commas)" })
        readonly property bool numValid: {
            var v = numInput.text
            switch (numField) {
            case "ip": return win.ipOk(v)
            case "prefix": return win.prefixOk(v)
            case "gateway": return v === "" || win.ipOk(v)
            case "dns": return win.dnsOk(v)
            }
            return false
        }
        readonly property bool numLast: numFields.indexOf(numField) === numFields.length - 1
        function openNumpad(field) {
            numField = field
            numInput.text = String(win.draft[field] || "")
            numInput.cursorPosition = numInput.text.length
            mode = "numpad"
            numInput.forceActiveFocus()
        }
        function numSave() { if (numValid) win.setDraft(numField, numInput.text) }
        function numNext() {
            if (!numValid) return
            numSave()
            var i = numFields.indexOf(numField)
            if (i + 1 < numFields.length) openNumpad(numFields[i + 1])
            else close()
        }
        function numDone() { if (numValid) { numSave(); close() } }

        readonly property Item editing: mode === "password" ? passwordInput
                                      : mode === "hidden" ? (hiddenPassword.activeFocus ? hiddenPassword : hiddenName)
                                      : null
        readonly property bool canJoin: mode === "password" ? passwordInput.text.length >= 8 && passwordInput.text.length <= 63
                                      : mode === "hidden" ? hiddenName.text.length > 0
                                                            && (security === "open"
                                                                || (hiddenPassword.text.length >= 8 && hiddenPassword.text.length <= 63))
                                      : false
        property bool lastWasHidden: false
        function join() {
            if (!canJoin) return
            if (mode === "password") {
                lastWasHidden = false
                wifi.connectWithPassword(ssid, passwordInput.text)
            } else if (mode === "hidden") {
                lastWasHidden = true
                ssid = hiddenName.text
                wifi.connectHidden(hiddenName.text, security === "open" ? "" : hiddenPassword.text, security)
            }
            mode = ""
            keyHandler.forceActiveFocus()
        }

        Connections {
            target: wifi
            function onPasswordNeeded(ssidName, sec) { sheet.openPassword(ssidName, sec, false) }
            // The network refused the password: the sheet again, with what was typed
            function onPasswordRejected(ssidName) {
                sheet.errorText = wifi.outcome.detail || "Wrong password."
                if (sheet.lastWasHidden) sheet.openHidden(true)
                else sheet.openPassword(ssidName, sheet.security, true)
            }
            function onConnected(ssidName) { passwordInput.text = ""; hiddenPassword.text = ""; sheet.errorText = "" }
        }

        // The page dims behind a sheet
        Rectangle {
            anchors.fill: parent
            color: "#000000"
            opacity: sheet.mode !== "" ? 0.55 : 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
            MouseArea { anchors.fill: parent; enabled: sheet.mode !== ""; onClicked: sheet.close() }
        }

        Rectangle {
            id: panel
            width: parent.width
            readonly property real wanted: sheet.mode === "detail" ? detailBody.height
                                          : sheet.mode === "numpad" ? numBar.height + numPad.height
                                          : fieldBar.height + keyboard.height
            height: wanted
            y: sheet.mode !== "" ? parent.height - height : parent.height
            Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            color: t.bg
            Rectangle { width: parent.width; height: 1; color: t.border }
            MouseArea { anchors.fill: parent }   // taps on the sheet stay on it

            // ---- interface detail -------------------------------------------
            Item {
                id: detailBody
                visible: sheet.mode === "detail" || (sheet.mode === "" && sheet.detail !== null)
                width: parent.width
                height: Math.min(win.height - 120 * s, 26 * s + detGrid.y + detGrid.height + 30 * s)
                readonly property var f: sheet.detail || ({})
                readonly property var r: role(sheet.detail)
                readonly property var c: status.rates[f.name] || ({})
                Item {
                    anchors.fill: parent
                    anchors.leftMargin: 40 * s; anchors.rightMargin: 40 * s
                    anchors.topMargin: 26 * s; anchors.bottomMargin: 26 * s

                    Rectangle {
                        id: detBadge
                        width: 64 * s; height: width; radius: 18 * s
                        color: withAlpha(detailBody.r.tint, 0.16)
                        border.color: withAlpha(detailBody.r.tint, 0.45)
                        Image {
                            anchors.centerIn: parent
                            width: parent.width * 0.6; height: width
                            sourceSize: Qt.size(width, height)
                            source: "qrc:/icons/" + iconOf(sheet.detail) + ".svg"
                        }
                    }
                    Text {
                        id: detTitle
                        anchors.left: detBadge.right; anchors.leftMargin: 20 * s
                        anchors.verticalCenter: detBadge.verticalCenter
                        text: (detailBody.f.friendly || "") + "   "
                        color: t.text
                        font.family: t.font; font.pixelSize: 30 * s; font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.left: detTitle.right
                        anchors.baseline: detTitle.baseline
                        text: detailBody.f.name || ""
                        color: t.sub
                        font.family: t.font; font.pixelSize: 22 * s
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: detBadge.verticalCenter
                        spacing: 18 * s
                        Pill { label: detailBody.r.label; tint: detailBody.r.tint; anchors.verticalCenter: parent.verticalCenter }
                        ActionButton {
                            anchors.verticalCenter: parent.verticalCenter
                            height: 60 * s
                            label: detailBody.f.type === "wifi" ? "WiFi settings" : "Wired settings"
                            onClicked: {
                                if (detailBody.f.type !== "wifi") { win.wiredPort = detailBody.f.name; win.resetDraft() }
                                win.section = detailBody.f.type === "wifi" ? "wifi" : "wired"
                                sheet.close()
                            }
                        }
                        CloseButton { anchors.verticalCenter: parent.verticalCenter; onClicked: sheet.close() }
                    }

                    Column {
                        id: detGrid
                        anchors.top: detBadge.bottom; anchors.topMargin: 30 * s
                        width: parent.width
                        spacing: 22 * s
                        readonly property real gap: 30 * s
                        readonly property real cell: (width - 3 * gap) / 4
                        readonly property var f: detailBody.f
                        readonly property var c: detailBody.c
                        function pair(a, b) {
                            return (a === undefined || a < 0 ? "—" : a) + "  /  " + (b === undefined || b < 0 ? "—" : b)
                        }
                        Row {
                            spacing: detGrid.gap
                            KeyValue { width: detGrid.cell; key: "ADDRESS"; value: detGrid.f.ip ? detGrid.f.ip + "/" + detGrid.f.prefix : "" }
                            KeyValue { width: detGrid.cell; key: "GATEWAY"; value: detGrid.f.gateway || "" }
                            KeyValue { width: detGrid.cell; key: "DNS"; value: (detGrid.f.dns || "").split(",").join("  ") }
                            KeyValue { width: detGrid.cell; key: "MAC"; value: detGrid.f.mac || "" }
                        }
                        Row {
                            spacing: detGrid.gap
                            KeyValue {
                                width: detGrid.cell; key: "LEASE"
                                value: detGrid.f.leasetime ? durationText(detGrid.f.leasetime)
                                                             + (detGrid.f.dhcpserver ? " from " + detGrid.f.dhcpserver : "") : ""
                            }
                            KeyValue {
                                width: detGrid.cell; key: detGrid.f.type === "wifi" ? "NETWORK" : "LINK"
                                value: detGrid.f.type === "wifi"
                                       ? (detGrid.f.ssid ? detGrid.f.ssid + "  ·  " + detGrid.f.signal + "%  ·  " + bandText(detGrid.f.band) : "")
                                       : carrierOf(detGrid.f) === 1 ? (speedText(detGrid.f.speed) || "Cable in") : "No cable"
                            }
                            KeyValue {
                                width: detGrid.cell; key: "MODE"
                                value: detGrid.f.mode && detGrid.f.mode !== "off" ? modeText(detGrid.f.mode)
                                       + (detGrid.f.profile ? "  ·  " + detGrid.f.profile : "") : ""
                            }
                            KeyValue {
                                width: detGrid.cell; key: "DRIVER"
                                value: (detGrid.f.driver || "") + (detGrid.f.product ? "  ·  " + detGrid.f.product : "")
                            }
                        }
                        Row {
                            spacing: detGrid.gap
                            KeyValue {
                                width: detGrid.cell * 2 + detGrid.gap; key: "IPv6"
                                value: (detGrid.f.ip6 || "").split(",").join("\n")
                            }
                            KeyValue { width: detGrid.cell; key: "PACKETS  DOWN / UP"; value: detGrid.pair(detGrid.c.rx_packets, detGrid.c.tx_packets) }
                            KeyValue {
                                width: detGrid.cell; key: "ERRORS / DROPPED"
                                value: detGrid.pair(detGrid.c.rx_errors + detGrid.c.tx_errors, detGrid.c.rx_dropped + detGrid.c.tx_dropped)
                                valueColor: detGrid.c.rx_errors + detGrid.c.tx_errors > 0 ? t.warn : t.text
                            }
                        }
                    }
                }
            }

            // ---- text entry: the field just above the keyboard --------------
            Item {
                id: fieldBar
                visible: sheet.mode === "password" || sheet.mode === "hidden"
                width: parent.width
                height: 118 * s


                // Password for a network from the list
                Item {
                    anchors.fill: parent
                    anchors.leftMargin: 40 * s; anchors.rightMargin: 40 * s
                    visible: sheet.mode === "password"
                    Column {
                        id: pwTitle
                        anchors.left: parent.left
                        anchors.right: pwField.left; anchors.rightMargin: 24 * s
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4 * s
                        Text {
                            width: parent.width; elide: Text.ElideRight
                            text: "Join “" + sheet.ssid + "”"
                            color: t.text
                            font.family: t.font; font.pixelSize: 26 * s; font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                            text: sheet.errorText !== "" ? sheet.errorText
                                  : securityText(sheet.security) + " password, 8 to 63 characters"
                            color: sheet.errorText !== "" ? t.bad : t.sub
                            font.family: t.font; font.pixelSize: 17 * s
                        }
                    }
                    Field {
                        id: pwField
                        anchors.right: pwButtons.left; anchors.rightMargin: 24 * s
                        anchors.verticalCenter: parent.verticalCenter
                        width: 620 * s
                        secret: true
                        placeholder: "Password"
                    }
                    Row {
                        id: pwButtons
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 16 * s
                        ActionButton { height: 66 * s; width: 150 * s; label: "Cancel"; onClicked: sheet.close() }
                        ActionButton {
                            height: 66 * s; width: 200 * s
                            primary: true
                            enabled: sheet.canJoin
                            label: "Connect"
                            onClicked: sheet.join()
                        }
                    }
                }

                // A hidden network: its name, its security, the password
                Item {
                    anchors.fill: parent
                    anchors.leftMargin: 40 * s; anchors.rightMargin: 40 * s
                    visible: sheet.mode === "hidden"
                    Row {
                        id: hiddenFieldsRow
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 20 * s
                        Column {
                            spacing: 6 * s
                            Text {
                                text: sheet.errorText !== "" ? sheet.errorText : "HIDDEN NETWORK NAME"
                                width: hiddenNameField.width; elide: Text.ElideRight
                                color: sheet.errorText !== "" ? t.bad : t.sub
                                font.family: t.font; font.pixelSize: 15 * s; font.weight: Font.DemiBold; font.letterSpacing: 1 * s
                            }
                            Field { id: hiddenNameField; width: 440 * s; placeholder: "Network name" }
                        }
                        Column {
                            spacing: 6 * s
                            Text {
                                text: "SECURITY"
                                color: t.sub
                                font.family: t.font; font.pixelSize: 15 * s; font.weight: Font.DemiBold; font.letterSpacing: 1 * s
                            }
                            Row {
                                spacing: 8 * s
                                Repeater {
                                    model: [{ key: "open", label: "Open" }, { key: "wpa2", label: "WPA2" }, { key: "wpa3", label: "WPA3" }]
                                    Rectangle {
                                        readonly property bool sel: sheet.security === modelData.key
                                        width: 108 * s; height: 66 * s; radius: 14 * s
                                        color: sel ? withAlpha(t.accent, 0.22) : "transparent"
                                        border.color: sel ? withAlpha(t.accent, 0.8) : t.border
                                        Text {
                                            anchors.centerIn: parent
                                            text: modelData.label
                                            color: parent.sel ? t.text : t.sub
                                            font.family: t.font; font.pixelSize: 20 * s; font.weight: Font.DemiBold
                                        }
                                        MouseArea { anchors.fill: parent; onClicked: sheet.security = modelData.key }
                                    }
                                }
                            }
                        }
                        Column {
                            spacing: 6 * s
                            opacity: sheet.security === "open" ? 0.4 : 1
                            Text {
                                text: "PASSWORD"
                                color: t.sub
                                font.family: t.font; font.pixelSize: 15 * s; font.weight: Font.DemiBold; font.letterSpacing: 1 * s
                            }
                            Field { id: hiddenPasswordField; width: 440 * s; secret: true; placeholder: "8 to 63 characters" }
                        }
                    }
                    Row {
                        anchors.right: parent.right
                        y: hiddenFieldsRow.y + hiddenNameField.y
                        spacing: 16 * s
                        ActionButton { height: 66 * s; width: 150 * s; label: "Cancel"; onClicked: sheet.close() }
                        ActionButton {
                            height: 66 * s; width: 200 * s
                            primary: true
                            enabled: sheet.canJoin
                            label: "Connect"
                            onClicked: sheet.join()
                        }
                    }
                }
            }

            // ---- the numeric pad's field, just above it
            Item {
                id: numBar
                visible: sheet.mode === "numpad"
                width: parent.width
                height: 118 * s
                Item {
                    anchors.fill: parent
                    anchors.leftMargin: 40 * s; anchors.rightMargin: 40 * s
                    Column {
                        anchors.left: parent.left
                        anchors.right: numFieldBox.left; anchors.rightMargin: 24 * s
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4 * s
                        Text {
                            width: parent.width; elide: Text.ElideRight
                            text: (win.wport ? win.wport.name + "  ·  " : "") + (win.draft.mode === "server" ? "DHCP server" : "Fixed address")
                            color: t.sub
                            font.family: t.font; font.pixelSize: 17 * s
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                            text: sheet.numLabels[sheet.numField] || ""
                            color: t.text
                            font.family: t.font; font.pixelSize: 24 * s; font.weight: Font.DemiBold
                        }
                    }
                    Rectangle {
                        id: numFieldBox
                        anchors.right: numButtons.left; anchors.rightMargin: 24 * s
                        anchors.verticalCenter: parent.verticalCenter
                        width: 560 * s; height: 66 * s
                        radius: 14 * s
                        color: "#101828"
                        border.color: sheet.numValid ? t.accent : withAlpha(t.bad, 0.8)
                        border.width: 2
                        TextInput {
                            id: numInput
                            anchors.fill: parent
                            anchors.leftMargin: 20 * s; anchors.rightMargin: 150 * s
                            verticalAlignment: TextInput.AlignVCenter
                            color: t.text
                            selectionColor: withAlpha(t.accent, 0.5)
                            font.family: t.font; font.pixelSize: 28 * s
                            clip: true
                            maximumLength: sheet.numField === "dns" ? 47 : 15
                            // digits, dots and (for DNS) commas only: a USB keyboard types here too
                            validator: RegExpValidator { regExp: sheet.numField === "dns" ? /[0-9.,]*/ : /[0-9.]*/ }
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            Keys.onEscapePressed: sheet.close()
                            Keys.onReturnPressed: sheet.numLast ? sheet.numDone() : sheet.numNext()
                            Keys.onEnterPressed: sheet.numLast ? sheet.numDone() : sheet.numNext()
                            Keys.onTabPressed: sheet.numNext()
                        }
                        Text {
                            anchors.right: parent.right; anchors.rightMargin: 20 * s
                            anchors.verticalCenter: parent.verticalCenter
                            text: sheet.numField === "prefix" && win.prefixOk(numInput.text) ? win.maskText(numInput.text)
                                  : sheet.numValid ? "" : (numInput.text === "" ? "needed" : "not valid")
                            color: sheet.numValid ? t.sub : t.bad
                            font.family: t.font; font.pixelSize: 18 * s
                        }
                        MouseArea { anchors.fill: parent; onPressed: { numInput.forceActiveFocus(); mouse.accepted = false } }
                    }
                    Row {
                        id: numButtons
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 16 * s
                        ActionButton { height: 66 * s; width: 150 * s; label: "Cancel"; onClicked: sheet.close() }
                        ActionButton {
                            height: 66 * s; width: 150 * s
                            primary: true
                            enabled: sheet.numValid
                            label: "Done"
                            onClicked: sheet.numDone()
                        }
                    }
                }
            }
            NumPad {
                id: numPad
                visible: numBar.visible
                anchors.top: numBar.bottom
                width: parent.width
                s: win.s
                theme: t
                target: sheet.mode === "numpad" ? numInput : null
                listAllowed: sheet.numField === "dns"
                nextEnabled: sheet.numValid && !sheet.numLast
                doneEnabled: sheet.numValid
                onNext: sheet.numNext()
                onDone: sheet.numDone()
            }

            Keyboard {
                id: keyboard
                visible: fieldBar.visible
                anchors.top: fieldBar.bottom
                width: parent.width
                s: win.s
                theme: t
                target: sheet.editing
                passwordMode: sheet.mode === "password" || (sheet.mode === "hidden" && sheet.editing === hiddenPassword)
                doneLabel: "Connect"
                doneEnabled: sheet.canJoin
                onDone: sheet.join()
            }
        }
    }
    // The sheets' text inputs
    readonly property Item passwordInput: pwField.input
    readonly property Item hiddenName: hiddenNameField.input
    readonly property Item hiddenPassword: hiddenPasswordField.input

    // --open-sheet scroll-end: once the cards are laid out
    Timer {
        id: scrollEnd
        interval: 400
        onTriggered: overview.contentX = Math.max(0, overview.contentWidth - overview.width)
    }
    // --open-sheet: overlays for screenshots, once the data is in
    property bool sheetOpened: false
    onDataReadyChanged: if (dataReady) Qt.callLater(openInitialSheet)
    function openInitialSheet() {
        if (sheetOpened || initialSheet === "") return
        var parts = initialSheet.split(":")
        if (parts[0] === "detail") {
            var list = status.interfaces
            var name = parts[1] || (list.length > 0 ? list[0].name : "")
            if (name) sheet.openDetail(name)
        } else if (parts[0] === "keyboard") {
            var nets = wifi.networks, pick = null
            for (var i = 0; i < nets.length && !pick; ++i)
                if (nets[i].security !== "open" && nets[i].saved !== "1" && nets[i].security !== "enterprise") pick = nets[i]
            sheet.openPassword(pick ? pick.ssid : "Workshop", pick ? pick.security : "wpa2", false)
            passwordInput.text = "not-a-real-pw"
            if (parts[1]) keyboard.keyLayer = parts[1]
            if (parts[2] === "shown") keyboard.revealed = true
        } else if (parts[0] === "scroll-end") {
            scrollEnd.start()
        } else if (parts[0] === "hidden") {
            sheet.openHidden(false)
            hiddenName.text = "Bench-Hidden"
            if (parts[1]) keyboard.keyLayer = parts[1]
        } else if (parts[0].indexOf("apply-") === 0) {
            // an Apply as if held (screenshots of the applying and outcome states)
            if (parts[1]) win.wiredPort = parts[1]
            win.resetDraft()
            win.setDraftMode(parts[0].substring(6))
            wired.apply(win.wport.name, win.draft.mode, win.draft.ip, String(win.draft.prefix), win.draft.gateway, win.draft.dns)
        } else if (parts[0].indexOf("wired-") === 0 || parts[0] === "numpad" || parts[0] === "probe-warning") {
            // the Wired section with a mode chosen (and the pad open)
            if (parts[0].indexOf("wired-") === 0 && parts[1]) win.wiredPort = parts[1]
            win.resetDraft()
            var m = parts[0] === "numpad" ? "static" : parts[0] === "probe-warning" ? "server" : parts[0].substring(6)
            win.setDraftMode(m)
            if (parts[0] === "numpad") sheet.openNumpad(parts[1] || "ip")
        }
        sheetOpened = true
    }
}
