import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotStudio.Core 1.0
import QivotUI 1.0

/// "Connect to a server": PostgreSQL, MySQL / MariaDB or SQL Server.
/// Remembers what you typed per database type — never the password.
Popup {
    id: root
    property var database
    property bool busy: false

    readonly property var types: [
        { key: "postgres",  label: "PostgreSQL", port: 5432, user: "postgres",
          sample: { port: 55432, user: "qivot", password: "qivot" } },
        { key: "mysql",     label: "MySQL",      port: 3306, user: "root",
          sample: { port: 53306, user: "root", password: "qivot" } },
        { key: "sqlserver", label: "SQL Server", port: 1433, user: "sa",
          sample: { port: 51433, user: "sa", password: "Qivot_Samples1" } },
    ]
    property int typeIndex: 0
    readonly property var type: types[typeIndex]
    readonly property bool driverAvailable: database ? database.availableTypes[type.key] === true : true

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 460
    padding: 0
    closePolicy: busy ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
    Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#59000000" }

    background: Rectangle {
        radius: 14
        color: Theme.window
        border.width: 1
        border.color: Theme.separator
    }

    // What was last typed for each type, as JSON; passwords are never stored.
    function load() {
        const all = JSON.parse(Prefs.value("connect/fields", "{}"))
        const f = all[type.key] || {}
        host.text = f.host || "localhost"
        port.text = String(f.port || type.port)
        databaseName.text = f.database || ""
        user.text = f.user || type.user
        password.text = ""
    }
    function save() {
        const all = JSON.parse(Prefs.value("connect/fields", "{}"))
        all[type.key] = { host: host.text, port: Number(port.text), database: databaseName.text, user: user.text }
        Prefs.setValue("connect/fields", JSON.stringify(all))
        Prefs.setValue("connect/lastType", String(typeIndex))
    }

    // Open filled in for sample `id` on the sample servers.
    property string pendingSample: ""
    function openForSample(id) { pendingSample = id; open() }

    onAboutToShow: {
        typeIndex = Number(Prefs.value("connect/lastType", "0"))
        load()
        if (pendingSample.length) { useSample(pendingSample); pendingSample = "" }
        busy = false
        Qt.callLater(() => (databaseName.text.length ? password : databaseName).focusField())
    }
    // Switching type keeps a sample's fields, with that server's port and login.
    onTypeIndexChanged: if (visible) {
        const sample = filledSample.length && databaseName.text === filledSample ? filledSample : ""
        load()
        if (sample.length) useSample(sample)
    }

    // The sample servers (tools/sample-servers/docker-compose.yml): their
    // published test logins, filled in for one of the sample databases.
    property string filledSample: ""          // the sample useSample last filled in
    function useSample(id) {
        filledSample = id
        host.text = "localhost"
        port.text = String(type.sample.port)
        databaseName.text = id
        user.text = type.sample.user
        password.text = type.sample.password
    }

    function connect() {
        if (busy) return
        busy = true
        // Let "Connecting…" paint before the (blocking) connection attempt.
        Qt.callLater(() => {
            const ok = database.connectTo({ type: type.key, host: host.text, port: Number(port.text),
                                            database: databaseName.text, user: user.text,
                                            password: password.text })
            busy = false
            if (ok) { save(); root.close() }
        })
    }

    contentItem: Column {
        padding: 24
        spacing: 16

        Text {
            text: "Connect to a server"
            color: Theme.text
            font.pixelSize: 20; font.weight: Font.Bold
        }

        SegmentedControl {
            options: root.types.map(t => t.label)
            currentIndex: root.typeIndex
            onActivated: (i) => root.typeIndex = i
        }

        Column {
            width: 412
            spacing: 6
            Text {
                text: "Or open a sample on the sample servers:"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
            Flow {
                width: parent.width
                spacing: 6
                Repeater {
                    model: root.database ? root.database.samples : []
                    ActionButton {
                        text: modelData.id
                        implicitHeight: 26
                        onClicked: root.useSample(modelData.id)
                    }
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "Start them with: docker compose -f tools/sample-servers/docker-compose.yml up -d"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSmall + 1
            }
        }

        Row {
            spacing: 10
            TextBox { id: host; width: 300; label: "Host"; placeholder: "localhost"; onAccepted: root.connect() }
            TextBox {
                id: port; width: 102; label: "Port"
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 65535 }
                onAccepted: root.connect()
            }
        }
        TextBox { id: databaseName; width: 412; label: "Database"; placeholder: "e.g. pagila"; onAccepted: root.connect() }
        Row {
            spacing: 10
            TextBox { id: user; width: 201; label: "User"; onAccepted: root.connect() }
            TextBox { id: password; width: 201; label: "Password"; password: true; onAccepted: root.connect() }
        }

        Text {
            width: 412
            visible: !root.driverAvailable
            wrapMode: Text.WordWrap
            text: "Qt's " + root.type.label + " driver isn't installed on this computer, so this can't connect yet."
                  + (Qt.platform.os === "osx" && root.type.key === "postgres"
                     ? " On a Mac, installing Postgres.app provides it." : "")
            color: Theme.danger
            font.pixelSize: Theme.fontBody
        }
        Text {
            width: 412
            visible: root.database && root.database.error.length > 0 && !root.busy
            wrapMode: Text.WordWrap
            text: root.database ? root.database.error : ""
            color: Theme.danger
            font.pixelSize: Theme.fontBody
        }
        Text {
            width: 412
            wrapMode: Text.WordWrap
            text: root.type.key === "sqlserver"
                  ? "Studio never writes to the database. SQL Server has no read-only session setting, so to be certain, sign in with a read-only login."
                  : "The connection is made read-only on the server: nothing can be changed through it."
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSmall + 1
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 24
            spacing: 10
            ActionButton { text: "Cancel"; onClicked: root.close(); visible: !root.busy }
            ActionButton {
                text: root.busy ? "Connecting…" : "Connect"
                primary: true
                opacity: root.driverAvailable ? 1 : 0.5
                enabled: root.driverAvailable && !root.busy
                onClicked: root.connect()
            }
        }
    }
}
