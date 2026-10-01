import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotUI 1.0
import QivotStudio.Core 1.0

/// The Profile tab: a card per column with how much is empty, how much
/// repeats, the range, and its shape. Columns fill in as they're profiled
/// (in the background, so big tables don't hold up the window).
Item {
    id: root
    property var database
    property string tableName

    Profile { id: profile; session: root.database; table: root.tableName }

    function fmt(n) { return Number(n).toLocaleString(Qt.locale(), "f", 0) }
    function num(v) {
        const a = Math.abs(v)
        return a >= 1e6 || (a > 0 && a < 0.01) ? Number(v).toExponential(2)
             : Number(v).toLocaleString(Qt.locale(), "f", a >= 100 || Number.isInteger(v) ? 0 : 2)
    }
    function percent(part, whole) { return whole > 0 ? (100 * part / whole).toFixed(part === 0 || part === whole ? 0 : 1) + "%" : "" }

    // ---- Summary ----
    Item {
        id: summary
        width: parent.width; height: 34
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: profile.running ? "Profiling column " + Math.min(profile.done + 1, profile.columns.length)
                                    + " of " + profile.columns.length + "…"
                  : profile.columns.length ? "Every column: how full, how varied, and its shape." : ""
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
        }
        ActionButton {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: "Refresh"
            implicitHeight: 30
            enabled: !profile.running
            opacity: enabled ? 1 : 0.5
            onClicked: profile.refresh()
        }
    }

    Flickable {
        anchors { left: parent.left; right: parent.right; top: summary.bottom; topMargin: 10; bottom: parent.bottom }
        contentHeight: grid.height + 20
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        Flow {
            id: grid
            width: parent.width
            spacing: 14
            readonly property int perRow: Math.max(1, Math.floor((width + spacing) / (330 + spacing)))
            readonly property real cardWidth: (width - spacing * (perRow - 1)) / perRow

            Repeater {
                model: profile.columns
                Rectangle {
                    id: card
                    readonly property var c: modelData
                    readonly property bool done: c.done === true
                    readonly property bool failed: c.error !== undefined && c.error.length > 0
                    readonly property real filled: done && c.rows > 0 ? (c.rows - c.nulls) / c.rows : 0
                    readonly property bool unique: done && c.distinct > 1 && c.distinct === c.rows - c.nulls
                    width: grid.cardWidth
                    height: body.height + 28
                    radius: Theme.radius
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.separator
                    opacity: done ? 1 : 0.55
                    Behavior on opacity { NumberAnimation { duration: 180 } }

                    Column {
                        id: body
                        x: 14; y: 14
                        width: parent.width - 28
                        spacing: 10

                        // Name, type, and what stands out.
                        Row {
                            width: parent.width
                            spacing: 8
                            Text {
                                text: card.c.name
                                color: Theme.text
                                font.family: Theme.monoFont
                                font.pixelSize: Theme.fontBody + 1
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                width: Math.min(implicitWidth, parent.width - typeText.width - badges.width - 16)
                            }
                            Text {
                                id: typeText
                                topPadding: 2
                                text: card.c.type.toLowerCase()
                                color: Theme.textTertiary
                                font.family: Theme.monoFont
                                font.pixelSize: Theme.fontSmall
                            }
                        }
                        Row {
                            id: badges
                            spacing: 6
                            visible: card.done && !card.failed
                            Badge {
                                visible: card.done && card.c.rows > 0 && card.c.nulls === card.c.rows
                                text: "ALL EMPTY"; tone: "warning"
                            }
                            Badge { visible: card.unique; text: "UNIQUE"; tone: "key" }
                            Badge {
                                visible: card.done && card.c.distinct === 1
                                text: "ONE VALUE"; tone: "neutral"
                            }
                        }

                        Text {
                            visible: card.failed
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: card.failed ? card.c.error : ""
                            color: Theme.danger
                            font.pixelSize: Theme.fontSmall + 1
                        }
                        Text {
                            visible: !card.done
                            text: "Profiling…"
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontBody
                        }

                        // Filled vs empty.
                        Column {
                            visible: card.done && !card.failed
                            width: parent.width
                            spacing: 5
                            Rectangle {
                                width: parent.width; height: 6; radius: 3
                                color: Theme.surfaceRaised
                                Rectangle {
                                    width: parent.width * card.filled; height: parent.height; radius: 3
                                    color: card.filled < 0.5 ? Theme.warning : Theme.positive
                                }
                            }
                            Text {
                                width: parent.width
                                elide: Text.ElideRight
                                text: card.done ? root.percent(card.c.rows - card.c.nulls, card.c.rows) + " filled  ·  "
                                                  + root.fmt(card.c.nulls) + " empty"
                                                  + (card.c.distinct !== undefined ? "  ·  " + root.fmt(card.c.distinct) + " distinct" : "") : ""
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall + 1
                            }
                        }

                        // The range.
                        Text {
                            visible: card.done && !card.failed && card.c.min !== undefined && card.c.min.length > 0
                                     && card.c.kind !== "text"
                            width: parent.width
                            elide: Text.ElideRight
                            text: !visible ? ""
                                  : card.c.kind === "number"
                                    ? "min " + root.num(Number(card.c.min)) + (card.c.avg !== undefined ? "  ·  avg " + root.num(card.c.avg) : "")
                                      + "  ·  max " + root.num(Number(card.c.max))
                                    : card.c.min + "  →  " + card.c.max
                            color: Theme.text
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.fontSmall + 1
                        }

                        // Shape: a histogram...
                        Item {
                            id: histo
                            readonly property var bars: card.done && card.c.histogram ? card.c.histogram : []
                            readonly property real most: bars.reduce(function (m, b) { return Math.max(m, b.count) }, 1)
                            visible: bars.length > 0
                            width: parent.width
                            height: visible ? 70 : 0
                            Row {
                                id: histoBars
                                anchors { left: parent.left; right: parent.right; bottom: axis.top; bottomMargin: 4 }
                                height: 50
                                spacing: 2
                                Repeater {
                                    model: histo.bars
                                    Rectangle {
                                        width: (histoBars.width - histoBars.spacing * (histo.bars.length - 1)) / histo.bars.length
                                        height: Math.max(modelData.count > 0 ? 2 : 0, histoBars.height * modelData.count / histo.most)
                                        anchors.bottom: parent.bottom
                                        radius: 2
                                        color: Theme.accent
                                        opacity: 0.85
                                    }
                                }
                            }
                            Item {
                                id: axis
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                height: 14
                                Text { text: histo.bars.length ? root.num(histo.bars[0].from) : ""; color: Theme.textTertiary
                                       font.pixelSize: Theme.fontSmall; font.family: Theme.monoFont }
                                Text { anchors.right: parent.right
                                       text: histo.bars.length ? root.num(Number(card.c.max)) : ""
                                       color: Theme.textTertiary; font.pixelSize: Theme.fontSmall; font.family: Theme.monoFont }
                            }
                        }

                        Text {
                            visible: card.unique && card.c.kind !== "number" && card.c.kind !== "date"
                            text: "Every value is different."
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall + 1
                        }
                        // ...or the commonest values.
                        Column {
                            // (Not for a column where every value differs: each would show a count of 1.)
                            readonly property var values: card.done && card.c.top && !card.unique ? card.c.top : []
                            readonly property real most: values.length ? values[0].count : 1
                            visible: values.length > 0
                            width: parent.width
                            spacing: 4
                            Repeater {
                                model: parent.values
                                Item {
                                    width: parent.width
                                    height: 20
                                    Rectangle {
                                        width: parent.width * modelData.count / parent.parent.most
                                        height: parent.height
                                        radius: 3
                                        color: Theme.accentSoft
                                    }
                                    Text {
                                        anchors { left: parent.left; leftMargin: 6; right: countText.left; rightMargin: 8
                                                  verticalCenter: parent.verticalCenter }
                                        text: modelData.value
                                        elide: Text.ElideRight
                                        color: Theme.text
                                        font.pixelSize: Theme.fontSmall + 1
                                    }
                                    Text {
                                        id: countText
                                        anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                                        text: root.fmt(modelData.count)
                                        color: Theme.textSecondary
                                        font.family: Theme.monoFont
                                        font.pixelSize: Theme.fontSmall
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
