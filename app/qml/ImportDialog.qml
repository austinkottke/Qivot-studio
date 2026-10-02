import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// Importing a CSV file into a table: which file column goes where, a look at
/// the first rows, then one transaction (all rows or none).
Popup {
    id: root
    property var database
    property string tableName
    signal imported(int rows)

    function start(file) {
        importer.table = root.tableName
        if (importer.load(file)) open()
    }
    readonly property alias error: importer.error

    CsvImport { id: importer; session: root.database }

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(720, parent ? parent.width - 40 : 720)
    height: Math.min(640, parent ? parent.height - 40 : 640)
    padding: 22
    Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#59000000" }
    background: Rectangle { radius: 14; color: Theme.window; border.width: 1; border.color: Theme.separator }

    contentItem: Item {
        Column {
            id: head
            width: parent.width
            spacing: 6
            Text {
                text: "Import " + importer.fileName + " into " + root.tableName
                color: Theme.text
                font.pixelSize: 18; font.weight: Font.Bold
                width: parent.width; elide: Text.ElideMiddle
            }
            Text {
                text: Number(importer.rowCount).toLocaleString(Qt.locale(), "f", 0) + (importer.rowCount === 1 ? " row" : " rows")
                      + " · separated by " + importer.delimiterName
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
            Row {
                spacing: 8
                topPadding: 4
                Toggle { label: "First row names the columns"; on: importer.headerRow; onToggled: importer.headerRow = !importer.headerRow }
                Toggle { label: "Empty cells are NULL"; on: importer.emptyIsNull; onToggled: importer.emptyIsNull = !importer.emptyIsNull }
            }
        }

        // Where each of the file's columns goes, with its first value.
        Rectangle {
            id: mapFrame
            anchors { left: parent.left; right: parent.right; top: head.bottom; topMargin: 14; bottom: footer.top; bottomMargin: 14 }
            radius: Theme.radius
            color: Theme.surface
            border.width: 1; border.color: Theme.separator
            clip: true
            ListView {
                id: mapList
                anchors { fill: parent; margins: 6 }
                model: importer.fileColumns
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }
                header: Item {
                    width: mapList.width; height: 28
                    Text { x: 10; anchors.verticalCenter: parent.verticalCenter; text: "IN THE FILE"; color: Theme.textTertiary
                           font.pixelSize: Theme.fontSmall; font.weight: Font.Bold; font.letterSpacing: 0.5 }
                    Text { x: mapList.width * 0.55; anchors.verticalCenter: parent.verticalCenter; text: "GOES INTO"; color: Theme.textTertiary
                           font.pixelSize: Theme.fontSmall; font.weight: Font.Bold; font.letterSpacing: 0.5 }
                }
                delegate: Item {
                    width: mapList.width
                    height: 46
                    readonly property string sample: importer.preview.length ? (importer.preview[0][index] || "") : ""
                    Column {
                        x: 10; width: mapList.width * 0.55 - 40
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text { width: parent.width; text: modelData; color: Theme.text; font.pixelSize: Theme.fontBody
                               font.weight: Font.DemiBold; elide: Text.ElideRight }
                        Text { width: parent.width; text: parent.parent.sample.length ? "e.g. " + parent.parent.sample : "(empty)"
                               color: Theme.textTertiary; font.pixelSize: Theme.fontSmall + 1; elide: Text.ElideRight }
                    }
                    Text { x: mapList.width * 0.55 - 26; anchors.verticalCenter: parent.verticalCenter; text: "→"
                           color: Theme.textTertiary; font.pixelSize: Theme.fontHeading }
                    Choice {
                        x: mapList.width * 0.55; width: mapList.width * 0.45 - 10
                        anchors.verticalCenter: parent.verticalCenter
                        readonly property var choices: [""].concat(importer.tableColumns)
                        model: choices.map(function (c) { return c.length ? c : "Skip this column" })
                        currentIndex: Math.max(0, choices.indexOf(importer.mapping[index] || ""))
                        onActivated: function (i) { importer.setMapping(index, choices[i]) }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.separator
                                visible: index < mapList.count - 1 }
                }
            }
        }

        Item {
            id: footer
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 40
            Text {
                anchors { left: parent.left; right: buttons.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                text: importer.error.length ? importer.error : "All rows go in one transaction: if one fails, none are imported."
                color: importer.error.length ? Theme.danger : Theme.textTertiary
                font.pixelSize: Theme.fontBody
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Row {
                id: buttons
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                spacing: 8
                ActionButton { text: "Cancel"; onClicked: root.close() }
                ActionButton {
                    text: "Import " + Number(importer.rowCount).toLocaleString(Qt.locale(), "f", 0) + (importer.rowCount === 1 ? " row" : " rows")
                    primary: true
                    enabled: importer.mapping.some(function (m) { return m.length > 0 })
                    opacity: enabled ? 1 : 0.45
                    onClicked: if (importer.run()) { root.close(); root.imported(importer.imported) }
                }
            }
        }
    }
}
