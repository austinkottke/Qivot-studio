import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0

/// "Allow changes to this database?": the one way Studio gets to write.
/// Opens the session's writable connection on confirmation, then emits allowed().
Popup {
    id: root
    property var database
    signal allowed()

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 440
    padding: 24
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#59000000" }
    background: Rectangle { radius: 14; color: Theme.window; border.width: 1; border.color: Theme.separator }

    property string failure: ""
    onAboutToShow: failure = ""

    contentItem: Column {
        width: root.availableWidth
        spacing: 14
        Text {
            text: "Allow changes to " + (root.database ? root.database.displayName : "") + "?"
            color: Theme.text
            font.pixelSize: 18; font.weight: Font.Bold
            width: root.availableWidth; wrapMode: Text.Wrap
        }
        Text {
            width: root.availableWidth
            wrapMode: Text.Wrap
            lineHeight: 1.25
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
            text: "Studio will be able to change this database: apply designs, edit rows and import data. "
                  + "Every change is shown as SQL before it runs"
                  + (root.database && !root.database.isServer ? ", and the file is backed up before a design is applied." : ".")
                  + "\n\nUntil then — and after you lock it again — Studio only reads."
        }
        Text {
            width: root.availableWidth
            visible: root.failure.length > 0
            wrapMode: Text.Wrap
            text: root.failure
            color: Theme.danger
            font.pixelSize: Theme.fontBody
        }
        Row {
            anchors.right: parent.right
            spacing: 10
            ActionButton { text: "Cancel"; onClicked: root.close() }
            ActionButton {
                text: "Allow changes"
                primary: true
                onClicked: {
                    if (root.database.allowChanges(true)) { root.close(); root.allowed() }
                    else root.failure = root.database.error
                }
            }
        }
    }
}
