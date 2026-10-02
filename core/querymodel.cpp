#include "querymodel.h"
#include "datatransfer.h"
#include "cellformat.h"

#include <QElapsedTimer>
#include <QUuid>
#include <QSqlError>
#include <QSqlField>
#include <QSqlQuery>
#include <QSqlRecord>

// The Qt type of a result column ("int", "QString", …), on Qt 5 and 6.
static QString fieldTypeName(const QSqlField &field)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QString::fromLatin1(field.metaType().name());
#else
    return QString::fromLatin1(QMetaType::typeName(field.type()));
#endif
}

QueryModel::QueryModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

QueryModel::~QueryModel()
{
    cancel();
    waitForBackground(5000);
}

void QueryModel::setSession(DatabaseSession *session)
{
    if (session == m_session)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    if (m_session)
        connect(m_session, &DatabaseSession::openChanged, this, &QueryModel::clear);
    emit sessionChanged();
    clear();
}

QVariantList QueryModel::columns() const
{
    QVariantList out;
    for (const Column &c : m_columns)
        out << QVariantMap{ { QStringLiteral("name"), c.name }, { QStringLiteral("type"), c.type } };
    return out;
}

void QueryModel::setRunning(bool running)
{
    if (running == m_running)
        return;
    m_running = running;
    emit runningChanged();
}

void QueryModel::clear()
{
    if (m_running) {
        cancel();
        m_notice.clear();
    }
    beginResetModel();
    m_columns.clear();
    m_rows.clear();
    m_truncated = false;
    m_elapsed = 0;
    m_rowsAffected = -1;
    m_error.clear();
    m_notice.clear();
    m_sql.clear();
    m_cancelled = false;
    endResetModel();
    emit finished();
}

// The statement as it will run (trailing semicolons off), or why it can't.
QString QueryModel::prepare(const QString &text, QString *error) const
{
    QString sql = text.trimmed();
    while (sql.endsWith(QLatin1Char(';')))
        sql.chop(1);
    if (!m_session || !m_session->isOpen())
        *error = tr("Open a database first.");
    else if (sql.isEmpty())
        *error = tr("Type a query to run.");
    return sql;
}

QueryModel::Outcome QueryModel::execute(QSqlDatabase db, const QString &sql, const QString &dialect,
                                        const std::atomic_bool *cancel)
{
    Outcome o;
    QElapsedTimer timer;
    timer.start();
    // SQL Server has no read-only session, so run inside a transaction that
    // is always rolled back: a SELECT is unaffected, anything else is undone.
    const bool sandbox = dialect == QLatin1String("sqlserver");
    if (sandbox)
        db.transaction();
    {
        QSqlQuery q(db);
        q.setForwardOnly(true);
        if (!q.exec(sql)) {
            o.error = q.lastError().text().trimmed();
            if (o.error.isEmpty())
                o.error = tr("The query failed.");
        } else if (q.isSelect()) {
            const QSqlRecord rec = q.record();
            for (int i = 0; i < rec.count(); ++i)
                o.columns << Column{ rec.fieldName(i), fieldTypeName(rec.field(i)) };
            while ((!cancel || !cancel->load()) && q.next()) {
                if (o.rows.size() >= MaxRows) {
                    o.truncated = true;
                    break;
                }
                QVariantList row;
                row.reserve(o.columns.size());
                for (int i = 0; i < o.columns.size(); ++i)
                    row << q.value(i);
                o.rows << row;
            }
        } else {
            o.rowsAffected = q.numRowsAffected();
        }
    }
    if (sandbox) {
        db.rollback();
        if (o.error.isEmpty() && o.columns.isEmpty())
            o.notice = tr("Rolled back: on SQL Server, Studio undoes every statement, so nothing was changed.");
    }
    o.elapsed = timer.elapsed();
    return o;
}

void QueryModel::apply(const Outcome &o, bool cancelled)
{
    beginResetModel();
    m_columns = o.columns;
    m_rows = o.rows;
    m_truncated = o.truncated;
    m_rowsAffected = o.rowsAffected;
    m_error = o.error;
    m_notice = o.notice;
    m_elapsed = o.elapsed;
    m_cancelled = cancelled;
    endResetModel();
    emit finished();
}

bool QueryModel::run(const QString &text)
{
    if (m_running)
        cancel();
    Outcome o;
    m_sql = prepare(text, &o.error);
    if (o.error.isEmpty())
        o = execute(QSqlDatabase::database(m_session->connectionName(), false), m_sql, m_session->dialect(), nullptr);
    apply(o);
    return m_error.isEmpty();
}

// How to ask for the worker session's id, so cancel() can name it to the server.
static QString backendIdQuery(const QString &dialect)
{
    if (dialect == QLatin1String("postgres"))  return QStringLiteral("SELECT pg_backend_pid()");
    if (dialect == QLatin1String("mysql"))     return QStringLiteral("SELECT CONNECTION_ID()");
    if (dialect == QLatin1String("sqlserver")) return QStringLiteral("SELECT @@SPID");
    return QString();
}

void QueryModel::start(const QString &text)
{
    if (m_running)
        cancel();
    QString why;
    m_sql = prepare(text, &why);
    if (!why.isEmpty()) {
        Outcome o;
        o.error = why;
        apply(o);
        return;
    }

    const int generation = ++m_generation;
    m_shared = std::make_shared<Shared>();
    const std::shared_ptr<Shared> shared = m_shared;
    const QString connection = m_session->connectionName();
    const QString dialect = m_session->dialect();
    const QString sql = m_sql;
    QPointer<QueryModel> self(this);

    QThread *thread = QThread::create([=] {
        const QString name = QStringLiteral("query-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            QString error;
            QSqlDatabase db = DatabaseSession::openReadOnlyClone(connection, name, &error);
            Outcome o;
            if (!db.isOpen()) {
                o.error = QueryModel::tr("Couldn't open a connection for the query: %1").arg(error);
            } else {
                const QString idQuery = backendIdQuery(dialect);
                if (!idQuery.isEmpty()) {
                    QSqlQuery q(db);
                    if (q.exec(idQuery) && q.next())
                        shared->backend.store(q.value(0).toLongLong());
                }
                o = execute(db, sql, dialect, &shared->cancel);
            }
            db.close();
            if (!shared->cancel.load() && self)
                QMetaObject::invokeMethod(self.data(), [self, generation, o] { if (self) self->deliver(generation, o); },
                                          Qt::QueuedConnection);
        }
        QSqlDatabase::removeDatabase(name);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_threads << thread;
    m_timer.start();
    setRunning(true);
    thread->start();
}

void QueryModel::deliver(int generation, const Outcome &outcome)
{
    if (generation != m_generation || !m_running)
        return;                                 // stopped, or replaced by another run
    m_threads.removeAll(QPointer<QThread>());
    setRunning(false);
    apply(outcome);
}

void QueryModel::cancel()
{
    if (!m_running)
        return;
    ++m_generation;                             // whatever it still delivers is dropped
    m_shared->cancel.store(true);
    // Ask the server to stop it, from the session's own connection: the worker
    // is busy waiting for its answer. (SQLite has no such request; reading rows
    // stops instead.)
    const qint64 backend = m_shared->backend.load();
    if (backend >= 0 && m_session && m_session->isOpen()) {
        const QString dialect = m_session->dialect();
        const QString stop = dialect == QLatin1String("postgres") ? QStringLiteral("SELECT pg_cancel_backend(%1)")
                           : dialect == QLatin1String("mysql")    ? QStringLiteral("KILL QUERY %1")
                           : dialect == QLatin1String("sqlserver") ? QStringLiteral("KILL %1")
                           : QString();
        if (!stop.isEmpty())
            QSqlQuery(QSqlDatabase::database(m_session->connectionName(), false)).exec(stop.arg(backend));
    }
    Outcome o;
    o.notice = tr("Stopped after %1.").arg(m_timer.elapsed() < 1000 ? tr("%1 ms").arg(m_timer.elapsed())
                                                                      : tr("%1 s").arg(m_timer.elapsed() / 1000.0, 0, 'f', 1));
    o.elapsed = m_timer.elapsed();
    setRunning(false);
    apply(o, true);
}

bool QueryModel::waitForBackground(int ms)
{
    QElapsedTimer timer;
    timer.start();
    for (const QPointer<QThread> &t : std::as_const(m_threads)) {
        if (!t)
            continue;
        const qint64 left = ms - timer.elapsed();
        if (left <= 0 || !t->wait(static_cast<unsigned long>(left)))
            return false;
    }
    m_threads.clear();
    return true;
}

QVariantMap QueryModel::rowAt(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_rows.size())
        return out;
    for (int i = 0; i < m_columns.size(); ++i)
        out.insert(m_columns.at(i).name, m_rows.at(row).value(i));
    return out;
}

int QueryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int QueryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_columns.size();
}

QVariant QueryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size() || index.column() >= m_columns.size())
        return QVariant();
    const QVariant &v = m_rows.at(index.row()).at(index.column());
    switch (role) {
    case Qt::DisplayRole: return CellFormat::display(v);
    case NullRole:        return v.isNull();
    case NumberRole:      return CellFormat::isNumber(v);
    case RawRole:         return v;
    default:              return QVariant();
    }
}

QVariant QueryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();
    if (orientation == Qt::Horizontal)
        return section < m_columns.size() ? m_columns.at(section).name : QVariant();
    return section + 1;
}

QHash<int, QByteArray> QueryModel::roleNames() const
{
    return {
        { Qt::DisplayRole, "display" },
        { NullRole,        "isNull" },
        { NumberRole,      "isNumber" },
        { RawRole,         "raw" },
    };
}

QVariantMap QueryModel::exportTo(const QVariant &fileOrUrl, const QString &format)
{
    if (!m_session || !m_session->isOpen() || m_sql.trimmed().isEmpty() || m_columns.isEmpty())
        return { { QStringLiteral("ok"), false }, { QStringLiteral("error"), tr("There's no result to export.") } };
    QSqlQuery q(QSqlDatabase::database(m_session->connectionName(), false));
    q.setForwardOnly(true);
    if (!q.exec(m_sql))
        return { { QStringLiteral("ok"), false }, { QStringLiteral("error"), q.lastError().text() } };
    return DataTransfer::write(q, DataTransfer::localPath(fileOrUrl), format);
}
