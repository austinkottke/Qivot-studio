#include "rowsmodel.h"
#include "cellformat.h"
#include "datatransfer.h"

#include <QSqlDriver>
#include <QSqlField>
#include <QRegularExpression>
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
    if (m_session) {
        connect(m_session, &DatabaseSession::openChanged, this, &RowsModel::reload);
        connect(m_session, &DatabaseSession::changesAllowedChanged, this, &RowsModel::editableChanged);
    }
    emit sessionChanged();
    reload();
}

void RowsModel::setTable(const QString &table)
{
    if (table == m_table)
        return;
    m_table = table;
    dropPending();
    if (!m_undo.isEmpty()) {
        m_undo.clear();
        m_savedCount = 0;
        emit undoChanged();
    }
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
                             c.value(QStringLiteral("primaryKey")).toBool(),
                             c.value(QStringLiteral("autoIncrement")).toBool() };
    }
    m_primaryKey = info.value(QStringLiteral("primaryKey")).toStringList();
    m_isTable = info.value(QStringLiteral("kind")).toString() == QLatin1String("table");
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
    emit editableChanged();

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
    if (row < 0 || row >= m_matching || !m_session)
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
    for (int i = 0; i < m_columns.size(); ++i)
        out.insert(m_columns.at(i).name, data(index(row, i), RawRole));
    return out;
}

// ---------------------------------------------------------------------------

int RowsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(qMin<qint64>(m_matching + m_inserts.size(), std::numeric_limits<int>::max()));
}

int RowsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_columns.size();
}

QVariant RowsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.column() >= m_columns.size())
        return QVariant();
    QVariant v;
    bool edited = false, deleted = false;
    const bool inserted = index.row() >= m_matching;
    if (inserted) {
        const QHash<int, QVariant> &row = m_inserts.at(int(index.row() - m_matching));
        if (role == Qt::DisplayRole && !row.contains(index.column()))
            return QStringLiteral("default");
        v = row.value(index.column());
        edited = row.contains(index.column());
    } else {
        const QVariantList &values = rowValues(index.row());
        if (index.column() >= values.size())
            return QVariant();
        v = values.at(index.column());
        if (!m_changes.isEmpty()) {
            const auto it = m_changes.constFind(keyString(keyOf(values)));
            if (it != m_changes.constEnd()) {
                deleted = it->deleted;
                if (it->values.contains(index.column())) {
                    v = it->values.value(index.column());
                    edited = true;
                }
            }
        }
    }

    switch (role) {
    case EditedRole:
        return edited;
    case DeletedRole:
        return deleted;
    case InsertedRole:
        return inserted;
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
        { EditedRole,      "edited" },
        { DeletedRole,     "deleted" },
        { InsertedRole,    "inserted" },
    };
}

// ---------------------------------------------------------------------------
//  Editing
// ---------------------------------------------------------------------------

bool RowsModel::editable() const
{
    return notEditableReason().isEmpty();
}

QString RowsModel::notEditableReason() const
{
    if (!m_session || !m_session->isOpen() || m_table.isEmpty())
        return tr("No table.");
    if (!m_session->changesAllowed())
        return tr("Read-only: allow changes to edit rows.");
    if (!m_isTable)
        return tr("%1 is a view: its rows come from other tables.").arg(m_table);
    if (m_primaryKey.isEmpty())
        return tr("%1 has no primary key, so a row can't be told apart from an identical one.").arg(m_table);
    return QString();
}

QVariantList RowsModel::keyOf(const QVariantList &row) const
{
    QVariantList key;
    for (const QString &k : m_primaryKey)
        for (int i = 0; i < m_columns.size(); ++i)
            if (m_columns.at(i).name == k)
                key << row.value(i);
    return key;
}

QString RowsModel::keyString(const QVariantList &key)
{
    QStringList parts;
    for (const QVariant &v : key)
        parts << (v.isNull() ? QStringLiteral("\u0000null") : v.toString());
    return parts.join(QChar(0x1f));
}

void RowsModel::dropPending()
{
    const bool had = pendingCount() > 0;
    m_changes.clear();
    m_inserts.clear();
    if (had)
        emit pendingChanged();
}

bool RowsModel::setCell(int row, int column, const QVariant &value)
{
    if (!editable() || column < 0 || column >= m_columns.size() || row < 0 || row >= rowCount())
        return false;
    const QVariant v = value.isNull() || !value.isValid() ? QVariant() : QVariant(value.toString());
    if (row >= m_matching) {
        m_inserts[int(row - m_matching)].insert(column, v);
    } else {
        const QVariantList &values = rowValues(row);
        const QVariantList key = keyOf(values);
        const QString ks = keyString(key);
        Change &c = m_changes[ks];
        c.key = key;
        const QVariant &original = values.value(column);
        const bool same = (original.isNull() && v.isNull())
                          || (!original.isNull() && !v.isNull() && CellFormat::display(original) == v.toString());
        if (same)
            c.values.remove(column);
        else
            c.values.insert(column, v);
        if (c.values.isEmpty() && !c.deleted)
            m_changes.remove(ks);
    }
    emit dataChanged(index(row, 0), index(row, m_columns.size() - 1));
    emit pendingChanged();
    return true;
}

void RowsModel::toggleDelete(int row)
{
    if (!editable() || row < 0 || row >= rowCount())
        return;
    if (row >= m_matching) {
        const int i = int(row - m_matching);
        beginRemoveRows(QModelIndex(), row, row);
        m_inserts.remove(i);
        endRemoveRows();
    } else {
        const QVariantList key = keyOf(rowValues(row));
        const QString ks = keyString(key);
        Change &c = m_changes[ks];
        c.key = key;
        c.deleted = !c.deleted;
        if (c.values.isEmpty() && !c.deleted)
            m_changes.remove(ks);
        emit dataChanged(index(row, 0), index(row, m_columns.size() - 1));
    }
    emit pendingChanged();
}

int RowsModel::addRow()
{
    if (!editable())
        return -1;
    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    m_inserts.append(QHash<int, QVariant>());
    endInsertRows();
    emit pendingChanged();
    return row;
}

void RowsModel::discardChanges()
{
    if (!pendingCount())
        return;
    beginResetModel();
    m_changes.clear();
    m_inserts.clear();
    endResetModel();
    emit pendingChanged();
}

QString RowsModel::literal(const QVariant &v, int column) const
{
    if (v.isNull())
        return QStringLiteral("NULL");
    // Numbers as numbers: a key read from the table, or one typed into a numeric column.
    if (CellFormat::isNumber(v))
        return v.toString();
    static const QRegularExpression numericType(QStringLiteral("INT|REAL|FLOA|DOUB|NUM|DEC|MONEY"),
                                                QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression number(QStringLiteral("^-?(\\d+\\.?\\d*|\\.\\d+)([eE][-+]?\\d+)?$"));
    if (numericType.match(m_columns.at(column).type).hasMatch() && number.match(v.toString()).hasMatch())
        return v.toString();
    QSqlDatabase db = QSqlDatabase::database(m_session->connectionName(), false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QSqlField field(m_columns.at(column).name, QMetaType(QMetaType::QString));
#else
    QSqlField field(m_columns.at(column).name, QVariant::String);
#endif
    field.setValue(v.toString());
    return db.driver()->formatValue(field);
}

QString RowsModel::keyCondition(const QVariantList &key, QVariantList *binds, bool literals) const
{
    QStringList parts;
    for (int k = 0; k < m_primaryKey.size(); ++k) {
        int column = -1;
        for (int i = 0; i < m_columns.size(); ++i)
            if (m_columns.at(i).name == m_primaryKey.at(k))
                column = i;
        const QVariant v = key.value(k);
        if (literals) {
            parts << quoted(m_primaryKey.at(k)) + QStringLiteral(" = ") + literal(v, column);
        } else {
            parts << quoted(m_primaryKey.at(k)) + QStringLiteral(" = ?");
            *binds << v;
        }
    }
    return parts.join(QStringLiteral(" AND "));
}

QStringList RowsModel::pendingSql() const
{
    QStringList out;
    if (!m_session || !m_session->isOpen())
        return out;
    const QString table = fromClause();
    QVariantList none;
    for (const Change &c : m_changes) {
        if (c.deleted) {
            out << QStringLiteral("DELETE FROM %1 WHERE %2;").arg(table, keyCondition(c.key, &none, true));
            continue;
        }
        QStringList sets;
        for (auto it = c.values.constBegin(); it != c.values.constEnd(); ++it)
            sets << quoted(m_columns.at(it.key()).name) + QStringLiteral(" = ") + literal(it.value(), it.key());
        out << QStringLiteral("UPDATE %1 SET %2 WHERE %3;").arg(table, sets.join(QStringLiteral(", ")),
                                                              keyCondition(c.key, &none, true));
    }
    for (const QHash<int, QVariant> &row : m_inserts) {
        QList<int> cols = row.keys();
        std::sort(cols.begin(), cols.end());
        if (cols.isEmpty()) {
            out << (m_session->dialect() == QLatin1String("mysql")
                        ? QStringLiteral("INSERT INTO %1 () VALUES ();").arg(table)
                        : QStringLiteral("INSERT INTO %1 DEFAULT VALUES;").arg(table));
            continue;
        }
        QStringList names, values;
        for (int c : cols) {
            names << quoted(m_columns.at(c).name);
            values << literal(row.value(c), c);
        }
        out << QStringLiteral("INSERT INTO %1 (%2) VALUES (%3);").arg(table, names.join(QStringLiteral(", ")),
                                                                    values.join(QStringLiteral(", ")));
    }
    return out;
}

bool RowsModel::save()
{
    if (!pendingCount())
        return true;
    if (!editable()) {
        setError(notEditableReason());
        return false;
    }
    QSqlDatabase db = m_session->writeDatabase();
    const QString table = fromClause();
    const QString dialect = m_session->dialect();
    QStringList allCols;
    for (const Column &c : m_columns)
        allCols << quoted(c.name);
    int keyColumn = -1;                          // a single-column key, for new rows' ids
    if (m_primaryKey.size() == 1)
        for (int i = 0; i < m_columns.size(); ++i)
            if (m_columns.at(i).name == m_primaryKey.first())
                keyColumn = i;
    bool identity = false;                       // SQL Server: putting a deleted row back needs IDENTITY_INSERT
    for (const Column &c : m_columns)
        identity = identity || c.autoIncrement;

    if (!db.transaction()) {
        setError(db.lastError().text());
        return false;
    }
    QSqlQuery q(db);
    QVector<Inverse> undo;
    auto exec = [&](const QString &sql, const QVariantList &binds) {
        q.prepare(sql);
        for (const QVariant &b : binds)
            q.addBindValue(b);
        return q.exec();
    };
    auto failed = [&](const QString &what) {
        setError(tr("Nothing was saved: %1 failed — %2").arg(what, q.lastError().text()));
        return false;
    };
    // A row as it is now, by key (on the writing connection, inside the transaction).
    auto current = [&](const QVariantList &key, QVariantList *values) {
        QVariantList binds;
        if (!exec(QStringLiteral("SELECT %1 FROM %2 WHERE %3").arg(allCols.join(QStringLiteral(", ")), table,
                                                                  keyCondition(key, &binds, false)), binds) || !q.next())
            return false;
        values->clear();
        for (int i = 0; i < m_columns.size(); ++i)
            *values << q.value(i);
        q.finish();
        return true;
    };
    // "col = ?" for each, or "col IS NULL": how undo makes sure the row is still as saved.
    auto matches = [&](const QHash<int, QVariant> &values, QVariantList *binds) {
        QStringList parts;
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            if (it.value().isNull()) {
                parts << quoted(m_columns.at(it.key()).name) + QStringLiteral(" IS NULL");
            } else {
                parts << quoted(m_columns.at(it.key()).name) + QStringLiteral(" = ?");
                *binds << it.value();
            }
        }
        return parts;
    };

    bool ok = true;
    // Deletes first (a new row may reuse a key), then updates, then new rows.
    for (const Change &c : std::as_const(m_changes)) {
        if (!ok || !c.deleted)
            continue;
        QVariantList old;
        if (!current(c.key, &old)) { ok = failed(tr("finding a row to delete")); break; }
        QVariantList binds;
        const QString where = keyCondition(c.key, &binds, false);
        if (!exec(QStringLiteral("DELETE FROM %1 WHERE %2").arg(table, where), binds)) { ok = failed(tr("deleting a row")); break; }
        QStringList marks;
        for (int i = 0; i < m_columns.size(); ++i)
            marks << QStringLiteral("?");
        Inverse inv{ QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)").arg(table, allCols.join(QStringLiteral(", ")),
                                                                          marks.join(QStringLiteral(", "))), old, {}, {} };
        if (dialect == QLatin1String("sqlserver") && identity) {
            inv.before = QStringLiteral("SET IDENTITY_INSERT %1 ON").arg(table);
            inv.after = QStringLiteral("SET IDENTITY_INSERT %1 OFF").arg(table);
        }
        undo << inv;
    }
    for (const Change &c : std::as_const(m_changes)) {
        if (!ok || c.deleted || c.values.isEmpty())
            continue;
        QVariantList old;
        if (!current(c.key, &old)) { ok = failed(tr("finding a row to change")); break; }
        QStringList sets;
        QVariantList binds;
        for (auto it = c.values.constBegin(); it != c.values.constEnd(); ++it) {
            sets << quoted(m_columns.at(it.key()).name) + QStringLiteral(" = ?");
            binds << it.value();
        }
        const QString where = keyCondition(c.key, &binds, false);
        if (!exec(QStringLiteral("UPDATE %1 SET %2 WHERE %3").arg(table, sets.join(QStringLiteral(", ")), where), binds)) {
            ok = failed(tr("changing a row"));
            break;
        }
        // Undo: the old values back, where the row still has the new ones (its key may be among them).
        QVariantList newKey = c.key;
        for (int k = 0; k < m_primaryKey.size(); ++k)
            for (auto it = c.values.constBegin(); it != c.values.constEnd(); ++it)
                if (m_columns.at(it.key()).name == m_primaryKey.at(k))
                    newKey[k] = it.value();
        QStringList restore;
        QVariantList ib;
        for (auto it = c.values.constBegin(); it != c.values.constEnd(); ++it) {
            restore << quoted(m_columns.at(it.key()).name) + QStringLiteral(" = ?");
            ib << old.value(it.key());
        }
        QStringList cond = { keyCondition(newKey, &ib, false) };
        cond << matches(c.values, &ib);
        undo << Inverse{ QStringLiteral("UPDATE %1 SET %2 WHERE %3").arg(table, restore.join(QStringLiteral(", ")),
                                                                        cond.join(QStringLiteral(" AND "))), ib, {}, {} };
    }
    for (int r = 0; ok && r < m_inserts.size(); ++r) {
        const QHash<int, QVariant> &row = m_inserts.at(r);
        QList<int> cols = row.keys();
        std::sort(cols.begin(), cols.end());
        QStringList names, marks;
        QVariantList binds;
        for (int c : cols) {
            names << quoted(m_columns.at(c).name);
            marks << QStringLiteral("?");
            binds << row.value(c);
        }
        // The new row's key: given, or the one the database picked (asked for in its own way).
        const bool keyGiven = keyColumn >= 0 && row.contains(keyColumn) && !row.value(keyColumn).isNull();
        const QString keyName = keyColumn >= 0 ? quoted(m_columns.at(keyColumn).name) : QString();
        const bool returning = keyColumn >= 0 && !keyGiven && dialect == QLatin1String("postgres");
        const bool output = keyColumn >= 0 && !keyGiven && dialect == QLatin1String("sqlserver");
        QString sql;
        if (cols.isEmpty())
            sql = dialect == QLatin1String("mysql") ? QStringLiteral("INSERT INTO %1 () VALUES ()").arg(table)
                : output ? QStringLiteral("INSERT INTO %1 OUTPUT INSERTED.%2 DEFAULT VALUES").arg(table, keyName)
                         : QStringLiteral("INSERT INTO %1 DEFAULT VALUES").arg(table);
        else
            sql = output ? QStringLiteral("INSERT INTO %1 (%2) OUTPUT INSERTED.%3 VALUES (%4)")
                               .arg(table, names.join(QStringLiteral(", ")), keyName, marks.join(QStringLiteral(", ")))
                         : QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
                               .arg(table, names.join(QStringLiteral(", ")), marks.join(QStringLiteral(", ")));
        if (returning)
            sql += QStringLiteral(" RETURNING ") + keyName;
        if (!exec(sql, binds)) { ok = failed(tr("adding row %1").arg(r + 1)); break; }
        QVariant id;
        if (keyGiven)
            id = row.value(keyColumn);
        else if ((returning || output) && q.next())
            id = q.value(0);
        else if (keyColumn >= 0)
            id = q.lastInsertId();
        q.finish();
        if (!id.isValid() || id.isNull()) {
            undo << Inverse{ {}, {}, {}, {} };    // can't find it again: this save can't be undone
            continue;
        }
        QVariantList ib;
        QStringList cond = { keyCondition({ id }, &ib, false) };
        undo << Inverse{ QStringLiteral("DELETE FROM %1 WHERE %2").arg(table, cond.join(QStringLiteral(" AND "))), ib, {}, {} };
    }
    if (ok && !db.commit()) {
        ok = false;
        setError(tr("Nothing was saved: %1").arg(db.lastError().text()));
    }
    if (!ok) {
        db.rollback();
        return false;
    }
    const int saved = pendingCount();
    m_changes.clear();
    m_inserts.clear();
    // Undone backwards; a step that can't be (an unknown new id) means the whole save can't.
    m_undo.clear();
    m_undoNote.clear();
    bool complete = true;
    for (const Inverse &i : undo)
        complete = complete && !i.sql.isEmpty();
    if (!complete)
        m_undoNote = tr("A new row's key couldn't be read back, so this save can't be undone.");
    // Deleting here may have deleted or changed rows elsewhere (ON DELETE CASCADE / SET NULL),
    // which undo couldn't put back.
    bool deleted = false;
    for (const Inverse &i : std::as_const(undo))
        deleted = deleted || i.sql.startsWith(QLatin1String("INSERT"));
    if (deleted && complete) {
        for (const QiTableInfo &t : m_session->tableInfos())
            for (const QiForeignKeyInfo &fk : t.foreignKeys) {
                const QString action = fk.onDelete.toUpper();
                if (complete && fk.refTable == m_table && (action == QLatin1String("CASCADE") || action.startsWith(QLatin1String("SET")))) {
                    complete = false;
                    m_undoNote = tr("Deleting from %1 also changes %2 (ON DELETE %3), which undo couldn't put back, so this save can't be undone.")
                                     .arg(m_table, t.name, action);
                }
            }
    }
    if (complete)
        for (int i = undo.size() - 1; i >= 0; --i)
            m_undo << undo.at(i);
    m_undoTable = m_table;
    m_savedCount = complete ? saved : 0;
    setError(QString());
    emit pendingChanged();
    emit undoChanged();
    m_session->refresh();          // new row counts; reload() follows
    return true;
}

bool RowsModel::undoSave()
{
    if (!canUndo())
        return false;
    if (!m_session || !m_session->changesAllowed()) {
        setError(tr("Allow changes to undo the save."));
        return false;
    }
    QSqlDatabase db = m_session->writeDatabase();
    if (!db.transaction()) {
        setError(db.lastError().text());
        return false;
    }
    QSqlQuery q(db);
    for (const Inverse &i : std::as_const(m_undo)) {
        if (!i.before.isEmpty())
            q.exec(i.before);
        q.prepare(i.sql);
        for (const QVariant &b : i.binds)
            q.addBindValue(b);
        const bool ran = q.exec();
        const int affected = ran ? q.numRowsAffected() : -1;
        if (!i.after.isEmpty())
            QSqlQuery(db).exec(i.after);
        if (!ran || affected != 1) {
            setError(ran ? tr("Couldn't undo: some of the rows have changed since the save, so nothing was undone.")
                         : tr("Couldn't undo, so nothing was: %1").arg(q.lastError().text()));
            q = QSqlQuery();
            db.rollback();
            return false;
        }
    }
    q = QSqlQuery();
    if (!db.commit()) {
        db.rollback();
        setError(tr("Couldn't undo: %1").arg(db.lastError().text()));
        return false;
    }
    m_undo.clear();
    m_savedCount = 0;
    m_undoNote.clear();
    setError(QString());
    emit undoChanged();
    m_session->refresh();
    return true;
}

QVariantMap RowsModel::exportTo(const QVariant &fileOrUrl, const QString &format)
{
    if (!m_session || !m_session->isOpen() || m_columns.isEmpty())
        return { { QStringLiteral("ok"), false }, { QStringLiteral("error"), tr("No table.") } };
    QStringList cols;
    for (const Column &c : m_columns)
        cols << quoted(c.name);
    QVariantList binds;
    QSqlQuery q(QSqlDatabase::database(m_session->connectionName(), false));
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT ") + cols.join(QStringLiteral(", ")) + QStringLiteral(" FROM ") + fromClause()
              + whereClause(&binds) + orderClause());
    for (const QVariant &b : binds)
        q.addBindValue(b);
    if (!q.exec())
        return { { QStringLiteral("ok"), false }, { QStringLiteral("error"), q.lastError().text() } };
    return DataTransfer::write(q, DataTransfer::localPath(fileOrUrl), format);
}
