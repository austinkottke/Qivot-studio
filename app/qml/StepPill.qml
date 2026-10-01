import QtQuick 2.15
import QivotUI 1.0

/// A build step: `status` is "waiting", "running", "done" or "failed".
Rectangle {
    id: root
    property string label
    property string status: "waiting"
    readonly property color ink: status === "running" ? Theme.accent
                               : status === "done" ? Theme.positive
                               : status === "failed" ? Theme.danger : Theme.textTertiary
    height: 26
    width: pillText.implicitWidth + 24
    radius: 13
    color: status === "running" ? Theme.accentSoft : "transparent"
    border.width: 1
    border.color: status === "waiting" ? Theme.separator : ink
    Text {
        id: pillText
        anchors.centerIn: parent
        text: ({ running: "● ", done: "✓ ", failed: "✗ ", waiting: "" })[root.status] + root.label
        color: root.ink
        font.pixelSize: Theme.fontSmall + 1
        font.weight: Font.DemiBold
    }
}
