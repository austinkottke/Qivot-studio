import QtQuick 2.15

/// A small pill label. `tone`: "neutral", "accent", "key", "link", "positive", "warning", "danger".
Rectangle {
    id: root
    property string text
    property string tone: "neutral"

    readonly property color fg: tone === "accent" ? Theme.accent
                              : tone === "key" ? Theme.key
                              : tone === "link" ? Theme.link
                              : tone === "positive" ? Theme.positive
                              : tone === "warning" ? Theme.warning
                              : tone === "danger" ? Theme.danger
                              : Theme.textSecondary
    readonly property color bg: tone === "accent" ? Theme.accentSoft
                              : tone === "key" ? Theme.keySoft
                              : tone === "link" ? Theme.linkSoft
                              : tone === "warning" ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.16)
                              : tone === "danger" ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.16)
                              : Theme.surfaceRaised

    implicitWidth: label.implicitWidth + 12
    implicitHeight: 18
    radius: 4
    color: bg

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.fg
        font.pixelSize: 10
        font.weight: Font.Bold
        font.letterSpacing: 0.4
    }
}
