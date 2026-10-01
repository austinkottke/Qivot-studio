import QtQuick 2.15

/// The sidebar's 14 px line icons, drawn so they take any colour (`ink`).
/// `icon`: "table", "view", "virtual", "diagram", "query", "project", "design" or "sample".
Item {
    id: root
    property string icon: "table"
    property color ink: Theme.textSecondary
    width: 14; height: 14

    // table: a framed grid
    Rectangle {
        visible: root.icon === "table" || root.icon === "virtual"
        anchors.fill: parent; radius: 2; color: "transparent"
        border.width: 1.4; border.color: root.ink
        Rectangle { y: 4.5; width: parent.width; height: 1.4; color: root.ink }
        Rectangle { x: 5.8; y: 4.5; width: 1.4; height: parent.height - 4.5; color: root.ink
                    visible: root.icon === "table" }
    }
    // diagram: two linked boxes
    Item {
        visible: root.icon === "diagram"
        anchors.fill: parent
        Rectangle { x: 0; y: 0; width: 6; height: 6; radius: 1.5; color: "transparent"
                    border.width: 1.4; border.color: root.ink }
        Rectangle { x: 8; y: 8; width: 6; height: 6; radius: 1.5; color: "transparent"
                    border.width: 1.4; border.color: root.ink }
        Rectangle { x: 5.5; y: 2.3; width: 6; height: 1.4; color: root.ink }
        Rectangle { x: 10.3; y: 2.3; width: 1.4; height: 6; color: root.ink }
    }
    // query: a prompt  >_
    Item {
        visible: root.icon === "query"
        anchors.fill: parent
        Rectangle { x: 1; y: 4.2; width: 6; height: 1.5; radius: 0.75; color: root.ink; rotation: 35; transformOrigin: Item.Left }
        Rectangle { x: 1; y: 8.6; width: 6; height: 1.5; radius: 0.75; color: root.ink; rotation: -35; transformOrigin: Item.Left }
        Rectangle { x: 7.5; y: 11; width: 6; height: 1.5; radius: 0.75; color: root.ink }
    }
    // design: a pencil
    Item {
        visible: root.icon === "design"
        anchors.fill: parent
        Rectangle { x: 2; y: 5.5; width: 12; height: 3.4; radius: 0.8; rotation: -45; color: "transparent"
                    border.width: 1.4; border.color: root.ink }
        Rectangle { x: 0.5; y: 12; width: 3; height: 1.5; radius: 0.7; color: root.ink }
    }
    // project: a box with an arrow out of it (export)
    Item {
        visible: root.icon === "project"
        anchors.fill: parent
        Rectangle { x: 0.5; y: 5; width: 13; height: 8.5; radius: 2; color: "transparent"
                    border.width: 1.4; border.color: root.ink }
        Rectangle { x: 6.3; y: 0; width: 1.4; height: 8; color: root.ink }
        Rectangle { x: 6.8; y: 0.2; width: 4.2; height: 1.4; radius: 0.7; color: root.ink; rotation: 45; transformOrigin: Item.Left }
        Rectangle { x: 7.2; y: 0.2; width: 4.2; height: 1.4; radius: 0.7; color: root.ink; rotation: 135; transformOrigin: Item.Left }
    }
    // sample: a database (a cylinder, seen from the side)
    Item {
        visible: root.icon === "sample"
        anchors.fill: parent
        Rectangle { x: 1; y: 0.5; width: 12; height: 13; radius: 3; color: "transparent"
                    border.width: 1.4; border.color: root.ink }
        Rectangle { x: 1; y: 4.6; width: 12; height: 1.4; color: root.ink }
        Rectangle { x: 1; y: 8.6; width: 12; height: 1.4; color: root.ink }
    }
    // view: an eye
    Rectangle {
        visible: root.icon === "view"
        anchors.centerIn: parent; width: 14; height: 9; radius: 4.5; color: "transparent"
        border.width: 1.4; border.color: root.ink
        Rectangle { anchors.centerIn: parent; width: 4; height: 4; radius: 2; color: root.ink }
    }
}
