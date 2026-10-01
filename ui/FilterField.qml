import QtQuick 2.15

/// A compact search/filter box with a drawn magnifier and a clear button.
Rectangle {
    id: root
    property alias text: input.text
    property string placeholder: "Filter"

    implicitHeight: 30
    radius: 7
    color: Theme.surface
    border.width: 1
    border.color: input.activeFocus ? Theme.accent : Theme.separator

    Item {
        id: glass
        width: 14; height: 14
        anchors { left: parent.left; leftMargin: 9; verticalCenter: parent.verticalCenter }
        Rectangle { width: 10; height: 10; radius: 5; color: "transparent"
                    border.width: 1.6; border.color: Theme.textSecondary }
        Rectangle { x: 8; y: 9.5; width: 5; height: 1.6; radius: 0.8; color: Theme.textSecondary
                    rotation: 45; transformOrigin: Item.Left }
    }

    TextInput {
        id: input
        anchors { left: glass.right; leftMargin: 6; right: clear.left; rightMargin: 4
                  verticalCenter: parent.verticalCenter }
        color: Theme.text
        selectionColor: Theme.accentSoft
        selectedTextColor: Theme.text
        font.pixelSize: Theme.fontBody
        clip: true
        Keys.onEscapePressed: input.text = ""
    }
    Text {
        anchors.fill: input
        verticalAlignment: Text.AlignVCenter
        text: root.placeholder
        color: Theme.textTertiary
        font.pixelSize: Theme.fontBody
        visible: input.text.length === 0
    }

    Rectangle {
        id: clear
        anchors { right: parent.right; rightMargin: 7; verticalCenter: parent.verticalCenter }
        width: 15; height: 15; radius: 7.5
        color: Theme.textTertiary
        visible: input.text.length > 0
        Rectangle { anchors.centerIn: parent; width: 7; height: 1.5; color: Theme.surface; rotation: 45 }
        Rectangle { anchors.centerIn: parent; width: 7; height: 1.5; color: Theme.surface; rotation: -45 }
        MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: input.text = "" }
    }
}
