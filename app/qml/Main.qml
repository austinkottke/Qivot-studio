import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

ApplicationWindow {
    id: win
    width: 1180; height: 760
    minimumWidth: 860; minimumHeight: 520
    visible: true
    color: Theme.window
    title: db.isOpen ? db.displayName + " — Qivot Studio" : "Qivot Studio"

    // Set by main.cpp from the command line.
    property string startupFile: ""
    property var startupConnection: ({})        // from --connect
    property bool startupSample: false
    property string startupSampleId: ""
    property bool startupAllowChanges: false  // --allow-changes
    property bool startupEditDemo: false      // --edit-demo
    property string startupCompare: ""        // --compare-with
    property string startupExportDiagram: ""  // --export-diagram
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
    property bool screenshotMode: false      // --shot: ignore the real pointer

    property string view: "diagram"            // "diagram", "query", or "table"
    property string selectedTable: ""
    property int detailTab: 0             // Structure or Data; kept as you move between tables

    Database {
        id: db
        onOpenChanged: {
            if (isOpen && win.startupAllowChanges) { win.startupAllowChanges = false; allowChanges(true) }
            // A refresh after a change: stay where we are.
            if (refreshing) {
                if (win.selectedTable.length && table(win.selectedTable).name === undefined)
                    win.selectedTable = win.firstTable()
                return
            }
            const wanted = win.startupTable
            win.startupTable = ""                    // only for the first file opened
            win.selectedTable = !isOpen ? ""
                              : (wanted.length && table(wanted).name !== undefined) ? wanted
                              : win.firstTable()
            // A freshly opened database starts on the bird's-eye view, unless a table was asked for.
            win.view = win.startupView === "query" || win.startupView === "export" || win.startupView === "design"
                       || win.startupView === "samples" || win.startupView === "compare"
                       || win.startupProject.length
                       ? (win.startupProject.length ? "export" : win.startupView)
                     : wanted.length && win.startupView !== "diagram" ? "table" : "diagram"
        }
    }

    // The first real table (not a view), so a freshly opened file shows something useful.
    function firstTable() {
        for (let t of db.tables)
            if (t.kind === "table") return t.name
        return db.tables.length ? db.tables[0].name : ""
    }

    Component.onCompleted: {
        if (startupConnection.type) db.connectTo(startupConnection)
        else if (startupFile.length) db.open(startupFile)
        else if (startupSample) db.openSample(startupSampleId)
    }

    OpenDialog { id: openDialog; onPicked: function (file) { db.open(file) } }

    ConnectDialog { id: connectDialog; database: db }

    // Changes need the user's say-so: requestChanges(then) asks once, then runs `then`.
    AllowChangesDialog {
        id: allowDialog
        database: db
        property var then: null
        onAllowed: { if (then) then(); then = null }
        onClosed: if (!db.changesAllowed) then = null
    }
    function requestChanges(then) {
        if (db.changesAllowed) { if (then) then(); return }
        allowDialog.then = then || null
        allowDialog.open()
    }
    function showConnectDialog() { connectDialog.open() }

    // The Samples page before anything is open: from the welcome screen's Samples button.
    property bool browsingSamples: startupView === "samples" && !startupSample
    Connections { target: db; function onOpenChanged() { if (db.isOpen) win.browsingSamples = false } }

    WelcomeView {
        anchors.fill: parent
        visible: !db.isOpen && !win.browsingSamples
        error: db.error
        onOpenRequested: openDialog.open()
        onSamplesRequested: win.browsingSamples = true
        onConnectRequested: connectDialog.open()
        onFileDropped: (url) => db.open(url)
    }

    Loader {
        anchors.fill: parent
        active: !db.isOpen && win.browsingSamples
        visible: active
        sourceComponent: Rectangle {
            color: Theme.window
            SamplesView {
                anchors.fill: parent
                database: db
                canGoBack: true
                onBack: win.browsingSamples = false
                onOpenSample: function (id) { db.openSample(id) }
                onConnectSample: function (id) { connectDialog.openForSample(id) }
            }
        }
    }

    Item {
        anchors.fill: parent
        visible: db.isOpen

        SchemaSidebar {
            id: sidebar
            width: 272
            z: 2            // above the diagram, whose lines can extend under it
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            database: db
            selected: win.view === "table" ? win.selectedTable : ""
            diagramSelected: win.view === "diagram"
            querySelected: win.view === "query"
            exportSelected: win.view === "export"
            designSelected: win.view === "design"
            samplesSelected: win.view === "samples"
            compareSelected: win.view === "compare"
            onCompareRequested: win.view = "compare"
            onSamplesRequested: win.view = "samples"
            onDesignRequested: win.view = "design"
            onExportRequested: win.view = "export"
            onQueryRequested: win.view = "query"
            onSelect: (name) => { win.selectedTable = name; win.view = "table" }
            onDiagramRequested: win.view = "diagram"
            // Closing a sample goes back to the samples, to pick another.
            onAllowChangesRequested: win.requestChanges(null)
            onCloseRequested: {
                const wasSample = db.sampleId.length > 0
                db.close()
                win.browsingSamples = wasSample
            }
        }
        Rectangle {
            anchors { left: sidebar.right; top: parent.top; bottom: parent.bottom }
            width: 1; color: Theme.separator
        }

        DiagramView {
            anchors { left: sidebar.right; leftMargin: 1; right: parent.right
                      top: parent.top; bottom: parent.bottom }
            visible: win.view === "diagram"
            database: db
            initialFind: win.startupFind
            hoverEnabled: !win.screenshotMode
            exportOnLoad: win.startupExportDiagram
            // Remember where cards are dragged, per database.
            layoutKey: db.isOpen ? db.dialect + ":" + (db.filePath.length ? db.filePath : db.location + "/" + db.displayName) : ""
            onOpenTable: (name) => { win.selectedTable = name; win.detailTab = 0; win.view = "table" }
        }

        QueryView {
            anchors { left: sidebar.right; leftMargin: 29; right: parent.right; rightMargin: 28
                      top: parent.top; topMargin: 24; bottom: parent.bottom; bottomMargin: 24 }
            visible: win.view === "query"
            database: db
            initialQuery: win.startupQuery
            startMode: win.startupQueryBuilder || win.startupBuilderDemo ? "builder" : ""
            builderDemo: win.startupBuilderDemo
            runInitialQuery: win.startupQuery.length > 0
        }

        Loader {
            anchors { left: sidebar.right; leftMargin: 29; right: parent.right; rightMargin: 28
                      top: parent.top; topMargin: 20; bottom: parent.bottom; bottomMargin: 24 }
            active: win.view === "export"       // created on first visit, so nothing is generated before
            visible: active
            sourceComponent: ProjectView {
                database: db
                autoBuild: win.startupBuild
                projectsDir: win.projectsDir
                openProject: win.startupProject
                onBuildFinished: win.buildFinished()
            }
        }

        Loader {
            anchors { left: sidebar.right; leftMargin: 1; right: parent.right; top: parent.top; bottom: parent.bottom }
            active: win.view === "design"
            visible: active
            sourceComponent: DesignerView {
                database: db
                demo: win.startupDesignDemo
                demoMenu: win.startupDesignMenu
                initialTable: win.startupView === "design" ? win.startupDesignTable : ""
                initialTab: win.startupDesignTab.length ? win.startupDesignTab : "table"
                onOpenFile: function (path) { db.open(path) }
                onChangesRequested: function (then) { win.requestChanges(then) }
            }
        }

        Loader {
            anchors { left: sidebar.right; leftMargin: 1; right: parent.right; top: parent.top; bottom: parent.bottom }
            active: win.view === "samples"
            visible: active
            sourceComponent: SamplesView {
                database: db
                onOpenSample: function (id) { db.openSample(id) }
                onShowDiagram: win.view = "diagram"
                onConnectSample: function (id) { connectDialog.openForSample(id) }
            }
        }

        Loader {
            anchors { left: sidebar.right; leftMargin: 1; right: parent.right; top: parent.top; bottom: parent.bottom }
            active: win.view === "compare"
            visible: active
            sourceComponent: CompareView {
                database: db
                initialOther: win.startupCompare
                onChangesRequested: function (then) { win.requestChanges(then) }
            }
        }

        TableDetail {
            anchors { left: sidebar.right; leftMargin: 1; right: parent.right
                      top: parent.top; bottom: parent.bottom }
            visible: win.view === "table"
            initialRow: win.startupRow
            // Re-read when the database or the selection changes.
            info: (db.tables, win.selectedTable.length ? db.table(win.selectedTable) : ({}))
            database: db
            tab: win.detailTab
            onTabRequested: (t) => win.detailTab = t
            onNavigate: (name) => win.selectedTable = name
            onChangesRequested: win.requestChanges(null)
            editDemo: win.startupEditDemo
        }
    }
}
