#include "querybuilder.h"

#include "codegen.h"
#include "schemadesign.h"     // Migration::quoted

#include <QHash>
#include <algorithm>

namespace {

const QStringList &operatorList()
{
    static const QStringList ops = { QStringLiteral("="), QStringLiteral("≠"), QStringLiteral("<"),
                                     QStringLiteral("≤"), QStringLiteral(">"), QStringLiteral("≥"),
                                     QStringLiteral("contains"), QStringLiteral("starts with"),
                                     QStringLiteral("ends with"), QStringLiteral("is empty"),
                                     QStringLiteral("is not empty") };
    return ops;
}

bool takesValue(const QString &op)
{
    return op != QLatin1String("is empty") && op != QLatin1String("is not empty");
}

// Qivot takes one term as a QString and several as a QStringList; a braced
// one-element list would match both, so one term is passed plainly.
QString terms(const QStringList &quoted)
{
    return quoted.size() == 1 ? quoted.first()
                              : QStringLiteral("QStringList{ %1 }").arg(quoted.join(QStringLiteral(", ")));
}

QString cString(QString text)
{
    text.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    text.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + text + QLatin1Char('"');
}

} // namespace

QueryBuilder::QueryBuilder(QObject *parent)
    : QObject(parent)
{
}

void QueryBuilder::setSession(DatabaseSession *session)
{
    if (session == m_session)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    if (m_session)
        connect(m_session, &DatabaseSession::openChanged, this, &QueryBuilder::clear);
    emit sessionChanged();
    clear();
}

const QiTableInfo *QueryBuilder::info(const QString &table) const
{
    if (!m_session)
        return nullptr;
    for (const QiTableInfo &t : m_session->tableInfos())
        if (t.name == table)
            return &t;
    return nullptr;
}

QStringList QueryBuilder::tables() const
{
    QStringList out;
    if (!m_from.isEmpty())
        out << m_from;
    for (const Join &j : m_joins)
        out << j.table;
    return out;
}

QString QueryBuilder::q(const QString &identifier) const
{
    return Migration::quoted(identifier, m_session ? m_session->dialect() : QStringLiteral("sqlite"));
}

QString QueryBuilder::ref(const QString &table, const QString &column) const
{
    return q(table) + QLatin1Char('.') + q(column);
}

QString QueryBuilder::onClause(const Join &j) const
{
    QStringList parts;
    for (int i = 0; i < j.columns.size() && i < j.toColumns.size(); ++i)
        parts << ref(j.table, j.columns.at(i)) + QStringLiteral(" = ") + ref(j.to, j.toColumns.at(i));
    return parts.join(QStringLiteral(" AND "));
}

// Joins the schema means: a foreign key from a table in the query to one
// that isn't, or from one that isn't to a table in the query.
QVector<QueryBuilder::Join> QueryBuilder::candidates() const
{
    QVector<Join> out;
    if (!m_session || m_from.isEmpty())
        return out;
    const QStringList in = tables();
    for (const QString &name : in) {
        const QiTableInfo *t = info(name);
        if (!t)
            continue;
        for (const QiForeignKeyInfo &fk : t->foreignKeys) {             // to the parent
            if (in.contains(fk.refTable) || !info(fk.refTable))
                continue;
            Join j;
            j.table = fk.refTable;
            j.to = name;
            j.columns = fk.refColumns;
            j.toColumns = fk.columns;
            out << j;
        }
    }
    for (const QiTableInfo &other : m_session->tableInfos()) {          // from a child
        if (in.contains(other.name) || other.kind != QiTableInfo::Table)
            continue;
        for (const QiForeignKeyInfo &fk : other.foreignKeys) {
            if (!in.contains(fk.refTable))
                continue;
            Join j;
            j.table = other.name;
            j.to = fk.refTable;
            j.columns = fk.columns;
            j.toColumns = fk.refColumns;
            out << j;
        }
    }
    return out;
}

QVariantList QueryBuilder::joins() const
{
    QVariantList out;
    for (const Join &j : m_joins)
        out << QVariantMap{ { QStringLiteral("table"), j.table }, { QStringLiteral("to"), j.to },
                            { QStringLiteral("on"), onClause(j) }, { QStringLiteral("left"), j.left } };
    return out;
}

QVariantList QueryBuilder::joinable() const
{
    QVariantList out;
    const QVector<Join> c = candidates();
    for (int i = 0; i < c.size(); ++i)
        out << QVariantMap{ { QStringLiteral("table"), c.at(i).table }, { QStringLiteral("to"), c.at(i).to },
                            { QStringLiteral("on"), onClause(c.at(i)) }, { QStringLiteral("index"), i } };
    return out;
}

QVariantList QueryBuilder::available() const
{
    QVariantList out;
    for (const QString &name : tables()) {
        const QiTableInfo *t = info(name);
        if (!t)
            continue;
        QVariantList cols;
        for (const QiColumnInfo &c : t->columns) {
            bool selected = false;
            for (const Column &s : m_columns)
                selected = selected || (s.table == name && s.column == c.name);
            cols << QVariantMap{ { QStringLiteral("name"), c.name }, { QStringLiteral("type"), c.type },
                                 { QStringLiteral("selected"), selected },
                                 { QStringLiteral("key"), t->primaryKey.contains(c.name) } };
        }
        out << QVariantMap{ { QStringLiteral("table"), name }, { QStringLiteral("columns"), cols } };
    }
    return out;
}

QString QueryBuilder::label(const Column &c) const
{
    return c.aggregate.isEmpty() ? c.table + QLatin1Char('.') + c.column
                                 : c.aggregate + QLatin1Char('_') + c.column;
}

QVariantList QueryBuilder::columns() const
{
    QVariantList out;
    for (const Column &c : m_columns)
        out << QVariantMap{ { QStringLiteral("table"), c.table }, { QStringLiteral("column"), c.column },
                            { QStringLiteral("aggregate"), c.aggregate }, { QStringLiteral("label"), label(c) } };
    return out;
}

QVariantList QueryBuilder::filters() const
{
    QVariantList out;
    for (const Filter &f : m_filters)
        out << QVariantMap{ { QStringLiteral("table"), f.table }, { QStringLiteral("column"), f.column },
                            { QStringLiteral("op"), f.op }, { QStringLiteral("value"), f.value },
                            { QStringLiteral("key"), f.table + QLatin1Char('.') + f.column },
                            { QStringLiteral("takesValue"), takesValue(f.op) } };
    return out;
}

QVariantList QueryBuilder::sorts() const
{
    QVariantList out;
    for (const Sort &s : m_sorts)
        out << QVariantMap{ { QStringLiteral("key"), s.key }, { QStringLiteral("desc"), s.desc } };
    return out;
}

QStringList QueryBuilder::sortKeys() const
{
    QStringList out;
    for (const Column &c : m_columns)
        if (!c.aggregate.isEmpty())
            out << label(c);
    for (const QVariant &t : available())
        for (const QVariant &c : t.toMap().value(QStringLiteral("columns")).toList())
            out << t.toMap().value(QStringLiteral("table")).toString() + QLatin1Char('.') + c.toMap().value(QStringLiteral("name")).toString();
    return out;
}

QStringList QueryBuilder::operators() const { return operatorList(); }

QStringList QueryBuilder::aggregates() const
{
    return { QString(), QStringLiteral("count"), QStringLiteral("sum"), QStringLiteral("avg"),
             QStringLiteral("min"), QStringLiteral("max") };
}

void QueryBuilder::setLimit(int limit)
{
    limit = qMax(0, limit);
    if (limit == m_limit)
        return;
    m_limit = limit;
    emit changed();
}

void QueryBuilder::setDistinct(bool distinct)
{
    if (distinct == m_distinct)
        return;
    m_distinct = distinct;
    emit changed();
}

void QueryBuilder::clear()
{
    m_from.clear();
    m_joins.clear();
    m_columns.clear();
    m_filters.clear();
    m_sorts.clear();
    m_limit = 100;
    m_distinct = false;
    emit changed();
}

void QueryBuilder::setFrom(const QString &table)
{
    clear();
    if (!info(table))
        return;
    m_from = table;
    emit changed();
}

void QueryBuilder::join(int index)
{
    const QVector<Join> c = candidates();
    if (index < 0 || index >= c.size())
        return;
    m_joins << c.at(index);
    emit changed();
}

void QueryBuilder::setJoinLeft(const QString &table, bool left)
{
    for (Join &j : m_joins)
        if (j.table == table && j.left != left) {
            j.left = left;
            emit changed();
        }
}

void QueryBuilder::removeTable(const QString &table)
{
    if (table == m_from) {
        clear();
        return;
    }
    // The table, and everything joined through it.
    QStringList gone{ table };
    for (bool more = true; more;) {
        more = false;
        for (const Join &j : m_joins)
            if (gone.contains(j.to) && !gone.contains(j.table)) {
                gone << j.table;
                more = true;
            }
    }
    auto uses = [&](const QString &t) { return gone.contains(t); };
    m_joins.erase(std::remove_if(m_joins.begin(), m_joins.end(), [&](const Join &j) { return uses(j.table); }), m_joins.end());
    m_columns.erase(std::remove_if(m_columns.begin(), m_columns.end(), [&](const Column &c) { return uses(c.table); }), m_columns.end());
    m_filters.erase(std::remove_if(m_filters.begin(), m_filters.end(), [&](const Filter &f) { return uses(f.table); }), m_filters.end());
    const QStringList keys = sortKeys();
    m_sorts.erase(std::remove_if(m_sorts.begin(), m_sorts.end(), [&](const Sort &s) { return !keys.contains(s.key); }), m_sorts.end());
    emit changed();
}

void QueryBuilder::toggleColumn(const QString &table, const QString &column)
{
    for (int i = 0; i < m_columns.size(); ++i)
        if (m_columns.at(i).table == table && m_columns.at(i).column == column) {
            removeColumn(i);
            return;
        }
    if (!tables().contains(table))
        return;
    m_columns << Column{ table, column, QString() };
    emit changed();
}

void QueryBuilder::setAggregate(int index, const QString &aggregate)
{
    if (index < 0 || index >= m_columns.size() || !aggregates().contains(aggregate))
        return;
    const QString was = label(m_columns.at(index));
    m_columns[index].aggregate = aggregate;
    for (Sort &s : m_sorts)                       // a sort on it follows it
        if (s.key == was)
            s.key = label(m_columns.at(index));
    emit changed();
}

void QueryBuilder::removeColumn(int index)
{
    if (index < 0 || index >= m_columns.size())
        return;
    const QString was = label(m_columns.at(index));
    m_columns.removeAt(index);
    if (!sortKeys().contains(was))
        m_sorts.erase(std::remove_if(m_sorts.begin(), m_sorts.end(), [&](const Sort &s) { return s.key == was; }), m_sorts.end());
    emit changed();
}

void QueryBuilder::addFilter(const QString &table, const QString &column)
{
    QString t = table, c = column;
    if (t.isEmpty() || c.isEmpty()) {
        const QiTableInfo *base = info(m_from);
        if (!base || base->columns.isEmpty())
            return;
        t = m_from;
        c = base->columns.first().name;
    }
    m_filters << Filter{ t, c, QStringLiteral("="), QString() };
    emit changed();
}

void QueryBuilder::updateFilter(int index, const QVariantMap &changes)
{
    if (index < 0 || index >= m_filters.size())
        return;
    Filter &f = m_filters[index];
    if (changes.contains(QStringLiteral("key"))) {
        const QString key = changes.value(QStringLiteral("key")).toString();
        f.table = key.section(QLatin1Char('.'), 0, -2);
        f.column = key.section(QLatin1Char('.'), -1);
    }
    if (changes.contains(QStringLiteral("table")))
        f.table = changes.value(QStringLiteral("table")).toString();
    if (changes.contains(QStringLiteral("column")))
        f.column = changes.value(QStringLiteral("column")).toString();
    if (changes.contains(QStringLiteral("op")) && operatorList().contains(changes.value(QStringLiteral("op")).toString()))
        f.op = changes.value(QStringLiteral("op")).toString();
    if (changes.contains(QStringLiteral("value")))
        f.value = changes.value(QStringLiteral("value")).toString();
    emit changed();
}

void QueryBuilder::removeFilter(int index)
{
    if (index < 0 || index >= m_filters.size())
        return;
    m_filters.removeAt(index);
    emit changed();
}

void QueryBuilder::addSort(const QString &key, bool desc)
{
    const QStringList keys = sortKeys();
    const QString k = key.isEmpty() ? keys.value(0) : key;
    if (k.isEmpty() || !keys.contains(k))
        return;
    m_sorts << Sort{ k, desc };
    emit changed();
}

void QueryBuilder::updateSort(int index, const QVariantMap &changes)
{
    if (index < 0 || index >= m_sorts.size())
        return;
    if (changes.contains(QStringLiteral("key")) && sortKeys().contains(changes.value(QStringLiteral("key")).toString()))
        m_sorts[index].key = changes.value(QStringLiteral("key")).toString();
    if (changes.contains(QStringLiteral("desc")))
        m_sorts[index].desc = changes.value(QStringLiteral("desc")).toBool();
    emit changed();
}

void QueryBuilder::removeSort(int index)
{
    if (index < 0 || index >= m_sorts.size())
        return;
    m_sorts.removeAt(index);
    emit changed();
}

// ---- SQL ----

QString QueryBuilder::literal(const Filter &f) const
{
    const QiTableInfo *t = info(f.table);
    const QiColumnInfo *c = t ? t->column(f.column) : nullptr;
    const QString cpp = c ? CodeGen::cppType(c->type) : QStringLiteral("QString");
    bool number = false;
    f.value.trimmed().toDouble(&number);
    if (number && (cpp == QLatin1String("int") || cpp == QLatin1String("qint64") || cpp == QLatin1String("double")))
        return f.value.trimmed();
    return QLatin1Char('\'') + QString(f.value).replace(QLatin1Char('\''), QLatin1String("''")) + QLatin1Char('\'');
}

QString QueryBuilder::condition(const Filter &f) const
{
    const QString col = ref(f.table, f.column);
    const QString esc = QString(f.value).replace(QLatin1Char('\''), QLatin1String("''"));
    if (f.op == QLatin1String("is empty"))     return col + QStringLiteral(" IS NULL");
    if (f.op == QLatin1String("is not empty")) return col + QStringLiteral(" IS NOT NULL");
    if (f.op == QLatin1String("contains"))     return col + QStringLiteral(" LIKE '%") + esc + QStringLiteral("%'");
    if (f.op == QLatin1String("starts with"))  return col + QStringLiteral(" LIKE '") + esc + QStringLiteral("%'");
    if (f.op == QLatin1String("ends with"))    return col + QStringLiteral(" LIKE '%") + esc + QLatin1Char('\'');
    static const QHash<QString, QString> sqlOps = { { QStringLiteral("≠"), QStringLiteral("<>") },
                                                    { QStringLiteral("≤"), QStringLiteral("<=") },
                                                    { QStringLiteral("≥"), QStringLiteral(">=") } };
    return col + QLatin1Char(' ') + sqlOps.value(f.op, f.op) + QLatin1Char(' ') + literal(f);
}

QString QueryBuilder::sql() const
{
    if (m_from.isEmpty() || !m_session)
        return QString();
    const bool mssql = m_session->dialect() == QLatin1String("sqlserver");
    const bool grouped = std::any_of(m_columns.begin(), m_columns.end(), [](const Column &c) { return !c.aggregate.isEmpty(); });

    QStringList select;
    for (const Column &c : m_columns) {
        if (c.aggregate.isEmpty())
            select << ref(c.table, c.column);
        else
            select << QStringLiteral("%1(%2) AS %3").arg(c.aggregate.toUpper(), ref(c.table, c.column), q(label(c)));
    }
    if (select.isEmpty())
        select << (m_joins.isEmpty() ? QStringLiteral("*") : q(m_from) + QStringLiteral(".*"));

    QString head = QStringLiteral("SELECT ");
    if (m_distinct)
        head += QStringLiteral("DISTINCT ");
    if (mssql && m_limit > 0)
        head += QStringLiteral("TOP %1 ").arg(m_limit);
    QStringList lines{ head + select.join(QStringLiteral(", ")), QStringLiteral("FROM ") + q(m_from) };
    for (const Join &j : m_joins)
        lines << QStringLiteral("%1 %2 ON %3").arg(j.left ? QStringLiteral("LEFT JOIN") : QStringLiteral("JOIN"), q(j.table), onClause(j));
    QStringList where;
    for (const Filter &f : m_filters)
        if (!takesValue(f.op) || !f.value.isEmpty())
            where << condition(f);
    if (!where.isEmpty())
        lines << QStringLiteral("WHERE ") + where.join(QStringLiteral("\n  AND "));
    if (grouped) {
        QStringList groupBy;
        for (const Column &c : m_columns)
            if (c.aggregate.isEmpty())
                groupBy << ref(c.table, c.column);
        if (!groupBy.isEmpty())
            lines << QStringLiteral("GROUP BY ") + groupBy.join(QStringLiteral(", "));
    }
    QStringList order;
    for (const Sort &s : m_sorts) {
        const bool aggregate = !s.key.contains(QLatin1Char('.')) || std::any_of(m_columns.begin(), m_columns.end(), [&](const Column &c) {
                                   return !c.aggregate.isEmpty() && label(c) == s.key; });
        const QString term = aggregate ? q(s.key) : ref(s.key.section(QLatin1Char('.'), 0, -2), s.key.section(QLatin1Char('.'), -1));
        order << term + (s.desc ? QStringLiteral(" DESC") : QString());
    }
    if (!order.isEmpty())
        lines << QStringLiteral("ORDER BY ") + order.join(QStringLiteral(", "));
    if (!mssql && m_limit > 0)
        lines << QStringLiteral("LIMIT %1").arg(m_limit);
    return lines.join(QLatin1Char('\n')) + QLatin1Char(';');
}

// ---- C++ ----

QString QueryBuilder::className(const QString &table) const
{
    if (!m_session)
        return table;
    const QString name = CodeGen(m_session->tableInfos(), m_session->dialect()).className(table);
    return name.isEmpty() ? table : name;
}

QString QueryBuilder::cpp() const
{
    if (m_from.isEmpty() || !m_session)
        return QString();
    const bool grouped = std::any_of(m_columns.begin(), m_columns.end(), [](const Column &c) { return !c.aggregate.isEmpty(); });
    const bool rows = m_columns.isEmpty() && m_joins.isEmpty();     // whole rows of one table: typed models
    // Qivot writes names as given; with joins they need the table in front.
    auto name = [&](const QString &table, const QString &column) {
        return m_joins.isEmpty() ? column : table + QLatin1Char('.') + column;
    };

    QStringList chain;
    if (!rows) {
        QStringList fields;
        for (const Column &c : m_columns)
            fields << cString(c.aggregate.isEmpty() ? name(c.table, c.column)
                                                    : QStringLiteral("%1(%2)").arg(c.aggregate.toUpper(), name(c.table, c.column)));
        if (fields.isEmpty())
            fields << cString(m_from + QStringLiteral(".*"));
        chain << QStringLiteral(".select(%1)").arg(terms(fields));
    }
    if (m_distinct)
        chain << QStringLiteral(".distinct()");
    for (const Join &j : m_joins) {
        QStringList on;
        for (int i = 0; i < j.columns.size() && i < j.toColumns.size(); ++i)
            on << QStringLiteral("QiWhere(%1) == QiWhere(%2)").arg(cString(j.table + QLatin1Char('.') + j.columns.at(i)),
                                                                    cString(j.to + QLatin1Char('.') + j.toColumns.at(i)));
        chain << QStringLiteral(".join(QiJoin<%1>(%2%3))").arg(className(j.table), on.join(QStringLiteral(" && ")),
                                                               j.left ? QStringLiteral(", QiBaseJoin::Left") : QString());
    }
    for (const Filter &f : m_filters) {
        if (takesValue(f.op) && f.value.isEmpty())
            continue;
        const QString w = QStringLiteral("QiWhere(%1)").arg(cString(name(f.table, f.column)));
        const QString lit = literal(f);
        const QString value = lit.startsWith(QLatin1Char('\'')) ? cString(f.value) : lit;
        QString expr;
        if (f.op == QLatin1String("is empty"))          expr = w + QStringLiteral(".is(QVariant())");
        else if (f.op == QLatin1String("is not empty")) expr = w + QStringLiteral(".isNot(QVariant())");
        else if (f.op == QLatin1String("contains"))     expr = w + QStringLiteral(".like(%1)").arg(cString(QLatin1Char('%') + f.value + QLatin1Char('%')));
        else if (f.op == QLatin1String("starts with"))  expr = w + QStringLiteral(".like(%1)").arg(cString(f.value + QLatin1Char('%')));
        else if (f.op == QLatin1String("ends with"))    expr = w + QStringLiteral(".like(%1)").arg(cString(QLatin1Char('%') + f.value));
        else {
            static const QHash<QString, QString> ops = { { QStringLiteral("="), QStringLiteral("==") }, { QStringLiteral("≠"), QStringLiteral("!=") },
                                                         { QStringLiteral("≤"), QStringLiteral("<=") }, { QStringLiteral("≥"), QStringLiteral(">=") } };
            expr = QStringLiteral("%1 %2 %3").arg(w, ops.value(f.op, f.op), value);
        }
        chain << QStringLiteral(".filter(%1)").arg(expr);
    }
    if (grouped) {
        QStringList groupBy;
        for (const Column &c : m_columns)
            if (c.aggregate.isEmpty())
                groupBy << cString(name(c.table, c.column));
        if (!groupBy.isEmpty())
            chain << QStringLiteral(".groupBy(%1)").arg(terms(groupBy));
    }
    if (!m_sorts.isEmpty()) {
        QStringList order;
        for (const Sort &s : m_sorts) {
            QString term = s.key;
            for (const Column &c : m_columns)          // an aggregate sorts by its expression
                if (!c.aggregate.isEmpty() && label(c) == s.key)
                    term = QStringLiteral("%1(%2)").arg(c.aggregate.toUpper(), name(c.table, c.column));
            if (term == s.key && s.key.contains(QLatin1Char('.')))
                term = name(s.key.section(QLatin1Char('.'), 0, -2), s.key.section(QLatin1Char('.'), -1));
            order << cString(term + (s.desc ? QStringLiteral(" DESC") : QString()));
        }
        chain << QStringLiteral(".orderBy(%1)").arg(terms(order));
    }
    if (m_limit > 0)
        chain << QStringLiteral(".limit(%1)").arg(m_limit);

    const QString model = className(m_from);
    QString out;
    if (rows) {
        out = QStringLiteral("QiList<%1> rows = %1::objects()").arg(model);
        for (const QString &c : chain)
            out += QStringLiteral("\n    ") + c;
        out += QStringLiteral("\n    .all();\n\nfor (int i = 0; i < rows.size(); ++i)\n    qDebug() << *rows.at(i);\n");
    } else {
        out = QStringLiteral("auto query = %1::objects()").arg(model);
        for (const QString &c : chain)
            out += QStringLiteral("\n    ") + c;
        out += QStringLiteral(";\n\nif (query.exec()) {\n    while (query.next())\n        qDebug()");
        const int n = m_columns.isEmpty() ? 1 : m_columns.size();
        for (int i = 0; i < n; ++i)
            out += QStringLiteral(" << query.value(%1)").arg(i);
        out += QStringLiteral(";\n}\n");
    }
    return out;
}
