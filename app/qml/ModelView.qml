import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// The C++ tab: the Qivot model class for one table, ready to copy, with a
/// note for anything that doesn't map cleanly.
Item {
    id: root
    property var database
    property string tableName
    readonly property var model: database && tableName.length ? database.cppModel(tableName) : ({})
    readonly property bool hasModel: model.className !== undefined
    readonly property var warnings: hasModel ? model.warnings : []

    function copy(text) {
        clipboard.text = text
        clipboard.selectAll()
        clipboard.copy()
        copied.restart()
    }
    TextEdit { id: clipboard; visible: false }
    Timer { id: copied; interval: 1600 }

    // ---- Toolbar ----
    Item {
        id: toolbar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 36
        visible: root.hasModel

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "class " + (root.hasModel ? root.model.className : "") + " : public QiModel"
            color: Theme.textSecondary
            font.family: Theme.monoFont
            font.pixelSize: Theme.fontBody
        }
        Row {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: 10
            ActionButton {
                text: "Copy all models"
                onClicked: root.copy(root.database.cppHeader())
            }
            ActionButton {
                text: copied.running ? "Copied ✓" : "Copy"
                primary: true
                onClicked: root.copy(root.model.code)
            }
        }
    }

    // ---- Notes: anything Qivot can't express exactly ----
    Card {
        id: notes
        anchors { left: parent.left; right: parent.right; top: toolbar.bottom; topMargin: 14 }
        visible: root.warnings.length > 0
        height: visible ? implicitHeight : 0
        title: "Notes"
        detail: root.warnings.length

        Repeater {
            model: root.warnings
            Item {
                width: notes.width
                height: noteText.implicitHeight + 16
                Rectangle {
                    x: 16; y: 14; width: 6; height: 6; radius: 3
                    color: Theme.warning
                }
                Text {
                    id: noteText
                    anchors { left: parent.left; right: parent.right; leftMargin: 32; rightMargin: 16
                              verticalCenter: parent.verticalCenter }
                    text: (modelData.column.length ? "<b>" + modelData.column + "</b> — " : "") + modelData.message
                    textFormat: Text.StyledText
                    wrapMode: Text.Wrap
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                }
            }
        }
        Item { width: 1; height: 6 }
    }

    // ---- The code ----
    Rectangle {
        anchors { left: parent.left; right: parent.right; top: notes.visible ? notes.bottom : toolbar.bottom
                  topMargin: 14; bottom: parent.bottom }
        visible: root.hasModel
        radius: Theme.radius
        color: Theme.surface
        border.width: 1
        border.color: Theme.separator

        ScrollView {
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            TextArea {
                id: code
                text: root.hasModel ? root.model.code : ""
                readOnly: true
                selectByMouse: true
                wrapMode: TextArea.NoWrap
                font.family: Theme.monoFont
                font.pixelSize: Theme.fontBody + 1
                color: Theme.text
                selectionColor: Theme.accentSoft
                selectedTextColor: Theme.text
                padding: 18
                background: null
            }
        }
        SyntaxHighlighter { document: code.textDocument; language: "cpp"; dark: Theme.dark }
    }

    Text {
        anchors.centerIn: parent
        visible: !root.hasModel
        text: "Qivot can't map this kind of table to a model."
        color: Theme.textSecondary
        font.pixelSize: Theme.fontBody
    }
}
