import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// Export: the open database as a buildable C++ project. The files are
/// previewed first and nothing is written until Export; then the project
/// opens in the IDE (IdeView) to edit, build, test and run. Open Folder…
/// opens any other CMake project in the IDE.
Item {
    id: root
    property var database
    property bool autoBuild: false            // --build: export and build on open (screenshots, demos)
    property string openProject: ""           // --project: start with this folder open in the IDE
    property string projectsDir: ""           // --projects-dir: where projects go instead of ~/Documents/Qivot Projects
    signal buildFinished()

    ProjectExport {
        id: project
        session: root.database
        name: nameBox.text.trim()
    }

    /// The folder open in the IDE; "" shows the export preview.
    property string ideFolder: ""
    property string ideAction: ""
    function exportAndOpen(action) {
        if (!project.write()) return
        ideAction = action || ""
        ideFolder = project.projectPath
    }

    property string currentFile: "src/models.h"
    function language(file) {
        return file.endsWith(".md") ? "markdown"
             : file.indexOf("CMakeLists") >= 0 ? "cmake" : "cpp"
    }

    function reset() {
        nameBox.text = project.suggestedName
        if (project.files.indexOf(currentFile) < 0) currentFile = "src/models.h"
    }
    Connections {
        target: root.database
        function onOpenChanged() { root.reset() }
    }
    Component.onCompleted: {
        if (projectsDir.length) project.directory = projectsDir
        reset()
        if (openProject.length) { ideAction = autoBuild ? "test" : ""; ideFolder = openProject }
        else if (autoBuild) exportAndOpen("test")
    }

    // The explorer's rows: folders, then the files in them, like an IDE's project tree.
    readonly property var tree: {
        const rows = []
        let folder = ""
        const files = project.files.slice().sort((a, b) => {
            const da = a.indexOf("/") >= 0, db = b.indexOf("/") >= 0
            return da === db ? a.localeCompare(b) : (da ? -1 : 1)
        })
        for (const f of files) {
            const slash = f.lastIndexOf("/")
            const dir = slash >= 0 ? f.slice(0, slash) : ""
            if (dir !== folder && dir.length) rows.push({ folder: true, name: dir, path: dir, depth: 0 })
            folder = dir
            rows.push({ folder: false, name: f.slice(slash + 1), path: f, depth: dir.length ? 1 : 0 })
        }
        return rows
    }

    FolderPicker { id: folderPicker; onPicked: function (folder) { project.directory = folder } }
    FolderPicker {
        id: openPicker
        title: "Open a project folder"
        onPicked: function (folder) { root.ideAction = ""; root.ideFolder = String(folder) }
    }

    // ---- The IDE, once a project is open ----
    Loader {
        anchors.fill: parent
        active: root.ideFolder.length > 0
        visible: active
        sourceComponent: IdeView {
            folder: root.ideFolder
            database: root.database
            canRegenerate: root.ideFolder === project.projectPath
            autoAction: root.ideAction
            onRegenerateRequested: project.writeFile("src/models.h")
            onCloseRequested: root.ideFolder = ""
            onBuildFinished: root.buildFinished()
        }
    }

    // ---- The export preview ----
    Item {
        id: previewPane
        anchors.fill: parent
        visible: root.ideFolder.length === 0


        // ---- Toolbar: name, location, Export, Build & test ----
        Item {
            id: toolbar
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 62

            TextBox {
                id: nameBox
                label: "PROJECT"
                width: 220
                anchors.bottom: parent.bottom
            }
            Column {
                anchors { left: nameBox.right; leftMargin: 16; right: actions.left; rightMargin: 16
                          bottom: parent.bottom; bottomMargin: 2 }
                spacing: 5
                Text {
                    text: "LOCATION"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall + 1
                    font.weight: Font.DemiBold
                }
                Row {
                    width: parent.width
                    spacing: 10
                    Text {
                        width: Math.min(implicitWidth, parent.width - choose.width - 10)
                        height: choose.height
                        verticalAlignment: Text.AlignVCenter
                        text: project.displayPath
                        elide: Text.ElideMiddle
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                    }
                    ActionButton { id: choose; text: "Choose…"; implicitHeight: 32; onClicked: folderPicker.open() }
                }
            }
            Row {
                id: actions
                anchors { right: parent.right; bottom: parent.bottom }
                spacing: 10
                ActionButton { text: "Open Folder…"; onClicked: openPicker.open() }
                ActionButton {
                    visible: project.exists
                    text: "Open project"
                    onClicked: { root.ideAction = ""; root.ideFolder = project.projectPath }
                }
                ActionButton {
                    text: project.exists ? "Export again" : "Export"
                    primary: true
                    enabled: project.files.length > 0
                    opacity: enabled ? 1 : 0.5
                    onClicked: root.exportAndOpen()
                }
            }
        }

        Text {
            id: status
            anchors { left: parent.left; right: showFolder.visible ? showFolder.left : parent.right
                      rightMargin: 12; top: toolbar.bottom; topMargin: 10 }
            elide: Text.ElideRight
            text: project.error.length ? project.error
                  : project.exists ? "A project is already there. Open it, or Export again to replace its " + project.files.length + " files."
                  : project.files.length + " files, previewed below. Nothing is written until you export."
            color: project.error.length ? Theme.danger : Theme.textSecondary
            font.pixelSize: Theme.fontBody
        }

        Text {
            id: showFolder
            anchors { right: parent.right; verticalCenter: status.verticalCenter }
            visible: project.exists
            text: "Show folder"
            color: Theme.accent
            font.pixelSize: Theme.fontBody
            font.underline: folderMouse.containsMouse
            MouseArea {
                id: folderMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Qt.openUrlExternally("file://" + project.projectPath)
            }
        }

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: status.bottom; topMargin: 12; bottom: parent.bottom }
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.separator
            clip: true

            // Project explorer.
            Rectangle {
                id: explorer
                width: 230
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 1 }
                color: Theme.sidebar
                radius: Theme.radius

                Text {
                    id: explorerTitle
                    x: 16; y: 14
                    text: (project.name.length ? project.name : "PROJECT").toUpperCase()
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSmall; font.weight: Font.Bold; font.letterSpacing: 0.6
                }
                Column {
                    anchors { left: parent.left; right: parent.right; top: explorerTitle.bottom; topMargin: 8
                              leftMargin: 8; rightMargin: 8 }
                    spacing: 1
                    Repeater {
                        model: root.tree
                        Rectangle {
                            width: parent.width
                            height: 28
                            radius: Theme.radiusSmall
                            readonly property bool current: !modelData.folder && modelData.path === root.currentFile
                            color: current ? Theme.accent : rowMouse.containsMouse && !modelData.folder ? Theme.hover : "transparent"

                            FileIcon {
                                id: icon
                                x: 10 + modelData.depth * 16
                                anchors.verticalCenter: parent.verticalCenter
                                folder: modelData.folder
                                ink: parent.current ? "white" : modelData.folder ? Theme.textSecondary : Theme.accent
                            }
                            Text {
                                anchors { left: icon.right; leftMargin: 8; right: parent.right; rightMargin: 8
                                          verticalCenter: parent.verticalCenter }
                                text: modelData.name
                                elide: Text.ElideRight
                                color: parent.current ? "white" : Theme.text
                                font.pixelSize: Theme.fontBody
                                font.weight: modelData.folder ? Font.DemiBold : Font.Normal
                            }
                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: !modelData.folder
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.currentFile = modelData.path
                            }
                        }
                    }
                }
            }
            Rectangle { anchors { left: explorer.right; top: parent.top; bottom: parent.bottom }
                        width: 1; color: Theme.separator }

            // The file.
            Item {
                anchors { left: explorer.right; leftMargin: 1; right: parent.right; top: parent.top; bottom: parent.bottom }
                Item {
                    id: fileTab
                    width: parent.width; height: 40
                    Text {
                        x: 18; anchors.verticalCenter: parent.verticalCenter
                        text: root.currentFile
                        color: Theme.text
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.fontBody
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                        text: viewer.lineCount + " lines"
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSmall + 1
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.separator }
                }
                ScrollView {
                    anchors { left: parent.left; right: parent.right; top: fileTab.bottom; bottom: parent.bottom }
                    clip: true
                    TextArea {
                        id: viewer
                        text: project.files.length ? project.content(root.currentFile) : ""
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextArea.NoWrap
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.fontBody
                        color: Theme.text
                        selectionColor: Theme.accentSoft
                        selectedTextColor: Theme.text
                        padding: 18
                        background: null
                    }
                }
                SyntaxHighlighter { document: viewer.textDocument; language: root.language(root.currentFile); dark: Theme.dark }
            }
        }
    }
}
