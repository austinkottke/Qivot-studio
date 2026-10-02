import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// Query: build one by pointing (QueryBuilderView), or write SQL in tabs and
/// ⌘↩ to run it; either way the result is in the grid. Read-only on every
/// database (see QueryModel), so it's safe to explore with. Queries run in the
/// background: the window stays usable, and Stop (⌘.) ends one.
Item {
    id: root
    property var database
    property string initialQuery: ""             // from --query
    property bool runInitialQuery: false
    property bool explainInitialQuery: false     // --explain: its plan instead
    property bool completeDemo: false            // --complete-demo
    /// "builder" or "sql"; remembered between runs (--query starts on SQL).
    property string startMode: ""                // --query-builder: "builder"
    property bool builderDemo: false             // --builder-demo
    property string mode: initialQuery.length ? "sql" : startMode.length ? startMode : Prefs.value("query/mode", "builder")
    onModeChanged: Prefs.setValue("query/mode", mode)

    // ---- Tabs: each its own editor and result; kept per database ----
    ListModel { id: tabsModel }                  // { uid, title }
    property int currentTab: 0
    property int nextUid: 1
    readonly property var current: tabRepeater.count > currentTab ? tabRepeater.itemAt(currentTab) : null
    // Command-line demos and screenshots don't overwrite the tabs someone has open.
    readonly property bool persistTabs: !initialQuery.length && !completeDemo
    function tabsKey() { return "query/tabs/" + Qt.md5(library.scope) }

    function newTab(sql, title) {
        const uid = nextUid++
        tabsModel.append({ uid: uid, title: title || "" })
        currentTab = tabsModel.count - 1
        const item = tabRepeater.itemAt(currentTab)
        if (item) { item.setText(sql || ""); Qt.callLater(item.focusEditor) }
        saveTabs.restart()
        return item
    }
    function closeTab(i) {
        if (tabsModel.count <= 1) { const only = tabRepeater.itemAt(0); if (only) { only.setText(""); only.clearResults() } return }
        const t = tabRepeater.itemAt(i)
        if (t) t.stop()
        tabsModel.remove(i)
        if (currentTab >= tabsModel.count) currentTab = tabsModel.count - 1
        else if (i < currentTab) currentTab--
        saveTabs.restart()
    }
    function tabTitle(i) {
        const t = tabsModel.get(i)
        return t && t.title.length ? t.title : "Query " + (i + 1)
    }
    // The tabs as they were for this database, or one with a first query.
    function restoreTabs() {
        tabsModel.clear()
        currentTab = 0
        let saved = null
        if (persistTabs && library.scope.length) {
            try { saved = JSON.parse(Prefs.value(tabsKey(), "")) } catch (e) { saved = null }
        }
        const list = saved && saved.tabs && saved.tabs.length ? saved.tabs : [{ title: "", sql: initialQuery.length ? initialQuery : starter() }]
        for (let i = 0; i < list.length; ++i) {
            tabsModel.append({ uid: nextUid++, title: list[i].title || "" })
            const item = tabRepeater.itemAt(i)
            if (item) item.setText(list[i].sql || "")
        }
        currentTab = saved ? Math.max(0, Math.min(tabsModel.count - 1, saved.current || 0)) : 0
    }
    Timer {
        id: saveTabs
        interval: 600
        onTriggered: {
            if (!root.persistTabs || !library.scope.length) return
            const tabs = []
            for (let i = 0; i < tabsModel.count; ++i) {
                const item = tabRepeater.itemAt(i)
                tabs.push({ title: tabsModel.get(i).title, sql: item ? item.text : "" })
            }
            Prefs.setValue(root.tabsKey(), JSON.stringify({ tabs: tabs, current: root.currentTab }))
        }
    }
    onCurrentTabChanged: { historyIndex = -1; saveTabs.restart() }

    function currentSql() { return current ? current.currentSql() : "" }
    function run() {
        if (mode === "builder") { builderView.run(); return }
        if (current) current.run()
        historyIndex = -1
    }
    function stop() {
        if (mode === "builder") builderResult.cancel()
        else if (current) current.stop()
    }
    function explain() { if (current) current.explain() }
    readonly property bool running: mode === "builder" ? builderResult.running : (current ? current.running : false)

    // A first query that works on whatever is open.
    function starter() {
        if (!database || !database.isOpen) return ""
        const first = database.tables.find(t => t.kind === "table") || database.tables[0]
        return first ? "SELECT *\nFROM " + first.name + "\nLIMIT 100;" : ""
    }
    Connections {
        target: root.database
        function onOpenChanged() {
            if (root.database.refreshing) return
            historyIndex = -1
            root.restoreTabs()
        }
    }
    Component.onCompleted: {
        restoreTabs()
        if (runInitialQuery && initialQuery.length) Qt.callLater(explainInitialQuery ? explain : run)
        if (completeDemo) Qt.callLater(function () { mode = "sql"; if (current) current.completeDemo() })
    }

    // ---- History and saved queries, per database (QueryLibrary) ----
    QueryLibrary {
        id: library
        scope: root.database && root.database.isOpen
               ? root.database.dialect + ":" + (root.database.filePath.length ? root.database.filePath
                                                                             : root.database.location + "/" + root.database.displayName)
               : ""
    }
    readonly property alias queryLibrary: library
    readonly property var okHistory: library.history.filter(function (h) { return h.ok })
    property int historyIndex: -1
    function step(delta) {
        const h = okHistory
        if (!h.length || !current) return
        historyIndex = Math.max(0, Math.min(h.length - 1, historyIndex + delta))
        current.setText(h[historyIndex].sql)
    }
    property bool panelOpen: Prefs.value("query/panel", "open") === "open"
    onPanelOpenChanged: Prefs.setValue("query/panel", panelOpen ? "open" : "closed")

    // The builder's own result; each SQL tab has one too.
    QueryResult { id: builderResult; session: root.database }
    readonly property alias result: builderResult

    Shortcut { sequence: "Ctrl+Return"; enabled: root.visible; onActivated: root.run() }
    Shortcut { sequence: "Ctrl+Enter";  enabled: root.visible; onActivated: root.run() }
    Shortcut { sequence: "Ctrl+."; enabled: root.visible && root.running; onActivated: root.stop() }
    Shortcut { sequence: "Ctrl+Shift+Return"; enabled: root.visible && root.mode === "sql"; onActivated: root.explain() }
    Shortcut { sequence: "Ctrl+S"; enabled: root.visible && root.mode === "sql"; onActivated: saveDialog.ask("") }
    Shortcut { sequence: "Ctrl+T"; enabled: root.visible && root.mode === "sql"; onActivated: root.newTab("", "") }
    Shortcut { sequence: "Ctrl+W"; enabled: root.visible && root.mode === "sql"; onActivated: root.closeTab(root.currentTab) }

    function fmt(n) { return Number(n).toLocaleString(Qt.locale(), "f", 0) }

    // ---- Toolbar ----
    Item {
        id: toolbar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 38

        Row {
            id: toolbarButtons
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            SegmentedControl {
                anchors.verticalCenter: parent.verticalCenter
                options: [ "Builder", "SQL" ]
                currentIndex: root.mode === "builder" ? 0 : 1
                onActivated: function (i) { root.mode = i === 0 ? "builder" : "sql" }
            }
            Item { width: 4; height: 1 }
            ActionButton {
                text: root.running ? "Stop   ⌘." : "Run   ⌘↩"
                primary: !root.running
                onClicked: root.running ? root.stop() : root.run()
            }
            ActionButton {
                visible: root.mode === "sql"
                text: "Explain"
                onClicked: root.explain()
                HoverHandler { id: explainHover }
                ToolTip.visible: explainHover.hovered; ToolTip.delay: 600
                ToolTip.text: "How the database would run it, without running it (⇧⌘↩)"
            }
            ActionButton { visible: root.mode === "sql"; text: "Save…"; onClicked: saveDialog.ask("") }
            ActionButton {
                visible: root.mode === "sql"
                text: "‹ Older"
                enabled: root.okHistory.length > 0
                opacity: enabled ? 1 : 0.5
                onClicked: root.step(+1)
            }
            ActionButton {
                visible: root.mode === "sql"
                text: "Newer ›"
                enabled: root.historyIndex > 0
                opacity: enabled ? 1 : 0.5
                onClicked: root.step(-1)
            }
        }
        ActionButton {
            id: panelButton
            visible: root.mode === "sql"
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: root.panelOpen ? "Queries ›" : "‹ Queries"
            onClicked: root.panelOpen = !root.panelOpen
        }
        Text {
            // Shortened, then gone, when the buttons need the room.
            anchors { left: toolbarButtons.right; leftMargin: 16
                      right: panelButton.visible ? panelButton.left : parent.right; rightMargin: panelButton.visible ? 12 : 0
                      verticalCenter: parent.verticalCenter }
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
            visible: width > 120
            text: !root.database ? ""
                  : root.database.dialect === "sqlserver"
                    ? "Every statement is rolled back · nothing is changed"
                    : "Read-only session · nothing can be changed"
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSmall + 1
        }
    }

    QueryBuilderView {
        id: builderView
        anchors { left: parent.left; right: parent.right; top: toolbar.bottom; topMargin: 12; bottom: parent.bottom }
        visible: root.mode === "builder"
        database: root.database
        query: root.result
        demo: root.builderDemo
        onEditAsSql: function (sql) { root.newTab(sql, ""); root.mode = "sql" }
    }

    // ---- The tab strip ----
    Item {
        id: tabStrip
        visible: root.mode === "sql"
        anchors { left: parent.left; right: panel.visible ? panel.left : parent.right; rightMargin: panel.visible ? 14 : 0
                  top: toolbar.bottom; topMargin: 10 }
        height: 32
        Flickable {
            id: tabFlick
            anchors { left: parent.left; right: addTab.left; rightMargin: 6; top: parent.top; bottom: parent.bottom }
            contentWidth: tabRow.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            Row {
                id: tabRow
                spacing: 4
                height: parent.height
                Repeater {
                    model: tabsModel
                    Rectangle {
                        id: chip
                        readonly property bool on: index === root.currentTab
                        readonly property var item: tabRepeater.count > index ? tabRepeater.itemAt(index) : null
                        height: 30
                        width: Math.min(220, chipLabel.implicitWidth + (renaming ? 120 : 0) + 44)
                        radius: 7
                        color: on ? Theme.surface : chipMouse.containsMouse ? Theme.hover : "transparent"
                        border.width: on ? 1 : 0
                        border.color: Theme.separator
                        property bool renaming: false
                        // A dot while its query runs.
                        Rectangle {
                            id: runningDot
                            visible: chip.item && chip.item.running
                            x: 10; anchors.verticalCenter: parent.verticalCenter
                            width: 6; height: 6; radius: 3
                            color: Theme.accent
                        }
                        Text {
                            id: chipLabel
                            visible: !chip.renaming
                            anchors { left: parent.left; leftMargin: runningDot.visible ? 22 : 12; verticalCenter: parent.verticalCenter }
                            width: Math.min(implicitWidth, 160)
                            text: root.tabTitle(index) + (model.title, "")
                            color: chip.on ? Theme.text : Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            font.weight: chip.on ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                        }
                        Field {
                            visible: chip.renaming
                            anchors { left: parent.left; leftMargin: 4; right: closeBox.left; verticalCenter: parent.verticalCenter }
                            implicitHeight: 26
                            text: root.tabTitle(index)
                            onVisibleChanged: if (visible) { forceActiveFocus(); selectAll() }
                            onAccepted: { tabsModel.setProperty(index, "title", text.trim()); chip.renaming = false; saveTabs.restart() }
                            Keys.onEscapePressed: chip.renaming = false
                            onActiveFocusChanged: if (!activeFocus) chip.renaming = false
                        }
                        MouseArea {
                            id: chipMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                            onClicked: function (mouse) {
                                if (mouse.button === Qt.MiddleButton) { root.closeTab(index); return }
                                root.currentTab = index
                            }
                            onDoubleClicked: chip.renaming = true
                        }
                        Rectangle {
                            id: closeBox
                            anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                            width: 18; height: 18; radius: 4
                            visible: chipMouse.containsMouse || closeMouse.containsMouse || chip.on
                            color: closeMouse.containsMouse ? Theme.hover : "transparent"
                            Rectangle { anchors.centerIn: parent; width: 8; height: 1.4; color: Theme.textSecondary; rotation: 45 }
                            Rectangle { anchors.centerIn: parent; width: 8; height: 1.4; color: Theme.textSecondary; rotation: -45 }
                            MouseArea { id: closeMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.closeTab(index) }
                        }
                        HoverHandler { id: chipHover }
                        ToolTip.visible: chipHover.hovered && !chip.renaming; ToolTip.delay: 900
                        ToolTip.text: "Double-click to rename · ⌘T new tab · ⌘W close"
                    }
                }
            }
        }
        ActionButton {
            id: addTab
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: "+"
            implicitHeight: 28
            onClicked: root.newTab("", "")
        }
    }

    // ---- The tabs themselves: only the current one shows ----
    Item {
        id: tabArea
        visible: root.mode === "sql"
        anchors { left: parent.left; right: panel.visible ? panel.left : parent.right; rightMargin: panel.visible ? 14 : 0
                  top: tabStrip.bottom; topMargin: 8; bottom: parent.bottom }
        Repeater {
            id: tabRepeater
            model: tabsModel
            SqlTab {
                anchors.fill: parent
                visible: index === root.currentTab
                database: root.database
                library: root.queryLibrary
                onEdited: saveTabs.restart()
            }
        }
    }

    // ---- Queries: what was run here, and what's been saved ----
    function ago(iso) {
        const s = (Date.now() - new Date(iso).getTime()) / 1000
        return s < 60 ? "just now" : s < 3600 ? Math.floor(s / 60) + " min ago"
             : s < 86400 ? Math.floor(s / 3600) + " h ago" : new Date(iso).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }
    property string panelTab: "history"
    Rectangle {
        id: panel
        visible: root.mode === "sql" && root.panelOpen
        anchors { right: parent.right; top: toolbar.bottom; topMargin: 12; bottom: parent.bottom }
        width: Math.min(320, root.width * 0.32)
        radius: Theme.radius
        color: Theme.surface
        border.width: 1; border.color: Theme.separator
        clip: true

        SegmentedControl {
            id: panelTabs
            anchors { left: parent.left; leftMargin: 10; top: parent.top; topMargin: 10 }
            options: [ "History", "Saved  " + library.saved.length ]
            currentIndex: root.panelTab === "saved" ? 1 : 0
            onActivated: function (i) { root.panelTab = i === 1 ? "saved" : "history" }
        }
        FilterField {
            id: panelFilter
            anchors { left: parent.left; right: parent.right; top: panelTabs.bottom; margins: 10 }
            placeholder: root.panelTab === "saved" ? "Find a saved query" : "Find in history"
        }
        ListView {
            id: panelList
            anchors { left: parent.left; right: parent.right; top: panelFilter.bottom; topMargin: 6; bottom: panelFooter.top; margins: 4 }
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }
            model: {
                const needle = panelFilter.text.trim().toLowerCase()
                const list = root.panelTab === "saved" ? library.saved : library.history
                return needle.length ? list.filter(function (q) {
                    return q.sql.toLowerCase().indexOf(needle) >= 0 || (q.name || "").toLowerCase().indexOf(needle) >= 0
                }) : list
            }
            delegate: Rectangle {
                width: panelList.width
                height: entry.implicitHeight + 16
                radius: 6
                color: entryMouse.containsMouse ? Theme.hover : "transparent"
                Column {
                    id: entry
                    x: 10; y: 8; width: parent.width - 20
                    spacing: 3
                    Text {
                        visible: root.panelTab === "saved"
                        width: parent.width
                        text: modelData.name || ""
                        color: Theme.text
                        font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: modelData.sql.replace(/\s+/g, " ")
                        color: root.panelTab === "saved" ? Theme.textSecondary : Theme.text
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.fontSmall + 1
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: root.panelTab === "history"
                        width: parent.width
                        text: modelData.ok
                              ? Number(modelData.rows).toLocaleString(Qt.locale(), "f", 0) + (modelData.rows === 1 ? " row" : " rows")
                                + " · " + modelData.ms + " ms · " + root.ago(modelData.at)
                              : "Failed · " + root.ago(modelData.at) + " · " + (modelData.error || "")
                        color: modelData.ok ? Theme.textTertiary : Theme.danger
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; x: 10; width: parent.width - 20; height: 1; color: Theme.separator
                            visible: index < panelList.count - 1 }
                MouseArea {
                    id: entryMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    // One click puts it in the editor; a double click runs it too.
                    onClicked: function (mouse) {
                        if (mouse.button === Qt.RightButton) {
                            root.menuName = modelData.name || ""
                            root.menuSql = modelData.sql
                            (root.panelTab === "saved" ? savedMenu : historyMenu).popup()
                            return
                        }
                        if (root.current) root.current.setText(modelData.sql)
                        historyIndex = -1
                    }
                    onDoubleClicked: { if (root.current) root.current.setText(modelData.sql); root.run() }
                }
            }
        }
        Text {
            anchors.centerIn: panelList
            width: panelList.width - 40
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            visible: panelList.count === 0
            text: panelFilter.text.length ? "Nothing matches."
                  : root.panelTab === "saved" ? "Save a query (⌘S) to keep it here, for this database."
                  : "Queries you run here appear in this list."
                    + "\nRight-click one to open it in a new tab."
            color: Theme.textTertiary
            font.pixelSize: Theme.fontBody
        }
        Item {
            id: panelFooter
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: root.panelTab === "history" && library.history.length ? 36 : 0
            visible: height > 0
            Text {
                anchors { left: parent.left; leftMargin: 14; right: clearLink.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
                elide: Text.ElideRight
                text: "Click to edit · double-click to run"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSmall
            }
            Text {
                id: clearLink
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                text: "Clear"
                color: Theme.accent
                font.pixelSize: Theme.fontSmall + 1
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: library.clearHistory() }
            }
        }
    }
    property string menuName: ""               // a saved query's name ("" for history)
    property string menuSql: ""
    ContextMenu {
        id: historyMenu
        ContextMenuItem { text: "Open in new tab"; onTriggered: root.newTab(root.menuSql, "") }
        ContextMenuItem { text: "Run in new tab"; onTriggered: { root.newTab(root.menuSql, ""); root.run() } }
    }
    ContextMenu {
        id: savedMenu
        ContextMenuItem { text: "Open in new tab"; onTriggered: root.newTab(root.menuSql, root.menuName) }
        ContextMenuItem { text: "Run in new tab"; onTriggered: { root.newTab(root.menuSql, root.menuName); root.run() } }
        ContextMenuSeparator { }
        ContextMenuItem { text: "Rename…"; onTriggered: saveDialog.askRename(root.menuName) }
        ContextMenuItem { text: "Delete"; danger: true; onTriggered: library.remove(root.menuName) }
    }

    // ---- Save (or rename) a query ----
    Popup {
        id: saveDialog
        property string renaming: ""
        function ask(name) {
            renaming = ""
            nameField.text = name.length ? name : library.suggestName(root.currentSql())
            open()
        }
        function askRename(name) { renaming = name; nameField.text = name; open() }
        function commit() {
            const name = nameField.text.trim()
            if (!name.length) return
            if (renaming.length) { if (library.rename(renaming, name)) close(); return }
            library.save(name, root.currentSql())
            root.panelTab = "saved"
            root.panelOpen = true
            close()
        }
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: 420
        padding: 22
        onOpened: { nameField.forceActiveFocus(); nameField.selectAll() }
        Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#59000000" }
        background: Rectangle { radius: 14; color: Theme.window; border.width: 1; border.color: Theme.separator }
        contentItem: Column {
            width: saveDialog.availableWidth
            spacing: 12
            Text {
                text: saveDialog.renaming.length ? "Rename “" + saveDialog.renaming + "”" : "Save this query"
                color: Theme.text
                font.pixelSize: 17; font.weight: Font.Bold
            }
            Field {
                id: nameField
                width: saveDialog.availableWidth
                placeholderText: "Name"
                onAccepted: saveDialog.commit()
            }
            Text {
                width: saveDialog.availableWidth
                visible: !saveDialog.renaming.length && library.hasName(nameField.text)
                text: "There's a saved query with this name: saving replaces it."
                color: Theme.warning
                font.pixelSize: Theme.fontBody
                wrapMode: Text.Wrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                ActionButton { text: "Cancel"; onClicked: saveDialog.close() }
                ActionButton {
                    text: saveDialog.renaming.length ? "Rename" : library.hasName(nameField.text) ? "Replace" : "Save"
                    primary: true
                    onClicked: saveDialog.commit()
                }
            }
        }
    }
}
