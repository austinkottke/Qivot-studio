import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0

/// First screen: open a database, connect to a server, browse the samples, or drop a file.
Rectangle {
    id: root
    property string error
    signal openRequested()
    signal samplesRequested()
    signal connectRequested()
    signal fileDropped(url url)

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
            text: "Explore any database: SQLite, PostgreSQL, MySQL and SQL Server. Tables, keys, relationships and data."
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
            text: "or drop a .db file anywhere in this window · files open read-only"
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
