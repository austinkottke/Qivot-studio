import QtQuick 2.15
import QtQuick.Controls 2.15

/// An item in a ContextMenu: a check mark when `checked`, an arrow when it
/// opens a submenu, red when `danger` (e.g. Delete).
MenuItem {
    id: item
    property bool danger: false
    implicitHeight: 30
    leftPadding: 30
    rightPadding: 26
    font.pixelSize: Theme.fontBody

    readonly property color ink: !item.enabled ? Theme.textTertiary
                               : item.highlighted ? "white"
                               : item.danger ? Theme.danger : Theme.text
    contentItem: Text {
        text: item.text
        color: item.ink
        font: item.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Text {
        x: 11
        anchors.verticalCenter: parent.verticalCenter
        visible: item.checked
        text: "✓"
        color: item.highlighted ? "white" : Theme.accent
        font.pixelSize: Theme.fontBody
        font.weight: Font.Bold
    }
    arrow: Text {
        x: item.width - width - 10
        anchors.verticalCenter: parent.verticalCenter
        visible: item.subMenu !== null
        text: "›"
        color: item.ink
        font.pixelSize: Theme.fontHeading
    }
    background: Rectangle {
        implicitWidth: 230
        radius: 6
        color: item.highlighted ? (item.danger ? Theme.danger : Theme.accent) : "transparent"
    }
}
