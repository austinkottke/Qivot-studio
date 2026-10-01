import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0

/// Samples: one page per sample database, to flip through (arrows, dots, the
/// names along the top, ← →, or a swipe). Each says what the sample shows and
/// what's in it, opens it here (a read-only SQLite copy), or connects to it on
/// the sample servers.
Item {
    id: root
    property var database
    property string initialSample: ""
    property bool canGoBack: false           // shown from the welcome screen: a way back to it
    signal openSample(string id)
    signal showDiagram()                     // for the sample that's already open
    signal connectSample(string id)          // the connect dialog, filled in for the sample servers
    signal back()

    readonly property var samples: database ? database.samples : []
    readonly property string openId: database ? database.sampleId : ""
    readonly property int current: pages.currentIndex

    // Each sample's colours, for its banner.
    function colours(id) {
        return id === "bookshop"   ? ["#FF9F0A", "#F2542D"]
             : id === "university" ? ["#5E5CE6", "#0A84FF"]
             : id === "company"    ? ["#20B2AA", "#0F6E8C"]
             : id === "music"      ? ["#FF375F", "#AF52DE"]
             :                       ["#5E9BFF", "#3A4FD9"]
    }
    function indexOf(id) {
        for (let i = 0; i < samples.length; ++i)
            if (samples[i].id === id) return i
        return -1
    }
    function go(i) { pages.currentIndex = Math.max(0, Math.min(samples.length - 1, i)) }
    function activate(s) { s.id === openId ? showDiagram() : openSample(s.id) }

    // Start on the one asked for, else the one that's open.
    Component.onCompleted: Qt.callLater(function () {
        go(Math.max(0, indexOf(initialSample.length ? initialSample : openId)))
        root.forceActiveFocus()
    })
    focus: true
    Keys.onLeftPressed: go(current - 1)
    Keys.onRightPressed: go(current + 1)
    Keys.onReturnPressed: if (samples.length) activate(samples[current])
    Keys.onEscapePressed: if (canGoBack) back()

    readonly property string composeCommand: "docker compose -f tools/sample-servers/docker-compose.yml up -d"
    function copy(text) {
        clipboard.text = text
        clipboard.selectAll()
        clipboard.copy()
        copied.restart()
    }
    TextEdit { id: clipboard; visible: false }
    Timer { id: copied; interval: 1600 }

    // ---- Header: title, and every sample by name ----
    Item {
        id: header
        anchors { left: parent.left; leftMargin: 28; right: parent.right; rightMargin: 28; top: parent.top; topMargin: 20 }
        height: titles.height

        Column {
            id: titles
            anchors { left: parent.left; right: chips.left; rightMargin: 16 }
            spacing: 3
            Text {
                visible: root.canGoBack
                text: "‹ Back"
                color: backMouse.containsMouse ? Theme.text : Theme.accent
                font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold
                bottomPadding: 6
                MouseArea { id: backMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: root.back() }
            }
            Text {
                text: "Samples"
                color: Theme.text
                font.pixelSize: Theme.fontTitle; font.weight: Font.Bold
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "Example databases, each built around relationships worth seeing."
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
        }
        // The names: where you are, and a jump to any other.
        Row {
            id: chips
            anchors { right: parent.right; bottom: parent.bottom }
            spacing: 6
            visible: root.width > 760
            Repeater {
                model: root.samples
                Rectangle {
                    readonly property bool on: index === root.current
                    width: chipText.implicitWidth + 24; height: 28; radius: 14
                    color: on ? Theme.text : chipMouse.containsMouse ? Theme.hover : "transparent"
                    border.width: on ? 0 : 1
                    border.color: Theme.separator
                    Behavior on color { ColorAnimation { duration: 120 } }
                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: modelData.title
                        color: parent.on ? Theme.window : Theme.textSecondary
                        font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold
                    }
                    MouseArea { id: chipMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: root.go(index) }
                }
            }
        }
    }

    // ---- The pages ----
    SwipeView {
        id: pages
        anchors { left: parent.left; right: parent.right; top: header.bottom; topMargin: 16; bottom: footer.top; bottomMargin: 10 }
        clip: true

        Repeater {
            model: root.samples
            Item {
                id: page
                readonly property var sample: modelData
                readonly property bool isOpen: modelData.id === root.openId
                readonly property var tint: root.colours(modelData.id)

                Rectangle {
                    id: card
                    anchors { fill: parent; leftMargin: 28; rightMargin: 28 }
                    radius: 16
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.separator
                    clip: true

                    // Banner: the sample's colours, name, size and the way in.
                    Rectangle {
                        id: banner
                        width: parent.width
                        height: 150
                        radius: 16
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: page.tint[0] }
                            GradientStop { position: 1.0; color: page.tint[1] }
                        }
                        Rectangle {            // square off the bottom corners
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                            height: 16
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: page.tint[0] }
                                GradientStop { position: 1.0; color: page.tint[1] }
                            }
                        }
                        Rectangle {
                            id: bigIcon
                            anchors { left: parent.left; leftMargin: 28; verticalCenter: parent.verticalCenter }
                            width: 72; height: 72; radius: 18
                            color: "#33FFFFFF"
                            NavIcon { anchors.centerIn: parent; icon: "sample"; ink: "white"; scale: 2.8 }
                        }
                        Column {
                            anchors { left: bigIcon.right; leftMargin: 20; right: openButton.left; rightMargin: 20
                                      verticalCenter: parent.verticalCenter }
                            spacing: 6
                            Row {
                                spacing: 10
                                Text {
                                    text: page.sample.title
                                    color: "white"
                                    font.pixelSize: 30; font.weight: Font.Bold
                                }
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: page.isOpen
                                    width: openText.implicitWidth + 14; height: 20; radius: 10
                                    color: "#40FFFFFF"
                                    Text { id: openText; anchors.centerIn: parent; text: "OPEN"; color: "white"
                                           font.pixelSize: Theme.fontSmall - 1; font.weight: Font.Bold; font.letterSpacing: 0.5 }
                                }
                            }
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                text: page.sample.summary
                                color: "#F2FFFFFF"
                                font.pixelSize: Theme.fontHeading
                            }
                            Text {
                                text: page.sample.tables + " tables · "
                                      + Number(page.sample.rows).toLocaleString(Qt.locale(), "f", 0) + " rows"
                                color: "#CCFFFFFF"
                                font.pixelSize: Theme.fontBody
                            }
                        }
                        // White button on the banner: open it (or go to it).
                        Rectangle {
                            id: openButton
                            anchors { right: parent.right; rightMargin: 28; verticalCenter: parent.verticalCenter }
                            width: openLabel.implicitWidth + 36; height: 40; radius: 10
                            color: openMouse.pressed ? "#E6FFFFFF" : "white"
                            Text {
                                id: openLabel
                                anchors.centerIn: parent
                                text: page.isOpen ? "Show diagram" : "Open " + page.sample.title
                                color: page.tint[1]
                                font.pixelSize: Theme.fontBody + 1; font.weight: Font.Bold
                            }
                            MouseArea { id: openMouse; anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: root.activate(page.sample) }
                        }
                    }

                    // Body: what it shows and what's in it | on a server.
                    Flickable {
                        id: body
                        anchors { left: parent.left; right: parent.right; top: banner.bottom; bottom: parent.bottom
                                  margins: 24 }
                        contentHeight: grid.height
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { }

                        Grid {
                            id: grid
                            width: body.width
                            columns: width > 720 ? 2 : 1
                            columnSpacing: 32
                            rowSpacing: 22
                            readonly property real colWidth: (width - columnSpacing * (columns - 1)) / columns

                            Column {
                                width: grid.colWidth
                                spacing: 10
                                SectionTitle { text: "WHAT IT SHOWS" }
                                Repeater {
                                    model: page.sample.highlights
                                    Row {
                                        spacing: 10
                                        Rectangle { y: 6; width: 7; height: 7; radius: 3.5; color: page.tint[0] }
                                        Text {
                                            width: grid.colWidth - 17
                                            wrapMode: Text.WordWrap
                                            text: modelData
                                            color: Theme.text
                                            font.pixelSize: Theme.fontBody + 1
                                        }
                                    }
                                }
                                Item { width: 1; height: 6 }
                                SectionTitle { text: "TABLES" }
                                Flow {
                                    width: grid.colWidth
                                    spacing: 6
                                    Repeater {
                                        model: page.sample.tableNames
                                        Rectangle {
                                            width: tableText.implicitWidth + 38; height: 26; radius: 6
                                            color: Theme.surfaceRaised
                                            border.width: 1
                                            border.color: Theme.separator
                                            NavIcon { anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                                                      icon: "table"; ink: Theme.textTertiary; scale: 0.85 }
                                            Text {
                                                id: tableText
                                                anchors { left: parent.left; leftMargin: 27; verticalCenter: parent.verticalCenter }
                                                text: modelData
                                                color: Theme.text
                                                font.pixelSize: Theme.fontSmall + 1
                                                font.family: Theme.monoFont
                                            }
                                        }
                                    }
                                }
                            }

                            Column {
                                width: grid.colWidth
                                spacing: 10
                                SectionTitle { text: "ON A SERVER" }
                                Text {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: "The same rows on PostgreSQL, MySQL and SQL Server, in a database called “"
                                          + page.sample.id + "”. Start the sample servers with Docker, from the Qivot Studio folder:"
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontBody
                                }
                                Rectangle {
                                    width: parent.width
                                    height: 40
                                    radius: Theme.radiusSmall
                                    color: Theme.surfaceRaised
                                    border.width: 1
                                    border.color: Theme.separator
                                    Text {
                                        anchors { left: parent.left; leftMargin: 12; right: copyButton.left; rightMargin: 8
                                                  verticalCenter: parent.verticalCenter }
                                        text: root.composeCommand
                                        color: Theme.text
                                        font.family: Theme.monoFont
                                        font.pixelSize: Theme.fontSmall + 1
                                        elide: Text.ElideRight
                                    }
                                    ActionButton {
                                        id: copyButton
                                        anchors { right: parent.right; rightMargin: 5; verticalCenter: parent.verticalCenter }
                                        implicitHeight: 30
                                        text: copied.running ? "Copied" : "Copy"
                                        onClicked: root.copy(root.composeCommand)
                                    }
                                }
                                ActionButton {
                                    text: "Connect to " + page.sample.title + " on a server…"
                                    onClicked: root.connectSample(page.sample.id)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- Footer: back, dots, forward ----
    Item {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: 14 }
        height: 36

        ArrowButton {
            anchors { right: dots.left; rightMargin: 14; verticalCenter: parent.verticalCenter }
            glyph: "‹"
            enabled: root.current > 0
            onClicked: root.go(root.current - 1)
        }
        PageIndicator {
            id: dots
            anchors.centerIn: parent
            count: pages.count
            currentIndex: pages.currentIndex
            interactive: true
            onCurrentIndexChanged: if (currentIndex !== pages.currentIndex) root.go(currentIndex)
            delegate: Rectangle {
                implicitWidth: index === dots.currentIndex ? 22 : 8
                implicitHeight: 8
                radius: 4
                color: index === dots.currentIndex ? Theme.accent : Theme.separator
                Behavior on implicitWidth { NumberAnimation { duration: 160 } }
            }
        }
        ArrowButton {
            anchors { left: dots.right; leftMargin: 14; verticalCenter: parent.verticalCenter }
            glyph: "›"
            enabled: root.current < pages.count - 1
            onClicked: root.go(root.current + 1)
        }
        Text {
            anchors { right: parent.right; rightMargin: 28; verticalCenter: parent.verticalCenter }
            text: (root.current + 1) + " of " + pages.count + "  ·  ← → to browse"
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSmall + 1
        }
    }

    component SectionTitle: Text {
        color: Theme.textTertiary
        font.pixelSize: Theme.fontSmall; font.weight: Font.Bold; font.letterSpacing: 0.6
    }
    component ArrowButton: Rectangle {
        property string glyph
        signal clicked()
        width: 32; height: 32; radius: 16
        opacity: enabled ? 1 : 0.35
        color: arrowMouse.containsMouse && enabled ? Theme.hover : "transparent"
        border.width: 1
        border.color: Theme.separator
        Text { anchors.centerIn: parent; anchors.verticalCenterOffset: -1; text: parent.glyph
               color: Theme.text; font.pixelSize: 20 }
        MouseArea { id: arrowMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: if (parent.enabled) parent.clicked() }
    }
}
