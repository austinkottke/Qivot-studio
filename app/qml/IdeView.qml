import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// The IDE: a project folder's files in an explorer, open files in tabs, and
/// Build / Test / Run with the output, the compiler's problems and the test
/// results underneath. Everything is saved before a build.
Item {
    id: root
    property alias folder: ws.root
    property var database                     // passes the server password to the tests
    property bool canRegenerate: false        // the folder is an export of the open database
    property string startFile: "src/models.h"
    property string autoAction: ""            // "build" | "test" | "run" on open (screenshots, demos)
    signal regenerateRequested()
    signal closeRequested()
    signal buildFinished()

    Workspace {
        id: ws
        onFileChanged: function (path) {
            // Changed on disk (e.g. Export again): reload unless there are unsaved edits.
            const i = root.tabIndex(path)
            const editor = i >= 0 ? editors.itemAt(i) : null
            if (editor && !editor.dirty) editor.load(ws.read(path))
        }
    }
    ProjectBuild {
        id: build
        directory: ws.root
        session: root.database
        onFinished: {
            root.buildFinished()
            if (errorCount > 0) panel.tab = "problems"
            else if (mode === "test" && results.length) panel.tab = "tests"
            else panel.tab = "output"
        }
    }

    property real explorerWidth: 240

    // ---- Open files ----
    ListModel { id: tabs }                     // { path, name }
    property int current: -1
    readonly property var currentEditor: current >= 0 ? editors.itemAt(current) : null

    function tabIndex(path) {
        for (let i = 0; i < tabs.count; ++i) if (tabs.get(i).path === path) return i
        return -1
    }
    function language(path) {
        return path.endsWith(".md") ? "markdown"
             : path.indexOf("CMakeLists") >= 0 || path.endsWith(".cmake") ? "cmake" : "cpp"
    }
    function openFile(path, line, column) {
        if (!ws.exists(path)) return
        let i = tabIndex(path)
        if (i < 0) {
            tabs.append({ path: path, name: path.slice(path.lastIndexOf("/") + 1) })
            i = tabs.count - 1
        }
        current = i
        if (line > 0) Qt.callLater(function () { const e = editors.itemAt(i); if (e) e.goTo(line, column) })
        else Qt.callLater(function () { const e = editors.itemAt(i); if (e) e.focusEditor() })
    }
    function save(i) {
        const e = editors.itemAt(i)
        if (e && e.dirty && ws.write(tabs.get(i).path, e.text)) e.markSaved()
    }
    function saveAll() { for (let i = 0; i < tabs.count; ++i) save(i) }
    function closeTab(i, force) {
        const e = editors.itemAt(i)
        if (!force && e && e.dirty) { confirmClose.index = i; confirmClose.open(); return }
        tabs.remove(i)
        if (current >= tabs.count) current = tabs.count - 1
        else if (current > i) current--
    }
    readonly property int unsaved: {
        let n = 0
        for (let i = 0; i < editors.count; ++i) { const e = editors.itemAt(i); if (e && e.dirty) ++n }
        return n
    }
    // Problems: errors first, the project's own files before the rest; warnings
    // from outside the project (Qivot, Qt) only when asked for.
    property bool showOutsideWarnings: false
    readonly property var outsideWarnings: build.problems.filter(function (p) {
        return p.severity === "warning" && !ws.relativePath(p.file).length })
    readonly property var shownProblems: build.problems
        .filter(function (p) { return root.showOutsideWarnings || p.severity === "error" || ws.relativePath(p.file).length })
        .sort(function (a, b) {
            const rank = function (p) { return (p.severity === "error" ? 0 : 2) + (ws.relativePath(p.file).length ? 0 : 1) }
            return rank(a) - rank(b)
        })

    function run(mode) {
        saveAll()
        panel.tab = "output"
        build.start(mode)
    }

    onFolderChanged: { tabs.clear(); current = -1 }
    Component.onCompleted: {
        if (startFile.length && ws.exists(startFile)) openFile(startFile)
        if (autoAction.length) run(autoAction)
    }

    Shortcut { sequences: [StandardKey.Save]; enabled: root.visible; onActivated: root.save(root.current) }
    Shortcut { sequence: "Ctrl+B"; enabled: root.visible && !build.running; onActivated: root.run("build") }
    Shortcut { sequence: "Ctrl+U"; enabled: root.visible && !build.running; onActivated: root.run("test") }
    Shortcut { sequence: "Ctrl+R"; enabled: root.visible && !build.running; onActivated: root.run("run") }
    Shortcut { sequence: "Ctrl+W"; enabled: root.visible && root.current >= 0; onActivated: root.closeTab(root.current) }

    // ---- Toolbar ----
    Item {
        id: toolbar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 40

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: ws.name
                color: Theme.text
                font.pixelSize: Theme.fontHeading + 2
                font.weight: Font.Bold
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: ws.fileCount + " files" + (root.unsaved ? "  ·  " + root.unsaved + " unsaved" : "")
                color: root.unsaved ? Theme.warning : Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
        }
        Row {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: 8
            ActionButton { text: "Close"; implicitHeight: 34; onClicked: root.closeRequested() }
            ActionButton {
                visible: root.canRegenerate
                text: "Regenerate models"
                implicitHeight: 34
                enabled: !build.running
                opacity: enabled ? 1 : 0.5
                onClicked: root.regenerateRequested()
            }
            Item { width: 8; height: 1 }
            ActionButton {
                text: "Build  ⌘B"; implicitHeight: 34
                visible: !build.running
                enabled: build.cmake.length > 0
                onClicked: root.run("build")
            }
            ActionButton {
                text: "Test  ⌘U"; implicitHeight: 34
                visible: !build.running
                enabled: build.cmake.length > 0
                onClicked: root.run("test")
            }
            ActionButton {
                text: build.running ? "Stop" : "Run  ⌘R"; implicitHeight: 34
                primary: true
                enabled: build.running || build.cmake.length > 0
                onClicked: build.running ? build.stop() : root.run("run")
            }
        }
    }

    SplitView {
        anchors { left: parent.left; right: parent.right; top: toolbar.bottom; topMargin: 12; bottom: parent.bottom }
        orientation: Qt.Vertical
        handle: Rectangle {
            implicitHeight: 12
            color: "transparent"
            Rectangle { anchors.centerIn: parent; width: 44; height: 4; radius: 2
                        color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : Theme.separator }
        }

        // ---- Explorer | editors ----
        Rectangle {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 200
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.separator
            clip: true

            Rectangle {
                id: explorer
                width: root.explorerWidth
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 1 }
                color: Theme.sidebar
                radius: Theme.radius

                Text {
                    id: explorerTitle
                    x: 16; y: 14
                    text: "EXPLORER"
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSmall; font.weight: Font.Bold; font.letterSpacing: 0.6
                }
                ListView {
                    anchors { left: parent.left; right: parent.right; top: explorerTitle.bottom; topMargin: 8
                              bottom: parent.bottom; leftMargin: 8; rightMargin: 8 }
                    clip: true
                    model: ws
                    spacing: 1
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { }
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 26
                        radius: Theme.radiusSmall
                        readonly property bool isCurrent: !isDir && root.current >= 0 && tabs.count > root.current
                                                          && tabs.get(root.current).path === path
                        color: isCurrent ? Theme.accent : rowMouse.containsMouse ? Theme.hover : "transparent"

                        Text {
                            id: chevron
                            x: 4 + depth * 14
                            width: 12
                            anchors.verticalCenter: parent.verticalCenter
                            text: isDir ? (expanded ? "▾" : "▸") : ""
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSmall
                        }
                        FileIcon {
                            id: icon
                            anchors { left: chevron.right; leftMargin: 4; verticalCenter: parent.verticalCenter }
                            folder: isDir
                            ink: parent.isCurrent ? "white" : isDir ? Theme.textSecondary : Theme.accent
                        }
                        Text {
                            anchors { left: icon.right; leftMargin: 8; right: parent.right; rightMargin: 8
                                      verticalCenter: parent.verticalCenter }
                            text: name
                            elide: Text.ElideRight
                            color: parent.isCurrent ? "white" : Theme.text
                            font.pixelSize: Theme.fontBody
                            font.weight: isDir ? Font.DemiBold : Font.Normal
                        }
                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: isDir ? ws.toggle(index) : root.openFile(path)
                        }
                    }
                }
            }
            // The divider: drag it to resize the explorer.
            Rectangle {
                anchors { left: explorer.right; top: parent.top; bottom: parent.bottom }
                width: 1
                color: dividerMouse.containsMouse || dividerMouse.pressed ? Theme.accent : Theme.separator
                MouseArea {
                    id: dividerMouse
                    anchors { fill: parent; leftMargin: -4; rightMargin: -4 }
                    hoverEnabled: true
                    cursorShape: Qt.SplitHCursor
                    property real startX: 0
                    property real startWidth: 0
                    onPressed: function (mouse) { startX = mapToItem(root, mouse.x, 0).x; startWidth = root.explorerWidth }
                    onPositionChanged: function (mouse) {
                        if (pressed)
                            root.explorerWidth = Math.max(160, Math.min(520, startWidth + mapToItem(root, mouse.x, 0).x - startX))
                    }
                }
            }

            Item {
                id: editorArea
                anchors { left: explorer.right; leftMargin: 1; right: parent.right; top: parent.top; bottom: parent.bottom
                          rightMargin: 1; bottomMargin: 1 }

                // Tabs.
                Rectangle {
                    id: tabBar
                    width: parent.width; height: 38
                    color: Theme.surfaceRaised
                    Row {
                        height: parent.height
                        Repeater {
                            model: tabs
                            Rectangle {
                                readonly property bool active: index === root.current
                                readonly property var editor: editors.itemAt(index)
                                height: tabBar.height
                                width: tabLabel.implicitWidth + 52
                                color: active ? Theme.surface : tabMouse.containsMouse ? Theme.hover : "transparent"
                                Rectangle { visible: parent.active; width: parent.width; height: 2; color: Theme.accent }
                                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.separator }
                                MouseArea {
                                    id: tabMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: root.current = index
                                }
                                Text {
                                    id: tabLabel
                                    x: 14; anchors.verticalCenter: parent.verticalCenter
                                    text: name
                                    color: parent.active ? Theme.text : Theme.textSecondary
                                    font.pixelSize: Theme.fontBody
                                }
                                // Unsaved dot, which turns into × on hover (like most editors).
                                Item {
                                    width: 18; height: 18
                                    anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                                    readonly property bool dirty: parent.editor ? parent.editor.dirty : false
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 8; height: 8; radius: 4
                                        color: Theme.textSecondary
                                        visible: parent.dirty && !closeMouse.containsMouse
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        text: "×"
                                        color: closeMouse.containsMouse ? Theme.text : Theme.textTertiary
                                        font.pixelSize: Theme.fontHeading
                                        visible: closeMouse.containsMouse || (!parent.dirty && (tabMouse.containsMouse || parent.parent.active))
                                    }
                                    MouseArea {
                                        id: closeMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: root.closeTab(index)
                                    }
                                }
                            }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.separator }
                }

                // One editor per tab, so each keeps its text, undo and scroll position.
                Item {
                    anchors { left: parent.left; right: parent.right; top: tabBar.bottom; bottom: statusBar.top }
                    Repeater {
                        id: editors
                        model: tabs
                        CodeEditor {
                            anchors.fill: parent
                            visible: index === root.current
                            language: root.language(path)
                            Component.onCompleted: load(ws.read(path))
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: tabs.count === 0
                        text: "Open a file from the explorer.\n⌘B builds, ⌘U runs the tests, ⌘R runs the program."
                        horizontalAlignment: Text.AlignHCenter
                        lineHeight: 1.4
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontBody
                    }
                }

                Rectangle {
                    id: statusBar
                    anchors.bottom: parent.bottom
                    width: parent.width; height: 26
                    color: Theme.surfaceRaised
                    Rectangle { width: parent.width; height: 1; color: Theme.separator }
                    Text {
                        x: 14; anchors.verticalCenter: parent.verticalCenter
                        text: root.currentEditor ? tabs.get(root.current).path : ""
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall + 1
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                        text: root.currentEditor
                              ? "Ln " + root.currentEditor.line + ", Col " + root.currentEditor.column
                                + "   ·   " + ({ cpp: "C++", cmake: "CMake", markdown: "Markdown" })[root.currentEditor.language]
                                + "   ·   " + (root.currentEditor.dirty ? "Edited" : "Saved")
                              : ""
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall + 1
                    }
                }
            }
        }

        // ---- Output | Problems | Tests ----
        Rectangle {
            id: panel
            property string tab: "output"
            SplitView.preferredHeight: Math.max(220, root.height * 0.32)
            SplitView.minimumHeight: 120
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.separator
            clip: true

            Item {
                id: panelBar
                width: parent.width; height: 44
                SegmentedControl {
                    x: 12; anchors.verticalCenter: parent.verticalCenter
                    options: [ "Output",
                               "Problems" + (build.errorCount ? "  " + build.errorCount + " ✗" : "")
                                          + (build.problems.length - build.errorCount - root.outsideWarnings.length > 0
                                             ? "  " + (build.problems.length - build.errorCount - root.outsideWarnings.length) + " ⚠" : ""),
                               "Tests" + (build.results.length ? "  " + build.passedCount + "/" + build.results.length : "") ]
                    currentIndex: ["output", "problems", "tests"].indexOf(panel.tab)
                    onActivated: function (index) { panel.tab = ["output", "problems", "tests"][index] }
                }
                Row {
                    anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                    spacing: 6
                    visible: build.stage.length > 0
                    Repeater {
                        model: build.mode === "build" ? [["configure", "Configure"], ["build", "Build"]]
                             : [["configure", "Configure"], ["build", "Build"],
                                build.mode === "run" ? ["run", "Run"] : ["test", "Test"]]
                        StepPill {
                            label: modelData[1]
                            readonly property var order: ["configure", "build", build.mode === "run" ? "run" : "test"]
                            readonly property int mine: order.indexOf(modelData[0])
                            readonly property int now: order.indexOf(build.step)
                            status: mine < now ? "done" : mine > now ? "waiting"
                                  : build.running ? "running" : build.stage === "passed" ? "done" : "failed"
                        }
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.separator }
            }

            // Output.
            ScrollView {
                anchors { left: parent.left; right: parent.right; top: panelBar.bottom; bottom: parent.bottom }
                visible: panel.tab === "output"
                clip: true
                TextArea {
                    text: build.log.length ? build.log : "Build, test or run the project to see its output here."
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextArea.NoWrap
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.fontSmall + 1
                    color: build.log.length ? Theme.textSecondary : Theme.textTertiary
                    padding: 14
                    background: null
                    onTextChanged: cursorPosition = length
                }
            }

            // Problems: click one to open the file at its line.
            ListView {
                anchors { left: parent.left; right: parent.right; top: panelBar.bottom; bottom: parent.bottom; margins: 6 }
                visible: panel.tab === "problems"
                clip: true
                model: root.shownProblems
                boundsBehavior: Flickable.StopAtBounds
                footer: Text {
                    visible: root.outsideWarnings.length > 0
                    height: visible ? 32 : 0
                    leftPadding: 10
                    verticalAlignment: Text.AlignVCenter
                    text: (root.showOutsideWarnings ? "Hide " : "Show ") + root.outsideWarnings.length
                          + " warnings from outside the project (Qivot, Qt)"
                    color: Theme.accent
                    font.pixelSize: Theme.fontBody
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.showOutsideWarnings = !root.showOutsideWarnings
                    }
                }
                ScrollBar.vertical: ScrollBar { }
                delegate: Rectangle {
                    readonly property string relative: ws.relativePath(modelData.file)
                    width: ListView.view.width
                    height: 30
                    radius: Theme.radiusSmall
                    color: problemMouse.containsMouse && relative.length ? Theme.hover : "transparent"
                    Text {
                        id: mark
                        x: 10; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.severity === "error" ? "✗" : "⚠"
                        color: modelData.severity === "error" ? Theme.danger : Theme.warning
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.Bold
                    }
                    Text {
                        id: where
                        anchors { left: mark.right; leftMargin: 10; verticalCenter: parent.verticalCenter }
                        text: (parent.relative.length ? parent.relative : modelData.file.slice(modelData.file.lastIndexOf("/") + 1))
                              + ":" + modelData.line
                        color: parent.relative.length ? Theme.link : Theme.textSecondary
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.fontSmall + 1
                    }
                    Text {
                        anchors { left: where.right; leftMargin: 12; right: parent.right; rightMargin: 10
                                  verticalCenter: parent.verticalCenter }
                        text: modelData.message
                        elide: Text.ElideRight
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                    }
                    MouseArea {
                        id: problemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: parent.relative.length ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: if (parent.relative.length) root.openFile(parent.relative, modelData.line, modelData.column)
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: root.shownProblems.length === 0 && root.outsideWarnings.length === 0
                    text: build.stage.length ? "No errors or warnings." : "Errors and warnings from the compiler show up here."
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontBody
                }
            }

            // Tests: one chip per model.
            Flow {
                anchors { left: parent.left; right: parent.right; top: panelBar.bottom; margins: 14 }
                visible: panel.tab === "tests"
                spacing: 6
                Repeater {
                    model: build.results
                    Rectangle {
                        height: 28
                        width: chipText.implicitWidth + 24
                        radius: 14
                        color: modelData.passed ? (Theme.dark ? "#2630D158" : "#1F248A3D")
                                                : (Theme.dark ? "#33FF453A" : "#1FD70015")
                        Text {
                            id: chipText
                            anchors.centerIn: parent
                            text: (modelData.passed ? "✓ " : "✗ ") + modelData.name + "   " + modelData.seconds.toFixed(2) + " s"
                            color: modelData.passed ? Theme.positive : Theme.danger
                            font.pixelSize: Theme.fontSmall + 1
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: panel.tab === "tests" && build.results.length === 0
                text: "Test (⌘U) builds the project and runs a test per model."
                color: Theme.textTertiary
                font.pixelSize: Theme.fontBody
            }
        }
    }

    // Closing a tab with unsaved edits.
    Popup {
        id: confirmClose
        property int index: -1
        anchors.centerIn: parent
        width: 380
        modal: true
        padding: 22
        background: Rectangle { radius: 14; color: Theme.window; border.width: 1; border.color: Theme.separator }
        Column {
            width: parent.width
            spacing: 16
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: "Save changes to " + (confirmClose.index >= 0 && confirmClose.index < tabs.count
                                            ? tabs.get(confirmClose.index).name : "") + "?"
                color: Theme.text
                font.pixelSize: Theme.fontHeading
                font.weight: Font.DemiBold
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                ActionButton { text: "Cancel"; onClicked: confirmClose.close() }
                ActionButton { text: "Don't Save"; onClicked: { confirmClose.close(); root.closeTab(confirmClose.index, true) } }
                ActionButton {
                    text: "Save"; primary: true
                    onClicked: { root.save(confirmClose.index); confirmClose.close(); root.closeTab(confirmClose.index, true) }
                }
            }
        }
    }
}
