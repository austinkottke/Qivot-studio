import QtQuick 2.15

/// An on/off chip: `label`, `on`, tinted with `ink`; emits toggled() when clicked.
Rectangle {
    property string label
    property bool on: false
    property color ink: Theme.accent
    signal toggled()
    height: 30
    width: toggleText.implicitWidth + 20
    radius: Theme.radiusSmall
    opacity: enabled ? 1 : 0.4
    color: on ? Qt.rgba(ink.r, ink.g, ink.b, Theme.dark ? 0.22 : 0.14) : "transparent"
    border.width: 1
    border.color: on ? ink : Theme.separator
    Text {
        id: toggleText
        anchors.centerIn: parent
        text: parent.label
        color: parent.on ? parent.ink : Theme.textSecondary
        font.pixelSize: Theme.fontSmall + 1
        font.weight: Font.DemiBold
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.toggled() }
}
