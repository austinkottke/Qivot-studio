import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0

/// Right side: the selected table or view, as its Structure or its Data.
Item {
    id: root
    property var info: ({})
    property var database                 // for the Data tab
    property int tab: 0                    // 0 = Structure, 1 = Data, 2 = Profile, 3 = C++
    property int initialRow: -1            // --select-row: open this row in the inspector
    signal navigate(string name)
    signal tabRequested(int tab)

    readonly property bool hasInfo: info !== undefined && info.name !== undefined
    readonly property var columns: hasInfo ? info.columns : []
    readonly property var foreignKeys: hasInfo ? info.foreignKeys : []
    readonly property var referencedBy: hasInfo ? info.referencedBy : []
    readonly property var indexes: hasInfo ? info.indexes : []
    readonly property bool isView: hasInfo && info.kind !== "table"

    function plural(n, one, many) { return Number(n).toLocaleString(Qt.locale(), "f", 0) + " " + (n === 1 ? one : many) }
    function action(rule) { return rule && rule !== "NO ACTION" ? rule : "" }

    // A text cell in a table-like row.
    component Cell: Text {
        color: Theme.text
        font.pixelSize: Theme.fontBody
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        height: parent ? parent.height : 0
    }
    // A clickable table reference, e.g. "author.id".
    component Ref: Text {
        id: ref
        property string target
        color: Theme.link
        font.pixelSize: Theme.fontBody
        font.underline: refMouse.containsMouse
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        MouseArea {
            id: refMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.navigate(ref.target)
        }
    }
    // One row of a card's list, with a hairline under all but the last.
    component ListRow: Item {
        property bool last: false
        width: parent ? parent.width : 0
        height: 36
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 16 }
                    height: 1; color: Theme.separator; visible: !parent.last }
    }

    // ---- Title, summary and the Structure | Data switch ----
    Item {
        id: titleBar
        visible: root.hasInfo
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: 28; rightMargin: 28; topMargin: 26 }
        height: titleColumn.height

        Column {
            id: titleColumn
            spacing: 6
            Row {
                spacing: 10
                Text {
                    text: root.hasInfo ? root.info.name : ""
                    color: Theme.text
                    font.pixelSize: Theme.fontTitle; font.weight: Font.Bold
                }
                Badge {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.hasInfo ? root.info.kind.toUpperCase() : ""
                    tone: root.isView ? "accent" : "neutral"
                }
            }
            Text {
                text: root.hasInfo
                      ? [ root.plural(root.info.rows, "row", "rows"),
                          root.plural(root.columns.length, "column", "columns"),
                          root.isView ? "" : root.plural(root.indexes.length, "index", "indexes") ]
                        .filter(s => s.length).join("  ·  ")
                      : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
        }

        SegmentedControl {
            anchors { right: parent.right; verticalCenter: titleColumn.verticalCenter }
            options: [ "Structure", "Data", "Profile", "C++" ]
            currentIndex: root.tab
            onActivated: (index) => root.tabRequested(index)
        }
    }

    // ---- Data: created only while shown, so Structure never queries rows ----
    Loader {
        anchors { left: parent.left; right: parent.right; top: titleBar.bottom; bottom: parent.bottom
                  leftMargin: 28; rightMargin: 28; topMargin: 20; bottomMargin: 24 }
        active: root.hasInfo && root.tab === 1
        visible: active
        sourceComponent: DataView {
            database: root.database
            tableName: root.info.name
            initialRow: root.initialRow
        }
    }

    // ---- Profile: what's in each column ----
    Loader {
        anchors { left: parent.left; right: parent.right; top: titleBar.bottom; bottom: parent.bottom
                  leftMargin: 28; rightMargin: 28; topMargin: 20; bottomMargin: 24 }
        active: root.hasInfo && root.tab === 2
        visible: active
        sourceComponent: ProfileView {
            database: root.database
            tableName: root.info.name
        }
    }

    // ---- C++: the Qivot model for this table ----
    Loader {
        anchors { left: parent.left; right: parent.right; top: titleBar.bottom; bottom: parent.bottom
                  leftMargin: 28; rightMargin: 28; topMargin: 20; bottomMargin: 24 }
        active: root.hasInfo && root.tab === 3
        visible: active
        sourceComponent: ModelView {
            database: root.database
            tableName: root.info.name
        }
    }

    // ---- Structure ----
    Flickable {
        anchors { left: parent.left; right: parent.right; top: titleBar.bottom; bottom: parent.bottom
                  topMargin: 18 }
        contentHeight: page.height + 40
        clip: true
        visible: root.hasInfo && root.tab === 0
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        Column {
            id: page
            x: 28
            width: root.width - 56
            spacing: 18

            // ---- Columns ----
            Card {
                width: page.width
                title: "Columns"
                detail: root.columns.length

                ListRow {
                    height: 30
                    Row {
                        anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
                        Repeater {
                            model: [ ["NAME", 0.30], ["TYPE", 0.20], ["NULL", 0.13], ["DEFAULT", 0.15], ["REFERENCES", 0.22] ]
                            Cell {
                                width: (page.width - 32) * modelData[1]
                                text: modelData[0]
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontSmall; font.weight: Font.Bold; font.letterSpacing: 0.5
                            }
                        }
                    }
                }
                Repeater {
                    model: root.columns
                    ListRow {
                        last: index === root.columns.length - 1
                        readonly property real w: page.width - 32
                        Row {
                            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
                            Item {
                                width: parent.parent.w * 0.30; height: parent.height
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 6
                                    Text {
                                        text: modelData.name
                                        color: Theme.text
                                        font.pixelSize: Theme.fontBody
                                        font.weight: modelData.primaryKey ? Font.DemiBold : Font.Normal
                                    }
                                    Badge { visible: modelData.primaryKey; text: "PK"; tone: "key"
                                            anchors.verticalCenter: parent.verticalCenter }
                                    Badge { visible: modelData.references.length > 0; text: "FK"; tone: "link"
                                            anchors.verticalCenter: parent.verticalCenter }
                                    Badge { visible: modelData.autoIncrement; text: "AUTO"; tone: "accent"
                                            anchors.verticalCenter: parent.verticalCenter }
                                }
                            }
                            Cell {
                                width: parent.parent.w * 0.20
                                text: modelData.type.length ? modelData.type : "—"
                                color: modelData.type.length ? Theme.text : Theme.textTertiary
                                font.family: Theme.monoFont; font.pixelSize: Theme.fontBody - 1
                            }
                            Cell {
                                width: parent.parent.w * 0.13
                                text: modelData.nullable ? "null" : "not null"
                                color: modelData.nullable ? Theme.textTertiary : Theme.text
                            }
                            Cell {
                                width: parent.parent.w * 0.15
                                text: modelData.defaultValue.length ? modelData.defaultValue : "—"
                                color: modelData.defaultValue.length ? Theme.text : Theme.textTertiary
                                font.family: Theme.monoFont; font.pixelSize: Theme.fontBody - 1
                            }
                            Item {
                                width: parent.parent.w * 0.22; height: parent.height
                                Ref {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.min(implicitWidth, parent.width)
                                    visible: modelData.references.length > 0
                                    text: "→ " + modelData.references
                                    target: modelData.refTable
                                }
                            }
                        }
                    }
                }
            }

            // ---- Foreign keys: this table → others ----
            Card {
                width: page.width
                visible: root.foreignKeys.length > 0
                title: "Foreign keys"
                detail: root.foreignKeys.length
                Repeater {
                    model: root.foreignKeys
                    ListRow {
                        last: index === root.foreignKeys.length - 1
                        Row {
                            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                            spacing: 8
                            Text { text: modelData.columns.join(", "); color: Theme.text
                                   font.pixelSize: Theme.fontBody; font.family: Theme.monoFont }
                            Text { text: "→"; color: Theme.textTertiary; font.pixelSize: Theme.fontBody }
                            Ref { text: modelData.refTable + " (" + modelData.refColumns.join(", ") + ")"
                                  target: modelData.refTable }
                        }
                        Row {
                            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                            spacing: 6
                            Badge { visible: root.action(modelData.onDelete).length > 0
                                    text: "ON DELETE " + root.action(modelData.onDelete) }
                            Badge { visible: root.action(modelData.onUpdate).length > 0
                                    text: "ON UPDATE " + root.action(modelData.onUpdate) }
                        }
                    }
                }
            }

            // ---- Referenced by: others → this table ----
            Card {
                width: page.width
                visible: root.referencedBy.length > 0
                title: "Referenced by"
                detail: root.referencedBy.length
                Repeater {
                    model: root.referencedBy
                    ListRow {
                        last: index === root.referencedBy.length - 1
                        Row {
                            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                            spacing: 8
                            Ref { text: modelData.table + "." + modelData.columns.join(", ")
                                  target: modelData.table }
                            Text { text: "→  " + modelData.refColumns.join(", "); color: Theme.textSecondary
                                   font.pixelSize: Theme.fontBody; font.family: Theme.monoFont }
                        }
                        Badge {
                            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                            visible: root.action(modelData.onDelete).length > 0
                            text: "ON DELETE " + root.action(modelData.onDelete)
                        }
                    }
                }
            }

            // ---- Indexes ----
            Card {
                width: page.width
                visible: !root.isView
                title: "Indexes"
                detail: root.indexes.length
                Text {
                    visible: root.indexes.length === 0
                    leftPadding: 16; topPadding: 12; bottomPadding: 12
                    text: "No indexes. Lookups on columns other than the rowid scan the whole table."
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                }
                Repeater {
                    model: root.indexes
                    ListRow {
                        last: index === root.indexes.length - 1
                        Row {
                            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                            spacing: 12
                            Text { text: modelData.name; color: Theme.text
                                   font.pixelSize: Theme.fontBody; font.family: Theme.monoFont }
                            Text { text: "(" + modelData.columns.join(", ") + ")"; color: Theme.textSecondary
                                   font.pixelSize: Theme.fontBody }
                        }
                        Row {
                            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                            spacing: 6
                            Badge { visible: modelData.unique; text: "UNIQUE"; tone: "positive" }
                            Badge { visible: modelData.implicit; text: "FROM CONSTRAINT" }
                        }
                    }
                }
            }

            Text {
                visible: root.isView
                text: root.hasInfo && root.info.kind === "virtual"
                      ? "A virtual table: its storage is managed by an SQLite extension (such as full-text search)."
                      : "A view: a saved query. It has columns but no keys or indexes of its own."
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !root.hasInfo
        text: "Select a table"
        color: Theme.textTertiary
        font.pixelSize: Theme.fontHeading
    }
}
