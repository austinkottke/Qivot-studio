import QtQuick 2.15

/// A folder or a file page, drawn so it tints with `ink`.
Item {
    id: root
    property bool folder
    property color ink
    width: 14; height: 14
    // folder: a tab and a body
    Rectangle { visible: root.folder; x: 0; y: 1; width: 6; height: 3; radius: 1; color: root.ink }
    Rectangle { visible: root.folder; y: 3; width: 14; height: 10; radius: 2; color: root.ink; opacity: 0.85 }
    // file: a page with lines
    Rectangle {
        visible: !root.folder
        x: 2; width: 10; height: 14; radius: 2
        color: "transparent"; border.width: 1.4; border.color: root.ink
        Column {
            x: 2.5; y: 4; spacing: 2
            Repeater { model: 3; Rectangle { width: 5; height: 1.2; color: root.ink } }
        }
    }
}
