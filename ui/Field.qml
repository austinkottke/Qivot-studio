import QtQuick 2.15
import QtQuick.Controls 2.15

/// A one-line text field in the theme (no label; see TextBox for a labelled one).
TextField {
    color: Theme.text
    placeholderTextColor: Theme.textTertiary
    selectByMouse: true
    font.pixelSize: Theme.fontBody
    leftPadding: 10; rightPadding: 10
    implicitHeight: 32
    background: Rectangle {
        radius: Theme.radiusSmall
        color: parent.enabled ? Theme.window : "transparent"
        border.width: 1
        border.color: parent.activeFocus ? Theme.accent : Theme.separator
    }
}
