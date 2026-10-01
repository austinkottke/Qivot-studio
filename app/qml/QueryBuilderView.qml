import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// The query builder: pick a table, join related ones along their keys, pick
/// columns (counted, summed, ...), filter and sort. The SQL and the Qivot C++
/// follow every change, and the result refreshes by itself.
Item {
    id: root
    property var database
    property var query                     // the QueryResult to run into (shared with the SQL console)
    property alias builder: b
    property bool demo: false              // --builder-demo: a typical query, for screenshots and the smoke test
    signal editAsSql(string sql)

    QueryBuilder { id: b; session: root.database }

    function run() { if (b.sql.length) root.query.run(b.sql) }
    // Read-only and limited, so the result can keep up with the edits.
    Timer { id: autorun; interval: 350; onTriggered: root.run() }
    Connections { target: b; function onChanged() { autorun.restart() } }

    // Books per author country, over 20 each, the biggest first.
    function runDemo() {
        b.setFrom("book")
        const j = b.joinable.findIndex(function (t) { return t.table === "author" })
        if (j < 0) return
        b.join(j)
        b.toggleColumn("author", "country")
        b.toggleColumn("book", "id")
        b.setAggregate(1, "count")
        b.toggleColumn("book", "price")
        b.setAggregate(2, "avg")
        b.addFilter("book", "price")
        b.updateFilter(0, { op: ">", value: "20" })
        b.addSort("count_id", true)
        b.limit = 10
    }

    function start() {
        if (demo && database && database.isOpen) { runDemo(); return }
        if (b.from.length || !database || !database.isOpen) return
        const first = database.tables.find(function (t) { return t.kind === "table" })
        if (first) b.setFrom(first.name)
    }
    Component.onCompleted: start()
    Connections { target: root.database; function onOpenChanged() { Qt.callLater(root.start) } }

    readonly property var tableNames: database && database.isOpen
        ? database.tables.filter(function (t) { return t.kind !== "internal" }).map(function (t) { return t.name }) : []
    readonly property var columnKeys: {
        const keys = []
        for (const t of b.available)
            for (const c of t.columns) keys.push(t.table + "." + c.name)
        return keys
    }

    component SectionTitle: Text {
        color: Theme.textTertiary
        font.pixelSize: Theme.fontSmall
        font.weight: Font.Bold
        font.letterSpacing: 0.6
        topPadding: 6
    }
    component Remove: Text {
        signal clicked()
        width: 22; height: 30
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        text: "×"
        color: removeMouse.containsMouse ? Theme.danger : Theme.textTertiary
        font.pixelSize: 18
        MouseArea { id: removeMouse; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked() }
    }

    // ---- The query, as a form ----
    Rectangle {
        id: form
        width: Math.min(420, root.width * 0.4)
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        radius: Theme.radius
        color: Theme.surface
        border.width: 1
        border.color: Theme.separator
        clip: true

        Flickable {
            anchors.fill: parent
            anchors.margins: 1
            contentHeight: body.height + 32
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }

            Column {
                id: body
                x: 16; y: 14
                width: parent.width - 32
                spacing: 10

                // From
                SectionTitle { text: "FROM" }
                Choice {
                    width: parent.width
                    model: root.tableNames
                    currentIndex: root.tableNames.indexOf(b.from)
                    onActivated: function (i) { b.setFrom(root.tableNames[i]) }
                }

                // Joins
                SectionTitle { text: "JOIN"; visible: b.joins.length > 0 || b.joinable.length > 0 }
                Repeater {
                    model: b.joins
                    Rectangle {
                        width: body.width
                        height: joinCol.height + 16
                        radius: Theme.radiusSmall
                        color: Theme.window
                        Column {
                            id: joinCol
                            x: 10; y: 8
                            width: parent.width - 20
                            spacing: 6
                            Row {
                                width: parent.width
                                spacing: 6
                                Text {
                                    width: parent.width - keep.width - drop.width - 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.table
                                    color: Theme.text
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Toggle {
                                    id: keep
                                    label: "Keep unmatched"
                                    on: modelData.left
                                    onToggled: b.setJoinLeft(modelData.table, !on)
                                }
                                Remove { id: drop; onClicked: b.removeTable(modelData.table) }
                            }
                            Text {
                                width: parent.width
                                text: "on " + modelData.on
                                color: Theme.textSecondary
                                font.family: Theme.monoFont
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                Choice {
                    width: parent.width
                    visible: b.joinable.length > 0
                    model: ["+ Join a related table…"].concat(b.joinable.map(function (j) { return j.table + "   (" + j.on + ")" }))
                    currentIndex: 0
                    onActivated: function (i) { if (i > 0) b.join(i - 1); currentIndex = 0 }
                }

                // Columns
                SectionTitle { text: "COLUMNS" }
                Text {
                    width: parent.width
                    visible: b.columns.length === 0
                    wrapMode: Text.Wrap
                    text: "None picked, so every column of " + b.from + ". Click columns to pick them."
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall + 1
                }
                Repeater {
                    model: b.available
                    Column {
                        width: body.width
                        spacing: 6
                        Text {
                            visible: b.available.length > 1
                            text: modelData.table
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall + 1
                            font.weight: Font.DemiBold
                        }
                        Flow {
                            width: parent.width
                            spacing: 6
                            Repeater {
                                model: modelData.columns
                                Toggle {
                                    label: modelData.name
                                    on: modelData.selected
                                    ink: modelData.key ? Theme.key : Theme.accent
                                    onToggled: b.toggleColumn(parent.parent.tableName, modelData.name)
                                }
                            }
                        }
                        readonly property string tableName: modelData.table
                    }
                }

                // Picked, with aggregates
                Repeater {
                    model: b.columns
                    Row {
                        width: body.width
                        spacing: 6
                        Text {
                            width: parent.width - agg.width - pickedRemove.width - 12
                            anchors.verticalCenter: parent.verticalCenter
                            text: (index + 1) + ".  " + modelData.table + "." + modelData.column
                            color: Theme.text
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.fontSmall + 1
                            elide: Text.ElideRight
                        }
                        Choice {
                            id: agg
                            width: 110
                            model: b.aggregates.map(function (a) { return a.length ? a.toUpperCase() : "—" })
                            currentIndex: b.aggregates.indexOf(modelData.aggregate)
                            onActivated: function (i) { b.setAggregate(index, b.aggregates[i]) }
                        }
                        Remove { id: pickedRemove; onClicked: b.removeColumn(index) }
                    }
                }

                // Filters
                SectionTitle { text: "WHERE" }
                Repeater {
                    model: b.filters
                    Row {
                        width: body.width
                        spacing: 6
                        Choice {
                            width: modelData.takesValue ? (parent.width - 36) * 0.42 : (parent.width - 36) * 0.6
                            model: root.columnKeys
                            currentIndex: root.columnKeys.indexOf(modelData.key)
                            onActivated: function (i) { b.updateFilter(index, { key: root.columnKeys[i] }) }
                        }
                        Choice {
                            width: modelData.takesValue ? (parent.width - 36) * 0.24 : (parent.width - 36) * 0.4
                            model: b.operators
                            currentIndex: b.operators.indexOf(modelData.op)
                            onActivated: function (i) { b.updateFilter(index, { op: b.operators[i] }) }
                        }
                        Field {
                            visible: modelData.takesValue
                            width: (parent.width - 36) * 0.34
                            text: modelData.value
                            placeholderText: "value"
                            onEditingFinished: if (text !== modelData.value) b.updateFilter(index, { value: text })
                        }
                        Remove { onClicked: b.removeFilter(index) }
                    }
                }
                ActionButton { text: "+ Filter"; implicitHeight: 30; enabled: b.from.length > 0; onClicked: b.addFilter() }

                // Sort
                SectionTitle { text: "ORDER BY" }
                Repeater {
                    model: b.sorts
                    Row {
                        width: body.width
                        spacing: 6
                        Choice {
                            width: parent.width - desc.width - 34
                            model: b.sortKeys
                            currentIndex: b.sortKeys.indexOf(modelData.key)
                            onActivated: function (i) { b.updateSort(index, { key: b.sortKeys[i] }) }
                        }
                        Toggle { id: desc; label: modelData.desc ? "Descending" : "Ascending"; on: modelData.desc
                                 onToggled: b.updateSort(index, { desc: !on }) }
                        Remove { onClicked: b.removeSort(index) }
                    }
                }
                ActionButton { text: "+ Sort"; implicitHeight: 30; enabled: b.from.length > 0; onClicked: b.addSort() }

                // Limit
                SectionTitle { text: "LIMIT" }
                Row {
                    spacing: 8
                    Field {
                        width: 100
                        text: b.limit > 0 ? String(b.limit) : ""
                        placeholderText: "no limit"
                        validator: IntValidator { bottom: 0 }
                        onEditingFinished: b.limit = text.length ? parseInt(text) : 0
                    }
                    Toggle { label: "Distinct rows"; on: b.distinct; onToggled: b.distinct = !on }
                }
            }
        }
    }

    // ---- The query as SQL / C++, over its result ----
    SplitView {
        anchors { left: form.right; leftMargin: 14; right: parent.right; top: parent.top; bottom: parent.bottom }
        orientation: Qt.Vertical
        handle: Rectangle {
            implicitHeight: 12
            color: "transparent"
            Rectangle { anchors.centerIn: parent; width: 44; height: 4; radius: 2
                        color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.separator }
        }

        Rectangle {
            id: preview
            property string tab: "sql"
            SplitView.preferredHeight: Math.min(root.height * 0.45, 320)
            SplitView.minimumHeight: 120
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.separator
            clip: true

            Item {
                id: previewBar
                width: parent.width; height: 42
                SegmentedControl {
                    x: 10; anchors.verticalCenter: parent.verticalCenter
                    options: [ "SQL", "Qivot C++" ]
                    currentIndex: preview.tab === "sql" ? 0 : 1
                    onActivated: function (i) { preview.tab = i === 0 ? "sql" : "cpp"; previewCode.load(preview.tab === "sql" ? b.sql : b.cpp) }
                }
                Row {
                    anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                    spacing: 8
                    ActionButton { text: "Copy"; implicitHeight: 30; onClicked: root.copy(preview.tab === "sql" ? b.sql : b.cpp) }
                    ActionButton { text: "Edit as SQL"; implicitHeight: 30; enabled: b.sql.length > 0; onClicked: root.editAsSql(b.sql) }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.separator }
            }
            CodeEditor {
                id: previewCode
                anchors { left: parent.left; right: parent.right; top: previewBar.bottom; bottom: parent.bottom; margins: 1 }
                readOnly: true
                language: preview.tab === "sql" ? "sql" : "cpp"
                Component.onCompleted: load(b.sql)
                Connections { target: b; function onChanged() { previewCode.load(preview.tab === "sql" ? b.sql : b.cpp) } }
            }
        }

        Item {
            SplitView.fillHeight: true
            Text {
                id: status
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 28
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: !b.sql.length ? "Pick a table to start."
                      : root.query.error.length ? root.query.error
                      : root.query.hasResult ? Number(root.query.resultRows).toLocaleString(Qt.locale(), "f", 0)
                                               + (root.query.resultRows === 1 ? " row" : " rows") + "  ·  " + root.query.elapsedMs + " ms"
                      : "…"
                color: root.query.error.length ? Theme.danger : Theme.textSecondary
                font.pixelSize: Theme.fontBody
                font.family: root.query.error.length ? Theme.monoFont : Qt.application.font.family
            }
            ResultGrid {
                anchors { left: parent.left; right: parent.right; top: status.bottom; topMargin: 6; bottom: parent.bottom }
                visible: root.query.hasResult
                model: root.query
                columns: root.query.columns
                valuesForRow: function (r) { return root.query.rowAt(r) }
                emptyText: "No rows match."
            }
        }
    }

    function copy(text) {
        clipboard.text = text
        clipboard.selectAll()
        clipboard.copy()
    }
    TextEdit { id: clipboard; visible: false }
}
