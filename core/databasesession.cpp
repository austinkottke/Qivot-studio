#include "databasesession.h"
#include "codegen.h"
#include "sampledatabase.h"
#include "diagramdata.h"
#include "sshtunnel.h"
#ifdef STUDIO_HAS_DUCKDB
#include "duckdbdriver.h"
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>

static QString kindName(QiTableInfo::Kind kind)
{
    switch (kind) {
    case QiTableInfo::Table:    return QStringLiteral("table");
    case QiTableInfo::View:     return QStringLiteral("view");
    case QiTableInfo::Virtual:  return QStringLiteral("virtual");
    case QiTableInfo::Internal: return QStringLiteral("internal");
    }
    return QString();
}

DatabaseSession::DatabaseSession(QObject *parent)
    : QObject(parent)
    , m_connection(QStringLiteral("studio_%1").arg(reinterpret_cast<quintptr>(this), 0, 16))
{
#ifdef STUDIO_HAS_DUCKDB
    // Qt ships no DuckDB driver: Qivot's, registered once for every session.
    static const bool registered = [] {
        QSqlDatabase::registerSqlDriver(QStringLiteral("QDUCKDB"), new QSqlDriverCreator<DuckDbDriver>());
        return true;
    }();
    Q_UNUSED(registered);
#endif
}

DatabaseSession::~DatabaseSession()
{
    close();
}

qint64 DatabaseSession::fileSize() const
{
    return m_path.isEmpty() ? 0 : QFileInfo(m_path).size();
}

QString DatabaseSession::dialectName() const
{
    if (m_dialect == QLatin1String("sqlite"))    return QStringLiteral("SQLite");
    if (m_dialect == QLatin1String("postgres"))  return QStringLiteral("PostgreSQL");
    if (m_dialect == QLatin1String("mysql"))     return QStringLiteral("MySQL");
    if (m_dialect == QLatin1String("sqlserver")) return QStringLiteral("SQL Server");
    if (m_dialect == QLatin1String("duckdb"))    return QStringLiteral("DuckDB");
    return m_dialect;
}

// Whether a Qt SQL driver actually loads. QSqlDatabase::drivers() lists every
// plugin file it finds, including ones that can't load because the database's
// client library is missing (on macOS, Qt's QPSQL without Postgres.app).
bool DatabaseSession::driverLoads(const QString &name)
{
    if (!QSqlDatabase::drivers().contains(name))
        return false;
    const QString probe = QStringLiteral("studio_probe_") + name;
    bool ok;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(name, probe);
        ok = db.isValid();
    }
    QSqlDatabase::removeDatabase(probe);
    return ok;
}

// MySQL's client takes "localhost" to mean its local socket and ignores the
// port, so a server in Docker on localhost:33069 is never reached; the local
// MySQL answers instead. Asking for 127.0.0.1 goes over TCP to the port given.
static QString serverHost(const QString &driver, const QString &host)
{
    if (driver == QLatin1String("QMYSQL") && host.trimmed().compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("127.0.0.1");
    return host;
}

// A value for a libpq connect option, quoted so a path with spaces stays whole.
static QString pgValue(QString v)
{
    v.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('\''), QLatin1String("\\'"));
    return QLatin1Char('\'') + v + QLatin1Char('\'');
}

// Host, port, login and TLS: set up the same way for the reading connection
// and the writing one. `ssl` is `{ mode, ca, cert, key, trust }` (see connectTo).
static void setUpServer(QSqlDatabase &db, const QString &driver, const QVariantMap &settings,
                        const QString &host, int port, const QString &password, bool reading)
{
    const QString database = settings.value(QStringLiteral("database")).toString().trimmed();
    const QString user = settings.value(QStringLiteral("user")).toString().trimmed();
    const QVariantMap ssl = settings.value(QStringLiteral("ssl")).toMap();
    const QString mode = ssl.value(QStringLiteral("mode")).toString();
    const QString ca = ssl.value(QStringLiteral("ca")).toString().trimmed();
    const QString cert = ssl.value(QStringLiteral("cert")).toString().trimmed();
    const QString key = ssl.value(QStringLiteral("key")).toString().trimmed();

    if (driver == QLatin1String("QODBC")) {
        // ODBC takes everything as one connection string. {braces} quote values
        // so a ';' in a password can't be read as the next setting.
        auto braced = [](QString v) { return QLatin1Char('{') + v.replace(QLatin1Char('}'), QLatin1String("}}")) + QLatin1Char('}'); };
        const QString odbcDriver = settings.value(QStringLiteral("odbcDriver"),
                                                  QStringLiteral("ODBC Driver 18 for SQL Server")).toString();
        // Encrypt: "off", "on" (the driver's default) or "strict"; the certificate
        // is trusted as it is unless asked not to (local servers are self-signed).
        const QString encrypt = mode == QLatin1String("off") ? QStringLiteral("no")
                              : mode == QLatin1String("strict") ? QStringLiteral("strict") : QStringLiteral("yes");
        const bool trust = ssl.value(QStringLiteral("trust"), true).toBool() && mode != QLatin1String("strict");
        db.setDatabaseName(QStringLiteral("Driver={%1};Server=%2,%3;Database=%4;Uid=%5;Pwd=%6;Encrypt=%7;TrustServerCertificate=%8;%9")
                               .arg(odbcDriver, host).arg(port)
                               .arg(braced(database), braced(user), braced(password), encrypt,
                                    trust ? QStringLiteral("yes") : QStringLiteral("no"),
                                    reading ? QStringLiteral("ApplicationIntent=ReadOnly;") : QString()));
        return;
    }
    db.setHostName(serverHost(driver, host));
    db.setPort(port);
    db.setDatabaseName(database);
    db.setUserName(user);
    db.setPassword(password);
    QStringList options;
    if (driver == QLatin1String("QPSQL")) {
        options << QStringLiteral("connect_timeout=8");
        static const QStringList modes = { "disable", "allow", "prefer", "require", "verify-ca", "verify-full" };
        if (modes.contains(mode))
            options << QStringLiteral("sslmode=") + mode;
        if (!ca.isEmpty())   options << QStringLiteral("sslrootcert=") + pgValue(ca);
        if (!cert.isEmpty()) options << QStringLiteral("sslcert=") + pgValue(cert);
        if (!key.isEmpty())  options << QStringLiteral("sslkey=") + pgValue(key);
    } else {
        // MySQL's drivers turn TLS on when given a certificate to check the server with.
        options << QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=8");
        if (mode != QLatin1String("off")) {
            if (!ca.isEmpty())   options << QStringLiteral("SSL_CA=") + ca;
            if (!cert.isEmpty()) options << QStringLiteral("SSL_CERT=") + cert;
            if (!key.isEmpty())  options << QStringLiteral("SSL_KEY=") + key;
        }
    }
    db.setConnectOptions(options.join(QLatin1Char(';')));
}

QVariantMap DatabaseSession::availableTypes() const
{
    static const QVariantMap types = {
        { QStringLiteral("postgres"),  driverLoads(QStringLiteral("QPSQL")) },
        { QStringLiteral("mysql"),     driverLoads(QStringLiteral("QMYSQL")) || driverLoads(QStringLiteral("QMARIADB")) },
        { QStringLiteral("sqlserver"), driverLoads(QStringLiteral("QODBC")) },
        { QStringLiteral("duckdb"),    driverLoads(QStringLiteral("QDUCKDB")) },
    };
    return types;
}

void DatabaseSession::setError(const QString &message)
{
    if (message == m_error)
        return;
    m_error = message;
    emit errorChanged();
}

void DatabaseSession::discard()
{
    if (QSqlDatabase::contains(m_connection)) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connection, false);
            db.close();
        }
        QSqlDatabase::removeDatabase(m_connection);
    }
}

bool DatabaseSession::load(QSqlDatabase db)
{
    QiSchema schema(db);
    m_dialect = schema.dialect();
    m_tables  = schema.tables(/*includeViews=*/true);
    if (m_tables.isEmpty() && !schema.lastError().isEmpty()) {
        setError(tr("Couldn't read the database's structure: %1").arg(schema.lastError()));
        return false;
    }
    for (const QiTableInfo &t : m_tables) {
        m_sqlNames.insert(t.name, schema.sqlName(t.name));
        m_rows.insert(t.name, schema.rowCount(t.name));
    }
    m_open = true;
    setError(QString());
    emit openChanged();
    return true;
}

bool DatabaseSession::open(const QVariant &fileOrUrl)
{
    QString path = fileOrUrl.toString();
    const QUrl url = fileOrUrl.toUrl();
    if (url.isLocalFile())
        path = url.toLocalFile();
    if (!QFileInfo::exists(path)) {
        setError(tr("No such file: %1").arg(path));
        return false;
    }

    if (isDuckDbFile(path))
        return openDuckDb(path);

    close();
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(path);
    // The Analyzer only reads. Opening read-only makes that a guarantee,
    // not a promise — nothing in the app can modify the user's file.
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!db.open()) {
        setError(tr("Couldn't open %1: %2").arg(QFileInfo(path).fileName(), db.lastError().text()));
        db = QSqlDatabase();
        discard();
        return false;
    }
    {
        // An unrelated file opens fine; the first catalog read is what fails.
        QSqlQuery probe(db);
        if (!probe.exec(QStringLiteral("SELECT count(*) FROM sqlite_master"))) {
            setError(tr("%1 isn't an SQLite database.").arg(QFileInfo(path).fileName()));
            probe = QSqlQuery();
            db = QSqlDatabase();
            discard();
            return false;
        }
    }

    m_path = path;
    m_displayName = QFileInfo(path).fileName();
    m_settings = { { QStringLiteral("type"), QStringLiteral("sqlite") },
                   { QStringLiteral("path"), QFileInfo(path).absoluteFilePath() } };
    m_location = QFileInfo(path).absolutePath();
    m_readOnly = true;
    if (!load(db)) {
        db = QSqlDatabase();
        close();
        return false;
    }
    return true;
}

bool DatabaseSession::isDuckDbFile(const QString &path)
{
    // DuckDB's files say so in their header ("DUCK" from byte 8); the usual
    // extensions are enough for one that's empty or new.
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray head = f.read(12);
        if (head.size() == 12 && head.mid(8, 4) == "DUCK")
            return true;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("duckdb") || suffix == QLatin1String("ddb");
}

bool DatabaseSession::openDuckDb(const QString &path)
{
#ifdef STUDIO_HAS_DUCKDB
    close();
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QDUCKDB"), m_connection);
    db.setDatabaseName(path);
    db.setConnectOptions(QStringLiteral("DUCKDB_OPEN_READONLY"));     // as SQLite files are: read-only
    if (!db.open()) {
        setError(tr("Couldn't open %1: %2").arg(QFileInfo(path).fileName(), db.lastError().text()));
        db = QSqlDatabase();
        discard();
        return false;
    }
    m_path = path;
    m_displayName = QFileInfo(path).fileName();
    m_settings = { { QStringLiteral("type"), QStringLiteral("duckdb") },
                   { QStringLiteral("path"), QFileInfo(path).absoluteFilePath() } };
    m_location = QFileInfo(path).absolutePath();
    m_readOnly = true;
    if (!load(db)) {
        db = QSqlDatabase();
        close();
        return false;
    }
    return true;
#else
    setError(tr("%1 is a DuckDB database, and this build of Studio doesn't include DuckDB.")
                 .arg(QFileInfo(path).fileName()));
    return false;
#endif
}

bool DatabaseSession::connectTo(const QVariantMap &settings)
{
    const QString type     = settings.value(QStringLiteral("type")).toString();
    const QString host     = settings.value(QStringLiteral("host"), QStringLiteral("localhost")).toString().trimmed();
    const QString database = settings.value(QStringLiteral("database")).toString().trimmed();
    const QString user     = settings.value(QStringLiteral("user")).toString().trimmed();
    const QString password = settings.value(QStringLiteral("password")).toString();
    int port = settings.value(QStringLiteral("port")).toInt();

    QString driver, label;
    if (type == QLatin1String("postgres"))       { driver = QStringLiteral("QPSQL"); label = QStringLiteral("PostgreSQL"); if (!port) port = 5432; }
    else if (type == QLatin1String("mysql"))     { driver = QStringLiteral("QMYSQL"); label = QStringLiteral("MySQL"); if (!port) port = 3306; }
    else if (type == QLatin1String("sqlserver")) { driver = QStringLiteral("QODBC"); label = QStringLiteral("SQL Server"); if (!port) port = 1433; }
    else { setError(tr("Unknown database type \"%1\".").arg(type)); return false; }

    if (driver == QLatin1String("QMYSQL") && !driverLoads(driver) && driverLoads(QStringLiteral("QMARIADB")))
        driver = QStringLiteral("QMARIADB");
    if (!driverLoads(driver)) {
        setError(tr("Qt's %1 driver (%2) isn't installed, so Studio can't connect to %1 yet.")
                     .arg(label, driver));
        return false;
    }
    if (database.isEmpty()) {
        setError(tr("Enter the name of the database to open."));
        return false;
    }

    close();
    // Through SSH: the database is then reached at a local port the tunnel forwards.
    const QVariantMap ssh = settings.value(QStringLiteral("ssh")).toMap();
    QString host2 = host;
    int port2 = port;
    if (!ssh.value(QStringLiteral("host")).toString().trimmed().isEmpty()) {
        m_tunnel = std::make_unique<SshTunnel>();
        SshTunnel::Options o;
        o.host = ssh.value(QStringLiteral("host")).toString().trimmed();
        o.port = ssh.value(QStringLiteral("port"), 22).toInt();
        o.user = ssh.value(QStringLiteral("user")).toString().trimmed();
        o.keyFile = ssh.value(QStringLiteral("key")).toString().trimmed();
        o.targetHost = host;
        o.targetPort = port;
        if (!m_tunnel->start(o)) {
            setError(tr("Couldn't open the SSH tunnel through %1 — %2").arg(o.host, m_tunnel->error()));
            m_tunnel.reset();
            return false;
        }
        host2 = QStringLiteral("127.0.0.1");
        port2 = m_tunnel->localPort();
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(driver, m_connection);
    setUpServer(db, driver, settings, host2, port2, password, /*reading=*/true);
    if (!db.open()) {
        QString why = db.lastError().text().trimmed();
        if (why.isEmpty())
            why = tr("no answer");
        setError(tr("Couldn't connect to %1 at %2:%3 — %4").arg(label, host).arg(port).arg(why));
        db = QSqlDatabase();
        discard();
        m_tunnel.reset();
        return false;
    }

    // Make the session itself read-only where the database can, so even a bug
    // in Studio couldn't change anything. SQL Server has no such session switch.
    const bool readOnly = makeReadOnly(db);

    m_displayName = database;
    m_location = host + QLatin1Char(':') + QString::number(port);
    if (m_tunnel)
        m_location += tr(" via %1").arg(ssh.value(QStringLiteral("host")).toString().trimmed());
    m_settings = { { QStringLiteral("type"), type }, { QStringLiteral("driver"), driver },
                   { QStringLiteral("host"), host }, { QStringLiteral("port"), port },
                   { QStringLiteral("database"), database }, { QStringLiteral("user"), user } };
    if (!settings.value(QStringLiteral("ssl")).toMap().isEmpty())
        m_settings.insert(QStringLiteral("ssl"), settings.value(QStringLiteral("ssl")));
    if (m_tunnel)
        m_settings.insert(QStringLiteral("ssh"), ssh);
    m_serverHost = host2;
    m_serverPort = port2;
    m_password = password;
    if (driver == QLatin1String("QODBC"))
        m_settings.insert(QStringLiteral("odbcDriver"),
                          settings.value(QStringLiteral("odbcDriver"), QStringLiteral("ODBC Driver 18 for SQL Server")));
    m_readOnly = readOnly;
    if (!load(db)) {
        db = QSqlDatabase();
        close();
        return false;
    }
    return true;
}

bool DatabaseSession::makeReadOnly(QSqlDatabase &db)
{
    QSqlQuery q(db);
    const QString driver = db.driverName();
    if (driver == QLatin1String("QPSQL"))
        return q.exec(QStringLiteral("SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY"));
    if (driver == QLatin1String("QMYSQL") || driver == QLatin1String("QMARIADB"))
        return q.exec(QStringLiteral("SET SESSION TRANSACTION READ ONLY"));
    return false;
}

QSqlDatabase DatabaseSession::openReadOnlyClone(const QString &connection, const QString &name, QString *error)
{
    // This overload of cloneDatabase is the one that's safe from another thread.
    QSqlDatabase db = QSqlDatabase::cloneDatabase(connection, name);
    if (!db.isValid()) {
        if (error)
            *error = tr("The database was closed.");
        return db;
    }
    // An SQLite clone keeps QSQLITE_OPEN_READONLY from the connect options;
    // a server session has to be told again.
    if (!db.open()) {
        if (error)
            *error = db.lastError().text().trimmed();
        return db;
    }
    makeReadOnly(db);
    return db;
}

QVariantList DatabaseSession::samples() const
{
    QVariantList out;
    for (const SampleDatabase::Info &s : SampleDatabase::catalogue())
        out << QVariantMap{ { QStringLiteral("id"), s.id }, { QStringLiteral("title"), s.title },
                            { QStringLiteral("summary"), s.summary }, { QStringLiteral("highlights"), s.highlights },
                            { QStringLiteral("tables"), s.tables.size() }, { QStringLiteral("tableNames"), s.tables },
                            { QStringLiteral("rows"), s.rows } };
    return out;
}

// The version a sample file was made at (PRAGMA user_version); 0 if unreadable.
static int sampleFileVersion(const QString &path)
{
    int version = 0;
    const QString conn = QStringLiteral("studio_sample_version");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
                version = q.value(0).toInt();
            q = QSqlQuery();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return version;
}

bool DatabaseSession::openSample(const QString &sampleId)
{
    const QString id = sampleId.isEmpty() ? QStringLiteral("bookshop") : sampleId;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + id + QStringLiteral(".db");
    if (!QFileInfo::exists(path) || sampleFileVersion(path) != SampleDatabase::version(id)) {
        QString why;
        if (!SampleDatabase::createFile(id, path, &why)) {
            setError(tr("Couldn't create the sample database: %1").arg(why));
            return false;
        }
    }
    if (!open(path))
        return false;
    m_sampleId = id;
    emit sampleChanged();
    return true;
}

bool DatabaseSession::allowChanges(bool on)
{
    if (on == m_changesAllowed)
        return true;
    if (!on) {
        closeWriter();
        return true;
    }
    if (!m_open)
        return false;
    const QString type = m_settings.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("duckdb")) {
        // DuckDB lets one process hold a file read-only or read-write, not both.
        setError(tr("Changing DuckDB files isn't supported yet: %1 stays read-only.").arg(m_displayName));
        return false;
    }
    QSqlDatabase db;
    if (type == QLatin1String("sqlite")) {
        if (!QFileInfo(m_path).isWritable()) {
            setError(tr("%1 can't be changed: the file isn't writable.").arg(m_displayName));
            return false;
        }
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), writeConnectionName());
        db.setDatabaseName(m_path);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));   // wait out a read in progress
    } else {
        // The same server (through the same tunnel), login and TLS as the reading
        // connection, without the read-only parts.
        const QString driver = m_settings.value(QStringLiteral("driver")).toString();
        db = QSqlDatabase::addDatabase(driver, writeConnectionName());
        setUpServer(db, driver, m_settings, m_serverHost, m_serverPort, m_password, /*reading=*/false);
    }
    if (!db.open()) {
        setError(tr("Couldn't open %1 for changes: %2").arg(m_displayName, db.lastError().text()));
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(writeConnectionName());
        return false;
    }
    if (type == QLatin1String("sqlite"))
        QSqlQuery(db).exec(QStringLiteral("PRAGMA foreign_keys = ON"));    // edits respect the references
    m_changesAllowed = true;
    setError(QString());
    emit changesAllowedChanged();
    return true;
}

QSqlDatabase DatabaseSession::writeDatabase() const
{
    return m_changesAllowed ? QSqlDatabase::database(writeConnectionName()) : QSqlDatabase();
}

void DatabaseSession::closeWriter()
{
    if (QSqlDatabase::contains(writeConnectionName())) {
        {
            QSqlDatabase db = QSqlDatabase::database(writeConnectionName(), false);
            db.close();
        }
        QSqlDatabase::removeDatabase(writeConnectionName());
    }
    if (m_changesAllowed) {
        m_changesAllowed = false;
        emit changesAllowedChanged();
    }
}

bool DatabaseSession::refresh()
{
    if (!m_open)
        return false;
    m_tables.clear();
    m_rows.clear();
    m_sqlNames.clear();
    m_refreshing = true;
    const bool ok = load(QSqlDatabase::database(m_connection));
    m_refreshing = false;
    return ok;
}

void DatabaseSession::close()
{
    const bool wasOpen = m_open;
    closeWriter();
    discard();
    m_open = false;
    m_readOnly = false;
    m_tables.clear();
    m_rows.clear();
    m_sqlNames.clear();
    m_path.clear();
    if (!m_sampleId.isEmpty()) {
        m_sampleId.clear();
        emit sampleChanged();
    }
    m_dialect.clear();
    m_location.clear();
    m_displayName.clear();
    m_settings.clear();
    m_password.clear();
    m_tunnel.reset();
    m_serverHost.clear();
    m_serverPort = 0;
    if (wasOpen)
        emit openChanged();
}

QVariantList DatabaseSession::tables() const
{
    QVariantList out;
    for (const QiTableInfo &t : m_tables) {
        out << QVariantMap{
            { QStringLiteral("name"),    t.name },
            { QStringLiteral("kind"),    kindName(t.kind) },
            { QStringLiteral("rows"),    m_rows.value(t.name, -1) },
            { QStringLiteral("columns"), t.columns.size() },
        };
    }
    return out;
}

QVariantMap DatabaseSession::table(const QString &name) const
{
    const QiTableInfo *info = nullptr;
    for (const QiTableInfo &t : m_tables)
        if (t.name == name)
            info = &t;
    if (!info)
        return {};

    // Which foreign key (if any) each column belongs to, for the column list.
    QHash<QString, QString> refOf;
    for (const QiForeignKeyInfo &fk : info->foreignKeys)
        for (int i = 0; i < fk.columns.size(); ++i)
            refOf.insert(fk.columns.at(i), fk.refTable + QLatin1Char('.') + fk.refColumns.value(i));

    QVariantList columns;
    for (const QiColumnInfo &c : info->columns) {
        const QString ref = refOf.value(c.name);
        columns << QVariantMap{
            { QStringLiteral("name"),          c.name },
            { QStringLiteral("type"),          c.type },
            { QStringLiteral("nullable"),      c.nullable },
            { QStringLiteral("primaryKey"),    c.primaryKey },
            { QStringLiteral("autoIncrement"), c.autoIncrement },
            { QStringLiteral("defaultValue"),  c.defaultValue.isNull() ? QString() : c.defaultValue.toString() },
            { QStringLiteral("references"),    ref },
            { QStringLiteral("refTable"),      ref.section(QLatin1Char('.'), 0, 0) },
        };
    }

    QVariantList foreignKeys;
    for (const QiForeignKeyInfo &fk : info->foreignKeys) {
        foreignKeys << QVariantMap{
            { QStringLiteral("columns"),    fk.columns },
            { QStringLiteral("refTable"),   fk.refTable },
            { QStringLiteral("refColumns"), fk.refColumns },
            { QStringLiteral("onDelete"),   fk.onDelete },
            { QStringLiteral("onUpdate"),   fk.onUpdate },
        };
    }

    QVariantList indexes;
    for (const QiIndexInfo &ix : info->indexes) {
        indexes << QVariantMap{
            { QStringLiteral("name"),     ix.name },
            { QStringLiteral("columns"),  ix.columns },
            { QStringLiteral("unique"),   ix.unique },
            { QStringLiteral("implicit"), ix.implicit },
        };
    }

    // The other direction: tables whose foreign keys point here.
    QVariantList referencedBy;
    for (const QiTableInfo &other : m_tables) {
        for (const QiForeignKeyInfo &fk : other.foreignKeys) {
            if (fk.refTable.compare(name, Qt::CaseInsensitive) == 0) {
                referencedBy << QVariantMap{
                    { QStringLiteral("table"),      other.name },
                    { QStringLiteral("columns"),    fk.columns },
                    { QStringLiteral("refColumns"), fk.refColumns },
                    { QStringLiteral("onDelete"),   fk.onDelete },
                };
            }
        }
    }

    return QVariantMap{
        { QStringLiteral("name"),         info->name },
        { QStringLiteral("kind"),         kindName(info->kind) },
        { QStringLiteral("rows"),         m_rows.value(info->name, -1) },
        { QStringLiteral("primaryKey"),   info->primaryKey },
        { QStringLiteral("columns"),      columns },
        { QStringLiteral("foreignKeys"),  foreignKeys },
        { QStringLiteral("indexes"),      indexes },
        { QStringLiteral("referencedBy"), referencedBy },
    };
}

QVariantMap DatabaseSession::diagram() const
{
    return DiagramData::build(m_tables, m_rows);
}

QVariantMap DatabaseSession::cppModel(const QString &table) const
{
    const CodeGen::Model m = CodeGen(m_tables, m_dialect).model(table);
    if (!m.isValid())
        return {};
    QVariantList warnings;
    for (const CodeGen::Warning &w : m.warnings)
        warnings << QVariantMap{ { QStringLiteral("column"), w.column }, { QStringLiteral("message"), w.message } };
    return { { QStringLiteral("className"), m.className },
             { QStringLiteral("code"), m.code },
             { QStringLiteral("warnings"), warnings } };
}

QString DatabaseSession::cppHeader() const
{
    return CodeGen(m_tables, m_dialect).header();
}
