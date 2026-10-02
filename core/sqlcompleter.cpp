#include "sqlcompleter.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace {

const QStringList kKeywords = {
    "SELECT", "FROM", "WHERE", "JOIN", "LEFT", "RIGHT", "INNER", "OUTER", "FULL", "CROSS", "ON", "USING",
    "AND", "OR", "NOT", "IN", "IS", "NULL", "LIKE", "BETWEEN", "EXISTS", "GROUP", "BY", "ORDER", "HAVING",
    "LIMIT", "OFFSET", "AS", "DISTINCT", "UNION", "ALL", "CASE", "WHEN", "THEN", "ELSE", "END", "ASC", "DESC",
    "WITH", "INSERT", "INTO", "VALUES", "UPDATE", "SET", "DELETE", "CREATE", "TABLE", "VIEW", "INDEX",
    "DROP", "ALTER", "ADD", "COLUMN", "PRIMARY", "KEY", "FOREIGN", "REFERENCES", "DEFAULT", "UNIQUE",
    "TRUE", "FALSE", "EXPLAIN", "OVER", "PARTITION", "WINDOW", "FETCH", "NEXT", "ROWS", "ONLY", "TOP" };
const QStringList kFunctions = {
    "COUNT", "SUM", "AVG", "MIN", "MAX", "COALESCE", "NULLIF", "CAST", "LOWER", "UPPER", "LENGTH", "TRIM",
    "SUBSTR", "SUBSTRING", "REPLACE", "ROUND", "ABS", "CONCAT", "NOW", "CURRENT_DATE", "CURRENT_TIMESTAMP",
    "DATE", "EXTRACT", "ROW_NUMBER", "RANK", "DENSE_RANK", "LAG", "LEAD", "STRING_AGG", "GROUP_CONCAT" };
// Words that can follow a table name and so aren't its alias.
const QSet<QString> kNotAliases = {
    "WHERE", "JOIN", "LEFT", "RIGHT", "INNER", "OUTER", "FULL", "CROSS", "ON", "USING", "GROUP", "ORDER",
    "HAVING", "LIMIT", "OFFSET", "UNION", "SET", "VALUES", "WINDOW", "FETCH", "AS", "NATURAL" };

bool identChar(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$'); }

QString unquote(QString s)
{
    s = s.section(QLatin1Char('.'), -1);                       // schema.table -> table
    if (s.size() >= 2 && (s.startsWith(QLatin1Char('"')) || s.startsWith(QLatin1Char('`')) || s.startsWith(QLatin1Char('['))))
        s = s.mid(1, s.size() - 2);
    return s;
}

} // namespace

void SqlCompleter::setSession(DatabaseSession *session)
{
    if (session == m_session)
        return;
    m_session = session;
    emit sessionChanged();
}

QVariantMap SqlCompleter::complete(const QString &text, int cursor, bool force) const
{
    cursor = qBound(0, cursor, int(text.size()));
    int start = cursor;
    while (start > 0 && identChar(text.at(start - 1)))
        --start;
    const QString prefix = text.mid(start, cursor - start);
    // `alias.` or `table.` just before the word.
    QString qualifier;
    if (start > 0 && text.at(start - 1) == QLatin1Char('.')) {
        int q = start - 1;
        while (q > 0 && identChar(text.at(q - 1)))
            --q;
        qualifier = text.mid(q, start - 1 - q);
    }
    // The statement the cursor is in (between semicolons).
    const int from = text.lastIndexOf(QLatin1Char(';'), qMax(0, start - 1)) + 1;
    int to = text.indexOf(QLatin1Char(';'), cursor);
    if (to < 0)
        to = text.size();
    const QString statement = text.mid(from, to - from);
    // The word before this one: FROM, JOIN, ... want a table.
    static const QRegularExpression before(QStringLiteral("([A-Za-z_]+)\\s*$"));
    const QString lead = text.mid(from, start - from);
    const QString previous = before.match(lead).captured(1).toUpper();
    const bool wantsTable = qualifier.isEmpty()
                            && QStringList{ "FROM", "JOIN", "INTO", "UPDATE", "TABLE" }.contains(previous);

    QVariantMap out{ { QStringLiteral("start"), start }, { QStringLiteral("prefix"), prefix },
                     { QStringLiteral("items"), QVariantList() } };
    if (!m_session || !m_session->isOpen())
        return out;
    if (!force && prefix.isEmpty() && qualifier.isEmpty() && !wantsTable)
        return out;

    // The tables this statement uses, and their aliases.
    QHash<QString, QString> aliases;                           // alias (lower) -> table
    QStringList used;
    static const QRegularExpression ref(QStringLiteral("(?:\\bFROM|\\bJOIN|\\bUPDATE|\\bINTO|,)\\s+([\\w.\"`\\[\\]]+)(?:\\s+(?:AS\\s+)?([A-Za-z_]\\w*))?"),
                                        QRegularExpression::CaseInsensitiveOption);
    QHash<QString, const QiTableInfo *> byName;
    for (const QiTableInfo &t : m_session->tableInfos())
        byName.insert(t.name.toLower(), &t);
    auto it = ref.globalMatch(statement);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString table = unquote(m.captured(1));
        if (!byName.contains(table.toLower()))
            continue;
        if (!used.contains(byName.value(table.toLower())->name))
            used << byName.value(table.toLower())->name;
        aliases.insert(table.toLower(), byName.value(table.toLower())->name);
        const QString alias = m.captured(2);
        if (!alias.isEmpty() && !kNotAliases.contains(alias.toUpper()))
            aliases.insert(alias.toLower(), byName.value(table.toLower())->name);
    }

    QVariantList items;
    QSet<QString> seen;
    const QString p = prefix.toLower();
    auto add = [&](const QString &value, const QString &kind, const QString &detail) {
        if (items.size() >= MaxItems || !value.toLower().startsWith(p) || seen.contains(kind + value))
            return;
        if (!p.isEmpty() && value.compare(prefix, Qt::CaseInsensitive) == 0 && kind != QLatin1String("column"))
            return;                                            // already typed in full
        seen.insert(kind + value);
        items << QVariantMap{ { QStringLiteral("text"), value }, { QStringLiteral("kind"), kind },
                              { QStringLiteral("detail"), detail } };
    };
    auto columnsOf = [&](const QString &table) {
        if (const QiTableInfo *t = byName.value(table.toLower()))
            for (const QiColumnInfo &c : t->columns)
                add(c.name, QStringLiteral("column"), t->name + QStringLiteral(" · ") + c.type.toLower());
    };
    auto tables = [&]() {
        for (const QiTableInfo &t : m_session->tableInfos())
            add(t.name, t.kind == QiTableInfo::View ? QStringLiteral("view") : QStringLiteral("table"),
                QStringLiteral("%1 columns").arg(t.columns.size()));
    };

    if (!qualifier.isEmpty()) {
        columnsOf(aliases.value(qualifier.toLower(), qualifier));
    } else if (wantsTable) {
        tables();
    } else {
        for (const QString &t : used)
            columnsOf(t);
        tables();
        // Keywords in the case being typed.
        const bool lower = !prefix.isEmpty() && prefix == prefix.toLower();
        for (const QString &k : kKeywords)
            add(lower ? k.toLower() : k, QStringLiteral("keyword"), QString());
        for (const QString &f : kFunctions)
            add(lower ? f.toLower() : f, QStringLiteral("function"), QString());
    }
    out.insert(QStringLiteral("items"), items);
    return out;
}
