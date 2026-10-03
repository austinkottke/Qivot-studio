import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// One open database, with everything about it: its sidebar and screens, its
/// query tabs, where you are in it, and its dialogs. Before anything is open
/// (or after it's closed) it shows the welcome screen. The window holds one
/// of these per tab (Main.qml), so switching databases keeps each as it was.
Item {
    id: pane
    readonly property alias db: db
    readonly property alias redis: redis
    // What the window's tab shows, whichever is open here.
    readonly property bool anyOpen: db.isOpen || redis.isOpen
    readonly property string tabTitle: db.isOpen ? db.displayName : redis.isOpen ? redis.displayName : ""
    readonly property string tabDialect: db.isOpen ? db.dialect : redis.isOpen ? "redis" : ""
    readonly property string tabDetail: db.isOpen ? db.dialectName + " · " + db.location
                                      : redis.isOpen ? "Redis · " + redis.location : ""
    function closeAll() { db.close(); redis.close() }
    signal buildFinished()                   // --build: --shot waits for this

    // From the command line (Main.qml passes them to the first pane only).
    property string startupFile: ""
    property var startupConnection: ({})
    property bool startupSample: false
    property string startupSampleId: ""
    property bool startupAllowChanges: false
    property bool startupEditDemo: false
    property string startupCompare: ""
    property string startupExportDiagram: ""
    property bool startupExplain: false
    property bool startupCompleteDemo: false
    property string startupTable: ""
    property string startupView: ""
    property string startupQuery: ""
    property bool startupQueryBuilder: false
    property bool startupBuilderDemo: false
    property bool startupBuild: false
    property string projectsDir: ""
    property string startupProject: ""
    property bool startupDesignDemo: false
    property string startupDesignTab: ""
    property string startupDesignMenu: ""
    property string startupDesignTable: ""
    property string startupFind: ""
    property int startupRow: -1
    property bool screenshotMode: false
    property bool rememberConnections: true

    property string view: "diagram"            // "diagram", "query", or "table"
    property string selectedTable: ""
    property int detailTab: 0             // Structure or Data; kept as you move between tables

    Database {
        id: db
        onOpenChanged: {
            if (isOpen && pane.startupAllowChanges) { pane.startupAllowChanges = false; allowChanges(true) }
            // Recent, for the welcome screen (a sample says it's one just after opening).
            if (isOpen && !refreshing && pane.rememberConnections)
                Qt.callLater(function () { if (db.isOpen && !db.sampleId.length) ConnectionHistory.remember(db.connectionSettings) })
            // A refresh after a change: stay where we are.
            if (refreshing) {
                if (pane.selectedTable.length && table(pane.selectedTable).name === undefined)
                    pane.selectedTable = pane.firstTable()
                return
            }
            const wanted = pane.startupTable
            pane.startupTable = ""                    // only for the first file opened
            pane.selectedTable = !isOpen ? ""
                              : (wanted.length && table(wanted).name !== undefined) ? wanted
                              : pane.firstTable()
            // A freshly opened database starts on the bird's-eye view, unless a table was asked for.
            pane.view = pane.startupView === "query" || pane.startupView === "export" || pane.startupView === "design"
                       || pane.startupView === "samples" || pane.startupView === "compare"
                       || pane.startupProject.length
                       ? (pane.startupProject.length ? "export" : pane.startupView)
                     : wanted.length && pane.startupView !== "diagram" ? "table" : "diagram"
        }
    }

    // The first real table (not a view), so a freshly opened file shows something useful.
    function firstTable() {
        for (let t of db.tables)
            if (t.kind === "table") return t.name
        return db.tables.length ? db.tables[0].name : ""
    }

    Component.onCompleted: {
        if (startupConnection.type === "redis") redis.connectTo(startupConnection)
        else if (startupConnection.type) db.connectTo(startupConnection)
        else if (startupFile.length) db.open(startupFile)
        else if (startupSample) db.openSample(startupSampleId)
    }

    OpenDialog { id: openDialog; onPicked: function (file) { db.open(file) } }

    ConnectDialog { id: connectDialog; database: db; redis: redis }

    // A Redis server, instead of a database (RedisView).
    Redis { id: redis }
    Loader {
        anchors.fill: parent
        z: 3
        active: redis.isOpen
        visible: active
        sourceComponent: Rectangle {
            color: Theme.window
            RedisView {
                anchors.fill: parent
                redis: pane.redis
                initialKey: pane.startupTable
                initialMode: pane.startupView
                initialCommands: pane.startupQuery
                onCloseRequested: pane.redis.close()
            }
        }
    }

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
    Connections { target: db; function onOpenChanged() { if (db.isOpen) pane.browsingSamples = false } }

    WelcomeView {
        anchors.fill: parent
        visible: !db.isOpen && !redis.isOpen && !pane.browsingSamples
        error: db.error
        onOpenRequested: openDialog.open()
        onSamplesRequested: pane.browsingSamples = true
        onConnectRequested: connectDialog.open()
        onFileDropped: (url) => db.open(url)
        // A file opens; a server needs its password (never kept), so the dialog asks.
        onRecentOpened: (entry) => {
            if (entry.kind === "file") db.open(entry.settings.path)
            else connectDialog.openWith(entry.settings)
        }
    }

    Loader {
        anchors.fill: parent
        active: !db.isOpen && pane.browsingSamples
        visible: active
        sourceComponent: Rectangle {
            color: Theme.window
            SamplesView {
                anchors.fill: parent
                database: db
                canGoBack: true
                onBack: pane.browsingSamples = false
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
            selected: pane.view === "table" ? pane.selectedTable : ""
            diagramSelected: pane.view === "diagram"
            querySelected: pane.view === "query"
            exportSelected: pane.view === "export"
            designSelected: pane.view === "design"
            samplesSelected: pane.view === "samples"
            compareSelected: pane.view === "compare"
            onCompareRequested: pane.view = "compare"
            onSamplesRequested: pane.view = "samples"
            onDesignRequested: pane.view = "design"
            onExportRequested: pane.view = "export"
            onQueryRequested: pane.view = "query"
            onSelect: (name) => { pane.selectedTable = name; pane.view = "table" }
            onDiagramRequested: pane.view = "diagram"
            // Closing a sample goes back to the samples, to pick another.
            onAllowChangesRequested: pane.requestChanges(null)
            onCloseRequested: {
                const wasSample = db.sampleId.length > 0
                db.close()
                pane.browsingSamples = wasSample
            }
        }
        Rectangle {
            anchors { left: sidebar.right; top: parent.top; bottom: parent.bottom }
            width: 1; color: Theme.separator
        }

        DiagramView {
            anchors { left: sidebar.right; leftMargin: 1; right: parent.right
                      top: parent.top; bottom: parent.bottom }
            visible: pane.view === "diagram"
            database: db
            initialFind: pane.startupFind
            hoverEnabled: !pane.screenshotMode
            exportOnLoad: pane.startupExportDiagram
            // Remember where cards are dragged, per database.
            layoutKey: db.isOpen ? db.dialect + ":" + (db.filePath.length ? db.filePath : db.location + "/" + db.displayName) : ""
            onOpenTable: (name) => { pane.selectedTable = name; pane.detailTab = 0; pane.view = "table" }
        }

        QueryView {
            anchors { left: sidebar.right; leftMargin: 29; right: parent.right; rightMargin: 28
                      top: parent.top; topMargin: 24; bottom: parent.bottom; bottomMargin: 24 }
            visible: pane.view === "query"
            database: db
            initialQuery: pane.startupQuery
            startMode: pane.startupQueryBuilder || pane.startupBuilderDemo ? "builder" : ""
            builderDemo: pane.startupBuilderDemo
            explainInitialQuery: pane.startupExplain
            completeDemo: pane.startupCompleteDemo
            runInitialQuery: pane.startupQuery.length > 0
        }

        Loader {
            anchors { left: sidebar.right; leftMargin: 29; right: parent.right; rightMargin: 28
                      top: parent.top; topMargin: 20; bottom: parent.bottom; bottomMargin: 24 }
            active: pane.view === "export"       // created on first visit, so nothing is generated before
            visible: active
            sourceComponent: ProjectView {
                database: db
                autoBuild: pane.startupBuild
                projectsDir: pane.projectsDir
                openProject: pane.startupProject
                onBuildFinished: pane.buildFinished()
            }
        }

        Loader {
            anchors { left: sidebar.right; leftMargin: 1; right: parent.right; top: parent.top; bottom: parent.bottom }
            active: pane.view === "design"
            visible: active
            sourceComponent: DesignerView {
                database: db
                demo: pane.startupDesignDemo
                demoMenu: pane.startupDesignMenu
                initialTable: pane.startupView === "design" ? pane.startupDesignTable : ""
                initialTab: pane.startupDesignTab.length ? pane.startupDesignTab : "table"
                onOpenFile: function (path) { db.open(path) }
                onChangesRequested: function (then) { pane.requestChanges(then) }
            }
        }

        Loader {
            anchors { left: sidebar.right; leftMargin: 1; right: parent.right; top: parent.top; bottom: parent.bottom }
            active: pane.view === "samples"
            visible: active
            sourceComponent: SamplesView {
                database: db
                onOpenSample: function (id) { db.openSample(id) }
                onShowDiagram: pane.view = "diagram"
                onConnectSample: function (id) { connectDialog.openForSample(id) }
            }
        }

        Loader {
            anchors { left: sidebar.right; leftMargin: 1; right: parent.right; top: parent.top; bottom: parent.bottom }
            active: pane.view === "compare"
            visible: active
            sourceComponent: CompareView {
                database: db
                initialOther: pane.startupCompare
                onChangesRequested: function (then) { pane.requestChanges(then) }
            }
        }

        TableDetail {
            anchors { left: sidebar.right; leftMargin: 1; right: parent.right
                      top: parent.top; bottom: parent.bottom }
            visible: pane.view === "table"
            initialRow: pane.startupRow
            // Re-read when the database or the selection changes.
            info: (db.tables, pane.selectedTable.length ? db.table(pane.selectedTable) : ({}))
            database: db
            tab: pane.detailTab
            onTabRequested: (t) => pane.detailTab = t
            onNavigate: (name) => pane.selectedTable = name
            onChangesRequested: pane.requestChanges(null)
            editDemo: pane.startupEditDemo
        }
    }
}
