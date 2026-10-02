#include "querymodel.h"
#include "datatransfer.h"
#include "cellformat.h"

#include <QElapsedTimer>
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

void QueryModel::clear()
{
    beginResetModel();
    m_columns.clear();
    m_rows.clear();
    m_truncated = false;
    m_elapsed = 0;
    m_rowsAffected = -1;
    m_error.clear();
    m_notice.clear();
    m_sql.clear();
    endResetModel();
    emit finished();
}

bool QueryModel::run(const QString &text)
{
    QString sql = text.trimmed();
    while (sql.endsWith(QLatin1Char(';')))
        sql.chop(1);

    beginResetModel();
    m_columns.clear();
    m_rows.clear();
    m_truncated = false;
    m_rowsAffected = -1;
    m_error.clear();
    m_notice.clear();
    m_sql = sql;

    if (!m_session || !m_session->isOpen())
        m_error = tr("Open a database first.");
    else if (sql.isEmpty())
        m_error = tr("Type a query to run.");

    QElapsedTimer timer;
    timer.start();
    if (m_error.isEmpty()) {
        QSqlDatabase db = QSqlDatabase::database(m_session->connectionName(), false);
        // SQL Server has no read-only session, so run inside a transaction that
        // is always rolled back: a SELECT is unaffected, anything else is undone.
        const bool sandbox = m_session->dialect() == QLatin1String("sqlserver");
        if (sandbox)
            db.transaction();
        {
            QSqlQuery q(db);
            q.setForwardOnly(true);
            if (!q.exec(sql)) {
                m_error = q.lastError().text().trimmed();
                if (m_error.isEmpty())
                    m_error = tr("The query failed.");
            } else if (q.isSelect()) {
                const QSqlRecord rec = q.record();
                for (int i = 0; i < rec.count(); ++i)
                    m_columns << Column{ rec.fieldName(i), fieldTypeName(rec.field(i)) };
                while (q.next()) {
                    if (m_rows.size() >= MaxRows) {
                        m_truncated = true;
                        break;
                    }
                    QVariantList row;
                    row.reserve(m_columns.size());
                    for (int i = 0; i < m_columns.size(); ++i)
                        row << q.value(i);
                    m_rows << row;
                }
            } else {
                m_rowsAffected = q.numRowsAffected();
            }
        }
        if (sandbox) {
            db.rollback();
            if (m_error.isEmpty() && m_columns.isEmpty())
                m_notice = tr("Rolled back: on SQL Server, Studio undoes every statement, so nothing was changed.");
        }
    }
    m_elapsed = timer.elapsed();
    endResetModel();
    emit finished();
    return m_error.isEmpty();
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
