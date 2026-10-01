import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Shapes 1.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// Bird's-eye ER diagram of the whole database: every table as a card, every
/// foreign key as a line from its column to the column it references.
/// The mouse wheel zooms around the pointer (or pans: the Scroll menu in the
/// zoom bar chooses, and remembers); a trackpad pans with two fingers and zooms
/// with a pinch; drag the background to pan, drag cards to move them,
/// double-click to open. ⌘+ / ⌘− / ⌘0 zoom in, out and to fit.
Item {
    id: root
    clip: true
    property var database
    /// What to draw: anything with a diagram() (the database, or a Design).
    property var source: database
    /// Keep the cards where they are when the source changes (the designer):
    /// no re-layout, no re-fit; new tables appear to the right.
    property bool keepPositions: false
    /// A table to keep highlighted (the designer's selection).
    property string selectedTable: ""
    signal openTable(string name)
    signal tableClicked(string name)

    // ---- Editing (the designer) ----
    /// Right-click menus and drag-to-connect; off for the read-only diagram.
    property bool editable: false
    /// x, y: where to open a menu, in this item's coordinates.
    signal cardMenu(string table, real x, real y)
    signal columnMenu(string table, string column, real x, real y)
    /// worldX, worldY: the spot on the canvas, for a table created there.
    signal canvasMenu(real x, real y, real worldX, real worldY)
    /// A line was dragged from a column (or, with column "", from the table)
    /// and dropped on another table.
    signal connectRequested(string fromTable, string fromColumn, string toTable)
    /// Where the next new card goes (set before adding a table from the canvas menu).
    property var placeNext: null

    // The connection being dragged: { table, column, x0, y0, x, y } in canvas coordinates.
    property var linking: null
    readonly property string linkTarget: linking ? cardAt(linking.x, linking.y) : ""
    function cardAt(wx, wy) {
        for (let i = cardRects.length - 1; i >= 0; --i) {
            const r = cardRects[i]
            if (wx >= r.x && wx <= r.x + r.width && wy >= r.y && wy <= r.y + r.height)
                return diagram.tables[i] ? diagram.tables[i].name : ""
        }
        return ""
    }
    property string initialFind: ""         // --find: fly to this table once loaded
    property bool hoverEnabled: true        // off for screenshots, so the real pointer can't light a card

    property var diagram: ({ tables: [], links: [], width: 1, height: 1, headerHeight: 40, rowHeight: 24 })
    property var indexOf: ({})            // table name -> card index
    property real zoom: 1
    property string hovered: ""
    property var related: ({})            // names linked to the hovered table
    property var cardRects: []            // current card rectangles, for routing and the minimap
    property string found: ""             // the table "Find" jumped to

    // Card rectangles change as cards are dragged; refresh them at most once a frame.
    function refreshRects() {
        const r = []
        for (let i = 0; i < cards.count; ++i) {
            const c = cards.itemAt(i)
            r.push(c ? Qt.rect(c.x, c.y, c.width, c.height) : Qt.rect(0, 0, 0, 0))
        }
        cardRects = r
    }
    // The canvas: the layout's size, or more where cards were dragged or added beyond it.
    readonly property size extent: {
        let w = diagram.width, h = diagram.height
        for (const r of cardRects) { w = Math.max(w, r.x + r.width + 40); h = Math.max(h, r.y + r.height + 40) }
        return Qt.size(w, h)
    }
    property bool rectsPending: false
    function scheduleRects() {
        if (rectsPending) return
        rectsPending = true
        Qt.callLater(() => { rectsPending = false; refreshRects() })
    }

    // The top-left of a w×h spot near (cx, cy) that overlaps none of `taken`
    // (with a gap), searching outward ring by ring.
    function freeSpot(taken, w, h, cx, cy) {
        const gap = 36, step = 40
        const clear = function (x, y) {
            for (const r of taken)
                if (x < r.x + r.width + gap && x + w + gap > r.x && y < r.y + r.height + gap && y + h + gap > r.y) return false
            return x >= 20 && y >= 80
        }
        for (let ring = 0; ring < 60; ++ring)
            for (let dx = -ring; dx <= ring; ++dx)
                for (let dy = -ring; dy <= ring; ++dy) {
                    if (Math.max(Math.abs(dx), Math.abs(dy)) !== ring) continue
                    const x = cx - w / 2 + dx * step, y = cy - h / 2 + dy * step
                    if (clear(x, y)) return Qt.point(x, y)
                }
        return Qt.point(cx, cy)
    }

    // Bring a table into the middle of the view and mark it.
    function centerOn(name) {
        const c = cards.itemAt(name in indexOf ? indexOf[name] : -1)
        if (!c) return
        found = name
        if (zoom < 0.75) zoom = 0.9
        panX.to = Math.max(0, Math.min(flick.contentWidth - flick.width, (c.x + c.width / 2) * zoom - flick.width / 2))
        panY.to = Math.max(0, Math.min(flick.contentHeight - flick.height, (c.y + c.height / 2) * zoom - flick.height / 2))
        pan.restart()
    }
    // Bring a table into view without fuss: nudge it in if it's partly off
    // screen, centre it if it's right off; zoom out only if it can't fit.
    function reveal(name) {
        const c = name.length ? cards.itemAt(name in indexOf ? indexOf[name] : -1) : null
        if (!c || !visible || flick.width < 50) return
        const m = 24
        // Too big for the view at this zoom: zoom out until it fits.
        const fitZoom = Math.min((flick.width - 2 * m) / c.width, (flick.height - 2 * m) / c.height)
        if (fitZoom < zoom) setZoom(Math.max(0.15, fitZoom), flick.width / 2, flick.height / 2)
        const x = c.x * zoom, y = c.y * zoom, w = c.width * zoom, h = c.height * zoom
        const left = flick.contentX, top = flick.contentY, right = left + flick.width, bottom = top + flick.height
        let tx = left, ty = top
        if (x + w < left || x > right || y + h < top || y > bottom) {          // nowhere in sight: centre it
            tx = x + w / 2 - flick.width / 2
            ty = y + h / 2 - flick.height / 2
        } else {                                                                // partly: just enough
            if (x - m < left) tx = x - m
            else if (x + w + m > right) tx = x + w + m - flick.width
            if (y - m < top) ty = y - m
            else if (y + h + m > bottom) ty = y + h + m - flick.height
        }
        tx = Math.max(0, Math.min(flick.contentWidth - flick.width, tx))
        ty = Math.max(0, Math.min(flick.contentHeight - flick.height, ty))
        if (Math.abs(tx - left) < 1 && Math.abs(ty - top) < 1) return
        panX.to = tx
        panY.to = ty
        pan.restart()
    }
    // Choosing a table (in the designer's panel, its problems, a menu...) shows it.
    onSelectedTableChanged: Qt.callLater(function () { root.reveal(root.selectedTable) })

    function findTable(text) {
        const needle = text.trim().toLowerCase()
        if (!needle.length) { found = ""; return }
        const exact = diagram.tables.find(t => t.name.toLowerCase() === needle)
        const match = exact || diagram.tables.find(t => t.name.toLowerCase().indexOf(needle) >= 0)
        if (match) centerOn(match.name)
    }
    ParallelAnimation {
        id: pan
        NumberAnimation { id: panX; target: flick; property: "contentX"; duration: 380; easing.type: Easing.OutCubic }
        NumberAnimation { id: panY; target: flick; property: "contentY"; duration: 380; easing.type: Easing.OutCubic }
    }
    readonly property int tableCount: diagram.tables.length
    readonly property int linkCount: diagram.links.length

    function reload() {
        const d = source && (source.isOpen === undefined || source.isOpen) ? source.diagram() : null
        // Where the cards are now, to put them back (keepPositions).
        const was = {}
        let right = 0
        const keep = keepPositions && cards.count > 0
        if (keep)
            for (let i = 0; i < cards.count; ++i) {
                const c = cards.itemAt(i)
                if (!c) continue
                was[c.name] = Qt.point(c.x, c.y)
                right = Math.max(right, c.x + c.width)
            }
        links.model = []
        cards.model = []
        if (!d) return
        diagram = d
        const idx = {}
        d.tables.forEach((t, i) => idx[t.name] = i)
        indexOf = idx
        cards.model = d.tables             // cards first: the links bind to them
        if (keep) {
            // Kept cards go back where they were; new ones take the free spot
            // nearest the middle of the view.
            const taken = []
            const fresh = []
            for (let i = 0; i < cards.count; ++i) {
                const c = cards.itemAt(i)
                if (c.name in was) { c.x = was[c.name].x; c.y = was[c.name].y; taken.push(Qt.rect(c.x, c.y, c.width, c.height)) }
                else fresh.push(c)
            }
            let cx = (flick.contentX + flick.width / 2) / zoom, cy = (flick.contentY + flick.height / 2) / zoom
            if (placeNext && fresh.length) {           // a table made from the canvas menu: where it was asked for
                fresh[0].x = Math.max(20, placeNext.x); fresh[0].y = Math.max(20, placeNext.y)
                taken.push(Qt.rect(fresh[0].x, fresh[0].y, fresh[0].width, fresh[0].height))
                fresh.shift()
                placeNext = null
            }
            for (const c of fresh) {
                const p = freeSpot(taken, c.width, c.height, cx, cy)
                c.x = p.x; c.y = p.y
                taken.push(Qt.rect(c.x, c.y, c.width, c.height))
            }
            refreshRects()
            links.model = d.links
            return
        }
        refreshRects()
        links.model = d.links
        Qt.callLater(() => {
            pan.stop()
            fit()
            // Keep (or apply) an active Find across reloads, so a reload can't
            // re-fit the view out from under it.
            if (initialFind.length && finder.text !== initialFind) finder.text = initialFind
            else if (finder.text.length) findTable(finder.text)
        })
    }
    Connections {
        target: root.source
        ignoreUnknownSignals: true
        function onOpenChanged() { root.reload() }
        // A design re-checks itself after every edit (and when its database
        // opens): reload on that, so the cards' ERROR tags are never stale.
        function onDesignChanged() { if (root.source.problems === undefined) root.reload() }
        function onProblemsChanged() { root.reload() }
    }
    Component.onCompleted: { loadScrollPrefs(); reload() }

    // What's lit: the hovered table, else the one Find jumped to.
    readonly property string focusTable: hovered.length ? hovered : found.length ? found : selectedTable
    onFocusTableChanged: {
        const r = {}
        if (focusTable.length) {
            r[focusTable] = true
            for (const l of diagram.links) {
                if (l.from === focusTable) r[l.to] = true
                if (l.to === focusTable) r[l.from] = true
            }
        }
        related = r
    }

    // ---- Zoom ----
    // What scrolling does, for a mouse wheel ("zoom", "pan": up and down,
    // "panx": left and right) and for a trackpad ("pan" or "zoom"); shared by
    // every diagram, remembered, re-read when shown.
    property string wheelAction: "zoom"
    property string trackpadAction: "pan"
    function loadScrollPrefs() {
        wheelAction = Prefs.value("diagram/wheel", "zoom")
        trackpadAction = Prefs.value("diagram/trackpad", "pan")
    }
    function setScrollPref(key, value) {
        Prefs.setValue("diagram/" + key, value)
        loadScrollPrefs()
    }
    onVisibleChanged: if (visible) loadScrollPrefs()

    function panBy(dx, dy) {
        flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX - dx))
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY - dy))
    }
    // Smooth zoom: each step eases toward where the steps so far add up to,
    // keeping the point (px, py) of the view under the pointer.
    property real zoomGoal: 1
    property point zoomAnchor: Qt.point(0, 0)
    property real animatedZoom: 1
    onAnimatedZoomChanged: if (zoomAnim.running) setZoom(animatedZoom, zoomAnchor.x, zoomAnchor.y)
    NumberAnimation { id: zoomAnim; target: root; property: "animatedZoom"; duration: 140; easing.type: Easing.OutCubic }
    function zoomBy(factor, px, py) {
        const goal = Math.max(0.15, Math.min(2.5, (zoomAnim.running ? zoomGoal : zoom) * factor))
        zoomAnim.stop()
        zoomGoal = goal
        zoomAnchor = Qt.point(px, py)
        zoomAnim.from = zoom
        zoomAnim.to = goal
        zoomAnim.start()
    }
    function zoomCentre(factor) { zoomBy(factor, flick.width / 2, flick.height / 2) }

    function setZoom(z, px, py) {               // keep the point (px, py) of the view still
        const nz = Math.max(0.15, Math.min(2.5, z))
        const ox = (flick.contentX + px) / zoom, oy = (flick.contentY + py) / zoom
        zoom = nz
        flick.contentX = Math.max(0, ox * nz - px)
        flick.contentY = Math.max(0, oy * nz - py)
    }
    function fit() {
        if (flick.width < 50 || flick.height < 50) return       // not laid out yet
        const z = Math.min((flick.width - 40) / extent.width, (flick.height - 40) / extent.height, 1)
        zoom = Math.max(0.15, z)
        flick.contentX = Math.max(0, (flick.contentWidth - flick.width) / 2)
        flick.contentY = Math.max(0, (flick.contentHeight - flick.height) / 2)
    }
    function resetLayout() {
        for (let i = 0; i < cards.count; ++i) {
            const c = cards.itemAt(i)
            c.x = diagram.tables[i].x
            c.y = diagram.tables[i].y
        }
        refreshRects()
        fit()
    }

    // Where a link meets a card: the middle of the column's row, on the side facing the other card.
    function anchor(card, columnName, towardX) {
        const t = diagram.tables[card.cardIndex]
        let row = t.columns.findIndex(c => c.name === columnName)
        if (row < 0) row = 0
        const y = card.y + diagram.headerHeight + row * diagram.rowHeight + diagram.rowHeight / 2
        const right = towardX >= card.x + card.width / 2
        return { x: right ? card.x + card.width : card.x, y: y, dir: right ? 1 : -1 }
    }

    // ---- Background: dot grid that pans and zooms with the canvas ----
    Rectangle { anchors.fill: parent; color: Theme.window }
    Canvas {
        id: grid
        anchors.fill: parent
        property real step: 24 * root.zoom
        property real offX: -(flick.contentX % step)
        property real offY: -(flick.contentY % step)
        onStepChanged: requestPaint()
        onOffXChanged: requestPaint()
        onOffYChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections { target: Theme; function onDarkChanged() { grid.requestPaint() } }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (step < 6) return
            ctx.fillStyle = Theme.dark ? "#3A3A3E" : "#D4D4DA"
            for (let x = offX; x < width; x += step)
                for (let y = offY; y < height; y += step)
                    ctx.fillRect(x, y, 1.6, 1.6)
        }
    }

    // A dot on a card's right edge (on hover): drag it to another table to
    // make a reference. From a column: that column refers to the other
    // table's key. From the header: a new column is made for it.
    component Connector: Rectangle {
        id: dot
        property string table
        property string column
        property bool hovered: false
        readonly property bool dragging: root.linking !== null && root.linking.table === table && root.linking.column === column
        visible: root.editable && (hovered || dragging)
        width: 12; height: 12; radius: 6
        x: parent.width - width / 2
        anchors.verticalCenter: parent.verticalCenter
        color: dragging ? Theme.positive : Theme.accent
        border.width: 2
        border.color: Theme.surface
        z: 5
        MouseArea {
            anchors.fill: parent
            anchors.margins: -4
            preventStealing: true
            cursorShape: Qt.CrossCursor
            hoverEnabled: true
            ToolTip.visible: containsMouse && !pressed
            ToolTip.delay: 600
            ToolTip.text: dot.column.length ? "Drag to a table: " + dot.column + " refers to it"
                                            : "Drag to a table to refer to it"
            // `var`, not `const`: Qt 5 reports the names as already declared.
            onPressed: function (mouse) {
                var at = mapToItem(world, mouse.x, mouse.y)
                var from = dot.mapToItem(world, dot.width / 2, dot.height / 2)
                root.linking = { table: dot.table, column: dot.column, x0: from.x, y0: from.y, x: at.x, y: at.y }
            }
            onPositionChanged: function (mouse) {
                if (!root.linking) return
                var at = mapToItem(world, mouse.x, mouse.y)
                var l = root.linking
                root.linking = { table: l.table, column: l.column, x0: l.x0, y0: l.y0, x: at.x, y: at.y }
            }
            onReleased: {
                var l = root.linking, target = root.linkTarget
                root.linking = null
                if (l && target.length && target !== l.table)
                    root.connectRequested(l.table, l.column, target)
            }
            onCanceled: root.linking = null
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: Math.max(width, root.extent.width * root.zoom)
        contentHeight: Math.max(height, root.extent.height * root.zoom)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }
        ScrollBar.horizontal: ScrollBar { }

        Item {
            id: world
            width: root.diagram.width
            height: root.diagram.height
            scale: root.zoom
            transformOrigin: Item.TopLeft

            // ---- Relationship lines (behind the cards) ----
            Repeater {
                id: links
                delegate: Shape {
                    id: link
                    required property var modelData
                    readonly property Item from: cards.itemAt(modelData.from in root.indexOf ? root.indexOf[modelData.from] : -1)
                    readonly property Item to: cards.itemAt(modelData.to in root.indexOf ? root.indexOf[modelData.to] : -1)
                    readonly property bool lit: root.focusTable.length > 0
                                                && (modelData.from === root.focusTable || modelData.to === root.focusTable)
                    readonly property bool dim: root.focusTable.length > 0 && !lit
                    visible: from !== null && to !== null
                    z: lit ? 1 : 0
                    Component.onCompleted: if (Shape.CurveRenderer !== undefined) link.preferredRendererType = Shape.CurveRenderer
                    opacity: dim ? 0.12 : 1
                    Behavior on opacity { NumberAnimation { duration: 140 } }

                    // Many side (the foreign key) -> one side (the referenced key).
                    readonly property var a: visible ? root.anchor(from, modelData.fromColumns[0] || "", to.x + to.width / 2) : ({ x: 0, y: 0, dir: 1 })
                    readonly property var b: visible ? root.anchor(to, modelData.toColumns[0] || "", from.x + from.width / 2) : ({ x: 0, y: 0, dir: 1 })
                    readonly property bool self: modelData.from === modelData.to
                    readonly property real bend: self ? 60 : Math.max(50, Math.abs(b.x - a.x) / 2)
                    readonly property color ink: lit ? Theme.accent : (Theme.dark ? "#6E6E78" : "#A8A8B3")

                    ShapePath {
                        strokeColor: link.ink
                        strokeWidth: link.lit ? 2.2 : 1.4
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        PathSvg {
                            path: {
                                const a = link.a, b = link.b
                                let p
                                if (link.self) {
                                    // A self-reference loops out and back on the same side.
                                    const k = 60
                                    p = `M ${a.x} ${a.y} C ${a.x + a.dir * k} ${a.y} ${a.x + a.dir * k} ${b.y} ${a.x} ${b.y + 0.01}`
                                } else {
                                    // Routed around any card in the way (see ErLayout::route).
                                    p = DiagramGeometry.route(a.x, a.y, a.dir, b.x, b.y, b.dir, root.cardRects,
                                                              root.indexOf[link.modelData.from], root.indexOf[link.modelData.to])
                                }
                                // Crow's foot at the "many" end.
                                const fx = a.x + a.dir * 12
                                p += ` M ${fx} ${a.y} L ${a.x} ${a.y - 6} M ${fx} ${a.y} L ${a.x} ${a.y + 6}`
                                // Bar at the "one" end.
                                const bdir = link.self ? a.dir : b.dir
                                const ox = b.x + bdir * 9
                                p += ` M ${ox} ${b.y - 6} L ${ox} ${b.y + 6}`
                                return p
                            }
                        }
                    }
                }
            }

            // ---- Table cards ----
            Repeater {
                id: cards
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    required property int index
                    readonly property int cardIndex: index
                    readonly property string name: modelData.name
                    readonly property bool lit: root.focusTable === name
                    readonly property bool dim: root.focusTable.length > 0 && !root.related[name]

                    x: modelData.x
                    y: modelData.y
                    width: modelData.width
                    height: modelData.height
                    radius: 10
                    color: Theme.surface
                    readonly property bool broken: modelData.status === "error"
                    border.width: lit || broken ? 2 : 1
                    border.color: broken ? Theme.danger : lit ? Theme.accent : Theme.separator
                    opacity: dim ? 0.35 : 1
                    z: drag.active ? 3 : lit ? 2 : 1
                    onXChanged: root.scheduleRects()
                    onYChanged: root.scheduleRects()
                    Behavior on opacity { NumberAnimation { duration: 140 } }

                    // Soft shadow.
                    Rectangle {
                        anchors.fill: parent; anchors.topMargin: 3; anchors.leftMargin: 1; anchors.rightMargin: -1
                        anchors.bottomMargin: -4
                        radius: parent.radius; z: -1
                        color: Theme.dark ? "#40000000" : "#14000000"
                    }

                    // Header
                    Rectangle {
                        id: header
                        width: parent.width; height: root.diagram.headerHeight
                        radius: card.radius
                        color: card.lit ? Theme.accentSoft : Theme.surfaceRaised
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: card.radius
                                    color: parent.color }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.separator }
                        // Table icon
                        Rectangle {
                            id: tableIcon
                            anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                            width: 13; height: 13; radius: 2; color: "transparent"
                            border.width: 1.4; border.color: Theme.accent
                            Rectangle { y: 4; width: parent.width; height: 1.4; color: Theme.accent }
                        }
                        Text {
                            anchors { left: tableIcon.right; leftMargin: 8; right: rowsText.left; rightMargin: 8
                                      verticalCenter: parent.verticalCenter }
                            text: card.name
                            color: Theme.text
                            font.pixelSize: 14; font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            id: rowsText
                            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                            readonly property string status: card.modelData.status || ""
                            text: status === "error" ? "✗ ERROR" : status === "new" ? "NEW" : status === "changed" ? "EDITED"
                                : card.modelData.rows >= 0 ? Number(card.modelData.rows).toLocaleString(Qt.locale(), "f", 0) : ""
                            font.weight: status.length ? Font.Bold : Font.Normal
                            color: status === "error" ? Theme.danger : status === "new" ? Theme.positive
                                 : status === "changed" ? Theme.warning : Theme.textTertiary
                            font.pixelSize: Theme.fontSmall; font.family: Theme.monoFont
                            HoverHandler { id: tagHover; enabled: rowsText.status === "error" }
                            ToolTip.visible: tagHover.hovered
                            ToolTip.text: (card.modelData.errors || []).join("\n")
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            enabled: root.editable
                            onTapped: {
                                const p = header.mapToItem(root, point.position.x, point.position.y)
                                root.cardMenu(card.name, p.x, p.y)
                            }
                        }
                        Connector { table: card.name; column: ""; hovered: headerHover.hovered }
                        HoverHandler { id: headerHover; enabled: root.editable }
                    }

                    // Columns
                    Column {
                        y: root.diagram.headerHeight
                        width: parent.width
                        Repeater {
                            model: card.modelData.columns
                            Item {
                                id: row
                                required property var modelData
                                width: card.width
                                height: root.diagram.rowHeight
                                HoverHandler { id: rowHover; enabled: root.editable }
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    enabled: root.editable
                                    onTapped: {
                                        const p = row.mapToItem(root, point.position.x, point.position.y)
                                        root.columnMenu(card.name, row.modelData.name, p.x, p.y)
                                    }
                                }
                                Connector { table: card.name; column: row.modelData.name; hovered: rowHover.hovered }
                                Text {
                                    id: keyMark
                                    anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                                    width: 20
                                    text: modelData.primaryKey ? "PK" : modelData.foreignKey ? "FK" : ""
                                    color: modelData.primaryKey ? Theme.key : Theme.link
                                    font.pixelSize: 9; font.weight: Font.Bold
                                }
                                Text {
                                    anchors { left: keyMark.right; leftMargin: 4; right: typeText.left; rightMargin: 8
                                              verticalCenter: parent.verticalCenter }
                                    text: modelData.name
                                    color: Theme.text
                                    font.pixelSize: 12
                                    font.weight: modelData.primaryKey ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                }
                                Text {
                                    id: typeText
                                    anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                                    width: Math.min(implicitWidth, card.width * 0.45)
                                    horizontalAlignment: Text.AlignRight
                                    text: modelData.type.toLowerCase()
                                    color: Theme.textTertiary
                                    font.pixelSize: 11; font.family: Theme.monoFont
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    HoverHandler {
                        enabled: root.hoverEnabled
                        onHoveredChanged: {
                            if (hovered) root.hovered = card.name
                            else if (root.hovered === card.name) root.hovered = ""
                        }
                    }
                    DragHandler { id: drag; cursorShape: Qt.ClosedHandCursor; enabled: !root.linking }
                    TapHandler {
                        onTapped: root.tableClicked(card.name)
                        onDoubleTapped: root.openTable(card.name)
                    }
                    // While a connection is dragged over it: the drop target.
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        radius: card.radius + 4
                        color: "transparent"
                        border.width: 2
                        border.color: Theme.positive
                        visible: root.linking !== null && root.linkTarget === card.name && root.linking.table !== card.name
                    }
                }
            }

            // ---- A connection being dragged ----
            Shape {
                visible: root.linking !== null
                z: 10
                ShapePath {
                    strokeColor: root.linkTarget.length && root.linking && root.linkTarget !== root.linking.table
                                 ? Theme.positive : Theme.accent
                    strokeWidth: 2
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [4, 3]
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathSvg {
                        path: root.linking
                              ? "M %1 %2 C %3 %2 %4 %5 %6 %5".arg(root.linking.x0).arg(root.linking.y0)
                                    .arg(root.linking.x0 + 60).arg(root.linking.x - 60).arg(root.linking.y).arg(root.linking.x)
                              : "M 0 0"
                    }
                }
            }
        }

        // Right-click on empty canvas.
        TapHandler {
            acceptedButtons: Qt.RightButton
            enabled: root.editable
            onTapped: {
                const w = world.mapFromItem(flick, point.position.x, point.position.y)
                if (root.cardAt(w.x, w.y).length) return          // a card's own menu handles that
                const p = flick.mapToItem(root, point.position.x, point.position.y)
                root.canvasMenu(p.x, p.y, w.x, w.y)
            }
        }

        // Scrolling, as the Scroll menu says: zoom around the pointer, or pan.
        // ⌘/Ctrl always zooms; Shift pans sideways; Alt does the other thing.
        WheelHandler {
            target: null
            onWheel: function (event) {
                event.accepted = true            // ours: the Flickable mustn't scroll it as well
                // A trackpad (or Magic Mouse) scrolls in phases (began, changed,
                // ended, momentum); a wheel has none. Not the pixel delta: macOS
                // gives a wheel notch one too.
                const touchpad = typeof PointerDevice !== "undefined" && event.device
                                 && event.device.type === PointerDevice.TouchPad
                const precise = touchpad || (event.phase !== undefined && event.phase !== Qt.NoScrollPhase)
                let action = precise ? root.trackpadAction : root.wheelAction
                if (action === "panx") action = "pan"
                if (event.modifiers & Qt.AltModifier) action = action === "zoom" ? "pan" : "zoom"
                if (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) action = "zoom"
                else if (event.modifiers & Qt.ShiftModifier) action = "pan"

                if (action === "pan") {
                    if (precise) {                       // a trackpad pans both ways at once
                        const pixels = event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0
                        root.panBy(pixels ? event.pixelDelta.x : event.angleDelta.x / 2,
                                   pixels ? event.pixelDelta.y : event.angleDelta.y / 2)
                        return
                    }
                    // A wheel turns one way: up/down or left/right (the Scroll menu),
                    // Shift for the other; where that way has nowhere to go (the
                    // whole height fits, say), the one that does.
                    const amount = (event.angleDelta.y || event.angleDelta.x) / 2
                    let sideways = root.wheelAction === "panx"
                    if (event.modifiers & Qt.ShiftModifier) sideways = !sideways
                    const roomX = flick.contentWidth > flick.width + 1, roomY = flick.contentHeight > flick.height + 1
                    if (sideways && !roomX && roomY) sideways = false
                    else if (!sideways && !roomY && roomX) sideways = true
                    root.panBy(sideways ? amount : 0, sideways ? 0 : amount)
                    return
                }
                // A notch is 120; a smooth-scrolling mouse sends a fraction of one.
                const steps = precise && event.pixelDelta.y !== 0 ? event.pixelDelta.y / 60
                                                                  : (event.angleDelta.y || event.angleDelta.x) / 120
                if (steps !== 0)
                    root.zoomBy(Math.pow(1.15, steps), point.position.x - flick.contentX, point.position.y - flick.contentY)
            }
        }
        PinchHandler {
            target: null
            property real startZoom: 1
            onActiveChanged: if (active) startZoom = root.zoom
            onActiveScaleChanged: root.setZoom(startZoom * activeScale, centroid.position.x - flick.contentX,
                                               centroid.position.y - flick.contentY)
        }
    }

    // ---- Summary, top right (the layout fills from the top left) ----
    Rectangle {
        anchors { right: parent.right; top: parent.top; margins: 16 }
        width: summary.implicitWidth + 24; height: 32; radius: 8
        color: Theme.surface; border.width: 1; border.color: Theme.separator
        Text {
            id: summary
            anchors.centerIn: parent
            text: root.tableCount + (root.tableCount === 1 ? " table" : " tables") + "  ·  "
                  + root.linkCount + (root.linkCount === 1 ? " relationship" : " relationships")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
        }
    }

    // ---- Zoom controls, bottom left ----
    Rectangle {
        anchors { left: parent.left; bottom: parent.bottom; margins: 16 }
        width: controls.implicitWidth + 8; height: 36; radius: 9
        color: Theme.surface; border.width: 1; border.color: Theme.separator
        Row {
            id: controls
            anchors.centerIn: parent
            spacing: 2
            component Tool: Rectangle {
                property string label
                property string tip
                signal activated()
                width: Math.max(30, toolText.implicitWidth + 16); height: 28; radius: 6
                color: toolMouse.pressed ? Theme.pressed : toolMouse.containsMouse ? Theme.hover : "transparent"
                Text { id: toolText; anchors.centerIn: parent; text: parent.label; color: Theme.text
                       font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold }
                MouseArea { id: toolMouse; anchors.fill: parent; hoverEnabled: true; onClicked: parent.activated() }
                ToolTip.visible: toolMouse.containsMouse && tip.length > 0
                ToolTip.text: tip
                ToolTip.delay: 500
            }
            Tool { label: "−"; tip: "Zoom out (⌘−)"; onActivated: root.zoomCentre(1 / 1.25) }
            Text {
                width: 48; height: 28
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                text: Math.round(root.zoom * 100) + "%"
                color: Theme.textSecondary; font.pixelSize: Theme.fontSmall + 1; font.family: Theme.monoFont
            }
            Tool { label: "+"; tip: "Zoom in (⌘+)"; onActivated: root.zoomCentre(1.25) }
            Rectangle { width: 1; height: 18; color: Theme.separator; anchors.verticalCenter: parent.verticalCenter }
            Tool { label: "Fit"; tip: "Fit the whole schema (⌘0)"; onActivated: root.fit() }
            Tool { label: "Tidy"; tip: "Re-run the automatic layout"; onActivated: root.resetLayout() }
            Rectangle { width: 1; height: 18; color: Theme.separator; anchors.verticalCenter: parent.verticalCenter }
            Tool {
                id: scrollTool
                label: "Scroll: " + (root.wheelAction === "zoom" ? "Zoom" : root.wheelAction === "panx" ? "Pan ↔" : "Pan ↕") + " ▾"
                tip: "What the mouse wheel and trackpad do"
                onActivated: scrollMenu.popup(scrollTool, 0, -scrollMenu.implicitHeight - 6)
            }
        }
    }

    // What scrolling does, chosen from the zoom bar.
    ContextMenu {
        id: scrollMenu
        implicitWidth: 270
        ContextMenuItem { text: "MOUSE WHEEL"; enabled: false; font.pixelSize: Theme.fontSmall; font.weight: Font.Bold }
        ContextMenuItem { text: "Zooms (Shift pans)"; checkable: true; checked: root.wheelAction === "zoom"
                          onTriggered: root.setScrollPref("wheel", "zoom") }
        ContextMenuItem { text: "Pans up and down (⌘ zooms)"; checkable: true; checked: root.wheelAction === "pan"
                          onTriggered: root.setScrollPref("wheel", "pan") }
        ContextMenuItem { text: "Pans left and right (⌘ zooms)"; checkable: true; checked: root.wheelAction === "panx"
                          onTriggered: root.setScrollPref("wheel", "panx") }
        ContextMenuSeparator { }
        ContextMenuItem { text: "TRACKPAD"; enabled: false; font.pixelSize: Theme.fontSmall; font.weight: Font.Bold }
        ContextMenuItem { text: "Two fingers pan, pinch zooms"; checkable: true; checked: root.trackpadAction === "pan"
                          onTriggered: root.setScrollPref("trackpad", "pan") }
        ContextMenuItem { text: "Two fingers zoom"; checkable: true; checked: root.trackpadAction === "zoom"
                          onTriggered: root.setScrollPref("trackpad", "zoom") }
        ContextMenuSeparator { }
        ContextMenuItem { text: "Shift: the other way · Alt: zoom ⇄ pan"; enabled: false
                          font.pixelSize: Theme.fontSmall }
    }

    // Zoom from the keyboard, while this diagram is showing.
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; enabled: root.visible; onActivated: root.zoomCentre(1.25) }
    Shortcut { sequences: [StandardKey.ZoomOut]; enabled: root.visible; onActivated: root.zoomCentre(1 / 1.25) }
    Shortcut { sequence: "Ctrl+0"; enabled: root.visible; onActivated: root.fit() }

    // ---- Find a table, top left ----
    FilterField {
        id: finder
        anchors { left: parent.left; top: parent.top; margins: 16 }
        width: 220
        placeholder: "Find table"
        onTextChanged: root.findTable(text)
    }

    // ---- Minimap, bottom right: the whole schema, and where you are in it ----
    Rectangle {
        id: minimap
        readonly property real k: Math.min(200 / root.diagram.width, 130 / root.diagram.height)
        visible: root.tableCount > 0
        anchors { right: parent.right; bottom: parent.bottom; margins: 16 }
        width: root.diagram.width * k + 12
        height: root.diagram.height * k + 12
        radius: 8
        color: Theme.surface
        border.width: 1
        border.color: Theme.separator
        clip: true

        Item {
            x: 6; y: 6
            width: root.diagram.width * minimap.k
            height: root.diagram.height * minimap.k
            Repeater {
                model: root.cardRects
                Rectangle {
                    required property var modelData
                    required property int index
                    readonly property string name: root.diagram.tables[index] ? root.diagram.tables[index].name : ""
                    x: modelData.x * minimap.k; y: modelData.y * minimap.k
                    width: Math.max(2, modelData.width * minimap.k); height: Math.max(2, modelData.height * minimap.k)
                    radius: 1.5
                    color: root.related[name] ? Theme.accent : Theme.textTertiary
                    opacity: 0.6
                }
            }
            // The part of the schema on screen.
            Rectangle {
                id: viewport
                x: flick.contentX / root.zoom * minimap.k
                y: flick.contentY / root.zoom * minimap.k
                width: Math.min(parent.width, flick.width / root.zoom * minimap.k)
                height: Math.min(parent.height, flick.height / root.zoom * minimap.k)
                color: Theme.accentSoft
                border.width: 1.5
                border.color: Theme.accent
                radius: 2
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                function panTo(mx, my) {
                    flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width,
                                                          mx / minimap.k * root.zoom - flick.width / 2))
                    flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height,
                                                          my / minimap.k * root.zoom - flick.height / 2))
                }
                onPressed: (mouse) => panTo(mouse.x, mouse.y)
                onPositionChanged: (mouse) => panTo(mouse.x, mouse.y)
            }
        }
    }

    Text {
        anchors { right: minimap.left; bottom: parent.bottom; margins: 18 }
        text: "Drag tables · double-click to open · " + (root.wheelAction === "zoom" ? "scroll to zoom, drag the background to pan"
                                                                                      : "scroll to pan (Shift: the other way), ⌘-scroll to zoom")
        visible: implicitWidth + minimap.width + 360 < root.width     // only where it clears the zoom bar
        color: Theme.textTertiary
        font.pixelSize: Theme.fontSmall
    }

    Text {
        anchors.centerIn: parent
        visible: root.tableCount === 0
        text: "No tables to draw."
        color: Theme.textTertiary
        font.pixelSize: Theme.fontHeading
    }
}
