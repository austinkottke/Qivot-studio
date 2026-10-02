import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// One SQL tab of the Query screen: its editor (with autocomplete), and the
/// result or plan of what was run in it. Queries run in the background
/// (QueryResult.start), so the window stays usable and Stop can end them.
Item {
    id: tab
    property var database
    property var library                      // the screen's QueryLibrary: every run is recorded
    property alias text: editor.text
    readonly property alias query: query
    readonly property alias plan: plan
    readonly property bool running: query.running
    property string resultsTab: "results"     // "results" or "plan"
    signal edited()

    function currentSql() { return editor.selectedText.length ? editor.selectedText : editor.text }
    function focusEditor() { editor.forceActiveFocus() }
    function setText(sql) { editor.text = sql }

    property string recording: ""             // the SQL a run in progress will be recorded as
    function run() {
        const sql = currentSql()
        if (!sql.trim().length) return
        completionPopup.close()
        recording = sql.trim()
        startedAt = Date.now()
        elapsed = 0
        query.start(sql)
        resultsTab = "results"
    }
    function stop() { query.cancel() }
    // How the database would run it (without running it).
    function explain() {
        const sql = currentSql()
        if (!sql.trim().length) return
        completionPopup.close()
        plan.explain(sql)
        resultsTab = "plan"
    }
    function clearResults() { query.clear(); plan.clear(); resultsTab = "results"; recording = "" }

    QueryResult {
        id: query
        session: tab.database
        onFinished: {
            if (!tab.recording.length || running) return
            if (tab.library)
                tab.library.record(tab.recording, hasResult ? resultRows : Math.max(0, rowsAffected), elapsedMs,
                                   !error.length && !cancelled, cancelled ? "Stopped" : error)
            tab.recording = ""
        }
    }
    QueryPlan { id: plan; session: tab.database }

    // A clock while a query runs.
    property double startedAt: 0
    property double elapsed: 0
    Timer {
        interval: 100; repeat: true
        running: query.running
        onTriggered: tab.elapsed = Date.now() - tab.startedAt
    }
    function seconds(ms) { return ms < 1000 ? Math.round(ms) + " ms" : (ms / 1000).toFixed(1) + " s" }
    function fmt(n) { return Number(n).toLocaleString(Qt.locale(), "f", 0) }

    // ---- Autocomplete ----
    SqlCompleter { id: completer; session: tab.database }
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
    // --complete-demo: a half-typed query with the list showing.
    function completeDemo() {
        editor.text = "SELECT b.title, a.\nFROM book b\nJOIN author a ON a.id = b.author_id"
        editor.forceActiveFocus()
        editor.cursorPosition = 18
        updateCompletion(false)
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical
        handle: Rectangle {
            implicitHeight: 12
            color: "transparent"
            Rectangle { anchors.centerIn: parent; width: 44; height: 4; radius: 2
                        color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.separator }
        }

        // ---- Editor ----
        Rectangle {
            SplitView.preferredHeight: Math.min(tab.height * 0.45, Math.max(160, editor.contentHeight + 34))
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
                            if (e.key === Qt.Key_Down) { tab.completionIndex = Math.min(tab.completion.items.length - 1, tab.completionIndex + 1); e.accepted = true; return }
                            if (e.key === Qt.Key_Up) { tab.completionIndex = Math.max(0, tab.completionIndex - 1); e.accepted = true; return }
                            if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Tab)
                                && !(e.modifiers & Qt.ControlModifier)) { tab.acceptCompletion(); e.accepted = true; return }
                            if (e.key === Qt.Key_Escape) { completionPopup.close(); e.accepted = true; return }
                        }
                        if (e.key === Qt.Key_Space && (e.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier))) {
                            tab.updateCompletion(true)
                            e.accepted = true
                            return
                        }
                        typing = e.text.length > 0 && !(e.modifiers & Qt.ControlModifier)
                    }
                    onTextChanged: {
                        tab.edited()
                        if (typing) { typing = false; Qt.callLater(tab.updateCompletion, false) }
                    }
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
                            model: tab.completion.items
                            currentIndex: tab.completionIndex
                            boundsBehavior: Flickable.StopAtBounds
                            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                            delegate: Rectangle {
                                width: completionList.width
                                height: 26
                                radius: 5
                                color: index === tab.completionIndex ? Theme.accent : completionMouse.containsMouse ? Theme.hover : "transparent"
                                readonly property bool on: index === tab.completionIndex
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
                                    onClicked: { tab.completionIndex = index; tab.acceptCompletion(); editor.forceActiveFocus() }
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
                currentIndex: tab.resultsTab === "plan" ? 1 : 0
                onActivated: function (i) { if (i === 1 && !plan.hasPlan && !plan.error.length) tab.explain(); else tab.resultsTab = i === 1 ? "plan" : "results" }
            }
            ExportButton {
                id: queryExport
                anchors { right: parent.right; top: parent.top }
                visible: query.hasResult && !query.running && tab.resultsTab === "results"
                target: query
            }
            Text {
                anchors { right: queryExport.left; rightMargin: 10; verticalCenter: queryExport.verticalCenter }
                visible: queryExport.visible && queryExport.message.length > 0
                text: queryExport.message
                color: queryExport.failed ? Theme.danger : Theme.positive
                font.pixelSize: Theme.fontBody
            }
            // While it runs: how long so far, and a way to stop.
            Row {
                id: runningRow
                visible: query.running && tab.resultsTab === "results"
                anchors { left: resultTabs.right; leftMargin: 14; verticalCenter: resultTabs.verticalCenter }
                spacing: 10
                BusyIndicator { width: 20; height: 20; running: parent.visible; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Running…  " + tab.seconds(tab.elapsed)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    font.family: Theme.monoFont
                }
                ActionButton { text: "Stop   ⌘."; implicitHeight: 26; onClicked: tab.stop() }
            }
            Text {
                id: status
                visible: tab.resultsTab === "results" && !query.running
                anchors { left: resultTabs.right; leftMargin: 14; right: queryExport.visible ? queryExport.left : parent.right
                          rightMargin: queryExport.visible ? 200 : 0; verticalCenter: resultTabs.verticalCenter }
                height: 28
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: query.error.length ? query.error
                      : query.notice.length ? query.notice
                      : query.hasResult
                        ? tab.fmt(query.resultRows) + (query.resultRows === 1 ? " row" : " rows")
                          + (query.truncated ? " (the first " + tab.fmt(query.resultRows) + " are shown)" : "")
                          + "  ·  " + query.elapsedMs + " ms"
                      : query.lastSql.length
                        ? "Done in " + query.elapsedMs + " ms"
                          + (query.rowsAffected >= 0 ? " · " + query.rowsAffected + " rows affected" : "")
                        : "Write a query and press ⌘↩ to run it."
                color: query.error.length ? Theme.danger
                     : query.notice.length ? (query.cancelled ? Theme.warning : Theme.accent) : Theme.textSecondary
                font.pixelSize: Theme.fontBody
                font.family: query.error.length ? Theme.monoFont : Qt.application.font.family
            }

            ResultGrid {
                anchors { left: parent.left; right: parent.right; top: resultTabs.bottom; topMargin: 10; bottom: parent.bottom }
                visible: query.hasResult && tab.resultsTab === "results"
                opacity: query.running ? 0.45 : 1
                model: query
                columns: query.columns
                valuesForRow: (r) => query.rowAt(r)
                emptyText: "The query returned no rows."
            }

            // ---- The plan: how the database would run it ----
            Item {
                id: planView
                anchors { left: parent.left; right: parent.right; top: resultTabs.bottom; topMargin: 10; bottom: parent.bottom }
                visible: tab.resultsTab === "plan"
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
                    language: tab.database && tab.database.dialect === "postgres" ? "cpp" : "sql"
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
}
