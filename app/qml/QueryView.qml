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
    /// "builder" or "sql"; remembered between runs (--query starts on SQL).
    property string startMode: ""                // --query-builder: "builder"
    property bool builderDemo: false             // --builder-demo
    property string mode: initialQuery.length ? "sql" : startMode.length ? startMode : Prefs.value("query/mode", "builder")
    onModeChanged: Prefs.setValue("query/mode", mode)

    function run() {
        if (mode === "builder") { builderView.run(); return }
        const sql = editor.selectedText.length ? editor.selectedText : editor.text
        if (!sql.trim().length) return
        query.run(sql)
        if (!query.error.length) remember(sql.trim())
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
            editor.text = root.starter()
            historyIndex = -1
        }
    }
    Component.onCompleted: {
        editor.text = initialQuery.length ? initialQuery : starter()
        if (runInitialQuery && initialQuery.length) Qt.callLater(run)
    }

    // ---- History: the last 30 successful queries, kept between sessions ----
    property int historyIndex: -1
    property string history: Prefs.value("console/history", "[]")
    function historyList() { try { return JSON.parse(history) } catch (e) { return [] } }
    function remember(sql) {
        const h = historyList().filter(q => q !== sql)
        h.unshift(sql)
        history = JSON.stringify(h.slice(0, 30))
        Prefs.setValue("console/history", history)
        historyIndex = -1
    }
    function step(delta) {
        const h = historyList()
        if (!h.length) return
        historyIndex = Math.max(0, Math.min(h.length - 1, historyIndex + delta))
        editor.text = h[historyIndex]
    }

    QueryResult { id: query; session: root.database }
    readonly property alias result: query

    Shortcut { sequence: "Ctrl+Return"; enabled: root.visible; onActivated: root.run() }
    Shortcut { sequence: "Ctrl+Enter";  enabled: root.visible; onActivated: root.run() }

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
                text: "‹ Older"
                enabled: root.historyList().length > 0
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
        Text {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
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
        anchors { left: parent.left; right: parent.right; top: toolbar.bottom; topMargin: 12; bottom: parent.bottom }
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
                }
            }
            SyntaxHighlighter { document: editor.textDocument; dark: Theme.dark }
        }

        // ---- Result ----
        Item {
            SplitView.fillHeight: true

            Text {
                id: status
                anchors { left: parent.left; right: parent.right; top: parent.top }
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
                anchors { left: parent.left; right: parent.right; top: status.bottom; topMargin: 6; bottom: parent.bottom }
                visible: query.hasResult
                model: query
                columns: query.columns
                valuesForRow: (r) => query.rowAt(r)
                emptyText: "The query returned no rows."
            }
        }
    }
}
