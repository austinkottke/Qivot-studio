import QtQuick 2.15

/// A sidebar row: icon, label, and a right-aligned detail (e.g. a row count).
/// `icon`: "table", "view", "virtual", "diagram", "query", "project", "design" or "sample".
/// `current` marks the item that's open, without selecting it.
Rectangle {
    id: root
    property string label
    property string detail
    property string icon: "table"
    property bool selected: false
    property bool current: false
    signal clicked()

    implicitHeight: 30
    radius: Theme.radiusSmall
    color: selected ? Theme.accent : mouse.containsMouse ? Theme.hover : "transparent"

    readonly property color ink: selected ? "white" : Theme.textSecondary

    NavIcon {
        id: iconItem
        anchors { left: parent.left; leftMargin: 9; verticalCenter: parent.verticalCenter }
        icon: root.icon
        ink: root.ink
    }

    Text {
        anchors { left: iconItem.right; leftMargin: 8; right: detailText.left; rightMargin: 8
                  verticalCenter: parent.verticalCenter }
        text: root.label
        color: root.selected ? "white" : Theme.text
        font.pixelSize: Theme.fontBody
        font.weight: root.current ? Font.DemiBold : Font.Normal
        elide: Text.ElideRight
    }
    Text {
        id: detailText
        anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
        text: root.detail
        color: root.selected ? "#E6FFFFFF" : root.current ? Theme.accent : Theme.textTertiary
        font.pixelSize: Theme.fontSmall
        font.family: Theme.monoFont
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }
}
