import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0

/// A grid of rows from any model with display / isNull / isNumber roles —
/// a table's data or a query's result. Row numbers, optional sortable headers,
/// alternate shading, and an inspector showing the selected row in full.
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

    onColumnsChanged: { selectedRow = -1; grid.forceLayout() }

    Rectangle {
        id: frame
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom
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
                    enabled: root.sortable
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.sortRequested(column)
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
                    onClicked: function (mouse) {
                        if (mouse.button === Qt.RightButton) {
                            root.menuRow = row; root.menuColumn = column
                            root.selectedRow = row
                            cellMenu.popup()
                            return
                        }
                        root.selectedRow = (root.selectedRow === row ? -1 : row)
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
        ContextMenuItem {
            text: "Copy value"
            onTriggered: {
                const v = root.valuesForRow ? root.valuesForRow(root.menuRow)[root.columns[root.menuColumn].name] : null
                clipboard.text = v === null || v === undefined ? "NULL" : String(v)
                clipboard.selectAll(); clipboard.copy()
            }
        }
    }
    TextEdit { id: clipboard; visible: false }
}
