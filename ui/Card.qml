import QtQuick 2.15

/// A titled surface. Children go in the body, stacked vertically.
Rectangle {
    id: root
    property string title
    property string detail            // shown to the right of the title, e.g. a count
    default property alias content: body.data

    implicitHeight: column.implicitHeight
    radius: Theme.radius
    color: Theme.surface
    border.width: 1
    border.color: Theme.separator

    Column {
        id: column
        width: parent.width

        Item {
            width: parent.width
            height: root.title.length ? 44 : 0
            visible: root.title.length > 0
            Text {
                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.fontHeading
                font.weight: Font.DemiBold
            }
            Text {
                anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                text: root.detail
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.separator }
        }

        Column {
            id: body
            width: parent.width
        }
    }
}
