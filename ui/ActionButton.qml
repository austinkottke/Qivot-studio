import QtQuick 2.15

/// A push button. `primary` for the one main action on a screen.
Rectangle {
    id: root
    property string text
    property bool primary: false
    signal clicked()

    implicitWidth: label.implicitWidth + 36
    implicitHeight: 38
    radius: 8
    activeFocusOnTab: true

    color: primary
        ? (mouse.pressed ? Qt.darker(Theme.accent, 1.15)
                         : mouse.containsMouse ? Qt.lighter(Theme.accent, 1.08) : Theme.accent)
        : (mouse.pressed ? Theme.pressed : mouse.containsMouse ? Theme.hover : Theme.surface)
    border.width: primary ? 0 : 1
    border.color: Theme.separator
    Behavior on color { ColorAnimation { duration: 90 } }

    // Focus ring for keyboard users.
    Rectangle {
        anchors.fill: parent; anchors.margins: -3
        radius: parent.radius + 3; color: "transparent"
        border.width: 2; border.color: Theme.accentSoft
        visible: root.activeFocus
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.primary ? "white" : Theme.text
        font.pixelSize: Theme.fontBody
        font.weight: Font.DemiBold
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        enabled: root.enabled
        onClicked: root.clicked()
    }
    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
}
