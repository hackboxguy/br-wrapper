import QtQuick 2.12

// The on-screen keyboard (the image has none). Four rows, keys at least
// 120·s × 68·s, layers abc / ABC / 123 / #+= that together hold every
// printable ASCII character - WiFi passwords use all of them.
//
// Keys act on press, with a short tint: a touch release is not guaranteed on
// every panel (handover 3.3). Backspace repeats while held, and stops by
// itself after a while in case the release never comes.
//
// It types into `target` (an ordinary TextInput), so a USB keyboard plugged
// into the Pi types into the same field.
Rectangle {
    id: kb
    property real s: 1
    property var theme                   // main.qml's tokens
    property Item target
    property bool passwordMode: false    // shows the Show/Hide key
    property bool revealed: false
    property string doneLabel: "Done"
    property bool doneEnabled: true
    property string keyLayer: "abc"         // abc | ABC | 123 | sym
    property bool capsLock: false
    signal done()

    readonly property real unit: 120 * s
    readonly property real keyH: 68 * s
    readonly property real gap: 10 * s
    height: 4 * keyH + 3 * gap + 2 * 18 * s
    color: theme.card

    Rectangle { width: parent.width; height: 1; color: theme.border }

    function typeText(ch) {
        if (!target) return
        if (target.selectedText.length > 0) target.remove(target.selectionStart, target.selectionEnd)
        if (target.maximumLength > 0 && target.text.length >= target.maximumLength) return
        target.insert(target.cursorPosition, ch)
        if (keyLayer === "ABC" && !capsLock) keyLayer = "abc"
    }
    function backspace() {
        if (!target) return false
        if (target.selectedText.length > 0) { target.remove(target.selectionStart, target.selectionEnd); return true }
        var p = target.cursorPosition
        if (p <= 0) return false
        target.remove(p - 1, p)
        return true
    }
    function press(key) {
        switch (key.kind) {
        case "char": typeText(key.ch); break
        case "space": typeText(" "); break
        case "back": backspace(); break
        case "shift":
            if (keyLayer === "abc") { keyLayer = "ABC"; capsLock = false }
            else if (!capsLock) capsLock = true           // second tap: caps lock
            else { keyLayer = "abc"; capsLock = false }
            break
        case "layer": keyLayer = key.to; capsLock = false; break
        case "eye": revealed = !revealed; break
        case "done": if (doneEnabled) done(); break
        }
    }

    function chars(str) {
        var out = []
        for (var i = 0; i < str.length; ++i) out.push({ kind: "char", ch: str.charAt(i), label: str.charAt(i), w: 1 })
        return out
    }
    function bottomRow(toLabel, to) {
        var row = [{ kind: "layer", label: toLabel, to: to, w: 1.5 }]
        if (passwordMode) row.push({ kind: "eye", label: revealed ? "Hide" : "Show", w: 1.3 })
        row.push({ kind: "space", label: "", w: passwordMode ? 4.6 : 5.9 })
        row.push({ kind: "char", ch: ".", label: ".", w: 1 })
        row.push({ kind: "done", label: doneLabel, w: 2.2 })
        return row
    }
    readonly property var rows: {
        var letters = keyLayer === "ABC"
        var l = function (str) { return chars(letters ? str.toUpperCase() : str) }
        if (keyLayer === "abc" || keyLayer === "ABC") return [
            l("qwertyuiop"),
            l("asdfghjkl"),
            [{ kind: "shift", label: "", w: 1.5 }].concat(l("zxcvbnm")).concat([{ kind: "back", label: "", w: 1.5 }]),
            bottomRow("123", "123")
        ]
        if (keyLayer === "123") return [
            chars("1234567890"),
            chars("-/:;()$&@\""),
            [{ kind: "layer", label: "#+=", to: "sym", w: 1.5 }].concat(chars(".,?!'")).concat([{ kind: "back", label: "", w: 1.5 }]),
            bottomRow("abc", "abc")
        ]
        return [
            chars("[]{}#%^*+="),
            chars("_\\|~<>`"),
            [{ kind: "layer", label: "123", to: "123", w: 1.5 }].concat(chars(".,?!'")).concat([{ kind: "back", label: "", w: 1.5 }]),
            bottomRow("abc", "abc")
        ]
    }

    // Backspace held: repeat, at most ~5 s (a lost release must not empty the field forever)
    Timer {
        id: repeat
        property int count: 0
        interval: 80; repeat: true
        onTriggered: { if (++count > 60 || !kb.backspace()) stop() }
    }
    Timer {
        id: repeatDelay
        interval: 500
        onTriggered: { repeat.count = 0; repeat.start() }
    }

    Column {
        anchors.centerIn: parent
        spacing: kb.gap
        Repeater {
            model: kb.rows
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: kb.gap
                Repeater {
                    model: modelData
                    Rectangle {
                        id: key
                        readonly property var k: modelData
                        readonly property bool special: k.kind !== "char" && k.kind !== "space"
                        readonly property bool lit: (k.kind === "shift" && kb.keyLayer === "ABC")
                                                    || (k.kind === "eye" && kb.revealed)
                        property bool flash: false
                        width: kb.unit * k.w + kb.gap * (k.w - 1)
                        height: kb.keyH
                        radius: 12 * kb.s
                        opacity: k.kind === "done" && !kb.doneEnabled ? 0.4 : 1
                        color: flash ? kb.theme.cardPressed
                             : k.kind === "done" ? kb.theme.accent
                             : lit ? Qt.rgba(kb.theme.accent.r, kb.theme.accent.g, kb.theme.accent.b, 0.30)
                             : special ? "#141C2C" : "#222C40"
                        border.color: k.kind === "done" ? kb.theme.accent : kb.theme.border
                        Text {
                            anchors.centerIn: parent
                            visible: key.k.label !== ""
                            text: key.k.label
                            color: key.k.kind === "done" ? "#081018" : kb.theme.text
                            font.family: kb.theme.font
                            font.pixelSize: (key.special ? 22 : 30) * kb.s
                            font.weight: key.special ? Font.DemiBold : Font.Medium
                        }
                        Image {
                            visible: key.k.kind === "shift" || key.k.kind === "back"
                            anchors.centerIn: parent
                            width: 34 * kb.s; height: width
                            sourceSize: Qt.size(width, height)
                            source: key.k.kind === "back" ? "qrc:/icons/backspace.svg"
                                  : key.k.kind !== "shift" ? ""
                                  : kb.capsLock ? "qrc:/icons/capslock.svg" : "qrc:/icons/shift.svg"
                        }
                        Timer { id: flashOff; interval: 140; onTriggered: key.flash = false }
                        MouseArea {
                            anchors.fill: parent
                            onPressed: {
                                key.flash = true
                                flashOff.restart()
                                kb.press(key.k)
                                if (key.k.kind === "back") repeatDelay.restart()
                            }
                            onReleased: { repeatDelay.stop(); repeat.stop() }
                            onCanceled: { repeatDelay.stop(); repeat.stop() }
                        }
                    }
                }
            }
        }
    }
}
