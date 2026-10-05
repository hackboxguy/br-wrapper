import QtQuick 2.12

// The numeric pad for addresses (plan 4.5): digits, ".", backspace with
// repeat, Next (the next field) and Done. "," separates two DNS servers and
// is shown only where a list is allowed. Keys act on press, with a short
// tint, as the keyboard's do: a touch release is not guaranteed on every
// panel (handover 3.3). It types into `target`, an ordinary TextInput, so a
// USB keyboard types into the same field.
Rectangle {
    id: pad
    property real s: 1
    property var theme                    // main.qml's tokens
    property Item target
    property bool listAllowed: false      // shows the "," key
    property bool nextEnabled: true
    property bool doneEnabled: true
    property string nextLabel: "Next"
    signal next()
    signal done()

    readonly property real unit: 150 * s
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
    }
    function backspace() {
        if (!target) return false
        if (target.selectedText.length > 0) { target.remove(target.selectionStart, target.selectionEnd); return true }
        var p = target.cursorPosition
        if (p <= 0) return false
        target.remove(p - 1, p)
        return true
    }
    function press(k) {
        switch (k.kind) {
        case "char": typeText(k.ch); break
        case "back": backspace(); break
        case "next": if (nextEnabled) next(); break
        case "done": if (doneEnabled) done(); break
        }
    }

    readonly property var rows: [
        [{ kind: "char", ch: "1" }, { kind: "char", ch: "2" }, { kind: "char", ch: "3" }, { kind: "back" }],
        [{ kind: "char", ch: "4" }, { kind: "char", ch: "5" }, { kind: "char", ch: "6" }, { kind: "char", ch: "." }],
        [{ kind: "char", ch: "7" }, { kind: "char", ch: "8" }, { kind: "char", ch: "9" }, { kind: "next" }],
        [listAllowed ? { kind: "char", ch: "," } : { kind: "none" }, { kind: "char", ch: "0" }, { kind: "none" },
         { kind: "done" }]
    ]

    Timer {
        id: repeat
        property int count: 0
        interval: 80; repeat: true
        onTriggered: { if (++count > 60 || !pad.backspace()) stop() }
    }
    Timer {
        id: repeatDelay
        interval: 500
        onTriggered: { repeat.count = 0; repeat.start() }
    }

    Column {
        anchors.centerIn: parent
        spacing: pad.gap
        Repeater {
            model: pad.rows
            Row {
                spacing: pad.gap
                Repeater {
                    model: modelData
                    Rectangle {
                        id: key
                        readonly property var k: modelData
                        readonly property bool action: k.kind === "next" || k.kind === "done"
                        readonly property bool keyOn: k.kind === "done" ? pad.doneEnabled
                                                 : k.kind === "next" ? pad.nextEnabled : true
                        property bool flash: false
                        // an empty cell keeps its place in the grid
                        readonly property bool blank: k.kind === "none"
                        width: pad.unit * (index === 3 ? 1.4 : 1)   // the right column is wider
                        height: pad.keyH
                        radius: 12 * pad.s
                        opacity: blank ? 0 : keyOn ? 1 : 0.4
                        color: flash ? pad.theme.cardPressed
                             : k.kind === "done" ? pad.theme.accent
                             : action || k.kind === "back" ? "#141C2C" : "#222C40"
                        border.color: k.kind === "done" ? pad.theme.accent : pad.theme.border
                        Text {
                            anchors.centerIn: parent
                            visible: key.k.kind !== "back"
                            text: key.k.kind === "char" ? key.k.ch : key.k.kind === "next" ? pad.nextLabel : "Done"
                            color: key.k.kind === "done" ? "#081018" : pad.theme.text
                            font.family: pad.theme.font
                            font.pixelSize: (key.action ? 24 : 32) * pad.s
                            font.weight: key.action ? Font.DemiBold : Font.Medium
                        }
                        Image {
                            visible: key.k.kind === "back"
                            anchors.centerIn: parent
                            width: 34 * pad.s; height: width
                            sourceSize: Qt.size(width, height)
                            source: key.k.kind === "back" ? "qrc:/icons/backspace.svg" : ""
                        }
                        Timer { id: flashOff; interval: 140; onTriggered: key.flash = false }
                        MouseArea {
                            anchors.fill: parent
                            enabled: !key.blank
                            onPressed: {
                                key.flash = true
                                flashOff.restart()
                                pad.press(key.k)
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
