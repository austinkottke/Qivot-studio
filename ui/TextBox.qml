import QtQuick 2.15

/// A labelled single-line text field. `password: true` masks the text.
Column {
    id: root
    property string label
    property alias text: input.text
    property string placeholder
    property bool password: false
    property alias inputMethodHints: input.inputMethodHints
    property alias validator: input.validator
    signal accepted()

    function focusField() { input.forceActiveFocus() }

    spacing: 5

    Text {
        visible: root.label.length > 0
        text: root.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSmall + 1
        font.weight: Font.DemiBold
    }
    Rectangle {
        width: root.width
        height: 34
        radius: 7
        color: Theme.surface
        border.width: input.activeFocus ? 2 : 1
        border.color: input.activeFocus ? Theme.accent : Theme.separator

        TextInput {
            id: input
            anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
            verticalAlignment: TextInput.AlignVCenter
            echoMode: root.password ? TextInput.Password : TextInput.Normal
            color: Theme.text
            selectionColor: Theme.accentSoft
            selectedTextColor: Theme.text
            font.pixelSize: Theme.fontBody + 1
            clip: true
            selectByMouse: true
            activeFocusOnTab: true
            onAccepted: root.accepted()
        }
        Text {
            anchors.fill: input
            verticalAlignment: Text.AlignVCenter
            visible: input.text.length === 0
            text: root.placeholder
            color: Theme.textTertiary
            font.pixelSize: Theme.fontBody + 1
            elide: Text.ElideRight
        }
    }
}
