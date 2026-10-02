import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// Compare: this database's structure against another's — a file, a server or
/// a sample — and the SQL that makes one match the other.
Item {
    id: root
    property var database
    signal changesRequested(var then)         // ask to allow changes, then run `then`
    property string initialOther: ""          // --compare-with: a file, or sample:<id>
    Component.onCompleted: if (initialOther.length) {
        if (initialOther.indexOf("sample:") === 0) other.openSample(initialOther.slice(7))
        else other.open(initialOther)
    }

    SchemaCompare { id: compare; session: root.database }
    readonly property var other: compare.other
    readonly property string thisName: database ? database.displayName : ""
    readonly property string otherName: other.isOpen ? other.displayName : ""
    property string note: ""                   // what the last save or apply did
    property bool noteFailed: false

    OpenDialog { id: openOther; onPicked: function (file) { root.other.open(file) } }
    ConnectDialog { id: connectOther; database: root.other }
    DataFileDialog {
        id: saveSql
        saving: true
        kind: "sql"
        onPicked: function (file) {
            root.noteFailed = !compare.saveMigration(file)
            root.note = root.noteFailed ? compare.error : "Saved " + String(file).slice(String(file).lastIndexOf("/") + 1)
        }
    }
    TextEdit { id: clipboard; visible: false }
    ContextMenu {
        id: sampleMenu
        Repeater {
            model: root.database ? root.database.samples : []
            ContextMenuItem { text: modelData.title; onTriggered: root.other.openSample(modelData.id) }
        }
    }

    function applyNow() {
        const r = compare.applyToThis()
        root.noteFailed = !r.ok
        root.note = r.ok ? root.thisName + " now matches " + root.otherName + "."
                           + (r.backup ? " The file as it was is saved as " + r.backup.slice(r.backup.lastIndexOf("/") + 1) + "." : "")
                         : r.error + (r.failedStatement ? "\n" + r.failedStatement : "")
    }

    // ---- Header ----
    Column {
        id: header
        anchors { left: parent.left; leftMargin: 28; right: parent.right; rightMargin: 28; top: parent.top; topMargin: 22 }
        spacing: 4
        Text { text: "Compare"; color: Theme.text; font.pixelSize: Theme.fontTitle; font.weight: Font.Bold }
        Text {
            width: parent.width; wrapMode: Text.Wrap
            text: "How " + root.thisName + "'s structure differs from another database's, and the SQL that makes one match the other."
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
        }
    }

    // ---- What to compare with ----
    Row {
        id: picker
        anchors { left: header.left; top: header.bottom; topMargin: 16 }
        spacing: 8
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.other.isOpen ? "Compared with" : "Compare with"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
        }
        Rectangle {
            visible: root.other.isOpen
            anchors.verticalCenter: parent.verticalCenter
            width: otherLabel.implicitWidth + 20; height: 30; radius: 15
            color: Theme.accentSoft
            Text {
                id: otherLabel
                anchors.centerIn: parent
                text: root.otherName + "  ·  " + root.other.dialectName
                color: Theme.accent
                font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold
            }
        }
        ActionButton { text: root.other.isOpen ? "Another file…" : "A file…"; implicitHeight: 30; onClicked: openOther.open() }
        ActionButton { text: "A server…"; implicitHeight: 30; onClicked: connectOther.open() }
        ActionButton { id: sampleButton; text: "A sample ▾"; implicitHeight: 30
                       onClicked: sampleMenu.popup(sampleButton, 0, sampleButton.height + 4) }
    }
    Text {
        anchors { left: picker.right; leftMargin: 12; verticalCenter: picker.verticalCenter; right: parent.right; rightMargin: 28 }
        visible: root.other.error.length > 0 && !root.other.isOpen
        text: root.other.error
        color: Theme.danger
        font.pixelSize: Theme.fontBody
        elide: Text.ElideRight
    }

    // ---- Nothing to compare with yet ----
    Column {
        visible: !root.other.isOpen
        anchors.centerIn: parent
        spacing: 8
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Choose a database to compare with"
               color: Theme.text; font.pixelSize: Theme.fontHeading; font.weight: Font.DemiBold }
        Text { anchors.horizontalCenter: parent.horizontalCenter; width: 420; horizontalAlignment: Text.AlignHCenter
               wrapMode: Text.Wrap; color: Theme.textSecondary; font.pixelSize: Theme.fontBody
               text: "An older copy of this file, a server with the same schema, or a sample — tables and columns are matched by name." }
    }

    // ---- The comparison ----
    Item {
        visible: root.other.isOpen
        anchors { left: parent.left; leftMargin: 28; right: parent.right; rightMargin: 28; top: picker.bottom; topMargin: 16
                  bottom: parent.bottom; bottomMargin: 24 }

        SegmentedControl {
            id: direction
            options: [ "Make " + root.otherName + " match " + root.thisName, "Make " + root.thisName + " match " + root.otherName ]
            currentIndex: compare.direction === "toThis" ? 1 : 0
            onActivated: function (i) { compare.direction = i === 1 ? "toThis" : "toOther"; root.note = "" }
        }
        Text {
            anchors { left: direction.right; leftMargin: 14; verticalCenter: direction.verticalCenter; right: parent.right }
            visible: !compare.sameDialect
            text: "Different kinds of database: types are compared as written, so many columns show as changed."
            color: Theme.warning
            font.pixelSize: Theme.fontBody
            elide: Text.ElideRight
        }

        // The differences, in words.
        Rectangle {
            id: changesPanel
            anchors { left: parent.left; top: direction.bottom; topMargin: 14; bottom: parent.bottom }
            width: Math.min(400, parent.width * 0.4)
            radius: Theme.radius
            color: Theme.surface
            border.width: 1; border.color: Theme.separator
            clip: true
            Text {
                id: changesTitle
                x: 14; y: 12
                text: compare.changes.length === 0 ? "They match" : compare.changes.length + (compare.changes.length === 1 ? " difference" : " differences")
                color: compare.changes.length === 0 ? Theme.positive : Theme.text
                font.pixelSize: Theme.fontHeading; font.weight: Font.DemiBold
            }
            ListView {
                anchors { left: parent.left; right: parent.right; top: changesTitle.bottom; topMargin: 8; bottom: parent.bottom; margins: 6 }
                model: compare.changes
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }
                delegate: Item {
                    width: ListView.view.width
                    height: 28
                    readonly property color ink: /^(Create|Add)/.test(modelData) ? Theme.positive
                                               : /^Drop/.test(modelData) ? Theme.danger : Theme.warning
                    Rectangle { x: 10; anchors.verticalCenter: parent.verticalCenter; width: 7; height: 7; radius: 3.5; color: parent.ink }
                    Text {
                        x: 26; width: parent.width - 32
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // The SQL, and what to do with it.
        Item {
            anchors { left: changesPanel.right; leftMargin: 16; right: parent.right; top: changesPanel.top; bottom: parent.bottom }
            Row {
                id: sqlButtons
                spacing: 8
                ActionButton { text: "Copy SQL"; implicitHeight: 30; enabled: compare.migration.length > 0
                               onClicked: { clipboard.text = compare.migration; clipboard.selectAll(); clipboard.copy() } }
                ActionButton { text: "Save .sql…"; implicitHeight: 30; enabled: compare.migration.length > 0; onClicked: saveSql.open() }
                ActionButton {
                    visible: compare.direction === "toThis"
                    text: "Apply to " + root.thisName + "…"
                    primary: true
                    implicitHeight: 30
                    enabled: compare.migration.length > 0
                    opacity: enabled ? 1 : 0.45
                    onClicked: confirmApply.open()
                }
            }
            Text {
                anchors { left: sqlButtons.right; leftMargin: 12; right: parent.right; verticalCenter: sqlButtons.verticalCenter }
                text: root.note
                color: root.noteFailed ? Theme.danger : Theme.positive
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
            }
            CodeEditor {
                anchors { left: parent.left; right: parent.right; top: sqlButtons.bottom; topMargin: 10; bottom: parent.bottom }
                text: compare.migration.length ? compare.migration : "-- Nothing to change: the two match."
                language: "sql"
                readOnly: true
            }
        }
    }

    Popup {
        id: confirmApply
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: 460
        padding: 24
        Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#59000000" }
        background: Rectangle { radius: 14; color: Theme.window; border.width: 1; border.color: Theme.separator }
        contentItem: Column {
            width: confirmApply.availableWidth
            spacing: 12
            Text { width: confirmApply.availableWidth; wrapMode: Text.Wrap; color: Theme.text; font.pixelSize: 18; font.weight: Font.Bold
                   text: "Make " + root.thisName + " match " + root.otherName + "?" }
            Text {
                width: confirmApply.availableWidth; wrapMode: Text.Wrap; lineHeight: 1.2
                color: Theme.textSecondary; font.pixelSize: Theme.fontBody
                text: compare.changes.length + (compare.changes.length === 1 ? " change" : " changes")
                      + (compare.changes.some(function (c) { return /^Drop/.test(c) }) ? ", including drops: their data goes with them. " : ". ")
                      + (root.database && !root.database.isServer ? "The file is backed up first. " : "")
                      + "It runs in one transaction: if anything fails, nothing changes."
            }
            Row {
                anchors.right: parent.right
                spacing: 10
                ActionButton { text: "Cancel"; onClicked: confirmApply.close() }
                ActionButton {
                    text: root.database && root.database.changesAllowed ? "Apply" : "Allow changes and apply"
                    primary: true
                    onClicked: { confirmApply.close(); root.changesRequested(root.applyNow) }
                }
            }
        }
    }
}
