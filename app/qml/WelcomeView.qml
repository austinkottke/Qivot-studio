import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// First screen: open a database, connect to a server, browse the samples, or
/// drop a file — or pick up where you were: saved connections and recent files.
Rectangle {
    id: root
    property string error
    signal openRequested()
    signal samplesRequested()
    signal connectRequested()
    signal fileDropped(url url)
    signal recentOpened(var entry)            // from the list: a file, or a server to connect to

    color: drop.containsDrag ? Theme.accentSoft : Theme.window
    Behavior on color { ColorAnimation { duration: 120 } }

    // Scrolls when the window is shorter than the page; centred when it isn't.
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, page.height + 64)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

    Column {
        id: page
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(32, (flick.height - height) / 2)
        spacing: 0
        width: Math.min(680, root.width - 48)

        // App mark: a rounded tile with a stylised table.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 84; height: 84; radius: 20
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#5E9BFF" }
                GradientStop { position: 1.0; color: "#3A4FD9" }
            }
            Rectangle {
                anchors.centerIn: parent
                width: 44; height: 36; radius: 5; color: "transparent"
                border.width: 3; border.color: "white"
                Rectangle { y: 11; width: parent.width; height: 3; color: "white" }
                Rectangle { x: 15; y: 11; width: 3; height: parent.height - 11; color: "white" }
            }
        }

        Item { width: 1; height: 22 }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Qivot Studio"
            color: Theme.text
            font.pixelSize: 30; font.weight: Font.Bold
        }
        Item { width: 1; height: 8 }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: "Explore any database: SQLite, DuckDB, PostgreSQL, MySQL and SQL Server. Tables, keys, relationships and data."
            color: Theme.textSecondary
            font.pixelSize: Theme.fontHeading
        }

        Item { width: 1; height: 28 }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            ActionButton { text: "Open Database…"; primary: true; onClicked: root.openRequested() }
            ActionButton { text: "Connect to Server…"; onClicked: root.connectRequested() }
            ActionButton { text: "Samples"; onClicked: root.samplesRequested() }
        }

        Item { width: 1; height: 18 }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "or drop a database file (SQLite or DuckDB) anywhere in this window · files open read-only"
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSmall + 1
        }

        Item { width: 1; height: 16 }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: root.error.length > 0
            text: root.error
            color: Theme.danger
            font.pixelSize: Theme.fontBody
        }

        // ---- Saved connections, then what was opened lately ----
        Item { width: 1; height: ConnectionHistory.entries.length ? 26 : 0 }
        Repeater {
            model: ConnectionHistory.entries
            Column {
                width: page.width
                readonly property var prev: index > 0 ? ConnectionHistory.entries[index - 1] : null
                Text {     // a heading where the group starts
                    visible: !parent.prev || parent.prev.saved !== modelData.saved
                    topPadding: index > 0 ? 14 : 0
                    bottomPadding: 6
                    leftPadding: 10
                    text: modelData.saved ? "SAVED" : "RECENT"
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSmall; font.weight: Font.Bold; font.letterSpacing: 0.6
                }
                Rectangle {
                    id: rowBox
                    width: page.width
                    height: 46
                    radius: 8
                    color: rowMouse.containsMouse ? Theme.hover : "transparent"
                    opacity: modelData.missing ? 0.55 : 1
                    // What it is: a file, or which server.
                    Rectangle {
                        id: badge
                        x: 10; anchors.verticalCenter: parent.verticalCenter
                        width: 30; height: 30; radius: 7
                        color: modelData.kind === "file" ? Theme.surfaceRaised : Theme.accentSoft
                        Text {
                            anchors.centerIn: parent
                            text: { const t = modelData.settings.type
                                    return t === "postgres" ? "PG" : t === "mysql" ? "MY" : t === "sqlserver" ? "MS" : t === "duckdb" ? "DK" : t === "redis" ? "RD" : "DB" }
                            color: modelData.kind === "file" ? Theme.textSecondary : Theme.accent
                            font.pixelSize: 10; font.weight: Font.Bold
                        }
                    }
                    Column {
                        anchors { left: badge.right; leftMargin: 12; right: actions.left; rightMargin: 10
                                  verticalCenter: parent.verticalCenter }
                        spacing: 2
                        Text {
                            width: parent.width
                            text: modelData.title
                            color: Theme.text
                            font.pixelSize: Theme.fontBody + 1; font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: modelData.missing ? "Not found · " + modelData.detail : modelData.detail
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSmall + 1
                            elide: Text.ElideMiddle
                        }
                    }
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.recentOpened(modelData)
                    }
                    // Keep it (★) or let it go (×).
                    Row {
                        id: actions
                        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        spacing: 2
                        visible: rowMouse.containsMouse || starMouse.containsMouse || forgetMouse.containsMouse || modelData.saved
                        Rectangle {
                            width: 26; height: 26; radius: 6
                            color: starMouse.containsMouse ? Theme.surfaceRaised : "transparent"
                            Text { anchors.centerIn: parent; text: modelData.saved ? "★" : "☆"
                                   color: modelData.saved ? Theme.warning : Theme.textTertiary; font.pixelSize: 15 }
                            MouseArea {
                                id: starMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: ConnectionHistory.setSaved(modelData.id, !modelData.saved)
                            }
                            HoverHandler { id: starHover }
                            ToolTip.visible: starHover.hovered; ToolTip.delay: 600
                            ToolTip.text: modelData.saved ? "Remove from Saved" : "Save this connection"
                        }
                        Rectangle {
                            width: 26; height: 26; radius: 6
                            visible: rowMouse.containsMouse || starMouse.containsMouse || forgetMouse.containsMouse
                            color: forgetMouse.containsMouse ? Theme.surfaceRaised : "transparent"
                            Rectangle { anchors.centerIn: parent; width: 9; height: 1.5; color: Theme.textSecondary; rotation: 45 }
                            Rectangle { anchors.centerIn: parent; width: 9; height: 1.5; color: Theme.textSecondary; rotation: -45 }
                            MouseArea {
                                id: forgetMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: ConnectionHistory.forget(modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }

    }

    DropArea {
        id: drop
        anchors.fill: parent
        onDropped: (event) => {
            if (event.hasUrls && event.urls.length)
                root.fileDropped(event.urls[0])
        }
    }
}
