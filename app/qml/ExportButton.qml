import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0

/// "Export ▾": CSV or JSON, to a file the user picks. `target` is anything
/// with exportTo(file, format) — a table's rows or a query's result. What
/// happened is in `message` (and `failed`).
ActionButton {
    id: root
    property var target
    property string message: ""
    property bool failed: false
    text: "Export ▾"
    implicitHeight: 28
    onClicked: menu.popup(root, 0, root.height + 4)

    ContextMenu {
        id: menu
        implicitWidth: 200
        ContextMenuItem { text: "CSV…"; onTriggered: { dialog.kind = "csv"; dialog.open() } }
        ContextMenuItem { text: "JSON…"; onTriggered: { dialog.kind = "json"; dialog.open() } }
    }
    DataFileDialog {
        id: dialog
        saving: true
        onPicked: function (file) {
            const r = root.target.exportTo(file, kind)
            root.failed = !r.ok
            root.message = r.ok ? "Exported " + Number(r.rows).toLocaleString(Qt.locale(), "f", 0)
                                  + (r.rows === 1 ? " row to " : " rows to ") + String(r.path).slice(String(r.path).lastIndexOf("/") + 1)
                                : r.error
            clearMessage.restart()
        }
    }
    Timer { id: clearMessage; interval: 6000; onTriggered: root.message = "" }
}
