import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// The rows of one table: filter, sort by clicking a header, click a row to inspect it.
Item {
    id: root
    property var database
    property string tableName
    property int initialRow: -1

    Rows {
        id: rows
        session: root.database
        table: root.tableName
        filter: filterField.text
    }
    onTableNameChanged: filterField.text = ""

    function fmt(n) { return Number(n).toLocaleString(Qt.locale(), "f", 0) }

    // ---- Toolbar ----
    Row {
        id: toolbar
        anchors { left: parent.left; top: parent.top }
        spacing: 14
        height: 30

        FilterField {
            id: filterField
            width: 280
            anchors.verticalCenter: parent.verticalCenter
            placeholder: "Filter rows (any column)"
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: rows.matchingRows === rows.totalRows
                  ? root.fmt(rows.totalRows) + (rows.totalRows === 1 ? " row" : " rows")
                  : root.fmt(rows.matchingRows) + " of " + root.fmt(rows.totalRows) + " rows"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            visible: rows.sortColumn >= 0
            Text {
                text: rows.sortColumn >= 0
                      ? "Sorted by " + rows.columns[rows.sortColumn].name + (rows.sortDescending ? " ↓" : " ↑")
                      : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
            Text {
                text: "Clear"
                color: Theme.accent
                font.pixelSize: Theme.fontBody
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: rows.clearSort() }
            }
        }
    }

    Text {
        anchors { right: parent.right; verticalCenter: toolbar.verticalCenter }
        visible: rows.error.length > 0
        text: rows.error
        color: Theme.danger
        font.pixelSize: Theme.fontSmall
        elide: Text.ElideRight
        width: Math.min(implicitWidth, parent.width / 2)
    }

    ResultGrid {
        id: results
        anchors { left: parent.left; right: parent.right; top: toolbar.bottom; topMargin: 12; bottom: parent.bottom }
        model: rows
        columns: rows.columns
        valuesForRow: (r) => rows.rowAt(r)
        sortable: true
        sortColumn: rows.sortColumn
        sortDescending: rows.sortDescending
        onSortRequested: (c) => rows.sortBy(c)
        emptyText: rows.totalRows === 0 ? "This table is empty." : "No rows match “" + filterField.text + "”"
        Connections { target: rows; function onCountsChanged() { results.selectedRow = -1 } }
        Component.onCompleted: if (root.initialRow >= 0) Qt.callLater(() => results.selectedRow = root.initialRow)
    }
}
