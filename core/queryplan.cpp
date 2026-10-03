#include "queryplan.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QXmlStreamReader>

namespace {

QVariantMap node(int depth, const QString &label, const QString &detail, const QString &table,
                 double rows, double cost, bool scan)
{
    return { { QStringLiteral("depth"), depth }, { QStringLiteral("label"), label },
             { QStringLiteral("detail"), detail }, { QStringLiteral("table"), table },
             { QStringLiteral("rows"), rows }, { QStringLiteral("cost"), cost },
             { QStringLiteral("share"), 0.0 }, { QStringLiteral("scan"), scan } };
}

// Each step's cost as a share of the most expensive (the whole query, usually).
void shares(QVariantList &nodes)
{
    double top = 0;
    for (const QVariant &v : nodes)
        top = qMax(top, v.toMap().value(QStringLiteral("cost")).toDouble());
    if (top <= 0)
        return;
    for (QVariant &v : nodes) {
        QVariantMap m = v.toMap();
        const double c = m.value(QStringLiteral("cost")).toDouble();
        m.insert(QStringLiteral("share"), c > 0 ? c / top : 0.0);
        v = m;
    }
}

// ---- DuckDB: EXPLAIN (FORMAT json), a tree of operators ----
void duckNode(const QJsonObject &p, int depth, QVariantList &out)
{
    const QString name = p.value(QStringLiteral("name")).toString().trimmed();
    QString table;
    double rows = -1;
    QStringList detail;
    const QJsonValue info = p.value(QStringLiteral("extra_info"));
    if (info.isObject()) {
        const QJsonObject o = info.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            const QString text = it.value().isArray()
                ? [&] { QStringList parts; for (const QJsonValue &x : it.value().toArray()) parts << x.toString(); return parts.join(QStringLiteral(", ")); }()
                : it.value().toVariant().toString();
            if (it.key() == QLatin1String("Table"))
                table = text.section(QLatin1Char('.'), -1);      // "file.main.book": the table is book
            else if (it.key() == QLatin1String("Estimated Cardinality"))
                rows = text.toDouble();
            else if (!text.trimmed().isEmpty())
                detail << it.key() + QStringLiteral(": ") + text.simplified();
        }
    } else if (info.isString() && !info.toString().trimmed().isEmpty()) {
        detail << info.toString().simplified();
    }
    if (!table.isEmpty())
        detail.prepend(QStringLiteral("on ") + table);
    // A column store reads whole tables as a matter of course; it's one with no
    // filter at all that reads every row.
    const bool scan = name.contains(QLatin1String("SCAN")) && !table.isEmpty()
                      && !(info.isObject() && info.toObject().contains(QStringLiteral("Filters")));
    out << node(depth, name, detail.join(QStringLiteral(" · ")), table, rows, -1, scan);
    for (const QJsonValue &c : p.value(QStringLiteral("children")).toArray())
        duckNode(c.toObject(), depth + 1, out);
}

// ---- PostgreSQL ----
void pgNode(const QJsonObject &p, int depth, QVariantList &out)
{
    const QString type = p.value(QStringLiteral("Node Type")).toString();
    QString label = type;
    if (p.contains(QStringLiteral("Join Type")) && !type.contains(p.value(QStringLiteral("Join Type")).toString()))
        label += QStringLiteral(" (") + p.value(QStringLiteral("Join Type")).toString() + QStringLiteral(")");
    const QString table = p.value(QStringLiteral("Relation Name")).toString();
    QStringList detail;
    if (!table.isEmpty())
        detail << QStringLiteral("on ") + table
                  + (p.value(QStringLiteral("Alias")).toString() != table && !p.value(QStringLiteral("Alias")).toString().isEmpty()
                         ? QStringLiteral(" ") + p.value(QStringLiteral("Alias")).toString() : QString());
    if (p.contains(QStringLiteral("Index Name")))
        detail << QStringLiteral("using ") + p.value(QStringLiteral("Index Name")).toString();
    for (const char *k : { "Index Cond", "Hash Cond", "Merge Cond", "Join Filter", "Filter", "Sort Key", "Group Key" }) {
        const QJsonValue v = p.value(QLatin1String(k));
        if (v.isString())
            detail << QLatin1String(k) + QStringLiteral(": ") + v.toString();
        else if (v.isArray()) {
            QStringList parts;
            for (const QJsonValue &x : v.toArray())
                parts << x.toString();
            detail << QLatin1String(k) + QStringLiteral(": ") + parts.join(QStringLiteral(", "));
        }
    }
    out << node(depth, label, detail.join(QStringLiteral(" · ")), table,
                p.value(QStringLiteral("Plan Rows")).toDouble(-1), p.value(QStringLiteral("Total Cost")).toDouble(-1),
                type == QLatin1String("Seq Scan"));
    for (const QJsonValue &c : p.value(QStringLiteral("Plans")).toArray())
        pgNode(c.toObject(), depth + 1, out);
}

} // namespace

// ---- The parsers ----

QVariantList QueryPlan::fromSqlite(const QList<QVariantList> &rows)
{
    // Rows come parents first; depth = parent's depth + 1.
    QHash<int, int> depthOf;
    QVariantList out;
    static const QRegularExpression scan(QStringLiteral("^SCAN (?:TABLE )?(\\S+)(?:\\s+AS\\s+\\S+)?$"));
    static const QRegularExpression table(QStringLiteral("^(?:SCAN|SEARCH) (?:TABLE )?(\\S+)"));
    for (const QVariantList &r : rows) {
        const int id = r.value(0).toInt(), parent = r.value(1).toInt();
        const int depth = parent > 0 && depthOf.contains(parent) ? depthOf.value(parent) + 1 : 0;
        depthOf.insert(id, depth);
        const QString detail = r.value(3).toString();
        const QString word = detail.section(QLatin1Char(' '), 0, 0);
        const QString rest = detail.section(QLatin1Char(' '), 1);
        out << node(depth, word.isEmpty() ? detail : word, rest, table.match(detail).captured(1), -1, -1,
                    scan.match(detail).hasMatch());
    }
    return out;
}

QVariantList QueryPlan::fromDuckDbJson(const QString &json)
{
    QVariantList out;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    const QJsonArray top = doc.isArray() ? doc.array() : QJsonArray{ doc.object() };
    for (const QJsonValue &v : top)
        if (v.isObject())
            duckNode(v.toObject(), 0, out);
    return out;
}

QVariantList QueryPlan::fromPostgresJson(const QString &json)
{
    QVariantList out;
    const QJsonArray a = QJsonDocument::fromJson(json.toUtf8()).array();
    if (!a.isEmpty())
        pgNode(a.first().toObject().value(QStringLiteral("Plan")).toObject(), 0, out);
    shares(out);
    return out;
}

QVariantList QueryPlan::fromMysqlTree(const QString &tree)
{
    // "-> Nested loop inner join  (cost=12.3 rows=10)", indented 4 per level.
    QVariantList out;
    static const QRegularExpression line(QStringLiteral("^(\\s*)-> (.*?)(?:\\s+\\(cost=([\\d.e+]+)(?:\\.\\.[\\d.e+]+)?\\s+rows=([\\d.e+]+)\\))?(?:\\s+\\(actual.*\\))?$"));
    static const QRegularExpression onTable(QStringLiteral("\\bon (\\w+)"));
    for (const QString &l : tree.split(QLatin1Char('\n'))) {
        const auto m = line.match(l);
        if (!m.hasMatch())
            continue;
        const QString what = m.captured(2).trimmed();
        // "Table scan on book", "Index lookup on b using idx (author_id=a.id)": a label and the rest.
        QString label = what, detail;
        const int on = what.indexOf(QLatin1String(" on "));
        if (on > 0) {
            label = what.left(on);
            detail = what.mid(on + 1);
        } else if (what.contains(QLatin1Char(':'))) {
            label = what.section(QLatin1Char(':'), 0, 0);
            detail = what.section(QLatin1Char(':'), 1).trimmed();
        }
        out << node(m.captured(1).size() / 4, label, detail, onTable.match(what).captured(1),
                    m.captured(4).isEmpty() ? -1 : m.captured(4).toDouble(),
                    m.captured(3).isEmpty() ? -1 : m.captured(3).toDouble(), what.startsWith(QLatin1String("Table scan")));
    }
    shares(out);
    return out;
}

QVariantList QueryPlan::fromSqlServerXml(const QString &xml)
{
    // RelOp elements, nested as the plan is; the table is on the Object inside.
    QVariantList out;
    QXmlStreamReader r(xml);
    int depth = -1;
    QVector<int> open;                       // index in `out` of each open RelOp
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement() && r.name() == QLatin1String("RelOp")) {
            ++depth;
            const auto a = r.attributes();
            const QString physical = a.value(QLatin1String("PhysicalOp")).toString();
            const QString logical = a.value(QLatin1String("LogicalOp")).toString();
            out << node(depth, physical, logical != physical ? logical : QString(), QString(),
                        a.value(QLatin1String("EstimateRows")).toDouble(),
                        a.value(QLatin1String("EstimatedTotalSubtreeCost")).toDouble(),
                        physical == QLatin1String("Table Scan") || physical == QLatin1String("Clustered Index Scan"));
            open << out.size() - 1;
        } else if (r.isEndElement() && r.name() == QLatin1String("RelOp")) {
            --depth;
            open.removeLast();
        } else if (r.isStartElement() && r.name() == QLatin1String("Object") && !open.isEmpty()) {
            QVariantMap m = out.at(open.last()).toMap();
            if (m.value(QStringLiteral("table")).toString().isEmpty()) {
                QString table = r.attributes().value(QLatin1String("Table")).toString();
                table.remove(QLatin1Char('[')).remove(QLatin1Char(']'));
                QString index = r.attributes().value(QLatin1String("Index")).toString();
                index.remove(QLatin1Char('[')).remove(QLatin1Char(']'));
                m.insert(QStringLiteral("table"), table);
                QString detail = m.value(QStringLiteral("detail")).toString();
                detail = (detail.isEmpty() ? QString() : detail + QStringLiteral(" · ")) + QStringLiteral("on ") + table
                         + (index.isEmpty() ? QString() : QStringLiteral(" using ") + index);
                m.insert(QStringLiteral("detail"), detail);
                out[open.last()] = m;
            }
        }
    }
    shares(out);
    return out;
}

// ---- Asking the database ----

void QueryPlan::setSession(DatabaseSession *session)
{
    if (session == m_session)
        return;
    m_session = session;
    emit sessionChanged();
    clear();
}

void QueryPlan::clear()
{
    m_nodes.clear();
    m_raw.clear();
    m_error.clear();
    emit changed();
}

int QueryPlan::scans() const
{
    int n = 0;
    for (const QVariant &v : m_nodes)
        n += v.toMap().value(QStringLiteral("scan")).toBool() ? 1 : 0;
    return n;
}

bool QueryPlan::explain(const QString &sqlIn)
{
    m_nodes.clear();
    m_raw.clear();
    m_error.clear();
    QString sql = sqlIn.trimmed();
    while (sql.endsWith(QLatin1Char(';')))
        sql.chop(1);
    if (!m_session || !m_session->isOpen() || sql.isEmpty()) {
        m_error = tr("Write a query to see its plan.");
        emit changed();
        return false;
    }
    QSqlQuery q(QSqlDatabase::database(m_session->connectionName()));
    const QString dialect = m_session->dialect();
    auto failed = [&](const QSqlQuery &query) {
        m_error = query.lastError().text().trimmed();
        if (m_error.isEmpty())
            m_error = tr("The database didn't give a plan for this.");
        emit changed();
        return false;
    };

    if (dialect == QLatin1String("sqlite")) {
        if (!q.exec(QStringLiteral("EXPLAIN QUERY PLAN ") + sql))
            return failed(q);
        QList<QVariantList> rows;
        QStringList text;
        while (q.next()) {
            rows << QVariantList{ q.value(0), q.value(1), q.value(2), q.value(3) };
            text << q.value(3).toString();
        }
        m_nodes = fromSqlite(rows);
        m_raw = text.join(QLatin1Char('\n'));
    } else if (dialect == QLatin1String("postgres")) {
        if (!q.exec(QStringLiteral("EXPLAIN (FORMAT JSON) ") + sql) || !q.next())
            return failed(q);
        m_raw = q.value(0).toString();
        m_nodes = fromPostgresJson(m_raw);
    } else if (dialect == QLatin1String("mysql")) {
        if (q.exec(QStringLiteral("EXPLAIN FORMAT=TREE ") + sql) && q.next()) {
            m_raw = q.value(0).toString();
            m_nodes = fromMysqlTree(m_raw);
        } else {
            // MariaDB, or MySQL before 8.0.16: the classic table, one row per table.
            if (!q.exec(QStringLiteral("EXPLAIN ") + sql))
                return failed(q);
            const QSqlRecord rec = q.record();
            QStringList text;
            while (q.next()) {
                const QString table = q.value(rec.indexOf(QStringLiteral("table"))).toString();
                const QString type = q.value(rec.indexOf(QStringLiteral("type"))).toString();
                const QString key = q.value(rec.indexOf(QStringLiteral("key"))).toString();
                const QString extra = q.value(rec.indexOf(QStringLiteral("Extra"))).toString();
                m_nodes << node(0, type == QLatin1String("ALL") ? tr("Table scan") : tr("Access (%1)").arg(type),
                                QStringLiteral("on ") + table + (key.isEmpty() ? QString() : QStringLiteral(" using ") + key)
                                    + (extra.isEmpty() ? QString() : QStringLiteral(" · ") + extra),
                                table, q.value(rec.indexOf(QStringLiteral("rows"))).toDouble(), -1, type == QLatin1String("ALL"));
                text << QStringLiteral("%1  %2  %3  %4").arg(table, type, key, extra);
            }
            m_raw = text.join(QLatin1Char('\n'));
        }
    } else if (dialect == QLatin1String("duckdb")) {
        if (!q.exec(QStringLiteral("EXPLAIN (FORMAT json) ") + sql) || !q.next())
            return failed(q);
        m_raw = q.value(q.record().count() - 1).toString();     // explain_key, explain_value
        m_nodes = fromDuckDbJson(m_raw);
    } else if (dialect == QLatin1String("sqlserver")) {
        // The plan instead of the rows, for this one statement. Forward-only:
        // the ODBC driver's scrollable cursor can't hold a showplan result.
        if (!q.exec(QStringLiteral("SET SHOWPLAN_XML ON")))
            return failed(q);
        QSqlQuery planQuery(QSqlDatabase::database(m_session->connectionName()));
        planQuery.setForwardOnly(true);
        const bool ok = planQuery.exec(sql) && planQuery.next();
        if (ok)
            m_raw = planQuery.value(0).toString();
        q = planQuery;
        QSqlQuery off(QSqlDatabase::database(m_session->connectionName()));
        off.exec(QStringLiteral("SET SHOWPLAN_XML OFF"));
        if (!ok)
            return failed(q);
        m_nodes = fromSqlServerXml(m_raw);
    } else {
        m_error = tr("Plans aren't available for %1.").arg(m_session->dialectName());
        emit changed();
        return false;
    }
    if (m_nodes.isEmpty())
        m_error = tr("The database didn't give a plan for this.");
    emit changed();
    return !m_nodes.isEmpty();
}
