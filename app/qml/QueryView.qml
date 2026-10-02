import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// Query: build one by pointing (QueryBuilderView), or write SQL and ⌘↩ to
/// run it; either way the result is in the grid. Read-only on every database
/// (see QueryModel), so it's safe to explore with.
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

    function currentSql() { return editor.selectedText.length ? editor.selectedText : editor.text }
    function run() {
        if (mode === "builder") { builderView.run(); return }
        const sql = currentSql()
        if (!sql.trim().length) return
        completionPopup.close()
        query.run(sql)
        library.record(sql.trim(), query.hasResult ? query.resultRows : Math.max(0, query.rowsAffected),
                       query.elapsedMs, !query.error.length, query.error)
        historyIndex = -1
        resultsTab = "results"
    }
    // How the database would run it (without running it).
    function explain() {
        const sql = currentSql()
        if (!sql.trim().length) return
        completionPopup.close()
        plan.explain(sql)
        resultsTab = "plan"
    }

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
            editor.text = root.starter()
            historyIndex = -1
            plan.clear()
            root.resultsTab = "results"
        }
    }
    Component.onCompleted: {
        editor.text = initialQuery.length ? initialQuery : starter()
        if (runInitialQuery && initialQuery.length) Qt.callLater(explainInitialQuery ? explain : run)
        if (completeDemo) Qt.callLater(function () {
            mode = "sql"
            editor.text = "SELECT b.title, a.\nFROM book b\nJOIN author a ON a.id = b.author_id"
            editor.forceActiveFocus()
            editor.cursorPosition = 18
            updateCompletion(false)
        })
    }

    // ---- History and saved queries, per database (QueryLibrary) ----
    QueryLibrary {
        id: library
        scope: root.database && root.database.isOpen
               ? root.database.dialect + ":" + (root.database.filePath.length ? root.database.filePath
                                                                             : root.database.location + "/" + root.database.displayName)
               : ""
    }
    readonly property var okHistory: library.history.filter(function (h) { return h.ok })
    property int historyIndex: -1
    function step(delta) {
        const h = okHistory
        if (!h.length) return
        historyIndex = Math.max(0, Math.min(h.length - 1, historyIndex + delta))
        editor.text = h[historyIndex].sql
    }
    QueryPlan { id: plan; session: root.database }
    property string resultsTab: "results"         // "results" or "plan"
    property bool panelOpen: Prefs.value("query/panel", "open") === "open"
    onPanelOpenChanged: Prefs.setValue("query/panel", panelOpen ? "open" : "closed")

    QueryResult { id: query; session: root.database }

    // ---- Autocomplete ----
    SqlCompleter { id: completer; session: root.database }
    property var completion: ({ start: 0, prefix: "", items: [] })
    property int completionIndex: 0
    function updateCompletion(force) {
        if (!editor.activeFocus) { completionPopup.close(); return }
        const r = completer.complete(editor.text, editor.cursorPosition, force)
        if (!r.items.length) { completionPopup.close(); return }
        completion = r
        completionIndex = 0
        if (!completionPopup.visible) completionPopup.open()
    }
    function acceptCompletion() {
        const item = completion.items[completionIndex]
        if (!item) return
        const start = completion.start, end = editor.cursorPosition
        editor.remove(start, end)
        editor.insert(start, item.text)
        editor.cursorPosition = start + item.text.length
        completionPopup.close()
    }
    readonly property alias result: query

    Shortcut { sequence: "Ctrl+Return"; enabled: root.visible; onActivated: root.run() }
    Shortcut { sequence: "Ctrl+Enter";  enabled: root.visible; onActivated: root.run() }
    Shortcut { sequence: "Ctrl+Shift+Return"; enabled: root.visible && root.mode === "sql"; onActivated: root.explain() }
    Shortcut { sequence: "Ctrl+S"; enabled: root.visible && root.mode === "sql"; onActivated: saveDialog.ask("") }

    function fmt(n) { return Number(n).toLocaleString(Qt.locale(), "f", 0) }

    // ---- Toolbar ----
    Item {
        id: toolbar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 38

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            SegmentedControl {
                anchors.verticalCenter: parent.verticalCenter
                options: [ "Builder", "SQL" ]
                currentIndex: root.mode === "builder" ? 0 : 1
                onActivated: function (i) { root.mode = i === 0 ? "builder" : "sql" }
            }
            Item { width: 4; height: 1 }
            ActionButton { text: "Run   ⌘↩"; primary: true; onClicked: root.run() }
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
            anchors { right: panelButton.visible ? panelButton.left : parent.right; rightMargin: panelButton.visible ? 12 : 0
                      verticalCenter: parent.verticalCenter }
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
        onEditAsSql: function (sql) { editor.text = sql; root.mode = "sql" }
    }

    SplitView {
        anchors { left: parent.left; right: panel.visible ? panel.left : parent.right; rightMargin: panel.visible ? 14 : 0
                  top: toolbar.bottom; topMargin: 12; bottom: parent.bottom }
        visible: root.mode === "sql"
        orientation: Qt.Vertical
        handle: Rectangle {
            implicitHeight: 12
            color: "transparent"
            Rectangle { anchors.centerIn: parent; width: 44; height: 4; radius: 2
                        color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.separator }
        }

        // ---- Editor ----
        Rectangle {
            SplitView.preferredHeight: Math.min(root.height * 0.45, Math.max(160, editor.contentHeight + 34))
            SplitView.minimumHeight: 90
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: editor.activeFocus ? Theme.accent : Theme.separator

            ScrollView {
                anchors.fill: parent
                anchors.margins: 1
                TextArea {
                    id: editor
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.fontBody + 1
                    color: Theme.text
                    selectionColor: Theme.accentSoft
                    selectedTextColor: Theme.text
                    placeholderText: "SELECT … FROM …"
                    placeholderTextColor: Theme.textTertiary
                    wrapMode: TextArea.NoWrap
                    selectByMouse: true
                    padding: 14
                    background: null
                    tabStopDistance: 28

                    // Autocomplete: as a word is typed, after `alias.` and after FROM/JOIN;
                    // Ctrl+Space (or ⌥Space) asks anywhere.
                    property bool typing: false
                    Keys.onPressed: function (e) {
                        if (completionPopup.visible) {
                            if (e.key === Qt.Key_Down) { root.completionIndex = Math.min(root.completion.items.length - 1, root.completionIndex + 1); e.accepted = true; return }
                            if (e.key === Qt.Key_Up) { root.completionIndex = Math.max(0, root.completionIndex - 1); e.accepted = true; return }
                            if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Tab)
                                && !(e.modifiers & Qt.ControlModifier)) { root.acceptCompletion(); e.accepted = true; return }
                            if (e.key === Qt.Key_Escape) { completionPopup.close(); e.accepted = true; return }
                        }
                        if (e.key === Qt.Key_Space && (e.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier))) {
                            root.updateCompletion(true)
                            e.accepted = true
                            return
                        }
                        typing = e.text.length > 0 && !(e.modifiers & Qt.ControlModifier)
                    }
                    onTextChanged: if (typing) { typing = false; Qt.callLater(root.updateCompletion, false) }
                    onActiveFocusChanged: if (!activeFocus) completionPopup.close()

                    Popup {
                        id: completionPopup
                        x: Math.min(editor.cursorRectangle.x, Math.max(0, editor.width - width))
                        y: editor.cursorRectangle.y + editor.cursorRectangle.height + 4
                        width: 340
                        height: Math.min(260, completionList.contentHeight + 8)
                        padding: 4
                        focus: false
                        closePolicy: Popup.CloseOnPressOutside
                        background: Rectangle { radius: 8; color: Theme.surface; border.width: 1; border.color: Theme.separator }
                        ListView {
                            id: completionList
                            anchors.fill: parent
                            clip: true
                            model: root.completion.items
                            currentIndex: root.completionIndex
                            boundsBehavior: Flickable.StopAtBounds
                            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                            delegate: Rectangle {
                                width: completionList.width
                                height: 26
                                radius: 5
                                color: index === root.completionIndex ? Theme.accent : completionMouse.containsMouse ? Theme.hover : "transparent"
                                readonly property bool on: index === root.completionIndex
                                Rectangle {
                                    id: kindBadge
                                    x: 6; anchors.verticalCenter: parent.verticalCenter
                                    width: 18; height: 18; radius: 4
                                    color: parent.on ? "#33FFFFFF"
                                         : modelData.kind === "column" ? Theme.linkSoft
                                         : modelData.kind === "table" || modelData.kind === "view" ? Theme.accentSoft : Theme.surfaceRaised
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.kind === "column" ? "C" : modelData.kind === "table" ? "T"
                                            : modelData.kind === "view" ? "V" : modelData.kind === "function" ? "ƒ" : "K"
                                        color: parent.parent.on ? "white" : modelData.kind === "column" ? Theme.link
                                             : modelData.kind === "table" || modelData.kind === "view" ? Theme.accent : Theme.textSecondary
                                        font.pixelSize: 10; font.weight: Font.Bold
                                    }
                                }
                                Text {
                                    anchors { left: kindBadge.right; leftMargin: 8; right: detailText.left; rightMargin: 8
                                              verticalCenter: parent.verticalCenter }
                                    text: modelData.text
                                    color: parent.on ? "white" : Theme.text
                                    font.family: Theme.monoFont
                                    font.pixelSize: Theme.fontBody
                                    elide: Text.ElideRight
                                }
                                Text {
                                    id: detailText
                                    anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                                    text: modelData.detail || ""
                                    color: parent.on ? "#D9FFFFFF" : Theme.textTertiary
                                    font.pixelSize: Theme.fontSmall
                                    width: Math.min(implicitWidth, 150)
                                    elide: Text.ElideLeft
                                }
                                MouseArea {
                                    id: completionMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: { root.completionIndex = index; root.acceptCompletion(); editor.forceActiveFocus() }
                                }
                            }
                        }
                    }
                }
            }
            SyntaxHighlighter { document: editor.textDocument; dark: Theme.dark }
        }

        // ---- Result ----
        Item {
            SplitView.fillHeight: true

            SegmentedControl {
                id: resultTabs
                anchors { left: parent.left; top: parent.top }
                options: [ "Results", "Plan" ]
                currentIndex: root.resultsTab === "plan" ? 1 : 0
                onActivated: function (i) { if (i === 1 && !plan.hasPlan && !plan.error.length) root.explain(); else root.resultsTab = i === 1 ? "plan" : "results" }
            }
            ExportButton {
                id: queryExport
                anchors { right: parent.right; top: parent.top }
                visible: query.hasResult && root.resultsTab === "results"
                target: query
            }
            Text {
                anchors { right: queryExport.left; rightMargin: 10; verticalCenter: queryExport.verticalCenter }
                visible: queryExport.visible && queryExport.message.length > 0
                text: queryExport.message
                color: queryExport.failed ? Theme.danger : Theme.positive
                font.pixelSize: Theme.fontBody
            }
            Text {
                id: status
                visible: root.resultsTab === "results"
                anchors { left: resultTabs.right; leftMargin: 14; right: queryExport.visible ? queryExport.left : parent.right
                          rightMargin: queryExport.visible ? 200 : 0; verticalCenter: resultTabs.verticalCenter }
                height: 28
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: query.error.length ? query.error
                      : query.notice.length ? query.notice
                      : query.hasResult
                        ? root.fmt(query.resultRows) + (query.resultRows === 1 ? " row" : " rows")
                          + (query.truncated ? " (the first " + root.fmt(query.resultRows) + " are shown)" : "")
                          + "  ·  " + query.elapsedMs + " ms"
                      : query.lastSql.length
                        ? "Done in " + query.elapsedMs + " ms"
                          + (query.rowsAffected >= 0 ? " · " + query.rowsAffected + " rows affected" : "")
                        : "Write a query and press ⌘↩ to run it."
                color: query.error.length ? Theme.danger
                     : query.notice.length ? Theme.accent : Theme.textSecondary
                font.pixelSize: Theme.fontBody
                font.family: query.error.length ? Theme.monoFont : Qt.application.font.family
            }

            ResultGrid {
                anchors { left: parent.left; right: parent.right; top: resultTabs.bottom; topMargin: 10; bottom: parent.bottom }
                visible: query.hasResult && root.resultsTab === "results"
                model: query
                columns: query.columns
                valuesForRow: (r) => query.rowAt(r)
                emptyText: "The query returned no rows."
            }

            // ---- The plan: how the database would run it ----
            Item {
                id: planView
                anchors { left: parent.left; right: parent.right; top: resultTabs.bottom; topMargin: 10; bottom: parent.bottom }
                visible: root.resultsTab === "plan"
                property bool showRaw: false
                Text {
                    id: planSummary
                    anchors { left: parent.left; right: rawToggle.left; rightMargin: 12; top: parent.top }
                    height: 28
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    text: plan.error.length ? plan.error
                          : !plan.hasPlan ? "Explain shows how the database would run the query — without running it."
                          : plan.nodes.length + (plan.nodes.length === 1 ? " step" : " steps")
                            + (plan.scans ? "  ·  " + plan.scans + (plan.scans === 1 ? " reads a whole table" : " read whole tables")
                                            + " (no index) — usually what to look at first" : "  ·  every table is read through an index")
                    color: plan.error.length ? Theme.danger : plan.scans ? Theme.warning : Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                }
                Toggle {
                    id: rawToggle
                    anchors { right: parent.right; verticalCenter: planSummary.verticalCenter }
                    visible: plan.hasPlan
                    label: "As the database says it"
                    on: planView.showRaw
                    onToggled: planView.showRaw = !planView.showRaw
                }
                CodeEditor {
                    anchors { left: parent.left; right: parent.right; top: planSummary.bottom; topMargin: 8; bottom: parent.bottom }
                    visible: planView.showRaw && plan.hasPlan
                    text: plan.raw
                    language: root.database && root.database.dialect === "postgres" ? "cpp" : "sql"
                    readOnly: true
                }
                Rectangle {
                    anchors { left: parent.left; right: parent.right; top: planSummary.bottom; topMargin: 8; bottom: parent.bottom }
                    visible: !planView.showRaw && plan.hasPlan
                    radius: Theme.radius
                    color: Theme.surface
                    border.width: 1; border.color: Theme.separator
                    clip: true
                    ListView {
                        id: planList
                        anchors { fill: parent; margins: 6 }
                        model: plan.nodes
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            width: planList.width
                            height: 44
                            radius: 6
                            color: modelData.scan ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.10) : "transparent"
                            Rectangle { visible: modelData.scan; width: 3; height: parent.height; radius: 1.5; color: Theme.warning }
                            // The tree: indented by depth, with a joint.
                            Text {
                                id: joint
                                x: 12 + modelData.depth * 20
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.depth > 0 ? "└" : "●"
                                color: Theme.textTertiary
                                font.pixelSize: modelData.depth > 0 ? Theme.fontHeading : 8
                            }
                            Column {
                                anchors { left: joint.right; leftMargin: 8; right: figures.left; rightMargin: 12
                                          verticalCenter: parent.verticalCenter }
                                spacing: 2
                                Text {
                                    width: parent.width
                                    text: modelData.label + (modelData.scan ? "   — reads all of " + (modelData.table || "the table") : "")
                                    color: modelData.scan ? Theme.warning : Theme.text
                                    font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    visible: text.length > 0
                                    text: modelData.detail
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSmall + 1
                                    font.family: Theme.monoFont
                                    elide: Text.ElideRight
                                }
                            }
                            // Estimated rows, and the step's share of the cost as a bar.
                            Row {
                                id: figures
                                anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                                spacing: 12
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: modelData.rows >= 0
                                    text: "~" + Number(modelData.rows).toLocaleString(Qt.locale(), "f", 0) + (modelData.rows === 1 ? " row" : " rows")
                                    color: Theme.textTertiary
                                    font.pixelSize: Theme.fontSmall + 1
                                    font.family: Theme.monoFont
                                }
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: modelData.cost >= 0
                                    width: 110; height: 8; radius: 4
                                    color: Theme.surfaceRaised
                                    Rectangle {
                                        width: Math.max(3, parent.width * modelData.share); height: parent.height; radius: 4
                                        color: modelData.scan ? Theme.warning : Theme.accent
                                    }
                                }
                            }
                        }
                    }
                }
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
                        if (mouse.button === Qt.RightButton && root.panelTab === "saved") {
                            root.menuName = modelData.name
                            savedMenu.popup()
                            return
                        }
                        editor.text = modelData.sql
                        historyIndex = -1
                    }
                    onDoubleClicked: { editor.text = modelData.sql; root.run() }
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
            color: Theme.textTertiary
            font.pixelSize: Theme.fontBody
        }
        Item {
            id: panelFooter
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: root.panelTab === "history" && library.history.length ? 36 : 0
            visible: height > 0
            Text {
                anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                text: "Click to edit · double-click to run"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSmall
            }
            Text {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                text: "Clear"
                color: Theme.accent
                font.pixelSize: Theme.fontSmall + 1
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: library.clearHistory() }
            }
        }
    }
    property string menuName: ""
    ContextMenu {
        id: savedMenu
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
