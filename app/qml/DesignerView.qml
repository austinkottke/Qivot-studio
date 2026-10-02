import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQml 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// The class designer: the schema on a canvas, the selected table in an
/// inspector, and what the design means — the Qivot classes (C++) and the
/// migration (SQL) — kept up to date with every edit. Nothing is written to
/// the database; a SQLite file can be migrated as a copy.
Item {
    id: root
    property var database
    property string initialTable: ""          // --table: start with this one selected
    property string initialTab: "table"       // --design-tab: table | cpp | sql
    property bool demo: false                 // --design-demo: start with a few example edits
    property string demoMenu: ""              // --design-menu table[.column]: open that menu (screenshots)
    signal openFile(string path)              // "Open the copy"
    signal changesRequested(var then)         // ask to allow changes, then run `then`
    property string appliedNote: ""           // what the last apply to the database did

    Design { id: design; autosave: !root.demo; session: root.database }

    property string selected: ""
    property var info: ({})
    property var tableProblems: []
    function refresh() {
        if (selected.length && design.table(selected).name === undefined) selected = ""
        info = selected.length ? design.table(selected) : ({})
        tableProblems = selected.length ? design.problemsFor(selected) : []
    }
    Connections { target: design; function onProblemsChanged() { root.refresh() } }
    onSelectedChanged: refresh()
    Connections { target: design; function onDesignChanged() { root.refresh() } }
    // A few typical edits on the bookshop sample, to show what the designer does.
    function runDemo() {
        if (design.table("book").name === undefined) return
        design.addColumn("book", "subtitle", "TEXT")
        const tag = design.addTable("tag")
        design.addColumn(tag, "label", "TEXT")
        design.updateColumn(tag, 1, { unique: true, nullable: false })
        const link = design.addTable("book_tag")
        design.addColumn(link, "book_id", "INTEGER")
        design.addColumn(link, "tag_id", "INTEGER")
        design.removeColumn(link, 0)
        design.updateColumn(link, 0, { primaryKey: true })
        design.updateColumn(link, 1, { primaryKey: true })
        design.setReference(link, "book_id", "book", "CASCADE")
        design.setReference(link, "tag_id", tag, "CASCADE")
        design.renameTable("orders", "purchase")
        // A lookup table, and a reference to it from book.
        const category = design.addTable("category")
        design.addColumn(category, "name", "TEXT")
        design.updateColumn(category, 1, { nullable: false, unique: true })
        design.addColumn("book", "category_id", "INTEGER")
        design.setReference("book", "category_id", category, "SET NULL")
    }
    Component.onCompleted: {
        panel.tab = initialTab
        // After this frame: the Design picks up its session (and resets) only once everything is created.
        Qt.callLater(function () {
            if (demo) { runDemo(); refit.start() }
            if (demoMenu.length) {
                const parts = demoMenu.split(".")
                Qt.callLater(function () {
                    root.openMenu(parts.length > 1 ? columnMenu : tableMenu, parts[0], parts[1] || "",
                                  canvas.width * 0.55, canvas.height * 0.3)
                })
            }
            if (initialTable.length) selected = initialTable
        })
    }

    function addTable() {
        selected = design.addTable("")
        panel.tab = "table"
        Qt.callLater(function () { tableName.forceActiveFocus(); tableName.selectAll() })
    }

    DesignFileDialog {
        id: fileDialog
        onPicked: function (file) {
            if (saving) design.save(file)
            else if (design.load(file)) { root.selected = ""; refit.start() }
        }
    }

    // ---- Right-click menus on the canvas ----
    property string menuTable: ""
    property string menuColumn: ""
    property var menuCol: ({})            // the column, as design.table() reports it
    property var otherTables: []          // tables the menu's table could refer to
    property point menuPoint: Qt.point(0, 0)
    function openMenu(menu, table, column, x, y) {
        menuTable = table
        menuColumn = column
        const cols = design.table(table).columns || []
        menuCol = cols.find(function (c) { return c.name === column }) || ({})
        otherTables = design.tables.map(function (t) { return t.name }).filter(function (n) { return n !== table })
        menu.popup(canvas, x, y)
    }
    function columnIndex() {
        const cols = design.table(menuTable).columns || []
        return cols.findIndex(function (c) { return c.name === menuColumn })
    }
    function setColumn(changes) { design.updateColumn(menuTable, columnIndex(), changes) }

    ContextMenu {
        id: tableMenu
        ContextMenuItem { text: "Edit " + root.menuTable; onTriggered: { root.selected = root.menuTable; panel.tab = "table" } }
        ContextMenuItem {
            text: "Add column"
            onTriggered: { design.addColumn(root.menuTable, "", ""); root.selected = root.menuTable; panel.tab = "table" }
        }
        ContextMenu {
            id: addReferenceMenu
            title: "Add a reference to"
            Instantiator {
                model: root.otherTables
                delegate: ContextMenuItem {
                    text: modelData
                    onTriggered: { design.addReference(root.menuTable, modelData); root.selected = root.menuTable; panel.tab = "table" }
                }
                onObjectAdded: function (index, object) { addReferenceMenu.insertItem(index, object) }
                onObjectRemoved: function (index, object) { addReferenceMenu.removeItem(object) }
            }
        }
        ContextMenuItem {
            text: "Rename…"
            onTriggered: {
                root.selected = root.menuTable
                panel.tab = "table"
                Qt.callLater(function () { tableName.forceActiveFocus(); tableName.selectAll() })
            }
        }
        ContextMenuSeparator { }
        ContextMenuItem {
            text: "Delete table"
            danger: true
            onTriggered: { design.removeTable(root.menuTable); if (root.selected === root.menuTable) root.selected = "" }
        }
    }

    ContextMenu {
        id: columnMenu
        ContextMenuItem { text: root.menuTable + "." + root.menuColumn; enabled: false }
        ContextMenuSeparator { }
        ContextMenuItem { text: "Key"; checkable: true; checked: root.menuCol.primaryKey === true
                          onTriggered: root.setColumn({ primaryKey: root.menuCol.primaryKey !== true }) }
        ContextMenuItem { text: "Required"; checkable: true; checked: root.menuCol.nullable === false
                          enabled: root.menuCol.primaryKey !== true
                          onTriggered: root.setColumn({ nullable: root.menuCol.nullable === false }) }
        ContextMenuItem { text: "Unique"; checkable: true; checked: root.menuCol.unique === true
                          enabled: root.menuCol.primaryKey !== true
                          onTriggered: root.setColumn({ unique: root.menuCol.unique !== true }) }
        ContextMenuSeparator { }
        ContextMenu {
            id: referencesMenu
            title: "References"
            ContextMenuItem { text: "Nothing"; checkable: true; checked: !root.menuCol.reference
                              onTriggered: design.setReference(root.menuTable, root.menuColumn, "") }
            Instantiator {
                model: root.otherTables
                delegate: ContextMenuItem {
                    text: modelData
                    checkable: true
                    checked: root.menuCol.reference === modelData
                    onTriggered: design.setReference(root.menuTable, root.menuColumn, modelData)
                }
                onObjectAdded: function (index, object) { referencesMenu.insertItem(index + 1, object) }
                onObjectRemoved: function (index, object) { referencesMenu.removeItem(object) }
            }
        }
        ContextMenu {
            id: onDeleteMenu
            title: "When the referenced row is deleted"
            Repeater {
                model: [ ["Do nothing (no action)", ""], ["Delete these rows too (cascade)", "CASCADE"],
                         ["Clear the reference (set null)", "SET NULL"], ["Refuse the delete (restrict)", "RESTRICT"] ]
                ContextMenuItem {
                    text: modelData[0]
                    enabled: !!root.menuCol.reference
                    checkable: true
                    checked: !!root.menuCol.reference && (root.menuCol.onDelete || "").toUpperCase() === modelData[1]
                    onTriggered: design.setOnDelete(root.menuTable, root.menuColumn, modelData[1])
                }
            }
        }
        ContextMenuSeparator { }
        ContextMenuItem { text: "Delete column"; danger: true; onTriggered: design.removeColumn(root.menuTable, root.columnIndex()) }
    }

    ContextMenu {
        id: canvasMenu
        ContextMenuItem { text: "New table here"; onTriggered: { canvas.placeNext = root.menuPoint; root.addTable() } }
        ContextMenuSeparator { }
        ContextMenuItem { text: "Fit"; onTriggered: canvas.fit() }
        ContextMenuItem { text: "Tidy up"; onTriggered: canvas.resetLayout() }
    }

    // Fit once the split view has given the canvas its width.
    Timer { id: refit; interval: 120; onTriggered: canvas.fit() }

    property string appliedTo: ""             // the copy the migration last ran on

    Shortcut { sequences: [StandardKey.Undo]; enabled: root.visible && design.canUndo; onActivated: design.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: root.visible && design.canRedo; onActivated: design.redo() }

    // ---- Toolbar ----
    Item {
        id: toolbar
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 28; rightMargin: 28; topMargin: 20 }
        height: 40
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Designer"
                color: Theme.text
                font.pixelSize: Theme.fontTitle - 4
                font.weight: Font.Bold
            }
            Badge {
                anchors.verticalCenter: parent.verticalCenter
                text: design.changes.length ? design.changes.length + (design.changes.length === 1 ? " CHANGE" : " CHANGES") : "NO CHANGES"
                tone: design.changes.length ? "accent" : "neutral"
            }
            Badge {
                anchors.verticalCenter: parent.verticalCenter
                visible: design.problems.length > 0
                readonly property int warnings: design.problems.length - design.errorCount
                text: design.errorCount ? design.errorCount + (design.errorCount === 1 ? " ERROR" : " ERRORS")
                                        : warnings + (warnings === 1 ? " WARNING" : " WARNINGS")
                tone: design.errorCount ? "danger" : "warning"
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: panel.tab = "sql" }
            }
            // Where the design came from, or what went wrong opening/saving it.
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: design.error.length && !root.info.name ? design.error
                    : design.restored ? "Picked up where you left off"
                    : design.filePath.length ? design.filePath.slice(design.filePath.lastIndexOf("/") + 1) : ""
                color: design.error.length && !root.info.name ? Theme.danger : Theme.textSecondary
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
                width: Math.min(implicitWidth, 360)
            }
        }
        Row {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: 8
            ActionButton { text: "Open…"; implicitHeight: 34; onClicked: { fileDialog.saving = false; fileDialog.open() } }
            ActionButton { text: "Save…"; implicitHeight: 34; onClicked: { fileDialog.saving = true; fileDialog.open() } }
            Item { width: 6; height: 1 }
            ActionButton { text: "Undo"; implicitHeight: 34; enabled: design.canUndo; opacity: enabled ? 1 : 0.45; onClicked: design.undo() }
            ActionButton { text: "Redo"; implicitHeight: 34; enabled: design.canRedo; opacity: enabled ? 1 : 0.45; onClicked: design.redo() }
            ActionButton {
                text: "Start over"; implicitHeight: 34
                enabled: design.changes.length > 0 || design.canUndo
                opacity: enabled ? 1 : 0.45
                onClicked: { design.discard(); root.appliedTo = "" }
            }
            ActionButton { text: "+ Table"; primary: true; implicitHeight: 34; onClicked: root.addTable() }
        }
    }

    // ---- Canvas | panel, with a divider to drag ----
    SplitView {
        id: split
        anchors { left: parent.left; right: parent.right; top: toolbar.bottom; topMargin: 12; bottom: parent.bottom }
        orientation: Qt.Horizontal
        handle: Rectangle {
            implicitWidth: 7
            color: "transparent"
            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 1; height: parent.height
                        color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.separator }
        }

        // ---- Canvas ----
        DiagramView {
            id: canvas
            SplitView.fillWidth: true
            SplitView.minimumWidth: 300
            source: design
            keepPositions: true
            editable: true
            selectedTable: root.selected
            onCardMenu: function (table, x, y) { root.openMenu(tableMenu, table, "", x, y) }
            onColumnMenu: function (table, column, x, y) { root.openMenu(columnMenu, table, column, x, y) }
            onCanvasMenu: function (x, y, wx, wy) { root.menuPoint = Qt.point(wx, wy); canvasMenu.popup(canvas, x, y) }
            onConnectRequested: function (fromTable, fromColumn, toTable) {
                if (fromColumn.length) design.setReference(fromTable, fromColumn, toTable)
                else design.addReference(fromTable, toTable)
                root.selected = fromTable
                panel.tab = "table"
            }
            onTableClicked: function (name) { root.selected = name; if (panel.tab === "sql") panel.tab = "table" }
            onOpenTable: function (name) { root.selected = name; panel.tab = "table" }
        }

        // ---- Inspector | C++ | SQL ----
        Rectangle {
            id: panel
            property string tab: "table"
            SplitView.preferredWidth: Math.min(640, Math.max(440, root.width * 0.44))
            SplitView.minimumWidth: 320
            color: Theme.window

            SegmentedControl {
                id: tabs
                x: 16; y: 4
                options: [ "Table", "C++", "SQL" + (design.changes.length ? "  " + design.changes.length : "") ]
                currentIndex: ["table", "cpp", "sql"].indexOf(panel.tab)
                onActivated: function (index) { panel.tab = ["table", "cpp", "sql"][index] }
            }

            // ---- Table ----
            Flickable {
                id: inspector
                anchors { left: parent.left; right: parent.right; top: tabs.bottom; topMargin: 14; bottom: parent.bottom }
                visible: panel.tab === "table"
                contentHeight: form.height + 40
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }

                Text {
                    visible: root.info.name === undefined
                    x: 16; width: parent.width - 32
                    wrapMode: Text.Wrap
                    lineHeight: 1.4
                    text: "Click a table to edit it, or add one with + Table.\n\nEvery change shows up as Qivot C++ and as the SQL that would make it — nothing is written to the database."
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                }

                Column {
                    id: form
                    visible: root.info.name !== undefined
                    x: 16; width: parent.width - 32
                    spacing: 14

                    Column {
                        width: parent.width
                        spacing: 4
                        visible: errorLines.count > 0
                        Repeater {
                            id: errorLines
                            model: root.tableProblems.filter(function (p) { return p.severity === "error" })
                            Column {
                                width: parent.width
                                spacing: 4
                                ProblemLine { width: parent.width; problem: modelData; showColumn: true }
                                ActionButton {
                                    visible: modelData.fix === "allowEmpty"
                                    text: "Allow empty"
                                    implicitHeight: 28
                                    onClicked: {
                                        const cols = root.info.columns || []
                                        const i = cols.findIndex(function (c) { return c.name === modelData.column })
                                        if (i >= 0)
                                            design.updateColumn(root.info.name, i, cols[i].reference.length
                                                                ? { nullable: true, defaultValue: "" } : { nullable: true })
                                    }
                                }
                            }
                        }
                    }

                    // Name
                    Row {
                        width: parent.width
                        spacing: 8
                        Field {
                            id: tableName
                            width: parent.width - deleteTable.width - 8
                            text: root.info.name || ""
                            font.pixelSize: Theme.fontHeading
                            font.weight: Font.DemiBold
                            onEditingFinished: if (text !== root.info.name && design.renameTable(root.info.name, text)) root.selected = text.trim()
                        }
                        ActionButton {
                            id: deleteTable
                            text: "Delete"
                            implicitHeight: 34
                            onClicked: { design.removeTable(root.info.name); root.selected = "" }
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: (root.info.isNew ? "New table" : root.info.origin !== root.info.name ? "Was " + root.info.origin : "In the database")
                              + ((root.info.referencedBy || []).length ? "  ·  referenced by " + root.info.referencedBy.join(", ") : "")
                        color: root.info.isNew ? Theme.positive : Theme.textSecondary
                        font.pixelSize: Theme.fontSmall + 1
                    }
                    Text {
                        visible: design.error.length > 0
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: design.error
                        color: Theme.danger
                        font.pixelSize: Theme.fontBody
                    }
                    // Problems about the table as a whole (column ones show on the column).
                    Repeater {
                        model: root.tableProblems.filter(function (p) { return !p.column.length })
                        ProblemLine { width: form.width; problem: modelData }
                    }

                    // Columns
                    Repeater {
                        model: root.info.columns || []
                        Rectangle {
                            id: col
                            required property var modelData
                            required property int index
                            width: form.width
                            height: colBody.height + 20
                            radius: Theme.radius
                            color: Theme.surface
                            border.width: 1
                            readonly property var problems: root.tableProblems.filter(function (p) { return p.column === col.modelData.name })
                            readonly property bool hasError: problems.some(function (p) { return p.severity === "error" })
                            border.color: hasError ? Theme.danger : modelData.isNew ? Theme.positive : Theme.separator
                            function set(changes) { design.updateColumn(root.info.name, col.index, changes) }

                            Column {
                                id: colBody
                                x: 10; y: 10
                                width: parent.width - 20
                                spacing: 8
                                Row {
                                    width: parent.width
                                    spacing: 6
                                    Field {
                                        width: parent.width - typeBox.width - removeCol.width - 12
                                        text: col.modelData.name
                                        font.family: Theme.monoFont
                                        onEditingFinished: if (text !== col.modelData.name) col.set({ name: text })
                                    }
                                    Choice {
                                        id: typeBox
                                        width: 150
                                        editable: true
                                        model: design.typeSuggestions
                                        Component.onCompleted: editText = col.modelData.type
                                        onAccepted: if (editText !== col.modelData.type) col.set({ type: editText })
                                        onActivated: function (i) { col.set({ type: design.typeSuggestions[i] }) }
                                    }
                                    Text {
                                        id: removeCol
                                        width: 22; height: 30
                                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                        text: "×"
                                        color: removeMouse.containsMouse ? Theme.danger : Theme.textTertiary
                                        font.pixelSize: 18
                                        MouseArea { id: removeMouse; anchors.fill: parent; hoverEnabled: true
                                                    onClicked: design.removeColumn(root.info.name, col.index) }
                                    }
                                }
                                Row {
                                    width: parent.width
                                    spacing: 6
                                    Toggle { label: "Key"; on: col.modelData.primaryKey; ink: Theme.key; onToggled: col.set({ primaryKey: !on }) }
                                    Toggle { label: "Required"; on: !col.modelData.nullable; enabled: !col.modelData.primaryKey
                                             onToggled: col.set({ nullable: on }) }
                                    Toggle { label: "Unique"; on: col.modelData.unique; enabled: !col.modelData.primaryKey
                                             onToggled: col.set({ unique: !on }) }
                                    Choice {
                                        id: refBox
                                        width: parent.width - x
                                        readonly property var targets: [""].concat(design.tables.map(function (t) { return t.name })
                                                                                   .filter(function (n) { return n !== root.info.name }))
                                        model: targets.map(function (n) { return n.length ? "→ " + n : "No reference" })
                                        currentIndex: Math.max(0, targets.indexOf(col.modelData.reference))
                                        onActivated: function (i) { design.setReference(root.info.name, col.modelData.name, targets[i]) }
                                    }
                                }
                                Field {
                                    id: defaultField
                                    width: parent.width
                                    text: col.modelData.defaultValue
                                    placeholderText: col.modelData.autoIncrement ? "Numbered automatically" : "Default (SQL, e.g. 0 or 'pending')"
                                    enabled: !col.modelData.autoIncrement
                                    font.family: Theme.monoFont
                                    onEditingFinished: if (text !== col.modelData.defaultValue) col.set({ defaultValue: text })
                                }
                                Repeater {
                                    model: col.problems
                                    ProblemLine { width: colBody.width; problem: modelData; showColumn: false }
                                }
                                // One-click ways out of a "required, but there's nothing to put in it" error.
                                Row {
                                    visible: col.problems.some(function (p) { return p.fix === "allowEmpty" })
                                    spacing: 8
                                    ActionButton { text: "Allow empty"; implicitHeight: 30; onClicked: col.set(col.modelData.reference.length ? { nullable: true, defaultValue: "" } : { nullable: true }) }
                                    ActionButton { text: "Add a default"; implicitHeight: 30
                                                   onClicked: { defaultField.forceActiveFocus(); defaultField.selectAll() } }
                                }
                            }
                        }
                    }
                    ActionButton {
                        text: "+ Column"
                        onClicked: design.addColumn(root.info.name, "", "")
                    }
                }
            }

            // ---- C++ ----
            Item {
                id: cppPane
                anchors { left: parent.left; right: parent.right; top: tabs.bottom; topMargin: 14; bottom: parent.bottom
                          leftMargin: 16; rightMargin: 16; bottomMargin: 16 }
                visible: panel.tab === "cpp"
                property string code: ""
                function update() {
                    code = root.selected.length ? (design.cppModel(root.selected).code || "") : design.cppHeader()
                    cppView.load(code)
                }
                Connections { target: design; function onDesignChanged() { cppPane.update() } }
                Connections { target: root; function onSelectedChanged() { cppPane.update() } }
                Text {
                    id: cppTitle
                    text: root.selected.length ? "The Qivot class for " + root.selected : "Every table, as models.h"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                }
                ActionButton {
                    anchors { right: parent.right; verticalCenter: cppTitle.verticalCenter }
                    implicitHeight: 30
                    text: "Copy"
                    onClicked: root.copy(parent.code)
                }
                Rectangle {
                    anchors { left: parent.left; right: parent.right; top: cppTitle.bottom; topMargin: 14; bottom: parent.bottom }
                    radius: Theme.radius
                    clip: true
                    border.width: 1
                    border.color: Theme.separator
                    color: Theme.surface
                    CodeEditor {
                        id: cppView
                        anchors.fill: parent
                        anchors.margins: 1
                        readOnly: true
                        language: "cpp"
                    }
                }
                Component.onCompleted: update()
            }

            // ---- SQL ----
            Item {
                anchors { left: parent.left; right: parent.right; top: tabs.bottom; topMargin: 14; bottom: parent.bottom
                          leftMargin: 16; rightMargin: 16; bottomMargin: 16 }
                visible: panel.tab === "sql"
                Column {
                    id: changeList
                    width: parent.width
                    spacing: 6
                    Text {
                        visible: design.problems.length > 0
                        text: design.errorCount ? "Problems: the migration would fail" : "Warnings"
                        color: design.errorCount ? Theme.danger : Theme.warning
                        font.pixelSize: Theme.fontHeading
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: design.problems
                        ProblemLine {
                            width: changeList.width
                            problem: modelData
                            clickable: true
                            onClicked: { root.selected = modelData.table; panel.tab = "table" }
                        }
                    }
                    Item { width: 1; height: design.problems.length ? 8 : 0 }
                    Text {
                        text: design.changes.length ? "Changes" : "No changes yet: edit a table, or add one."
                        color: design.changes.length ? Theme.text : Theme.textSecondary
                        font.pixelSize: Theme.fontHeading
                        font.weight: design.changes.length ? Font.DemiBold : Font.Normal
                    }
                    Repeater {
                        model: design.changes
                        Text {
                            width: changeList.width
                            elide: Text.ElideRight
                            text: "•  " + modelData
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                        }
                    }
                }
                Row {
                    id: sqlActions
                    anchors { left: parent.left; right: parent.right; top: changeList.bottom; topMargin: 14 }
                    spacing: 8
                    visible: design.changes.length > 0
                    ActionButton { text: "Copy SQL"; implicitHeight: 32; onClicked: root.copy(design.migration) }
                    ActionButton {
                        visible: design.dialect === "sqlite"
                        text: "Apply to a copy"
                        enabled: design.errorCount === 0
                        opacity: enabled ? 1 : 0.45
                        implicitHeight: 32
                        onClicked: { root.appliedNote = ""; root.appliedTo = design.applyToCopy() }
                    }
                    ActionButton {
                        text: "Apply to database…"
                        primary: true
                        enabled: design.errorCount === 0
                        opacity: enabled ? 1 : 0.45
                        implicitHeight: 32
                        onClicked: applyDialog.open()
                    }
                }
                Text {
                    id: applied
                    anchors { left: parent.left; right: parent.right; top: sqlActions.bottom; topMargin: 10 }
                    visible: text.length > 0
                    wrapMode: Text.Wrap
                    text: design.error.length ? design.error + (root.failedStatement.length ? "\n\n" + root.failedStatement : "")
                          : root.appliedNote.length ? root.appliedNote
                          : root.appliedTo.length ? "Migrated a copy: " + root.appliedTo.slice(root.appliedTo.lastIndexOf("/") + 1)
                                                   + ". The original is untouched." : ""
                    color: design.error.length ? Theme.danger : Theme.positive
                    font.pixelSize: Theme.fontBody
                }
                Text {
                    id: openCopy
                    anchors { left: parent.left; top: applied.bottom; topMargin: 4 }
                    visible: root.appliedTo.length > 0 && !design.error.length && !root.appliedNote.length
                    text: "Open the copy"
                    color: Theme.accent
                    font.pixelSize: Theme.fontBody
                    font.underline: openMouse.containsMouse
                    MouseArea { id: openMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: root.openFile(root.appliedTo) }
                }
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                              top: openCopy.visible ? openCopy.bottom : applied.visible ? applied.bottom : sqlActions.visible ? sqlActions.bottom : changeList.bottom
                              topMargin: 14 }
                    visible: design.changes.length > 0
                    radius: Theme.radius
                    clip: true
                    border.width: 1
                    border.color: Theme.separator
                    color: Theme.surface
                    CodeEditor {
                        id: sqlView
                        anchors.fill: parent
                        anchors.margins: 1
                        readOnly: true
                        language: "sql"
                        Component.onCompleted: load(design.migration)
                        Connections { target: design; function onDesignChanged() { sqlView.load(design.migration) } }
                    }
                }
            }
        }
    }

    function copy(text) {
        clipboard.text = text
        clipboard.selectAll()
        clipboard.copy()
    }
    TextEdit { id: clipboard; visible: false }

    // ---- Small controls ----
    // One problem: ✗ or ⚠, "table.column", what's wrong.
    component ProblemLine: Item {
        id: line
        property var problem: ({})
        property bool showColumn: true
        property bool clickable: false
        signal clicked()
        implicitHeight: lineText.implicitHeight + 6
        height: implicitHeight
        Text {
            id: mark
            y: 3
            text: line.problem.severity === "error" ? "✗" : "⚠"
            color: line.problem.severity === "error" ? Theme.danger : Theme.warning
            font.pixelSize: Theme.fontBody
            font.weight: Font.Bold
        }
        Text {
            id: lineText
            anchors { left: mark.right; leftMargin: 8; right: parent.right; top: parent.top; topMargin: 3 }
            wrapMode: Text.Wrap
            textFormat: Text.StyledText
            text: (line.showColumn ? "<b>" + line.problem.table + (line.problem.column ? "." + line.problem.column : "") + "</b>  " : "")
                  + line.problem.message
            color: Theme.text
            font.pixelSize: Theme.fontSmall + 1
        }
        MouseArea { anchors.fill: parent; enabled: line.clickable; cursorShape: Qt.PointingHandCursor; onClicked: line.clicked() }
    }

    // ---- Apply to the database itself ----
    property string failedStatement: ""
    function applyNow() {
        const count = design.changes.length
        const r = design.applyToDatabase()
        root.failedStatement = r.ok ? "" : (r.failedStatement || "")
        root.appliedTo = ""
        root.appliedNote = r.ok ? "Applied " + count + (count === 1 ? " change" : " changes") + " to " + root.database.displayName + "."
                                  + (r.backup ? " The file as it was is saved as " + r.backup.slice(r.backup.lastIndexOf("/") + 1) + "." : "")
                                : ""
    }
    Popup {
        id: applyDialog
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: 500
        padding: 24
        Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#59000000" }
        background: Rectangle { radius: 14; color: Theme.window; border.width: 1; border.color: Theme.separator }
        contentItem: Column {
            width: applyDialog.availableWidth
            spacing: 12
            Text {
                width: applyDialog.availableWidth; wrapMode: Text.Wrap
                text: "Apply " + design.changes.length + (design.changes.length === 1 ? " change" : " changes")
                      + " to " + (root.database ? root.database.displayName : "") + "?"
                color: Theme.text
                font.pixelSize: 18; font.weight: Font.Bold
            }
            Rectangle {
                width: applyDialog.availableWidth
                height: Math.min(180, applyList.implicitHeight + 16)
                radius: Theme.radiusSmall
                color: Theme.surface
                border.width: 1; border.color: Theme.separator
                clip: true
                Flickable {
                    anchors { fill: parent; margins: 8 }
                    contentHeight: applyList.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: applyList
                        width: applyDialog.availableWidth
                        Repeater {
                            model: design.changes
                            Text { width: applyList.width; elide: Text.ElideRight; text: "•  " + modelData
                                   color: Theme.text; font.pixelSize: Theme.fontBody }
                        }
                    }
                }
            }
            Text {
                width: applyDialog.availableWidth; wrapMode: Text.Wrap; lineHeight: 1.2
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
                text: (design.dialect === "sqlite"
                       ? "The file is copied first, as a backup beside it. "
                       : "Make sure you have a backup of the database. ")
                      + (design.dialect === "mysql"
                         ? "MySQL commits each change to the structure as it goes, so if one fails, the ones before it stay."
                         : "It all runs in one transaction: if anything fails, nothing changes.")
                      + (root.database && !root.database.changesAllowed ? "\n\nThis also allows changes to the database." : "")
            }
            Row {
                anchors.right: parent.right
                spacing: 10
                ActionButton { text: "Cancel"; onClicked: applyDialog.close() }
                ActionButton {
                    text: root.database && root.database.changesAllowed ? "Apply" : "Allow changes and apply"
                    primary: true
                    onClicked: {
                        applyDialog.close()
                        root.changesRequested(root.applyNow)
                    }
                }
            }
        }
    }
}
