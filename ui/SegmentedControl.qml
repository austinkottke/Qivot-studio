import QtQuick 2.15

/// A row of mutually exclusive options, e.g. "Structure | Data".
Rectangle {
    id: root
    property var options: []            // labels
    property int currentIndex: 0
    signal activated(int index)

    implicitWidth: row.implicitWidth + 4
    implicitHeight: 30
    radius: 8
    color: Theme.surfaceRaised

    Row {
        id: row
        anchors.centerIn: parent
        Repeater {
            model: root.options
            Rectangle {
                width: label.implicitWidth + 28
                height: root.height - 4
                radius: 6
                color: index === root.currentIndex ? Theme.surface : "transparent"
                border.width: index === root.currentIndex ? 1 : 0
                border.color: Theme.separator
                Behavior on color { ColorAnimation { duration: 120 } }
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    color: index === root.currentIndex ? Theme.text : Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    font.weight: index === root.currentIndex ? Font.DemiBold : Font.Normal
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { root.currentIndex = index; root.activated(index) }
                }
            }
        }
    }
}
