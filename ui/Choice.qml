import QtQuick 2.15
import QtQuick.Controls 2.15

/// A drop-down in the theme. `editable: true` also takes typed text (accepted() on Enter or leaving).
ComboBox {
    implicitHeight: 32
    font.pixelSize: Theme.fontBody
    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.window
        border.width: 1
        border.color: parent.activeFocus ? Theme.accent : Theme.separator
    }
    contentItem: TextField {
        text: parent.editable ? parent.editText : parent.displayText
        readOnly: !parent.editable
        enabled: true
        color: Theme.text
        font: parent.font
        leftPadding: 10
        verticalAlignment: Text.AlignVCenter
        selectByMouse: parent.editable
        background: null
        onTextEdited: if (parent.editable) parent.editText = text
        onAccepted: if (parent.editable) parent.accepted()
        onEditingFinished: if (parent.editable && parent.editText !== parent.displayText) parent.accepted()
        MouseArea {
            anchors.fill: parent
            enabled: !parent.parent.editable
            onClicked: parent.parent.popup.open()
        }
    }
}
