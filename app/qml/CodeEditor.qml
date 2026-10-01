import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// A code editor: line numbers, the current line highlighted, syntax colours,
/// auto-indent, and Tab as four spaces. `dirty` while the text differs from
/// what was loaded or last saved.
Item {
    id: root
    property string language: "cpp"
    property bool readOnly: false
    // Compared, not flagged on textChanged: the highlighter's first pass also
    // emits textChanged, without any edit.
    property string savedText: ""
    readonly property bool dirty: area.text !== savedText
    property alias text: area.text            // use load() to set it without marking it edited
    readonly property int line: lineAt(area.cursorPosition)
    readonly property int column: area.cursorPosition - area.text.lastIndexOf("\n", area.cursorPosition - 1)

    /// Replace the text without marking it edited (loading a file).
    function load(text) {
        area.text = text
        savedText = area.text           // as the editor holds it (line endings normalised)
    }
    function markSaved() { savedText = area.text }
    function focusEditor() { area.forceActiveFocus() }

    function lineAt(position) {
        let n = 1
        const t = area.text
        for (let i = t.indexOf("\n"); i >= 0 && i < position; i = t.indexOf("\n", i + 1)) ++n
        return n
    }
    /// Put the cursor on `line` (1-based) at `column`, scrolled into view.
    function goTo(line, column) {
        const t = area.text
        let pos = 0
        for (let n = 1; n < line; ++n) {
            const next = t.indexOf("\n", pos)
            if (next < 0) break
            pos = next + 1
        }
        area.cursorPosition = Math.min(t.length, pos + Math.max(0, (column || 1) - 1))
        area.forceActiveFocus()
        Qt.callLater(ensureVisible)
    }
    function ensureVisible() {
        const r = area.cursorRectangle
        const top = r.y, bottom = r.y + r.height
        if (top < flick.contentY + 20) flick.contentY = Math.max(0, top - 20)
        else if (bottom > flick.contentY + flick.height - 20) flick.contentY = bottom - flick.height + 20
        const left = gutter.width + r.x
        if (left < flick.contentX + gutter.width) flick.contentX = Math.max(0, r.x - 40)
        else if (left > flick.contentX + flick.width - 40) flick.contentX = left - flick.width + 40
    }

    Rectangle { anchors.fill: parent; color: Theme.surface }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: gutter.width + area.width
        contentHeight: Math.max(area.height, height)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }
        ScrollBar.horizontal: ScrollBar { }

        // The current line, across the full width.
        Rectangle {
            visible: area.activeFocus && area.selectionStart === area.selectionEnd
            y: area.cursorRectangle.y
            width: Math.max(flick.width, flick.contentWidth)
            height: area.cursorRectangle.height
            color: Theme.dark ? "#0DFFFFFF" : "#08000000"
        }

        // Line numbers: one Text with the same font, so the lines match the editor's.
        Text {
            id: gutter
            readonly property int lines: Math.max(1, area.lineCount)
            width: Math.max(3, String(lines).length) * fontMetrics.averageCharacterWidth + 30
            topPadding: area.topPadding
            rightPadding: 12
            horizontalAlignment: Text.AlignRight
            font: area.font
            color: Theme.textTertiary
            text: {
                const out = []
                for (let i = 1; i <= lines; ++i) out.push(i)
                return out.join("\n")
            }
        }
        Rectangle { x: gutter.width - 1; width: 1; height: flick.contentHeight; color: Theme.separator }
        FontMetrics { id: fontMetrics; font: area.font }

        TextArea {
            id: area
            x: gutter.width
            width: Math.max(implicitWidth, flick.width - gutter.width)
            height: Math.max(implicitHeight, flick.height)
            readOnly: root.readOnly
            selectByMouse: true
            persistentSelection: true
            wrapMode: TextArea.NoWrap
            font.family: Theme.monoFont
            font.pixelSize: Theme.fontBody
            color: Theme.text
            selectionColor: Theme.accentSoft
            selectedTextColor: Theme.text
            topPadding: 12; bottomPadding: 40; leftPadding: 12; rightPadding: 40
            background: null

            onCursorRectangleChanged: if (activeFocus) root.ensureVisible()

            Keys.onPressed: function (event) {
                if (root.readOnly) return
                if (event.key === Qt.Key_Tab && !(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier))) {
                    if (selectionStart !== selectionEnd) remove(selectionStart, selectionEnd)
                    insert(cursorPosition, "    ")
                    event.accepted = true
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                           && !(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier))) {
                    // Keep the line's indent; one more level after an opening brace.
                    if (selectionStart !== selectionEnd) remove(selectionStart, selectionEnd)
                    const t = text, pos = cursorPosition
                    const start = t.lastIndexOf("\n", pos - 1) + 1
                    const before = t.slice(start, pos)
                    const indent = before.match(/^[ \t]*/)[0]
                    const opens = /[{(\[]\s*$/.test(before)
                    insert(pos, "\n" + indent + (opens ? "    " : ""))
                    event.accepted = true
                } else if (event.text === "}") {
                    // A closing brace on an empty line goes back one level.
                    const t = text, pos = cursorPosition
                    const start = t.lastIndexOf("\n", pos - 1) + 1
                    const before = t.slice(start, pos)
                    if (/^ +$/.test(before) && before.length >= 4) {
                        remove(pos - 4, pos)
                        insert(cursorPosition, "}")
                        event.accepted = true
                    }
                }
            }
        }
    }

    SyntaxHighlighter { document: area.textDocument; language: root.language; dark: Theme.dark }
}
