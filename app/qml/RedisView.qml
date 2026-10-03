import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// A Redis server: its keys (found with a pattern, a page at a time) and each
/// one's value by type, a console, and what the server says about itself.
/// Read-only until changes are allowed (see RedisSession).
Item {
    id: root
    property var redis
    property string initialKey: ""            // --table: a key to show
    property string initialMode: ""           // --view console / server
    property string initialCommands: ""       // --query: run in the console, ';' between commands
    signal closeRequested()

    property string mode: "keys"              // "keys", "console" or "server"
    property string pattern: "*"
    property string cursor: "0"
    property var keys: []
    property bool scanned: false
    property string selectedKey: ""
    property var keyDetails: ({})
    property var valuePage: ({})

    function fmt(n) { return Number(n).toLocaleString(Qt.locale(), "f", 0) }
    function ttlText(ms) {
        if (ms === undefined || ms < 0) return ""
        const s = Math.round(ms / 1000)
        return s < 60 ? s + " s" : s < 3600 ? Math.round(s / 60) + " min" : s < 86400 ? Math.round(s / 3600) + " h" : Math.round(s / 86400) + " d"
    }
    function bytes(n) {
        if (n === undefined || n < 0) return ""
        return n < 1024 ? n + " B" : n < 1048576 ? (n / 1024).toFixed(1) + " KB" : (n / 1048576).toFixed(1) + " MB"
    }
    // Each type its own colour, the way they're told apart in the list.
    function typeColor(t) {
        return t === "string" ? "#3B82F6" : t === "hash" ? "#8B5CF6" : t === "list" ? "#10B981"
             : t === "set" ? "#F59E0B" : t === "zset" ? "#EF4444" : t === "stream" ? "#06B6D4"
             : t === "ReJSON-RL" ? "#EC4899" : Theme.textTertiary
    }
    // The same colour, faint, for a badge's background ("#AARRGGBB").
    function typeTint(t) {
        const c = typeColor(t)
        return typeof c === "string" && c.length === 7 ? "#29" + c.slice(1) : Theme.surfaceRaised
    }
    function typeLabel(t) {
        return t === "string" ? "STR" : t === "hash" ? "HASH" : t === "list" ? "LIST" : t === "set" ? "SET"
             : t === "zset" ? "ZSET" : t === "stream" ? "STRM" : t === "ReJSON-RL" ? "JSON" : String(t).toUpperCase().slice(0, 4)
    }

    // ---- Keys: SCAN, a page at a time ----
    function rescan() {
        keys = []
        cursor = "0"
        scanned = false
        more()
    }
    // SCAN gives keys in hash order: sorted here, numbers as numbers (bulk:2 before bulk:10).
    function naturalLess(a, b) {
        const re = /(\d+)|(\D+)/g
        const x = a.match(re) || [], y = b.match(re) || []
        for (let i = 0; i < Math.min(x.length, y.length); ++i) {
            if (x[i] === y[i]) continue
            const nx = /^\d/.test(x[i]), ny = /^\d/.test(y[i])
            if (nx && ny) return Number(x[i]) - Number(y[i])
            return x[i] < y[i] ? -1 : 1
        }
        return x.length - y.length
    }
    function more() {
        const page = redis.scan(pattern.length ? pattern : "*", cursor, 500)
        keys = keys.concat(page.keys).sort(function (a, b) { return root.naturalLess(a.key, b.key) })
        cursor = page.cursor
        scanned = true
    }
    function select(key) {
        selectedKey = key
        keyDetails = redis.keyInfo(key)
        valuePage = redis.value(key, null, 200)
        table.columns = columnsFor(valuePage.kind)
        table.reset(valuePage.rows || [])
    }
    function loadMoreValues() {
        if (valuePage.next === undefined || valuePage.next === null || valuePage.next === "0") return
        const page = redis.value(selectedKey, valuePage.next, 200)
        table.append(page.rows || [])
        valuePage = page
    }
    function columnsFor(kind) {
        return kind === "hash" ? [ "field", "value" ] : kind === "list" ? [ "index", "value" ]
             : kind === "set" ? [ "member" ] : kind === "zset" ? [ "member", "score" ]
             : kind === "stream" ? [ "id", "fields" ] : []
    }
    ListTable { id: table }

    Connections {
        target: root.redis
        function onOpenChanged() { if (root.redis.isOpen) root.rescan() }
        function onDatabaseChanged() { if (root.redis.isOpen) { root.selectedKey = ""; root.rescan() } }
    }
    Component.onCompleted: {
        if (!redis || !redis.isOpen) return
        rescan()
        if (initialKey.length) select(initialKey)
        if (initialMode === "console" || initialMode === "server") { mode = initialMode; if (mode === "server") redis.refresh() }
        if (initialCommands.length) {
            mode = "console"
            initialCommands.split(";").forEach(function (c) { if (c.trim().length) console_.runLine(c.trim()) })
        }
    }

    // ---- The sidebar ----
    Rectangle {
        id: side
        width: 272
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        color: Theme.sidebar
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.separator }

        Column {
            id: sideHead
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
            spacing: 4
            Item {
                width: parent.width; height: 24
                Text {
                    anchors { left: parent.left; right: closeBox.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    text: root.redis ? root.redis.displayName : ""
                    color: Theme.text
                    font.pixelSize: Theme.fontHeading; font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Rectangle {
                    id: closeBox
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    width: 22; height: 22; radius: 6
                    color: closeMouse.containsMouse ? Theme.hover : "transparent"
                    Rectangle { anchors.centerIn: parent; width: 10; height: 1.5; color: Theme.textSecondary; rotation: 45 }
                    Rectangle { anchors.centerIn: parent; width: 10; height: 1.5; color: Theme.textSecondary; rotation: -45 }
                    MouseArea { id: closeMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.closeRequested() }
                }
            }
            Text {
                width: parent.width
                text: root.redis ? "Redis " + (root.redis.info.version || "") + (root.redis.info.mode ? " · " + root.redis.info.mode : "")
                                   + " · " + root.redis.location : ""
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }
            Item { width: 1; height: 4 }
            Toggle {
                label: root.redis && root.redis.changesAllowed ? "Changes allowed · lock" : "Read-only"
                on: root.redis && root.redis.changesAllowed
                ink: root.redis && root.redis.changesAllowed ? Theme.danger : Theme.accent
                onToggled: {
                    if (root.redis.changesAllowed) root.redis.allowChanges(false)
                    else allowConfirm.open()
                }
            }
            Item { width: 1; height: 8 }
            Choice {
                id: dbChoice
                width: parent.width
                model: root.redis ? root.redis.databases.map(function (d) {
                    return "db" + d.index + (d.keys ? "  ·  " + root.fmt(d.keys) + (d.keys === 1 ? " key" : " keys") : "")
                }) : []
                currentIndex: root.redis ? root.redis.database : 0
                onActivated: function (i) { root.redis.selectDatabase(i) }
            }
            Item { width: 1; height: 4 }
            FilterField {
                id: patternField
                width: parent.width
                placeholder: "Keys matching… e.g. user:*"
                onTextChanged: patternTimer.restart()
            }
            Timer {
                id: patternTimer
                interval: 350
                onTriggered: {
                    const t = patternField.text.trim()
                    root.pattern = !t.length ? "*" : (t.indexOf("*") >= 0 || t.indexOf("?") >= 0 || t.indexOf("[") >= 0) ? t : "*" + t + "*"
                    root.rescan()
                }
            }
            Text {
                text: root.fmt(root.keys.length) + (root.cursor !== "0" ? "+" : "") + (root.keys.length === 1 ? " key" : " keys")
                      + (root.pattern !== "*" ? " matching " + root.pattern : "")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSmall
                topPadding: 4
            }
        }

        ListView {
            id: keyList
            anchors { left: parent.left; right: parent.right; top: sideHead.bottom; topMargin: 6; bottom: parent.bottom; bottomMargin: 6 }
            clip: true
            model: root.keys
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }
            delegate: Rectangle {
                width: keyList.width
                height: 30
                readonly property bool on: modelData.key === root.selectedKey
                color: on ? Theme.accent : keyMouse.containsMouse ? Theme.hover : "transparent"
                Rectangle {
                    id: typeBadge
                    x: 14; anchors.verticalCenter: parent.verticalCenter
                    width: 36; height: 16; radius: 4
                    color: parent.on ? "#33FFFFFF" : root.typeTint(modelData.type)
                    Text {
                        anchors.centerIn: parent
                        text: root.typeLabel(modelData.type)
                        color: parent.parent.on ? "white" : root.typeColor(modelData.type)
                        font.pixelSize: 9; font.weight: Font.Bold
                    }
                }
                Text {
                    anchors { left: typeBadge.right; leftMargin: 8; right: ttl.left; rightMargin: 6; verticalCenter: parent.verticalCenter }
                    text: modelData.key
                    color: parent.on ? "white" : Theme.text
                    font.pixelSize: Theme.fontBody
                    font.family: Theme.monoFont
                    elide: Text.ElideMiddle
                }
                Text {
                    id: ttl
                    anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    text: root.ttlText(modelData.ttl)
                    color: parent.on ? "#D9FFFFFF" : Theme.textTertiary
                    font.pixelSize: Theme.fontSmall
                }
                MouseArea { id: keyMouse; anchors.fill: parent; hoverEnabled: true; onClicked: { root.mode = "keys"; root.select(modelData.key) } }
            }
            footer: Item {
                width: keyList.width
                height: root.cursor !== "0" ? 44 : (root.scanned && !root.keys.length ? 60 : 0)
                ActionButton {
                    anchors.centerIn: parent
                    visible: root.cursor !== "0"
                    text: "Load more"
                    implicitHeight: 28
                    onClicked: root.more()
                }
                Text {
                    anchors.centerIn: parent
                    visible: root.scanned && !root.keys.length && root.cursor === "0"
                    text: root.pattern === "*" ? "This database is empty." : "No keys match."
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontBody
                }
            }
        }
    }

    // ---- The main area ----
    Item {
        anchors { left: side.right; leftMargin: 28; right: parent.right; rightMargin: 28; top: parent.top; topMargin: 22; bottom: parent.bottom; bottomMargin: 22 }

        SegmentedControl {
            id: modes
            options: [ "Keys", "Console", "Server" ]
            currentIndex: root.mode === "console" ? 1 : root.mode === "server" ? 2 : 0
            onActivated: function (i) { root.mode = i === 1 ? "console" : i === 2 ? "server" : "keys"; if (i === 2) root.redis.refresh() }
        }
        Text {
            anchors { right: parent.right; verticalCenter: modes.verticalCenter }
            text: root.redis && root.redis.changesAllowed ? "Changes allowed: commands that write will run"
                                                          : "Read-only · commands that change data are refused"
            color: root.redis && root.redis.changesAllowed ? Theme.danger : Theme.textTertiary
            font.pixelSize: Theme.fontSmall + 1
        }

        // ---- Keys: the one chosen ----
        Item {
            id: keyPane
            visible: root.mode === "keys"
            anchors { left: parent.left; right: parent.right; top: modes.bottom; topMargin: 16; bottom: parent.bottom }

            Text {
                anchors.centerIn: parent
                visible: !root.selectedKey.length
                text: root.keys.length ? "Choose a key to see its value." : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontHeading
            }

            Column {
                id: keyHead
                visible: root.selectedKey.length > 0
                anchors { left: parent.left; right: parent.right; top: parent.top }
                spacing: 6
                Row {
                    spacing: 10
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 44; height: 20; radius: 5
                        color: root.typeTint(root.keyDetails.type)
                        Text { anchors.centerIn: parent; text: root.typeLabel(root.keyDetails.type || ""); color: root.typeColor(root.keyDetails.type)
                               font.pixelSize: 10; font.weight: Font.Bold }
                    }
                    TextEdit {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.selectedKey
                        readOnly: true; selectByMouse: true
                        color: Theme.text
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        font.family: Theme.monoFont
                        selectionColor: Theme.accentSoft; selectedTextColor: Theme.text
                    }
                }
                Text {
                    text: {
                        const d = root.keyDetails
                        const parts = []
                        if (d.size !== undefined && d.size >= 0)
                            parts.push(d.type === "string" ? root.bytes(d.size) : root.fmt(d.size) + (d.type === "hash" ? " fields" : d.type === "stream" ? " entries" : " members"))
                        parts.push(d.ttl !== undefined && d.ttl >= 0 ? "expires in " + root.ttlText(d.ttl) : "no expiry")
                        if (d.encoding) parts.push(d.encoding)
                        if (d.memory !== undefined && d.memory >= 0) parts.push(root.bytes(d.memory) + " in memory")
                        return parts.join("   ·   ")
                    }
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                }
            }

            // A string (or JSON): its text, selectable.
            Rectangle {
                visible: root.selectedKey.length > 0 && root.valuePage.text !== undefined
                anchors { left: parent.left; right: parent.right; top: keyHead.bottom; topMargin: 14; bottom: parent.bottom }
                radius: Theme.radius
                color: Theme.surface
                border.width: 1; border.color: Theme.separator
                Text {
                    id: stringNote
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                    visible: text.length > 0
                    text: root.valuePage.binary ? "Binary: shown as hex bytes."
                          : root.valuePage.truncated ? "The first 1 MB of " + root.bytes(root.valuePage.length) + "."
                          : root.valuePage.json ? "JSON, formatted." : ""
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSmall + 1
                }
                ScrollView {
                    anchors { left: parent.left; right: parent.right; top: stringNote.visible ? stringNote.bottom : parent.top; bottom: parent.bottom; margins: 4 }
                    TextArea {
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextArea.WrapAnywhere
                        text: root.valuePage.text || ""
                        color: Theme.text
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.fontBody + 1
                        selectionColor: Theme.accentSoft; selectedTextColor: Theme.text
                        background: null
                        padding: 10
                    }
                }
            }

            // A hash, list, set, sorted set or stream: rows, in the usual grid.
            Text {
                id: rowsNote
                visible: root.selectedKey.length > 0 && root.valuePage.rows !== undefined
                anchors { left: parent.left; top: keyHead.bottom; topMargin: 14 }
                text: root.fmt(table.count) + " of " + root.fmt(root.valuePage.total || 0) + " shown"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSmall + 1
            }
            ActionButton {
                visible: rowsNote.visible && root.valuePage.next !== undefined && root.valuePage.next !== null && root.valuePage.next !== "0"
                anchors { right: parent.right; verticalCenter: rowsNote.verticalCenter }
                text: "Load more"
                implicitHeight: 26
                onClicked: root.loadMoreValues()
            }
            ResultGrid {
                visible: rowsNote.visible
                anchors { left: parent.left; right: parent.right; top: rowsNote.bottom; topMargin: 8; bottom: parent.bottom }
                model: table
                columns: table.columnInfo
                valuesForRow: (r) => table.rowAt(r)
                emptyText: "Empty."
            }
        }

        // ---- Console ----
        Item {
            id: console_
            visible: root.mode === "console"
            anchors { left: parent.left; right: parent.right; top: modes.bottom; topMargin: 16; bottom: parent.bottom }
            property var history: []
            property int historyAt: -1
            ListModel { id: transcript }       // { command, text, kind: "ok"|"error"|"refused", ms }
            function runLine(line) {
                const r = root.redis.run(line)
                transcript.append({ command: line, text: r.ok ? r.text : (r.text && r.text.length ? r.text : r.error),
                                    kind: r.refused ? "refused" : r.ok ? "ok" : "error", ms: r.ms !== undefined ? r.ms : -1 })
                history = history.concat([line])
                historyAt = -1
            }

            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top; bottom: input.top; bottomMargin: 10 }
                radius: Theme.radius
                color: Theme.surface
                border.width: 1; border.color: Theme.separator
                clip: true
                ListView {
                    id: out
                    anchors { fill: parent; margins: 10 }
                    model: transcript
                    spacing: 10
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { }
                    onCountChanged: Qt.callLater(positionViewAtEnd)
                    delegate: Column {
                        width: out.width
                        spacing: 3
                        Text {
                            width: parent.width
                            text: "› " + command + (ms >= 0 ? "   " + ms + " ms" : "")
                            color: Theme.textSecondary
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideRight
                        }
                        TextEdit {
                            width: parent.width
                            readOnly: true; selectByMouse: true
                            wrapMode: TextEdit.WrapAnywhere
                            text: model.text
                            color: kind === "refused" ? Theme.warning : kind === "error" ? Theme.danger : Theme.text
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.fontBody + 1
                            selectionColor: Theme.accentSoft; selectedTextColor: Theme.text
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: transcript.count === 0
                    width: parent.width - 60
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: "Commands as in redis-cli: GET key, HGETALL key, INFO memory, SCAN 0 MATCH user:*…\n↑ and ↓ go through what you've run."
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontBody
                }
            }
            Field {
                id: input
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                placeholderText: "Type a command and press Return"
                font.family: Theme.monoFont
                onAccepted: {
                    const line = text.trim()
                    if (!line.length) return
                    console_.runLine(line)
                    text = ""
                }
                Keys.onUpPressed: {
                    const h = parent.history
                    if (!h.length) return
                    parent.historyAt = parent.historyAt < 0 ? h.length - 1 : Math.max(0, parent.historyAt - 1)
                    text = h[parent.historyAt]
                }
                Keys.onDownPressed: {
                    const h = parent.history
                    if (parent.historyAt < 0) return
                    parent.historyAt = parent.historyAt + 1
                    if (parent.historyAt >= h.length) { parent.historyAt = -1; text = "" } else text = h[parent.historyAt]
                }
            }
        }

        // ---- Server: what INFO says ----
        Flickable {
            visible: root.mode === "server"
            anchors { left: parent.left; right: parent.right; top: modes.bottom; topMargin: 16; bottom: parent.bottom }
            contentHeight: serverColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }
            Column {
                id: serverColumn
                width: parent.width
                spacing: 18
                Flow {
                    width: parent.width
                    spacing: 12
                    Repeater {
                        model: {
                            const i = root.redis ? root.redis.info : ({})
                            return [
                                { label: "Version", value: (i.version || "") + (i.mode ? " · " + i.mode : "") },
                                { label: "Role", value: i.role || "" },
                                { label: "Memory", value: (i.memory || "") + (i.maxMemory && i.maxMemory !== "0B" ? " of " + i.maxMemory : "") },
                                { label: "Peak memory", value: i.peakMemory || "" },
                                { label: "Clients", value: root.fmt(i.clients || 0) },
                                { label: "Ops / second", value: root.fmt(i.opsPerSec || 0) },
                                { label: "Hit rate", value: i.hitRate >= 0 ? (i.hitRate * 100).toFixed(1) + " %" : "—" },
                                { label: "Uptime", value: (i.uptimeDays || 0) + (i.uptimeDays === 1 ? " day" : " days") },
                                { label: "Eviction", value: i.evictionPolicy || "" },
                                { label: "Persistence", value: i.persistence || "none" },
                            ]
                        }
                        Rectangle {
                            width: 180; height: 64; radius: Theme.radius
                            color: Theme.surface
                            border.width: 1; border.color: Theme.separator
                            Column {
                                anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                                spacing: 4
                                Text { text: modelData.label.toUpperCase(); color: Theme.textTertiary
                                       font.pixelSize: Theme.fontSmall; font.weight: Font.Bold; font.letterSpacing: 0.4 }
                                Text { text: modelData.value; color: Theme.text; font.pixelSize: Theme.fontHeading; font.weight: Font.DemiBold }
                            }
                        }
                    }
                }
                Repeater {
                    model: root.redis ? root.redis.infoSections : []
                    Column {
                        width: serverColumn.width
                        spacing: 4
                        Text { text: modelData.name; color: Theme.text; font.pixelSize: Theme.fontHeading; font.weight: Font.Bold }
                        Repeater {
                            model: modelData.rows
                            Row {
                                spacing: 12
                                Text { width: 280; text: modelData.key; color: Theme.textSecondary; font.family: Theme.monoFont
                                       font.pixelSize: Theme.fontSmall + 1; elide: Text.ElideRight }
                                Text { text: modelData.value; color: Theme.text; font.family: Theme.monoFont; font.pixelSize: Theme.fontSmall + 1 }
                            }
                        }
                    }
                }
            }
        }
    }

    // Changes need saying yes to, as for a database.
    Popup {
        id: allowConfirm
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 440
        padding: 22
        Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#59000000" }
        background: Rectangle { radius: 14; color: Theme.window; border.width: 1; border.color: Theme.separator }
        contentItem: Column {
            width: allowConfirm.availableWidth
            spacing: 12
            Text { text: "Allow changes to this Redis?"; color: Theme.text; font.pixelSize: 17; font.weight: Font.Bold }
            Text {
                width: allowConfirm.availableWidth
                wrapMode: Text.Wrap
                text: "Commands that write (SET, DEL, FLUSHDB, CONFIG SET…) will run from the console. Redis has no undo, and nothing asks again."
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                ActionButton { text: "Cancel"; onClicked: allowConfirm.close() }
                ActionButton { text: "Allow changes"; primary: true; onClicked: { root.redis.allowChanges(true); allowConfirm.close() } }
            }
        }
    }
}
