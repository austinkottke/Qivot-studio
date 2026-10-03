#ifndef DATABASESESSION_H
#define DATABASESESSION_H

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSqlDatabase>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <memory>
#include <qivot.hpp>

class SshTunnel;

/// One open database, as the UI sees it.
/**
  An SQLite file, or a PostgreSQL / MySQL / MariaDB / SQL Server database on a
  server. Either way it is opened for reading only, and its structure is read
  with QiSchema. QML gets plain lists and maps, so the screens never touch SQL.

  Changes (applying a design, editing rows, importing) need the user to allow
  them first: allowChanges(true) opens a second, writable connection, and only
  that one ever writes. The reading connection stays read-only throughout.

\code
    Database {
        id: db
        Component.onCompleted: connectTo({ type: "postgres", host: "localhost",
                                           database: "pagila", user: "me", password: "…" })
    }
    ListView { model: db.tables; delegate: Text { text: modelData.name + " " + modelData.rows } }
\endcode
 */
class DatabaseSession : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Database)

    Q_PROPERTY(bool isOpen READ isOpen NOTIFY openChanged)
    Q_PROPERTY(bool isServer READ isServer NOTIFY openChanged)
    Q_PROPERTY(QString dialect READ dialect NOTIFY openChanged)
    Q_PROPERTY(QString dialectName READ dialectName NOTIFY openChanged)
    Q_PROPERTY(QString location READ location NOTIFY openChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY openChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY openChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY openChanged)
    Q_PROPERTY(qint64 fileSize READ fileSize NOTIFY openChanged)
    Q_PROPERTY(QVariantList tables READ tables NOTIFY openChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantMap availableTypes READ availableTypes CONSTANT)
    /// The sample databases: [{id, title, summary, highlights}].
    Q_PROPERTY(QVariantList samples READ samples CONSTANT)
    /// Which sample is open ("" when it isn't one).
    Q_PROPERTY(QString sampleId READ sampleId NOTIFY sampleChanged)
    /// Whether the user has allowed changes to this database (see allowChanges()).
    Q_PROPERTY(bool changesAllowed READ changesAllowed NOTIFY changesAllowedChanged)
    /// True while refresh() re-reads the structure (so screens can keep their place).
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY openChanged)

public:
    explicit DatabaseSession(QObject *parent = nullptr);
    ~DatabaseSession() override;

    bool    isOpen() const { return m_open; }
    bool    isServer() const { return m_open && m_dialect != QLatin1String("sqlite"); }
    /// QiSchema's name for the database: "sqlite", "postgres", "mysql", "sqlserver".
    QString dialect() const { return m_dialect; }
    /// For people: "SQLite", "PostgreSQL", "MySQL", "SQL Server".
    QString dialectName() const;
    /// "host:port" for a server, the folder for a file.
    QString location() const { return m_location; }
    /// True when the database itself refuses writes for this session (not just Studio).
    bool    readOnly() const { return m_readOnly; }
    QString filePath() const { return m_path; }
    QString displayName() const { return m_displayName; }
    qint64  fileSize() const;
    QString error() const { return m_error; }

    /// Whether Qt's SQL driver `name` (QPSQL, QMYSQL, …) is there and loads:
    /// its plugin found, and the database's client library with it.
    static bool driverLoads(const QString &name);

    /// Which server types this build can connect to: `{ postgres, mysql, sqlserver }`
    /// (each true when Qt's driver for it is installed).
    QVariantMap availableTypes() const;
    QVariantList samples() const;
    QString sampleId() const { return m_sampleId; }
    bool changesAllowed() const { return m_changesAllowed; }
    bool refreshing() const { return m_refreshing; }

    /// Allow changes to this database (`on`), or go back to reading only.
    /// Allowing opens a separate, writable connection; false (with error())
    /// if it can't be opened — a file that isn't writable, say.
    Q_INVOKABLE bool allowChanges(bool on);

    /// The writable connection, while changes are allowed; invalid otherwise.
    QSqlDatabase writeDatabase() const;

    /// Re-read the structure and row counts after a change. Emits
    /// openChanged() with refreshing() true.
    Q_INVOKABLE bool refresh();

    /// Every table, view and virtual table, sorted by name:
    /// `{ name, kind: "table"|"view"|"virtual", rows, columns }`.
    QVariantList tables() const;

    /// Open an SQLite or DuckDB file (a `file:` URL or a plain path), read-only.
    Q_INVOKABLE bool open(const QVariant &fileOrUrl);

    /// Whether `path` is a DuckDB database (by its header, or its extension).
    static bool isDuckDbFile(const QString &path);

    /// Connect to a server: `{ type: "postgres"|"mysql"|"sqlserver", host, port,
    /// database, user, password, ssl?, ssh? }`. port may be omitted for the usual one.
    ///  - `ssl: { mode, ca, cert, key, trust }`: PostgreSQL's sslmode ("disable" …
    ///    "verify-full") and certificate files; MySQL: TLS with the CA file given
    ///    (mode "off" to leave it); SQL Server: Encrypt "off" | "on" | "strict",
    ///    and whether to trust the server's certificate (`trust`, default yes).
    ///  - `ssh: { host, port, user, key }`: reach the database through an SSH
    ///    server (see SshTunnel); host and port are then as that server sees them.
    Q_INVOKABLE bool connectTo(const QVariantMap &settings);

    /// connectTo() settings from a URL: `postgres://user:pass@host:port/database`,
    /// also mysql:// (mariadb://), sqlserver:// (mssql://) and redis://.
    static QVariantMap settingsFromUrl(const QString &url);

    /// Create sample `id` (default: the bookshop) in the app's data folder —
    /// once, or again when the sample has changed — and open it.
    Q_INVOKABLE bool openSample(const QString &id = QString());

    Q_INVOKABLE void close();

    /// Full description of one table, for the detail panel:
    /// `{ name, kind, rows, primaryKey, columns: [...], foreignKeys: [...],
    ///    indexes: [...], referencedBy: [...] }`. Empty if there's no such table.
    Q_INVOKABLE QVariantMap table(const QString &name) const;

    /// Everything the ER diagram draws, already laid out:
    /// `{ tables: [{ name, rows, x, y, width, height, columns: [{ name, type,
    ///    primaryKey, foreignKey }] }], links: [{ from, fromColumns, to,
    ///    toColumns, onDelete }], width, height, headerHeight, rowHeight }`.
    /// Views aren't included: they have no relationships to draw.
    Q_INVOKABLE QVariantMap diagram() const;

    /// The Qivot model class for one table (see CodeGen):
    /// `{ className, code, warnings: [{ column, message }] }`. Empty if there's no such table.
    Q_INVOKABLE QVariantMap cppModel(const QString &table) const;

    /// Every table's model in one models.h.
    Q_INVOKABLE QString cppHeader() const;

    /// How to name a listed table in SQL (quoted, schema-qualified where needed).
    QString sqlName(const QString &table) const { return m_sqlNames.value(table); }

    /// How this database was opened, without the password — what an exported
    /// project needs to open it again: `{ type: "sqlite", path }` or
    /// `{ type, driver, host, port, database, user, odbcDriver? }`.
    QVariantMap connectionSettings() const { return m_settings; }

    /// The password of a server connection, kept in memory only so a project
    /// build can pass it to its tests. Never saved anywhere.
    QString password() const { return m_password; }

    /// The tables as QiSchema read them (for code generation and export).
    const QVector<QiTableInfo> &tableInfos() const { return m_tables; }

    /// The Qt connection name, for code that needs raw access (RowsModel, tests).
    QString connectionName() const { return m_connection; }

    /// A connection of its own to the same database, named `name`, for another
    /// thread (Qt's connections belong to the thread that opens them), made as
    /// read-only as the session's own. Call it from the thread that will use it,
    /// and QSqlDatabase::removeDatabase(name) when done. Not open (with `error`)
    /// if it failed.
    static QSqlDatabase openReadOnlyClone(const QString &connection, const QString &name, QString *error = nullptr);

    /// Make a server session read-only where the database has a switch for it
    /// (PostgreSQL, MySQL); false where it hasn't (SQL Server) or it failed.
    static bool makeReadOnly(QSqlDatabase &db);

signals:
    void sampleChanged();
    void changesAllowedChanged();
    void openChanged();
    void errorChanged();

private:
    void setError(const QString &message);
    bool load(QSqlDatabase db);            // read the structure; becomes the open database
    bool openDuckDb(const QString &path);
    void discard();                        // drop a connection that failed part-way
    void closeWriter();
    QString writeConnectionName() const { return m_connection + QStringLiteral("_write"); }

    QString                 m_connection;
    bool                    m_open = false;
    bool                    m_readOnly = false;
    QString                 m_dialect;
    QString                 m_location;
    QString                 m_displayName;
    QString                 m_path;
    QString                 m_sampleId;
    bool                    m_changesAllowed = false;
    bool                    m_refreshing = false;
    QString                 m_error;
    QVector<QiTableInfo>    m_tables;
    QHash<QString, qint64>  m_rows;
    QHash<QString, QString> m_sqlNames;
    QVariantMap             m_settings;
    QString                 m_password;
    std::unique_ptr<SshTunnel> m_tunnel;
    QString                 m_serverHost;      // where the connections go: the server, or the tunnel's end
    int                     m_serverPort = 0;
};

#endif // DATABASESESSION_H
