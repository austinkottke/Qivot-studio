import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0

/// Left column: the open file, a filter, and every table and view.
Rectangle {
    id: root
    property var database
    property string selected
    property bool diagramSelected: false
    property bool querySelected: false
    property bool exportSelected: false
    property bool designSelected: false
    property bool samplesSelected: false
    property bool compareSelected: false
    signal compareRequested()
    signal select(string name)
    signal diagramRequested()
    signal queryRequested()
    signal exportRequested()
    signal designRequested()
    signal closeRequested()
    signal samplesRequested()
    signal allowChangesRequested()
    readonly property bool isSample: database && database.sampleId !== undefined && database.sampleId.length > 0

    color: Theme.sidebar

    function rowsText(n) {
        return n < 0 ? "" : Number(n).toLocaleString(Qt.locale(), "f", 0)
    }
    function matching(kinds) {
        const needle = filter.text.trim().toLowerCase()
        return (database ? database.tables : []).filter(t =>
            kinds.indexOf(t.kind) >= 0 && (!needle.length || t.name.toLowerCase().indexOf(needle) >= 0))
    }
    readonly property var tableItems: (filter.text, matching(["table"]))
    readonly property var viewItems:  (filter.text, matching(["view", "virtual"]))

    // ---- File header ----
    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: (root.isSample ? 82 : 64) + 24

        Column {
            anchors { left: parent.left; leftMargin: 16; right: closeButton.left; rightMargin: 8
                      verticalCenter: parent.verticalCenter }
            spacing: 3
            Text {
                width: parent.width
                text: root.database ? root.database.displayName : ""
                color: Theme.text
                font.pixelSize: Theme.fontHeading; font.weight: Font.DemiBold
                elide: Text.ElideMiddle
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: !root.database || !root.database.isOpen ? ""
                      : root.database.isServer
                        ? root.database.dialectName + " · " + root.database.location
                        : root.database.tables.length + " objects · "
                          + (root.database.fileSize / 1048576).toFixed(1) + " MB"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
            }
            // Read-only, or changes allowed: one click to lock again, a confirmation to unlock.
            Rectangle {
                readonly property bool open: root.database && root.database.changesAllowed
                width: lockText.implicitWidth + 18; height: 20; radius: 10
                color: open ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.16)
                            : lockMouse.containsMouse ? Theme.hover : "transparent"
                border.width: 1
                border.color: open ? Theme.warning : Theme.separator
                Text {
                    id: lockText
                    anchors.centerIn: parent
                    text: parent.open ? "✎ Changes allowed · lock" : (lockMouse.containsMouse ? "Read-only · allow changes…" : "Read-only")
                    color: parent.open ? Theme.warning : Theme.textSecondary
                    font.pixelSize: Theme.fontSmall; font.weight: Font.DemiBold
                }
                MouseArea {
                    id: lockMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: parent.open ? root.database.allowChanges(false) : root.allowChangesRequested()
                }
            }
            // A sample: the way back to all of them (closing this one).
            Text {
                visible: root.isSample
                topPadding: 3
                text: "‹ All samples"
                color: allMouse.containsMouse ? Theme.text : Theme.accent
                font.pixelSize: Theme.fontSmall + 1; font.weight: Font.DemiBold
                MouseArea { id: allMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: root.closeRequested() }
            }
        }

        // Close the file (back to the welcome screen).
        Rectangle {
            id: closeButton
            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
            width: 26; height: 26; radius: 6
            color: closeMouse.containsMouse ? Theme.hover : "transparent"
            Rectangle { anchors.centerIn: parent; width: 11; height: 1.6; radius: 0.8
                        color: Theme.textSecondary; rotation: 45 }
            Rectangle { anchors.centerIn: parent; width: 11; height: 1.6; radius: 0.8
                        color: Theme.textSecondary; rotation: -45 }
            MouseArea { id: closeMouse; anchors.fill: parent; hoverEnabled: true
                        onClicked: root.closeRequested() }
            ToolTip.visible: closeMouse.containsMouse
            ToolTip.text: root.isSample ? "Close the sample (back to all samples)" : "Close database"
            ToolTip.delay: 500
        }
    }

    FilterField {
        id: filter
        anchors { left: parent.left; right: parent.right; top: header.bottom
                  leftMargin: 12; rightMargin: 12 }
        placeholder: "Filter tables"
    }

    // ---- Tables and views ----
    Flickable {
        id: list
        anchors { left: parent.left; right: parent.right; top: filter.bottom; bottom: parent.bottom
                  topMargin: 10 }
        contentHeight: content.height + 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        Column {
            id: content
            x: 8; width: parent.width - 16
            spacing: 1

            component SectionLabel: Text {
                leftPadding: 8; topPadding: 10; bottomPadding: 4
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSmall; font.weight: Font.Bold; font.letterSpacing: 0.6
            }

            NavItem {
                width: content.width
                label: "Diagram"
                detail: ""
                icon: "diagram"
                selected: root.diagramSelected
                onClicked: root.diagramRequested()
            }
            NavItem {
                width: content.width
                label: "Query"
                detail: ""
                icon: "query"
                selected: root.querySelected
                onClicked: root.queryRequested()
            }
            NavItem {
                width: content.width
                label: "Designer"
                detail: ""
                icon: "design"
                selected: root.designSelected
                onClicked: root.designRequested()
            }
            NavItem {
                width: content.width
                label: "Compare"
                detail: ""
                icon: "compare"
                selected: root.compareSelected
                onClicked: root.compareRequested()
            }
            NavItem {
                width: content.width
                label: "Export"
                detail: "C++"
                icon: "project"
                selected: root.exportSelected
                onClicked: root.exportRequested()
            }
            NavItem {
                width: content.width
                label: "Samples"
                detail: root.database ? String(root.database.samples.length) : ""
                icon: "sample"
                selected: root.samplesSelected
                onClicked: root.samplesRequested()
            }

            SectionLabel { text: "TABLES  " + root.tableItems.length }
            Repeater {
                model: root.tableItems
                NavItem {
                    width: content.width
                    label: modelData.name
                    detail: root.rowsText(modelData.rows)
                    icon: "table"
                    selected: modelData.name === root.selected
                    onClicked: root.select(modelData.name)
                }
            }

            SectionLabel { text: "VIEWS  " + root.viewItems.length; visible: root.viewItems.length > 0 }
            Repeater {
                model: root.viewItems
                NavItem {
                    width: content.width
                    label: modelData.name
                    detail: root.rowsText(modelData.rows)
                    icon: modelData.kind === "view" ? "view" : "virtual"
                    selected: modelData.name === root.selected
                    onClicked: root.select(modelData.name)
                }
            }

            Text {
                visible: root.tableItems.length + root.viewItems.length === 0
                leftPadding: 8; topPadding: 12
                text: "Nothing matches “" + filter.text + "”"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
        }
    }
}
