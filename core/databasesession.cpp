#include "databasesession.h"
#include "codegen.h"
#include "sampledatabase.h"
#include "diagramdata.h"

#include <QDir>
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
static bool driverLoads(const QString &name)
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

QVariantMap DatabaseSession::availableTypes() const
{
    static const QVariantMap types = {
        { QStringLiteral("postgres"),  driverLoads(QStringLiteral("QPSQL")) },
        { QStringLiteral("mysql"),     driverLoads(QStringLiteral("QMYSQL")) || driverLoads(QStringLiteral("QMARIADB")) },
        { QStringLiteral("sqlserver"), driverLoads(QStringLiteral("QODBC")) },
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
    QSqlDatabase db = QSqlDatabase::addDatabase(driver, m_connection);
    if (driver == QLatin1String("QODBC")) {
        // ODBC takes everything as one connection string. {braces} quote values
        // so a ';' in a password can't be read as the next setting.
        auto braced = [](QString v) { return QLatin1Char('{') + v.replace(QLatin1Char('}'), QLatin1String("}}")) + QLatin1Char('}'); };
        const QString odbcDriver = settings.value(QStringLiteral("odbcDriver"),
                                                  QStringLiteral("ODBC Driver 18 for SQL Server")).toString();
        db.setDatabaseName(QStringLiteral("Driver={%1};Server=%2,%3;Database=%4;Uid=%5;Pwd=%6;"
                                          "TrustServerCertificate=yes;ApplicationIntent=ReadOnly;")
                               .arg(odbcDriver, host).arg(port).arg(braced(database), braced(user), braced(password)));
    } else {
        db.setHostName(host);
        db.setPort(port);
        db.setDatabaseName(database);
        db.setUserName(user);
        db.setPassword(password);
        db.setConnectOptions(driver == QLatin1String("QPSQL") ? QStringLiteral("connect_timeout=8")
                                                              : QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=8"));
    }
    if (!db.open()) {
        QString why = db.lastError().text().trimmed();
        if (why.isEmpty())
            why = tr("no answer");
        setError(tr("Couldn't connect to %1 at %2:%3 — %4").arg(label, host).arg(port).arg(why));
        db = QSqlDatabase();
        discard();
        return false;
    }

    // Make the session itself read-only where the database can, so even a bug
    // in Studio couldn't change anything. SQL Server has no such session switch.
    bool readOnly = false;
    {
        QSqlQuery q(db);
        if (driver == QLatin1String("QPSQL"))
            readOnly = q.exec(QStringLiteral("SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY"));
        else if (driver != QLatin1String("QODBC"))
            readOnly = q.exec(QStringLiteral("SET SESSION TRANSACTION READ ONLY"));
    }

    m_displayName = database;
    m_location = host + QLatin1Char(':') + QString::number(port);
    m_settings = { { QStringLiteral("type"), type }, { QStringLiteral("driver"), driver },
                   { QStringLiteral("host"), host }, { QStringLiteral("port"), port },
                   { QStringLiteral("database"), database }, { QStringLiteral("user"), user } };
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

void DatabaseSession::close()
{
    const bool wasOpen = m_open;
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
