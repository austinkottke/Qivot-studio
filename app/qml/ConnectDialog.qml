import QtQuick 2.15
import QtQuick.Controls 2.15
import QivotStudio.Core 1.0
import QivotUI 1.0

/// "Connect to a server": PostgreSQL, MySQL / MariaDB or SQL Server, directly
/// or through SSH, with the TLS settings each one has. Remembers what you
/// typed per database type, and offers the servers connected to lately and
/// the ones saved — never the password.
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
    readonly property var servers: ConnectionHistory.entries.filter(function (e) { return e.kind === "server" }).slice(0, 8)

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 480
    height: Math.min(form.implicitHeight, (Overlay.overlay ? Overlay.overlay.height : 800) - 40)
    padding: 0
    closePolicy: busy ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
    Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#59000000" }

    background: Rectangle {
        radius: 14
        color: Theme.window
        border.width: 1
        border.color: Theme.separator
    }

    // ---- TLS and SSH: each type's own choices ----
    readonly property var pgModes: [ { key: "", label: "Default" }, { key: "disable", label: "Off" },
                                     { key: "require", label: "Require" }, { key: "verify-ca", label: "Verify CA" },
                                     { key: "verify-full", label: "Verify full" } ]
    readonly property var msModes: [ { key: "off", label: "Off" }, { key: "on", label: "On" }, { key: "strict", label: "Strict" } ]
    property string sslMode: ""               // PostgreSQL: one of pgModes; MySQL: "" or "on"; SQL Server: msModes
    property bool sslTrust: true              // SQL Server: trust the server's certificate
    property bool useSsh: false
    property bool showAdvanced: false

    function sslSettings() {
        const s = {}
        if (type.key === "sqlserver") {
            s.mode = sslMode.length ? sslMode : "on"
            s.trust = sslTrust
            return s.mode === "on" && s.trust ? null : s       // the defaults: nothing to say
        }
        if (type.key === "postgres" && sslMode.length) s.mode = sslMode
        if (type.key === "mysql") { if (sslMode !== "on") return null; s.mode = "on" }
        if (caFile.text.trim().length) s.ca = caFile.text.trim()
        if (certFile.text.trim().length) s.cert = certFile.text.trim()
        if (keyFile.text.trim().length) s.key = keyFile.text.trim()
        return Object.keys(s).length ? s : null
    }
    function sshSettings() {
        if (!useSsh || !sshHost.text.trim().length) return null
        return { host: sshHost.text.trim(), port: Number(sshPort.text) || 22, user: sshUser.text.trim(), key: sshKey.text.trim() }
    }
    // Everything typed, as DatabaseSession.connectTo takes it.
    function collect(withPassword) {
        const s = { type: type.key, host: host.text, port: Number(port.text), database: databaseName.text, user: user.text }
        if (withPassword) s.password = password.text
        const ssl = sslSettings(), ssh = sshSettings()
        if (ssl) s.ssl = ssl
        if (ssh) s.ssh = ssh
        return s
    }
    // Fill the form from settings (saved, recent, or remembered for the type).
    function fill(f) {
        host.text = f.host || "localhost"
        port.text = String(f.port || type.port)
        databaseName.text = f.database || ""
        user.text = f.user || type.user
        password.text = ""
        const ssl = f.ssl || {}
        sslMode = type.key === "mysql" ? (ssl.mode === "on" || ssl.ca ? "on" : "") : (ssl.mode || "")
        sslTrust = ssl.trust === undefined ? true : ssl.trust
        caFile.text = ssl.ca || ""; certFile.text = ssl.cert || ""; keyFile.text = ssl.key || ""
        const ssh = f.ssh || {}
        useSsh = !!ssh.host
        sshHost.text = ssh.host || ""; sshPort.text = String(ssh.port || 22); sshUser.text = ssh.user || ""; sshKey.text = ssh.key || ""
        showAdvanced = useSsh || Object.keys(ssl).length > 0
    }

    // What was last typed for each type, as JSON; passwords are never stored.
    function load() {
        const all = JSON.parse(Prefs.value("connect/fields", "{}"))
        fill(all[type.key] || {})
    }
    function save() {
        const all = JSON.parse(Prefs.value("connect/fields", "{}"))
        all[type.key] = collect(false)
        Prefs.setValue("connect/fields", JSON.stringify(all))
        Prefs.setValue("connect/lastType", String(typeIndex))
    }
    // A saved or recent connection's settings, into the form.
    function use(settings) {
        const i = types.findIndex(function (t) { return t.key === settings.type })
        if (i < 0) return
        applying = true
        typeIndex = i
        applying = false
        fill(settings)
        filledSample = ""
        Qt.callLater(function () { password.focusField() })
    }
    property bool applying: false

    // Open filled in for sample `id` on the sample servers, or with saved settings.
    property string pendingSample: ""
    property var pendingSettings: null
    function openForSample(id) { pendingSample = id; open() }
    function openWith(settings) { pendingSettings = settings; open() }

    onAboutToShow: {
        typeIndex = Number(Prefs.value("connect/lastType", "0"))
        load()
        if (pendingSample.length) { useSample(pendingSample); pendingSample = "" }
        if (pendingSettings) { use(pendingSettings); pendingSettings = null }
        saveIt.on = false
        saveName.text = ""
        busy = false
        Qt.callLater(() => (databaseName.text.length ? password : databaseName).focusField())
    }
    // Switching type keeps a sample's fields, with that server's port and login.
    onTypeIndexChanged: if (visible && !applying) {
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
        useSsh = false
    }

    function connect() {
        if (busy) return
        busy = true
        // Let "Connecting…" paint before the (blocking) connection attempt.
        Qt.callLater(() => {
            const ok = database.connectTo(collect(true))
            busy = false
            if (!ok) return
            save()
            const id = ConnectionHistory.remember(database.connectionSettings)
            if (saveIt.on) ConnectionHistory.setSaved(id, true, saveName.text)
            root.close()
        })
    }

    contentItem: Flickable {
        clip: true
        contentWidth: width
        contentHeight: form.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

    Column {
        id: form
        width: parent.width
        padding: 24
        spacing: 16
        readonly property int inner: root.width - 48

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

        // Servers connected to before: one click fills everything in but the password.
        Column {
            width: form.inner
            spacing: 6
            visible: root.servers.length > 0
            Text {
                text: "Saved and recent:"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
            Flow {
                width: parent.width
                spacing: 6
                Repeater {
                    model: root.servers
                    ActionButton {
                        text: (modelData.saved ? "★ " : "") + modelData.title
                        implicitHeight: 26
                        onClicked: root.use(modelData.settings)
                        HoverHandler { id: serverHover }
                        ToolTip.visible: serverHover.hovered; ToolTip.delay: 500
                        ToolTip.text: modelData.detail
                    }
                }
            }
        }

        Column {
            width: form.inner
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
            TextBox { id: host; width: form.inner - 112; label: root.useSsh ? "Host (as the SSH server sees it)" : "Host"
                      placeholder: "localhost"; onAccepted: root.connect() }
            TextBox {
                id: port; width: 102; label: "Port"
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 65535 }
                onAccepted: root.connect()
            }
        }
        TextBox { id: databaseName; width: form.inner; label: "Database"; placeholder: "e.g. pagila"; onAccepted: root.connect() }
        Row {
            spacing: 10
            TextBox { id: user; width: (form.inner - 10) / 2; label: "User"; onAccepted: root.connect() }
            TextBox { id: password; width: (form.inner - 10) / 2; label: "Password"; password: true; onAccepted: root.connect() }
        }

        // ---- SSL and SSH ----
        Text {
            text: (root.showAdvanced ? "▾  " : "▸  ") + "SSL and SSH"
                  + (!root.showAdvanced && (root.useSsh || root.sslSettings()) ? "   ·   " + [root.useSsh ? "through SSH" : "", root.sslSettings() ? "TLS set" : ""].filter(s => s.length).join(", ") : "")
            color: Theme.accent
            font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.showAdvanced = !root.showAdvanced }
        }
        Column {
            width: form.inner
            spacing: 12
            visible: root.showAdvanced

            // TLS, as each database does it.
            Text { text: "Encryption (TLS)"; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall + 1; font.weight: Font.DemiBold }
            SegmentedControl {
                visible: root.type.key === "postgres"
                options: root.pgModes.map(m => m.label)
                currentIndex: Math.max(0, root.pgModes.findIndex(m => m.key === root.sslMode))
                onActivated: (i) => root.sslMode = root.pgModes[i].key
            }
            Toggle {
                visible: root.type.key === "mysql"
                label: root.sslMode === "on" ? "TLS on" : "TLS off"
                on: root.sslMode === "on"
                onToggled: root.sslMode = root.sslMode === "on" ? "" : "on"
            }
            Row {
                visible: root.type.key === "sqlserver"
                spacing: 12
                SegmentedControl {
                    options: root.msModes.map(m => m.label)
                    currentIndex: Math.max(0, root.msModes.findIndex(m => m.key === (root.sslMode.length ? root.sslMode : "on")))
                    onActivated: (i) => root.sslMode = root.msModes[i].key
                }
                Toggle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.sslMode !== "off" && root.sslMode !== "strict"
                    label: "Trust the server's certificate"
                    on: root.sslTrust
                    onToggled: root.sslTrust = !root.sslTrust
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSmall + 1
                text: root.type.key === "postgres"
                      ? "Default tries TLS and falls back to none. Verify CA checks the certificate against the CA file; Verify full checks the host name too."
                      : root.type.key === "mysql"
                        ? "MySQL's drivers turn TLS on when there's a certificate to check the server with: give the server's CA file."
                        : "On encrypts (and, unless trusted as it is, checks the certificate); Strict uses TDS 8 and always checks."
            }
            Column {
                width: parent.width
                spacing: 10
                visible: root.type.key !== "sqlserver" && (root.type.key === "postgres" || root.sslMode === "on")
                TextBox { id: caFile; width: parent.width; label: "CA certificate file"; placeholder: "/path/to/ca.pem" }
                Row {
                    spacing: 10
                    TextBox { id: certFile; width: (form.inner - 10) / 2; label: "Client certificate (optional)"; placeholder: "client-cert.pem" }
                    TextBox { id: keyFile; width: (form.inner - 10) / 2; label: "Client key (optional)"; placeholder: "client-key.pem" }
                }
            }

            // SSH: reach the database through a server that can.
            Item { width: 1; height: 2 }
            Toggle {
                label: root.useSsh ? "Through SSH" : "Connect through SSH"
                on: root.useSsh
                onToggled: root.useSsh = !root.useSsh
            }
            Column {
                width: parent.width
                spacing: 10
                visible: root.useSsh
                Row {
                    spacing: 10
                    TextBox { id: sshHost; width: form.inner - 112; label: "SSH server"; placeholder: "bastion.example.com"; onAccepted: root.connect() }
                    TextBox { id: sshPort; width: 102; label: "Port"; inputMethodHints: Qt.ImhDigitsOnly
                              validator: IntValidator { bottom: 1; top: 65535 } }
                }
                Row {
                    spacing: 10
                    TextBox { id: sshUser; width: (form.inner - 10) / 2; label: "SSH user" }
                    TextBox { id: sshKey; width: (form.inner - 10) / 2; label: "Key file (optional)"; placeholder: "~/.ssh/id_ed25519" }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Signs in with your key or ssh-agent, using this computer's ssh. A key with a passphrase needs to be in the agent. The database's host and port above are as the SSH server sees them."
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSmall + 1
                }
            }
        }

        // ---- Keep it ----
        Row {
            spacing: 10
            Toggle {
                id: saveIt
                anchors.verticalCenter: parent.verticalCenter
                label: on ? "Saved as" : "Save this connection"
                onToggled: { on = !on; if (on) Qt.callLater(saveName.focusField) }
            }
            TextBox {
                id: saveName
                visible: saveIt.on
                width: form.inner - saveIt.width - 10
                placeholder: databaseName.text.length ? databaseName.text + " on " + host.text : "Name"
                onAccepted: root.connect()
            }
        }

        Text {
            width: form.inner
            visible: !root.driverAvailable
            wrapMode: Text.WordWrap
            text: "Qt's " + root.type.label + " driver isn't installed on this computer, so this can't connect yet."
                  + (Qt.platform.os === "osx" && root.type.key === "postgres"
                     ? " On a Mac, installing Postgres.app provides it." : "")
            color: Theme.danger
            font.pixelSize: Theme.fontBody
        }
        Text {
            width: form.inner
            visible: root.database && root.database.error.length > 0 && !root.busy
            wrapMode: Text.WordWrap
            text: root.database ? root.database.error : ""
            color: Theme.danger
            font.pixelSize: Theme.fontBody
        }
        Text {
            width: form.inner
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
                text: root.busy ? (root.useSsh ? "Opening the tunnel…" : "Connecting…") : "Connect"
                primary: true
                opacity: root.driverAvailable ? 1 : 0.5
                enabled: root.driverAvailable && !root.busy
                onClicked: root.connect()
            }
        }
    }
    }
}
