#include "tableprofile.h"

#include "codegen.h"
#include "schemadesign.h"     // Migration::quoted

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <cmath>

namespace {

struct Job {
    QString     connection;       // the session's, to clone
    QString     table;            // as SQL (quoted, schema-qualified)
    QString     dialect;
    QiTableInfo info;
};

QString shortText(const QVariant &v)
{
    if (v.isNull())
        return QString();
    if (v.userType() == QMetaType::QByteArray)
        return QStringLiteral("(%1 bytes)").arg(v.toByteArray().size());
    QString s = v.toString().simplified();
    return s.size() > 60 ? s.left(60) + QChar(0x2026) : s;
}

// Everything about one column; `rows` is the table's row count.
QVariantMap profileColumn(QSqlQuery &q, const Job &job, const QiColumnInfo &c, qint64 rows)
{
    const QString kind = TableProfile::kindOf(c.type);
    const bool mssql = job.dialect == QLatin1String("sqlserver");
    const bool mysql = job.dialect == QLatin1String("mysql");
    const QString col = Migration::quoted(c.name, job.dialect);
    QVariantMap m{ { QStringLiteral("name"), c.name }, { QStringLiteral("type"), c.type },
                   { QStringLiteral("kind"), kind }, { QStringLiteral("rows"), rows },
                   { QStringLiteral("done"), true } };
    auto fail = [&](const QSqlQuery &query) {
        m.insert(QStringLiteral("error"), query.lastError().text().trimmed());
        return m;
    };
    auto limited = [&](const QString &select, int n) {     // SELECT ... with a row limit, in the dialect
        return mssql ? QString(select).replace(0, 6, QStringLiteral("SELECT TOP %1").arg(n))
                     : select + QStringLiteral(" LIMIT %1").arg(n);
    };

    // Counts, and the range where a range means something. Some types can't be
    // ordered or compared (tsvector, json, ...), so step down until a query works:
    // everything, then the counts, then just how many are filled.
    bool ranged = kind == QLatin1String("number") || kind == QLatin1String("date") || kind == QLatin1String("text");
    QString average;
    if (kind == QLatin1String("number"))
        average = mssql ? QStringLiteral(", AVG(CAST(%1 AS FLOAT))").arg(col) : QStringLiteral(", AVG(%1)").arg(col);
    const QString full = QStringLiteral("SELECT COUNT(%1), COUNT(DISTINCT %1), MIN(%1), MAX(%1)%2 FROM %3").arg(col, average, job.table);
    const QString counts = QStringLiteral("SELECT COUNT(%1), COUNT(DISTINCT %1) FROM %2").arg(col, job.table);
    const QString filledOnly = QStringLiteral("SELECT COUNT(%1) FROM %2").arg(col, job.table);
    bool hasDistinct = true;
    if (!(ranged && q.exec(full) && q.next())) {
        ranged = false;
        if (!(q.exec(counts) && q.next())) {
            hasDistinct = false;
            if (!(q.exec(filledOnly) && q.next()))
                return fail(q);
        }
    }
    const qint64 filled = q.value(0).toLongLong();
    const qint64 distinct = hasDistinct ? q.value(1).toLongLong() : -1;
    m.insert(QStringLiteral("nulls"), rows - filled);
    if (hasDistinct)
        m.insert(QStringLiteral("distinct"), distinct);
    if (ranged) {
        m.insert(QStringLiteral("min"), shortText(q.value(2)));
        m.insert(QStringLiteral("max"), shortText(q.value(3)));
    }
    double lo = 0, hi = 0;
    if (ranged && kind == QLatin1String("number")) {
        lo = q.value(2).toDouble();
        hi = q.value(3).toDouble();
        if (!q.value(4).isNull())
            m.insert(QStringLiteral("avg"), q.value(4).toDouble());
    }
    if (filled == 0)
        return m;

    // Numbers with a spread: a histogram. Anything else (and numbers with few
    // values, like a 1-5 rating): the commonest values.
    if (!hasDistinct)
        return m;
    const bool histogram = ranged && kind == QLatin1String("number") && hi > lo && distinct > TableProfile::Buckets;
    if (histogram) {
        const bool integral = CodeGen::cppType(c.type) == QLatin1String("int") || CodeGen::cppType(c.type) == QLatin1String("qint64");
        double width = (hi - lo) / TableProfile::Buckets;
        if (integral)
            width = std::ceil((hi - lo + 1) / TableProfile::Buckets);
        const QString w = QString::number(width, 'g', 17), base = QString::number(lo, 'g', 17);
        const QString bucket = job.dialect == QLatin1String("sqlite")
            ? QStringLiteral("CAST((%1 - %2) / %3 AS INTEGER)").arg(col, base, w)
            : QStringLiteral("FLOOR((%1 - %2) / %3)").arg(col, base, w);
        Q_UNUSED(mysql);
        if (!q.exec(QStringLiteral("SELECT b, COUNT(*) FROM (SELECT %1 AS b FROM %2 WHERE %3 IS NOT NULL) x GROUP BY b")
                        .arg(bucket, job.table, col)))
            return fail(q);
        QVector<qint64> counts(TableProfile::Buckets, 0);
        while (q.next())
            counts[qBound(0, q.value(0).toInt(), TableProfile::Buckets - 1)] += q.value(1).toLongLong();
        QVariantList bars;
        for (int i = 0; i < TableProfile::Buckets; ++i)
            bars << QVariantMap{ { QStringLiteral("from"), lo + i * width }, { QStringLiteral("to"), lo + (i + 1) * width },
                                 { QStringLiteral("count"), counts.at(i) } };
        m.insert(QStringLiteral("histogram"), bars);
    } else if (kind != QLatin1String("date") && CodeGen::cppType(c.type) != QLatin1String("QByteArray")) {
        if (!q.exec(limited(QStringLiteral("SELECT %1, COUNT(*) FROM %2 WHERE %1 IS NOT NULL GROUP BY %1 ORDER BY COUNT(*) DESC")
                                .arg(col, job.table), TableProfile::TopValues)))
            return m;                           // a type that can't be grouped: no list, not an error
        QVariantList top;
        while (q.next())
            top << QVariantMap{ { QStringLiteral("value"), shortText(q.value(0)) }, { QStringLiteral("count"), q.value(1).toLongLong() } };
        m.insert(QStringLiteral("top"), top);
    }
    return m;
}

} // namespace

TableProfile::TableProfile(QObject *parent)
    : QObject(parent)
{
}

TableProfile::~TableProfile()
{
    stop();
}

QString TableProfile::kindOf(const QString &type)
{
    const QString cpp = CodeGen::cppType(type);
    if (cpp == QLatin1String("int") || cpp == QLatin1String("qint64") || cpp == QLatin1String("double"))
        return QStringLiteral("number");
    if (cpp == QLatin1String("QDate") || cpp == QLatin1String("QDateTime") || cpp == QLatin1String("QTime"))
        return QStringLiteral("date");
    if (cpp == QLatin1String("QString"))
        return QStringLiteral("text");
    return QStringLiteral("other");
}

void TableProfile::setSession(DatabaseSession *session)
{
    if (session == m_session)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    if (m_session)
        connect(m_session, &DatabaseSession::openChanged, this, &TableProfile::start);
    emit sessionChanged();
    start();
}

void TableProfile::setTable(const QString &table)
{
    if (table == m_table)
        return;
    m_table = table;
    emit tableChanged();
    start();
}

void TableProfile::refresh()
{
    start();
}

int TableProfile::done() const
{
    int n = 0;
    for (const QVariant &c : m_columns)
        n += c.toMap().value(QStringLiteral("done")).toBool() ? 1 : 0;
    return n;
}

void TableProfile::stop()
{
    if (!m_thread)
        return;
    m_cancel->store(true);
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
    emit runningChanged();
}

void TableProfile::start()
{
    stop();
    ++m_generation;
    m_columns.clear();
    m_rows = -1;

    const QiTableInfo *info = nullptr;
    if (m_session && m_session->isOpen())
        for (const QiTableInfo &t : m_session->tableInfos())
            if (t.name == m_table)
                info = &t;
    if (!info) {
        emit columnsChanged();
        return;
    }
    for (const QiColumnInfo &c : info->columns)
        m_columns << QVariantMap{ { QStringLiteral("name"), c.name }, { QStringLiteral("type"), c.type },
                                  { QStringLiteral("kind"), kindOf(c.type) }, { QStringLiteral("done"), false } };
    emit columnsChanged();

    const Job job{ m_session->connectionName(), m_session->sqlName(m_table), m_session->dialect(), *info };
    const int generation = m_generation;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancel = m_cancel;
    QPointer<TableProfile> self(this);

    m_thread = QThread::create([job, generation, cancel, self] {
        const QString name = QStringLiteral("profile-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            // A connection of its own: Qt's connections belong to one thread.
            QSqlDatabase db = QSqlDatabase::cloneDatabase(job.connection, name);
            const bool open = db.open();
            QSqlQuery q(db);
            qint64 rows = -1;
            if (open && q.exec(QStringLiteral("SELECT COUNT(*) FROM ") + job.table) && q.next())
                rows = q.value(0).toLongLong();
            for (int i = 0; i < job.info.columns.size() && !cancel->load(); ++i) {
                QVariantMap column;
                if (rows < 0) {
                    const QiColumnInfo &c = job.info.columns.at(i);
                    column = { { QStringLiteral("name"), c.name }, { QStringLiteral("type"), c.type },
                               { QStringLiteral("kind"), kindOf(c.type) }, { QStringLiteral("done"), true },
                               { QStringLiteral("error"), open ? q.lastError().text() : db.lastError().text() } };
                } else {
                    column = profileColumn(q, job, job.info.columns.at(i), rows);
                }
                if (!cancel->load() && self)
                    QMetaObject::invokeMethod(self.data(), "deliver", Qt::QueuedConnection, Q_ARG(int, generation),
                                              Q_ARG(int, i), Q_ARG(QVariantMap, column), Q_ARG(qint64, rows));
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    });
    connect(m_thread, &QThread::finished, this, [this, generation] { threadDone(generation); }, Qt::QueuedConnection);
    m_thread->start(QThread::LowPriority);
    emit runningChanged();
}

void TableProfile::deliver(int generation, int index, const QVariantMap &column, qint64 rows)
{
    if (generation != m_generation || index < 0 || index >= m_columns.size())
        return;                             // from a run that was replaced
    m_rows = rows;
    m_columns[index] = column;
    emit columnsChanged();
    emit columnReady(index);
}

void TableProfile::threadDone(int generation)
{
    if (generation != m_generation || !m_thread)
        return;
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
    emit runningChanged();
    emit finished();
}
