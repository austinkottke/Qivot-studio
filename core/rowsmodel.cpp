#include "rowsmodel.h"
#include "cellformat.h"

#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <limits>

namespace {
const QVariantList kNoRow;
}

RowsModel::RowsModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void RowsModel::setSession(DatabaseSession *session)
{
    if (session == m_session)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    if (m_session)
        connect(m_session, &DatabaseSession::openChanged, this, &RowsModel::reload);
    emit sessionChanged();
    reload();
}

void RowsModel::setTable(const QString &table)
{
    if (table == m_table)
        return;
    m_table = table;
    // A new table starts unsorted and unfiltered.
    m_sortColumn = -1;
    m_sortDescending = false;
    const bool hadFilter = !m_filter.isEmpty();
    m_filter.clear();
    emit tableChanged();
    emit sortChanged();
    if (hadFilter)
        emit filterChanged();
    reload();
}

void RowsModel::setFilter(const QString &filter)
{
    if (filter == m_filter)
        return;
    m_filter = filter;
    emit filterChanged();
    requery();
}

void RowsModel::sortBy(int column)
{
    if (column < 0 || column >= m_columns.size())
        return;
    if (column == m_sortColumn) {
        m_sortDescending = !m_sortDescending;
    } else {
        m_sortColumn = column;
        m_sortDescending = false;
    }
    emit sortChanged();
    requery();
}

void RowsModel::clearSort()
{
    if (m_sortColumn < 0)
        return;
    m_sortColumn = -1;
    m_sortDescending = false;
    emit sortChanged();
    requery();
}

QVariantList RowsModel::columns() const
{
    QVariantList out;
    for (const Column &c : m_columns)
        out << QVariantMap{ { QStringLiteral("name"), c.name },
                            { QStringLiteral("type"), c.type },
                            { QStringLiteral("primaryKey"), c.primaryKey } };
    return out;
}

void RowsModel::setError(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

QString RowsModel::quoted(const QString &identifier) const
{
    QSqlDatabase db = QSqlDatabase::database(m_session->connectionName(), false);
    return db.driver()->escapeIdentifier(identifier, QSqlDriver::FieldName);
}

// ---------------------------------------------------------------------------

void RowsModel::reload()
{
    beginResetModel();
    m_columns.clear();
    m_primaryKey.clear();
    m_pages.clear();
    m_recent.clear();
    m_total = m_matching = 0;

    const QVariantMap info = (m_session && m_session->isOpen() && !m_table.isEmpty())
                             ? m_session->table(m_table) : QVariantMap();
    for (const QVariant &v : info.value(QStringLiteral("columns")).toList()) {
        const QVariantMap c = v.toMap();
        m_columns << Column{ c.value(QStringLiteral("name")).toString(),
                             c.value(QStringLiteral("type")).toString(),
                             c.value(QStringLiteral("primaryKey")).toBool() };
    }
    m_primaryKey = info.value(QStringLiteral("primaryKey")).toStringList();
    if (m_sortColumn >= m_columns.size())
        m_sortColumn = -1;

    if (!m_columns.isEmpty()) {
        QSqlQuery q(QSqlDatabase::database(m_session->connectionName(), false));
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM ") + fromClause()) && q.next())
            m_total = q.value(0).toLongLong();
        else
            setError(q.lastError().text());
    }
    endResetModel();
    emit columnsChanged();

    requery();
}

void RowsModel::requery()
{
    beginResetModel();
    m_pages.clear();
    m_recent.clear();
    m_matching = m_total;

    if (!m_columns.isEmpty() && !m_filter.trimmed().isEmpty()) {
        QVariantList binds;
        QSqlQuery q(QSqlDatabase::database(m_session->connectionName(), false));
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM ") + fromClause() + whereClause(&binds));
        for (const QVariant &b : binds)
            q.addBindValue(b);
        if (q.exec() && q.next()) {
            m_matching = q.value(0).toLongLong();
            setError(QString());
        } else {
            m_matching = 0;
            setError(q.lastError().text());
        }
    } else {
        setError(QString());
    }
    endResetModel();
    emit countsChanged();
}

QString RowsModel::fromClause() const
{
    // Schema-qualified and quoted the way this database wants ("sales"."orders").
    const QString name = m_session->sqlName(m_table);
    return name.isEmpty() ? quoted(m_table) : name;
}

QString RowsModel::whereClause(QVariantList *binds) const
{
    const QString needle = m_filter.trimmed();
    if (needle.isEmpty())
        return QString();

    // Match the text anywhere in any column, ignoring case. '!' is the escape
    // character on every database: a backslash would mean different things
    // inside MySQL's and Postgres's string literals. SQL Server also treats
    // '[' as a wildcard, so it is escaped too.
    const QString dialect = m_session->dialect();
    QString escaped = needle.toLower();
    escaped.replace(QLatin1Char('!'), QLatin1String("!!"))
           .replace(QLatin1Char('%'), QLatin1String("!%"))
           .replace(QLatin1Char('_'), QLatin1String("!_"));
    if (dialect == QLatin1String("sqlserver"))
        escaped.replace(QLatin1Char('['), QLatin1String("!["));
    const QString pattern = QLatin1Char('%') + escaped + QLatin1Char('%');

    const QString asText = dialect == QLatin1String("mysql")     ? QStringLiteral("CHAR")
                         : dialect == QLatin1String("sqlserver") ? QStringLiteral("NVARCHAR(MAX)")
                         : dialect == QLatin1String("duckdb")    ? QStringLiteral("VARCHAR")
                         :                                         QStringLiteral("TEXT");
    QStringList parts;
    for (const Column &c : m_columns) {
        parts << QStringLiteral("LOWER(CAST(%1 AS %2)) LIKE ? ESCAPE '!'").arg(quoted(c.name), asText);
        *binds << pattern;
    }
    return QStringLiteral(" WHERE ") + parts.join(QStringLiteral(" OR "));
}

QString RowsModel::orderClause() const
{
    QStringList terms;
    if (m_sortColumn >= 0)
        terms << quoted(m_columns.at(m_sortColumn).name)
                 + (m_sortDescending ? QStringLiteral(" DESC") : QStringLiteral(" ASC"));
    // Break ties by the primary key, so pages don't shuffle rows between them.
    for (const QString &k : m_primaryKey)
        if (m_sortColumn < 0 || m_columns.at(m_sortColumn).name != k)
            terms << quoted(k);
    return terms.isEmpty() ? QString() : QStringLiteral(" ORDER BY ") + terms.join(QStringLiteral(", "));
}

const QVariantList &RowsModel::rowValues(int row) const
{
    if (row < 0 || row >= rowCount() || !m_session)
        return kNoRow;
    const int page = row / PageSize;

    auto it = m_pages.find(page);
    if (it == m_pages.end()) {
        QVector<QVariantList> rows;
        QVariantList binds;
        QSqlQuery q(QSqlDatabase::database(m_session->connectionName(), false));
        q.setForwardOnly(true);
        QStringList cols;
        for (const Column &c : m_columns)
            cols << quoted(c.name);
        // SQL Server pages with OFFSET ... FETCH, which needs an ORDER BY to attach to.
        QString order = orderClause();
        QString paging;
        if (m_session->dialect() == QLatin1String("sqlserver")) {
            if (order.isEmpty())
                order = QStringLiteral(" ORDER BY (SELECT NULL)");
            paging = QStringLiteral(" OFFSET %1 ROWS FETCH NEXT %2 ROWS ONLY").arg(qint64(page) * PageSize).arg(PageSize);
        } else {
            paging = QStringLiteral(" LIMIT %1 OFFSET %2").arg(PageSize).arg(qint64(page) * PageSize);
        }
        q.prepare(QStringLiteral("SELECT ") + cols.join(QStringLiteral(", "))
                  + QStringLiteral(" FROM ") + fromClause() + whereClause(&binds) + order + paging);
        for (const QVariant &b : binds)
            q.addBindValue(b);
        if (q.exec()) {
            rows.reserve(PageSize);
            while (q.next()) {
                QVariantList values;
                values.reserve(m_columns.size());
                for (int i = 0; i < m_columns.size(); ++i)
                    values << q.value(i);
                rows << values;
            }
        }

        // Keep the cache bounded: evict the page used longest ago.
        while (m_pages.size() >= MaxPages && !m_recent.isEmpty())
            m_pages.remove(m_recent.takeFirst());
        it = m_pages.insert(page, rows);
    }
    m_recent.removeOne(page);
    m_recent.append(page);

    const int offset = row - page * PageSize;
    return offset < it->size() ? it->at(offset) : kNoRow;
}

QVariantMap RowsModel::rowAt(int row)
{
    QVariantMap out;
    const QVariantList &values = rowValues(row);
    for (int i = 0; i < values.size() && i < m_columns.size(); ++i)
        out.insert(m_columns.at(i).name, values.at(i));
    return out;
}

// ---------------------------------------------------------------------------

int RowsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(qMin<qint64>(m_matching, std::numeric_limits<int>::max()));
}

int RowsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_columns.size();
}

QVariant RowsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();
    const QVariantList &values = rowValues(index.row());
    if (index.column() >= values.size())
        return QVariant();
    const QVariant &v = values.at(index.column());

    switch (role) {
    case RawRole:
        return v;
    case NullRole:
        return v.isNull();
    case NumberRole:
        return CellFormat::isNumber(v);
    case Qt::DisplayRole:
        return CellFormat::display(v);
    default:
        return QVariant();
    }
}

QVariant RowsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();
    if (orientation == Qt::Horizontal)
        return section < m_columns.size() ? m_columns.at(section).name : QVariant();
    return section + 1;
}

QHash<int, QByteArray> RowsModel::roleNames() const
{
    return {
        { Qt::DisplayRole, "display" },
        { NullRole,        "isNull" },
        { NumberRole,      "isNumber" },
        { RawRole,         "raw" },
    };
}
