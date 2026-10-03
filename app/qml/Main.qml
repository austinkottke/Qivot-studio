import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// The window: a tab per open database (each a DatabasePane, which keeps its
/// own screens and place), and a new tab starts on the welcome screen.
ApplicationWindow {
    id: win
    width: 1180; height: 760
    minimumWidth: 860; minimumHeight: 520
    visible: true
    color: Theme.window
    title: current && current.anyOpen ? current.tabTitle + " — Qivot Studio" : "Qivot Studio"

    // Set by main.cpp from the command line.
    property string startupFile: ""
    property var startupConnection: ({})        // from --connect
    property bool startupSample: false
    property string startupSampleId: ""
    property bool startupAllowChanges: false  // --allow-changes
    property bool startupEditDemo: false      // --edit-demo
    property string startupCompare: ""        // --compare-with
    property string startupExportDiagram: ""  // --export-diagram
    property bool startupExplain: false       // --explain
    property bool startupCompleteDemo: false  // --complete-demo
    property string startupTable: ""
    property string startupView: ""          // "diagram" | "data" | "query" | "" (structure)
    property string startupQuery: ""         // from --query
    property bool startupQueryBuilder: false // --query-builder
    property bool startupBuilderDemo: false  // --builder-demo
    property bool startupBuild: false        // --build: export and build the project on the Export screen
    property string projectsDir: ""          // --projects-dir
    property string startupProject: ""       // --project: a folder to open in the IDE
    property bool startupDesignDemo: false   // --design-demo
    property string startupDesignTab: ""     // --design-tab
    property string startupDesignMenu: ""    // --design-menu
    property string startupDesignTable: ""   // --table, on the designer
    signal buildFinished()                   // --build: --shot waits for this
    property string startupFind: ""          // from --find
    property int startupRow: -1              // from --select-row
    property int startupDetailTab: 0         // --view data / profile / cpp: which tab of a table
    property bool screenshotMode: false      // --shot: ignore the real pointer
    property bool rememberConnections: true  // off for screenshots and tests: they don't add to Recent

    // ---- Tabs: one pane per database ----
    ListModel { id: tabs }                     // { uid }
    property int currentIndex: 0
    property int nextUid: 1
    readonly property var current: paneRepeater.count > currentIndex ? paneRepeater.itemAt(currentIndex) : null
    Component.onCompleted: tabs.append({ uid: nextUid++ })

    function newTab() {
        tabs.append({ uid: nextUid++ })
        currentIndex = tabs.count - 1
    }
    function closeTab(i) {
        const p = paneRepeater.itemAt(i)
        if (tabs.count <= 1) { if (p) p.closeAll(); return }      // the last tab stays, on the welcome screen
        tabs.remove(i)
        if (currentIndex >= tabs.count) currentIndex = tabs.count - 1
        else if (i < currentIndex) currentIndex--
    }
    function showConnectDialog() { if (current) current.showConnectDialog() }   // --connect-dialog

    Shortcut { sequence: "Ctrl+N"; onActivated: win.newTab() }
    Shortcut { sequence: "Ctrl+Shift+]"; onActivated: win.currentIndex = (win.currentIndex + 1) % tabs.count }
    Shortcut { sequence: "Ctrl+Shift+["; onActivated: win.currentIndex = (win.currentIndex + tabs.count - 1) % tabs.count }

    // Shown once there's something to switch between.
    readonly property bool showTabs: tabs.count > 1 || (current !== null && current.anyOpen)
    Rectangle {
        id: tabStrip
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: win.showTabs ? 36 : 0
        visible: win.showTabs
        color: Theme.surfaceRaised
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.separator }
        Flickable {
            anchors { left: parent.left; leftMargin: 8; right: addButton.left; rightMargin: 6; top: parent.top; bottom: parent.bottom }
            contentWidth: tabRow.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            Row {
                id: tabRow
                height: parent.height
                spacing: 2
                Repeater {
                    model: tabs
                    Rectangle {
                        id: chip
                        readonly property var p: paneRepeater.count > index ? paneRepeater.itemAt(index) : null
                        readonly property bool on: index === win.currentIndex
                        readonly property string dialect: p ? p.tabDialect : ""
                        y: 4
                        height: 28
                        width: Math.min(240, label.implicitWidth + badge.width + 52)
                        radius: 7
                        color: on ? Theme.window : chipMouse.containsMouse ? Theme.hover : "transparent"
                        border.width: on ? 1 : 0
                        border.color: Theme.separator
                        Rectangle {
                            id: badge
                            x: 8; anchors.verticalCenter: parent.verticalCenter
                            width: chip.dialect.length ? 24 : 0; height: 16; radius: 4
                            visible: width > 0
                            color: chip.dialect === "sqlite" ? Theme.surface : Theme.accentSoft
                            Text {
                                anchors.centerIn: parent
                                text: chip.dialect === "postgres" ? "PG" : chip.dialect === "mysql" ? "MY"
                                    : chip.dialect === "sqlserver" ? "MS" : chip.dialect === "duckdb" ? "DK"
                                    : chip.dialect === "redis" ? "RD" : "DB"
                                color: chip.dialect === "sqlite" ? Theme.textSecondary : Theme.accent
                                font.pixelSize: 9; font.weight: Font.Bold
                            }
                        }
                        Text {
                            id: label
                            anchors { left: badge.right; leftMargin: badge.visible ? 7 : 4; verticalCenter: parent.verticalCenter }
                            width: Math.min(implicitWidth, 170)
                            text: chip.p && chip.p.anyOpen ? chip.p.tabTitle : "New tab"
                            color: chip.on ? Theme.text : Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            font.weight: chip.on ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            id: chipMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                            onClicked: function (mouse) {
                                if (mouse.button === Qt.MiddleButton) win.closeTab(index)
                                else win.currentIndex = index
                            }
                        }
                        HoverHandler { id: chipHover }
                        ToolTip.visible: chipHover.hovered && chip.p && chip.p.anyOpen; ToolTip.delay: 700
                        ToolTip.text: chip.p ? chip.p.tabDetail : ""
                        Rectangle {
                            anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                            width: 18; height: 18; radius: 4
                            visible: chipMouse.containsMouse || closeMouse.containsMouse || chip.on
                            color: closeMouse.containsMouse ? Theme.hover : "transparent"
                            Rectangle { anchors.centerIn: parent; width: 8; height: 1.4; color: Theme.textSecondary; rotation: 45 }
                            Rectangle { anchors.centerIn: parent; width: 8; height: 1.4; color: Theme.textSecondary; rotation: -45 }
                            MouseArea { id: closeMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.closeTab(index) }
                        }
                    }
                }
            }
        }
        ActionButton {
            id: addButton
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            text: "+"
            implicitHeight: 26
            onClicked: win.newTab()
            HoverHandler { id: addHover }
            ToolTip.visible: addHover.hovered; ToolTip.delay: 600
            ToolTip.text: "Open another database (⌘N)"
        }
    }

    // ---- The panes: only the current one shows ----
    Item {
        anchors { left: parent.left; right: parent.right; top: tabStrip.bottom; bottom: parent.bottom }
        Repeater {
            id: paneRepeater
            model: tabs
            DatabasePane {
                anchors.fill: parent
                visible: index === win.currentIndex
                // The command line is for the first one.
                readonly property bool first: index === 0 && model.uid === 1
                startupFile: first ? win.startupFile : ""
                startupConnection: first ? win.startupConnection : ({})
                startupSample: first && win.startupSample
                startupSampleId: first ? win.startupSampleId : ""
                startupAllowChanges: first && win.startupAllowChanges
                startupEditDemo: first && win.startupEditDemo
                startupCompare: first ? win.startupCompare : ""
                startupExportDiagram: first ? win.startupExportDiagram : ""
                startupExplain: first && win.startupExplain
                startupCompleteDemo: first && win.startupCompleteDemo
                startupTable: first ? win.startupTable : ""
                startupView: first ? win.startupView : ""
                startupQuery: first ? win.startupQuery : ""
                startupQueryBuilder: first && win.startupQueryBuilder
                startupBuilderDemo: first && win.startupBuilderDemo
                startupBuild: first && win.startupBuild
                projectsDir: win.projectsDir
                startupProject: first ? win.startupProject : ""
                startupDesignDemo: first && win.startupDesignDemo
                startupDesignTab: first ? win.startupDesignTab : ""
                startupDesignMenu: first ? win.startupDesignMenu : ""
                startupDesignTable: first ? win.startupDesignTable : ""
                startupFind: first ? win.startupFind : ""
                startupRow: first ? win.startupRow : -1
                detailTab: first ? win.startupDetailTab : 0
                screenshotMode: win.screenshotMode
                rememberConnections: win.rememberConnections
                onBuildFinished: win.buildFinished()
            }
        }
    }

    // ---- Copy any text: right-click it ----
    // Labels, names, types, errors and figures are plain text all over the app;
    // a right-click on one (where nothing else has a menu) offers to copy it,
    // in full even when it's shown cut short.
    property var copyTarget: null
    function isText(item) {
        return item && typeof item.text === "string" && item.text.length > 0 && item.font !== undefined
               && item.visible && item.opacity > 0
    }
    // The topmost visible text under (x, y), in `item`'s coordinates.
    function textAt(item, x, y) {
        const kids = item.children
        for (let i = kids.length - 1; i >= 0; --i) {
            const c = kids[i]
            if (!c.visible || c.opacity === 0 || c.width <= 0 || c.height <= 0) continue
            const p = item.mapToItem(c, x, y)
            if (p.x < 0 || p.y < 0 || p.x >= c.width || p.y >= c.height) continue
            const inner = textAt(c, p.x, p.y)
            if (inner) return inner
            if (isText(c)) return c
        }
        return null
    }
    function copyText(text) {
        clipboardHelper.text = text
        clipboardHelper.selectAll()
        clipboardHelper.copy()
    }
    TextEdit { id: clipboardHelper; visible: false }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: {
            const p = point.position
            const t = win.textAt(win.contentItem, p.x, p.y)
            if (!t) return
            win.copyTarget = t
            copyMenu.popup()
        }
    }
    ContextMenu {
        id: copyMenu
        readonly property string full: win.copyTarget ? String(win.copyTarget.text) : ""
        readonly property string selected: win.copyTarget && win.copyTarget.selectedText ? win.copyTarget.selectedText : ""
        ContextMenuItem {
            text: copyMenu.selected.length ? "Copy selection"
                  : "Copy “" + (copyMenu.full.length > 40 ? copyMenu.full.slice(0, 38).replace(/\s+/g, " ") + "…"
                                                          : copyMenu.full.replace(/\s+/g, " ")) + "”"
            onTriggered: win.copyText(copyMenu.selected.length ? copyMenu.selected : copyMenu.full)
        }
    }
}
