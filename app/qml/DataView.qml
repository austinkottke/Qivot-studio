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
    signal changesRequested()                 // "allow changes" (Main asks the user)
    property bool editDemo: false             // --edit-demo: a few unsaved edits, to show them
    Timer {
        interval: 300; running: root.editDemo && rows.editable; onTriggered: {
            rows.setCell(0, 1, "Northwind Press")
            rows.setCell(1, 2, null)
            rows.toggleDelete(3)
            const r = rows.addRow()
            rows.setCell(r, 1, "Quill & Ink")
            results.selectedRow = 0
        }
    }

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
        // Editing: add a row, or how to be able to.
        ActionButton {
            visible: rows.editable
            anchors.verticalCenter: parent.verticalCenter
            text: "+ Row"
            implicitHeight: 28
            onClicked: {
                const r = rows.addRow()
                if (r >= 0) { results.selectedRow = r; results.positionAt(r) }
            }
        }
        Text {
            visible: !rows.editable && rows.notEditableReason.length > 0 && root.tableName.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: root.database && !root.database.changesAllowed ? "Read-only · allow changes to edit" : rows.notEditableReason
            color: root.database && !root.database.changesAllowed ? Theme.accent : Theme.textTertiary
            font.pixelSize: Theme.fontBody
            MouseArea {
                anchors.fill: parent
                enabled: root.database && !root.database.changesAllowed
                cursorShape: Qt.PointingHandCursor
                onClicked: root.changesRequested()
            }
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

    // Right of the toolbar: what just happened, then export and import.
    Row {
        id: transfer
        anchors { right: parent.right; verticalCenter: toolbar.verticalCenter }
        spacing: 8
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: text.length > 0
            text: root.transferMessage.length ? root.transferMessage : exportButton.message
            color: exportButton.failed && !root.transferMessage.length ? Theme.danger : Theme.positive
            font.pixelSize: Theme.fontBody
            elide: Text.ElideMiddle
            width: Math.min(implicitWidth, root.width * 0.35)
        }
        ActionButton {
            visible: rows.editable
            anchors.verticalCenter: parent.verticalCenter
            text: "Import CSV…"
            implicitHeight: 28
            onClicked: importFile.open()
        }
        ExportButton { id: exportButton; target: rows; anchors.verticalCenter: parent.verticalCenter }
    }
    property string transferMessage: ""
    Timer { id: clearTransfer; interval: 6000; onTriggered: root.transferMessage = "" }
    DataFileDialog { id: importFile; kind: "csv"; onPicked: function (file) {
        importDialog.start(file)
        if (importDialog.error.length) { root.transferMessage = importDialog.error; clearTransfer.restart() }
    } }
    ImportDialog {
        id: importDialog
        database: root.database
        tableName: root.tableName
        onImported: function (n) {
            root.transferMessage = "Imported " + Number(n).toLocaleString(Qt.locale(), "f", 0) + (n === 1 ? " row" : " rows")
            clearTransfer.restart()
        }
    }

    Text {
        anchors { right: transfer.left; rightMargin: 12; verticalCenter: toolbar.verticalCenter }
        visible: rows.error.length > 0
        text: rows.error
        color: Theme.danger
        font.pixelSize: Theme.fontSmall
        elide: Text.ElideRight
        width: Math.min(implicitWidth, parent.width / 2)
    }

    ResultGrid {
        id: results
        anchors { left: parent.left; right: parent.right; top: toolbar.bottom; topMargin: 12
                  bottom: pendingBar.visible ? pendingBar.top : parent.bottom; bottomMargin: pendingBar.visible ? 10 : 0 }
        model: rows
        editable: rows.editable
        onCellEdited: function (row, column, value) { rows.setCell(row, column, value) }
        onDeleteToggled: function (row) { rows.toggleDelete(row) }
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

    // ---- Unsaved changes: review the SQL, discard, or save ----
    Rectangle {
        id: pendingBar
        visible: rows.pendingCount > 0
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 48
        radius: Theme.radius
        color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.12)
        border.width: 1
        border.color: Theme.warning
        Text {
            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
            text: rows.pendingCount + (rows.pendingCount === 1 ? " unsaved change" : " unsaved changes")
            color: Theme.text
            font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold
        }
        Row {
            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            spacing: 8
            ActionButton { text: "Review SQL"; implicitHeight: 32; onClicked: reviewDialog.open() }
            ActionButton { text: "Discard"; implicitHeight: 32; onClicked: rows.discardChanges() }
            ActionButton { text: "Save"; primary: true; implicitHeight: 32; onClicked: rows.save() }
        }
    }

    Popup {
        id: reviewDialog
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(760, root.width)
        height: Math.min(520, root.height)
        padding: 20
        Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#59000000" }
        background: Rectangle { radius: 14; color: Theme.window; border.width: 1; border.color: Theme.separator }
        contentItem: Item {
            Text {
                id: reviewTitle
                text: "Saving runs " + rows.pendingSql.length + (rows.pendingSql.length === 1 ? " statement" : " statements")
                      + " in one transaction"
                color: Theme.text
                font.pixelSize: 17; font.weight: Font.Bold
            }
            CodeEditor {
                anchors { left: parent.left; right: parent.right; top: reviewTitle.bottom; topMargin: 12
                          bottom: reviewButtons.top; bottomMargin: 12 }
                text: rows.pendingSql.join("\n")
                language: "sql"
                readOnly: true
            }
            Row {
                id: reviewButtons
                anchors { right: parent.right; bottom: parent.bottom }
                spacing: 8
                ActionButton { text: "Close"; onClicked: reviewDialog.close() }
                ActionButton { text: "Save"; primary: true; onClicked: { reviewDialog.close(); rows.save() } }
            }
        }
    }
}
