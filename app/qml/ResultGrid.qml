import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// A grid of rows from any model with display / isNull / isNumber / raw roles —
/// a table's data or a query's result. Row numbers, optional sortable headers,
/// alternate shading, and an inspector showing the selected row in full.
/// Cells select as a block (drag, or Shift-click; a row number takes the row,
/// ⌘A everything): ⌘C copies it as tab-separated text, and the bar underneath
/// counts it and sums its numbers.
Item {
    id: root
    property var model
    property var columns: []                 // [{ name, type }]: an SQL type or a Qt type name
    property var valuesForRow                // (row) => ({ column: value }), for the inspector
    property bool sortable: false
    property int sortColumn: -1
    property bool sortDescending: false
    property string emptyText: "No rows."
    signal sortRequested(int column)

    // Editing (a table's rows, once changes are allowed): the grid asks, the model keeps track.
    property bool editable: false
    signal cellEdited(int row, int column, var value)     // value: text, or null for NULL
    signal deleteToggled(int row)
    property int editRow: -1
    property int editColumn: -1
    function startEdit(row, column) { if (editable) { editRow = row; editColumn = column } }
    function commitEdit(text) {
        if (editRow >= 0) cellEdited(editRow, editColumn, text)
        editRow = -1; editColumn = -1
    }
    // The cell the context menu is for.
    property int menuRow: -1
    property int menuColumn: -1

    property int selectedRow: -1

    // ---- The selected block: from where it started (anchor) to where it ends ----
    property int anchorRow: -1
    property int anchorColumn: -1
    property int endRow: -1
    property int endColumn: -1
    readonly property bool hasSelection: anchorRow >= 0 && endRow >= 0
    readonly property int selTop: Math.min(anchorRow, endRow)
    readonly property int selBottom: Math.max(anchorRow, endRow)
    readonly property int selLeft: Math.min(anchorColumn, endColumn)
    readonly property int selRight: Math.max(anchorColumn, endColumn)
    readonly property int selectedCells: hasSelection ? (selBottom - selTop + 1) * (selRight - selLeft + 1) : 0
    function inSelection(r, c) { return hasSelection && r >= selTop && r <= selBottom && c >= selLeft && c <= selRight }
    function selectBlock(r1, c1, r2, c2) { anchorRow = r1; anchorColumn = c1; endRow = r2; endColumn = c2 }
    function clearSelection() { anchorRow = -1; anchorColumn = -1; endRow = -1; endColumn = -1 }
    function selectAll() {
        if (grid.rows > 0 && grid.columns > 0) selectBlock(0, 0, grid.rows - 1, grid.columns - 1)
    }
    // The cell under a point in the grid's content (rows are 30 high).
    function cellAt(x, y) {
        const r = Math.max(0, Math.min(grid.rows - 1, Math.floor(y / 30)))
        let c = 0, edge = 0
        for (; c < grid.columns; ++c) {
            edge += columnWidth(c)
            if (x < edge) break
        }
        return { row: r, column: Math.max(0, Math.min(grid.columns - 1, c)) }
    }
    property string copiedNote: ""
    function copySelection(format, headers) {
        if (!hasSelection || !model) return
        let text = GridTools.text(model, selTop, selLeft, selBottom, selRight, format, headers)
        const rows = selBottom - selTop + 1
        if (rows === 1 && !headers) text = text.replace(/\n$/, "")       // one row: no line break after it
        clipboard.text = text
        clipboard.selectAll(); clipboard.copy()
        copiedNote = "Copied " + (selectedCells === 1 ? "1 value"
                     : fmt(Math.min(rows, 100000)) + (rows === 1 ? " row" : " rows") + (rows > 100000 ? " (the most at once)" : ""))
                     + (format === "csv" ? " as CSV" : "")
        copiedTimer.restart()
    }
    Timer { id: copiedTimer; interval: 2200; onTriggered: root.copiedNote = "" }
    // The bar's figures, worked out once the selection settles.
    property var summary: ({ cells: 0 })
    readonly property string selectionKey: selTop + "," + selLeft + "," + selBottom + "," + selRight
    onSelectionKeyChanged: summarize.restart()
    Timer {
        id: summarize
        interval: 120
        onTriggered: root.summary = root.selectedCells > 1 && root.model
                     ? GridTools.summary(root.model, root.selTop, root.selLeft, root.selBottom, root.selRight) : ({ cells: 0 })
    }
    function num(x) {
        const s = Number(x).toLocaleString(Qt.locale(), "f", Number.isInteger(x) ? 0 : 4)
        const dp = Qt.locale().decimalPoint
        return s.indexOf(dp) < 0 ? s : s.replace(new RegExp("\\" + dp + "?0+$"), "")
    }
    Connections {
        target: root.model
        ignoreUnknownSignals: true
        function onModelReset() { root.clearSelection() }
    }
    // --select-cells t,l,b,r (screenshots): a block selected once there are rows.
    property bool startupCellsDone: false
    function takeStartupCells() {
        if (startupCellsDone || grid.rows === 0 || !root.visible) return
        const args = Qt.application.arguments
        const at = args.indexOf("--select-cells")
        if (at < 0 || at + 1 >= args.length) { startupCellsDone = true; return }
        const p = args[at + 1].split(",").map(Number)
        if (p.length !== 4) return
        startupCellsDone = true
        selectBlock(p[0] - 1, p[1] - 1, p[2] - 1, p[3] - 1)
    }
    Connections { target: grid; function onRowsChanged() { Qt.callLater(root.takeStartupCells) } }
    // Re-read after each edit (pendingCount moves), so the inspector shows unsaved values.
    readonly property var selectedValues: (model && model.pendingCount, selectedRow >= 0 && valuesForRow
                                           ? valuesForRow(selectedRow) : ({}))

    function forceLayout() { grid.forceLayout() }
    // Scroll so `row` is in view (rows are 30 high).
    function positionAt(row) {
        Qt.callLater(function () {
            grid.contentY = Math.max(0, Math.min(Math.max(0, grid.contentHeight - grid.height), row * 30 - grid.height / 2))
        })
    }
    function fmt(n) { return Number(n).toLocaleString(Qt.locale(), "f", 0) }

    // Numbers line up on the right, so a column's alignment follows its type —
    // NULLs and the header included. Matches SQL types (INTEGER, NUMERIC(8,2))
    // and Qt's (int, qlonglong, double).
    function isNumericColumn(c) {
        const col = columns[c]
        return !!col && /INT|REAL|FLOA|DOUB|NUM|DEC|LONG|MONEY/i.test(col.type)
    }
    // A sensible starting width from the column's type and name.
    function columnWidth(c) {
        const col = columns[c]
        if (!col) return 120
        const t = col.type
        const w = /INT|LONG/i.test(t) ? 90
                : /REAL|FLOA|DOUB|NUM|DEC|MONEY/i.test(t) ? 104
                : /DATE|TIME/i.test(t) ? 168
                : 220
        return Math.max(w, col.name.length * 8 + 44)
    }

    onColumnsChanged: { selectedRow = -1; clearSelection(); grid.forceLayout() }

    Rectangle {
        id: frame
        anchors { left: parent.left; top: parent.top; bottom: selectionBar.visible ? selectionBar.top : parent.bottom
                  bottomMargin: selectionBar.visible ? 6 : 0
                  right: inspector.visible ? inspector.left : parent.right
                  rightMargin: inspector.visible ? 12 : 0 }
        radius: Theme.radius
        color: Theme.surface
        border.width: 1
        border.color: Theme.separator
        clip: true

        readonly property int rowNumberWidth: 64

        // Corner above the row numbers.
        Rectangle {
            x: 1; y: 1; width: frame.rowNumberWidth; height: header.height
            color: Theme.surfaceRaised; radius: Theme.radius - 1
        }

        HorizontalHeaderView {
            id: header
            anchors { left: grid.left; right: grid.right; top: parent.top; topMargin: 1 }
            syncView: grid
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                implicitHeight: 34
                implicitWidth: 100
                color: root.sortable && headerMouse.containsMouse ? Theme.hover : Theme.surfaceRaised
                Row {
                    anchors { left: parent.left; leftMargin: 10; right: parent.right; rightMargin: 10
                              verticalCenter: parent.verticalCenter }
                    spacing: 5
                    layoutDirection: root.isNumericColumn(column) ? Qt.RightToLeft : Qt.LeftToRight
                    Text {
                        text: display
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        // Only leave room for the sort arrow when there is one.
                        width: Math.min(implicitWidth, parent.width - (root.sortColumn === column ? 18 : 0))
                    }
                    Text {
                        visible: root.sortColumn === column
                        text: root.sortDescending ? "▼" : "▲"
                        color: Theme.accent
                        font.pixelSize: 9
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.separator }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.separator }
                MouseArea {
                    id: headerMouse
                    anchors.fill: parent
                    hoverEnabled: root.sortable
                    cursorShape: root.sortable ? Qt.PointingHandCursor : Qt.ArrowCursor
                    // Sorts where the grid can; otherwise takes the whole column.
                    onClicked: function (mouse) {
                        if (root.sortable) { root.sortRequested(column); return }
                        grid.forceActiveFocus()
                        if (grid.rows === 0) return
                        if ((mouse.modifiers & Qt.ShiftModifier) && root.hasSelection)
                            root.selectBlock(0, root.anchorColumn, grid.rows - 1, column)
                        else
                            root.selectBlock(0, column, grid.rows - 1, column)
                    }
                }
            }
        }

        VerticalHeaderView {
            id: rowNumbers
            anchors { left: parent.left; leftMargin: 1; top: grid.top; bottom: grid.bottom }
            width: frame.rowNumberWidth
            syncView: grid
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                implicitWidth: frame.rowNumberWidth
                implicitHeight: 30
                color: row === root.selectedRow ? Theme.accentSoft : Theme.surface
                Text {
                    anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                    text: row + 1
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFont
                }
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.separator }
                // A row number takes the whole row (Shift: down to here).
                MouseArea {
                    anchors.fill: parent
                    onClicked: function (mouse) {
                        grid.forceActiveFocus()
                        const last = Math.max(0, grid.columns - 1)
                        if ((mouse.modifiers & Qt.ShiftModifier) && root.hasSelection)
                            root.selectBlock(root.anchorRow, 0, row, last)
                        else {
                            root.selectBlock(row, 0, row, last)
                            root.selectedRow = row
                        }
                    }
                }
            }
        }

        TableView {
            id: grid
            anchors { left: rowNumbers.right; right: parent.right; top: header.bottom; bottom: parent.bottom
                      rightMargin: 1; bottomMargin: 1 }
            model: root.model
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            columnWidthProvider: (c) => root.columnWidth(c)
            rowHeightProvider: () => 30
            ScrollBar.vertical: ScrollBar { }
            ScrollBar.horizontal: ScrollBar { }
            Keys.onPressed: function (e) {
                if (e.matches(StandardKey.Copy) && root.hasSelection) { root.copySelection("tsv", false); e.accepted = true }
                else if (e.matches(StandardKey.SelectAll)) { root.selectAll(); e.accepted = true }
                else if (e.key === Qt.Key_Escape && root.hasSelection) { root.clearSelection(); e.accepted = true }
            }

            delegate: Rectangle {
                id: cellBox
                implicitWidth: 120
                implicitHeight: 30
                // Unsaved changes show where they are: edited cells, deleted and new rows.
                readonly property bool cellEdited: model.edited === true
                readonly property bool rowDeleted: model.deleted === true
                readonly property bool rowInserted: model.inserted === true
                readonly property bool editing: root.editRow === row && root.editColumn === column
                color: rowDeleted ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12)
                     : cellEdited ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.16)
                     : rowInserted ? Qt.rgba(Theme.positive.r, Theme.positive.g, Theme.positive.b, 0.10)
                     : row === root.selectedRow ? Theme.accentSoft
                     : row % 2 ? Theme.surfaceRaised : Theme.surface
                Rectangle {          // a mark at the edge of an edited cell
                    visible: cellBox.cellEdited && !cellBox.rowDeleted
                    width: 3; height: parent.height
                    color: Theme.warning
                }
                Rectangle {          // part of the selected block
                    anchors.fill: parent
                    visible: root.selectedCells > 1 && root.inSelection(row, column)
                    color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                }
                Rectangle {          // the one selected cell
                    anchors.fill: parent
                    visible: root.selectedCells === 1 && root.inSelection(row, column) && !cellBox.editing
                    color: "transparent"
                    border.width: 2; border.color: Theme.accent
                }
                Text {
                    visible: !cellBox.editing
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: isNumber || root.isNumericColumn(column) ? Text.AlignRight : Text.AlignLeft
                    text: display
                    color: isNull || (cellBox.rowInserted && !cellBox.cellEdited) ? Theme.textTertiary : Theme.text
                    font.italic: isNull || (cellBox.rowInserted && !cellBox.cellEdited)
                    font.strikeout: cellBox.rowDeleted
                    font.pixelSize: Theme.fontBody
                    font.family: isNumber ? Theme.monoFont : Qt.application.font.family
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: function (mouse) {
                        grid.forceActiveFocus()
                        if (mouse.button === Qt.RightButton) {
                            root.menuRow = row; root.menuColumn = column
                            if (!root.inSelection(row, column)) { root.selectBlock(row, column, row, column); root.selectedRow = row }
                            cellMenu.popup()
                            return
                        }
                        if ((mouse.modifiers & Qt.ShiftModifier) && root.hasSelection) {
                            root.endRow = row; root.endColumn = column
                            return
                        }
                        // Clicking the one selected cell again lets go of it (and the inspector).
                        if (root.selectedCells === 1 && root.inSelection(row, column)) {
                            root.clearSelection()
                            root.selectedRow = -1
                            return
                        }
                        root.selectBlock(row, column, row, column)
                        root.selectedRow = row
                    }
                    // Dragging stretches the block.
                    onPositionChanged: function (mouse) {
                        if (!pressed || !(pressedButtons & Qt.LeftButton) || !root.hasSelection) return
                        const p = mapToItem(grid.contentItem, mouse.x, mouse.y)
                        const cell = root.cellAt(p.x, p.y)
                        root.endRow = cell.row; root.endColumn = cell.column
                    }
                    onDoubleClicked: root.startEdit(row, column)
                }
                // The editor, in place of the text.
                Loader {
                    anchors.fill: parent
                    active: cellBox.editing
                    sourceComponent: Field {
                        text: isNull ? "" : (model.raw === undefined || model.raw === null ? "" : String(model.raw))
                        implicitHeight: 30
                        Component.onCompleted: { forceActiveFocus(); selectAll() }
                        onAccepted: root.commitEdit(text)
                        Keys.onEscapePressed: { root.editRow = -1; root.editColumn = -1 }
                        onActiveFocusChanged: if (!activeFocus && cellBox.editing) root.commitEdit(text)
                    }
                }
            }
        }

        Text {
            anchors.centerIn: grid
            visible: grid.rows === 0
            text: root.emptyText
            color: Theme.textSecondary
            font.pixelSize: Theme.fontHeading
        }
    }

    // ---- Inspector: every value of the selected row, in full ----
    Rectangle {
        id: inspector
        visible: root.selectedRow >= 0
        width: Math.min(340, root.width * 0.38)
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        radius: Theme.radius
        color: Theme.surface
        border.width: 1
        border.color: Theme.separator

        Item {
            id: inspectorHeader
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 44
            Text {
                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                text: "Row " + root.fmt(root.selectedRow + 1)
                color: Theme.text
                font.pixelSize: Theme.fontHeading; font.weight: Font.DemiBold
            }
            Text {
                readonly property bool deleted: root.selectedRow >= 0 && root.model && root.model.rowDeleted !== undefined
                                                && (root.model.pendingCount, root.model.rowDeleted(root.selectedRow))
                visible: root.editable
                anchors { right: parent.right; rightMargin: 44; verticalCenter: parent.verticalCenter }
                text: deleted ? "Restore row" : "Delete row"
                color: deleted ? Theme.accent : Theme.danger
                font.pixelSize: Theme.fontBody
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.deleteToggled(root.selectedRow) }
            }
            Rectangle {
                anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                width: 24; height: 24; radius: 6
                color: closeMouse.containsMouse ? Theme.hover : "transparent"
                Rectangle { anchors.centerIn: parent; width: 10; height: 1.5; color: Theme.textSecondary; rotation: 45 }
                Rectangle { anchors.centerIn: parent; width: 10; height: 1.5; color: Theme.textSecondary; rotation: -45 }
                MouseArea { id: closeMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.selectedRow = -1 }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.separator }
        }

        Flickable {
            anchors { left: parent.left; right: parent.right; top: inspectorHeader.bottom; bottom: parent.bottom }
            contentHeight: fields.height + 16
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }

            Column {
                id: fields
                x: 16; y: 12
                width: parent.width - 32
                spacing: 12
                Repeater {
                    model: root.columns
                    Column {
                        width: fields.width
                        spacing: 3
                        readonly property var value: root.selectedValues[modelData.name]
                        readonly property bool isNull: value === null || value === undefined
                        Text {
                            text: modelData.name.toUpperCase()
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSmall; font.weight: Font.Bold; font.letterSpacing: 0.4
                        }
                        // Editable: a field, and a way to say NULL.
                        Row {
                            visible: root.editable
                            width: parent.width
                            spacing: 6
                            Field {
                                id: valueField
                                width: parent.width - nullButton.width - 6
                                text: parent.parent.isNull ? "" : String(parent.parent.value)
                                placeholderText: parent.parent.isNull ? "NULL" : ""
                                onEditingFinished: {
                                    const was = parent.parent.isNull ? null : String(parent.parent.value)
                                    if (text !== (was === null ? "" : was) || (was === null && text.length))
                                        root.cellEdited(root.selectedRow, index, text)
                                }
                            }
                            ActionButton {
                                id: nullButton
                                text: "NULL"
                                implicitHeight: 32
                                onClicked: root.cellEdited(root.selectedRow, index, null)
                            }
                        }
                        TextEdit {
                            visible: !root.editable
                            width: parent.width
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            text: parent.isNull ? "NULL" : String(parent.value)
                            color: parent.isNull ? Theme.textTertiary : Theme.text
                            font.italic: parent.isNull
                            font.pixelSize: Theme.fontBody
                            selectionColor: Theme.accentSoft
                            selectedTextColor: Theme.text
                        }
                    }
                }
            }
        }
    }

    ContextMenu {
        id: cellMenu
        ContextMenuItem { text: "Edit"; enabled: root.editable; onTriggered: root.startEdit(root.menuRow, root.menuColumn) }
        ContextMenuItem { text: "Set to NULL"; enabled: root.editable; onTriggered: root.cellEdited(root.menuRow, root.menuColumn, null) }
        ContextMenuItem {
            readonly property bool deleted: root.menuRow >= 0 && root.model && root.model.rowDeleted !== undefined
                                            && root.model.rowDeleted(root.menuRow)
            text: deleted ? "Restore row" : "Delete row"
            danger: !deleted
            enabled: root.editable
            onTriggered: root.deleteToggled(root.menuRow)
        }
        ContextMenuSeparator { }
        ContextMenuItem { text: root.selectedCells > 1 ? "Copy   ⌘C" : "Copy value   ⌘C"; onTriggered: root.copySelection("tsv", false) }
        ContextMenuItem { text: "Copy with column names"; onTriggered: root.copySelection("tsv", true) }
        ContextMenuItem { text: "Copy as CSV"; onTriggered: root.copySelection("csv", true) }
        ContextMenuSeparator { }
        ContextMenuItem { text: "Select all   ⌘A"; onTriggered: root.selectAll() }
    }

    // ---- Under the grid: what's selected, summed up ----
    Item {
        id: selectionBar
        anchors { left: frame.left; right: frame.right; bottom: parent.bottom }
        height: 24
        visible: root.selectedCells > 1 || root.copiedNote.length > 0
        Text {
            anchors { left: parent.left; leftMargin: 4; right: hint.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
            elide: Text.ElideRight
            color: root.copiedNote.length ? Theme.positive : Theme.textSecondary
            font.pixelSize: Theme.fontSmall + 1
            text: {
                if (root.copiedNote.length) return root.copiedNote
                const s = root.summary
                if (!s || !s.cells) return root.fmt(root.selectedCells) + " cells"
                if (s.tooMany) return root.fmt(s.cells) + " cells — too many to add up"
                let parts = [root.fmt(s.cells) + " cells"]
                if (s.numbers > 0) {
                    parts.push("Sum " + root.num(s.sum), "Avg " + root.num(s.avg),
                               "Min " + root.num(s.min), "Max " + root.num(s.max))
                    if (s.numbers < s.values) parts.push(root.fmt(s.numbers) + " numbers")
                }
                if (s.nulls > 0) parts.push(root.fmt(s.nulls) + " NULL")
                return parts.join("   ·   ")
            }
        }
        Text {
            id: hint
            anchors { right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter }
            visible: !root.copiedNote.length
            text: "⌘C copies · right-click for more"
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSmall
        }
    }
    TextEdit { id: clipboard; visible: false }
}
