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

    property int selectedRow: -1
    readonly property var selectedValues: selectedRow >= 0 && valuesForRow ? valuesForRow(selectedRow) : ({})

    function forceLayout() { grid.forceLayout() }
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
                implicitWidth: 120
                implicitHeight: 30
                color: row === root.selectedRow ? Theme.accentSoft
                     : row % 2 ? Theme.surfaceRaised : Theme.surface
                Text {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: isNumber || root.isNumericColumn(column) ? Text.AlignRight : Text.AlignLeft
                    text: display
                    color: isNull ? Theme.textTertiary : Theme.text
                    font.italic: isNull
                    font.pixelSize: Theme.fontBody
                    font.family: isNumber ? Theme.monoFont : Qt.application.font.family
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: root.selectedRow = (root.selectedRow === row ? -1 : row)
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
                        TextEdit {
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
}
