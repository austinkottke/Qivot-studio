#ifndef QUERYPLAN_H
#define QUERYPLAN_H

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QVariantList>

#include "databasesession.h"

/// How the database would run a query — without running it — as a tree of
/// steps, the same shape for every database.
/**
  SQLite: EXPLAIN QUERY PLAN. PostgreSQL: EXPLAIN (FORMAT JSON). DuckDB:
  EXPLAIN (FORMAT json). MySQL:
  EXPLAIN FORMAT=TREE (or the classic table on MariaDB and older MySQL).
  SQL Server: SHOWPLAN_XML. None of them runs the query.

  `nodes` is the tree flattened, parents first:
  `{ depth, label, detail, table, rows (estimated; -1 unknown), cost (-1 unknown),
     share (cost as a part of the whole, 0..1), scan }` — `scan` marks a step
  that reads a whole table (no index), usually the one to look at.
 */
class QueryPlan : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(DatabaseSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY changed)
    Q_PROPERTY(bool hasPlan READ hasPlan NOTIFY changed)
    /// How many steps read a whole table.
    Q_PROPERTY(int scans READ scans NOTIFY changed)
    /// The database's own words for the plan (text, JSON or XML).
    Q_PROPERTY(QString raw READ raw NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    using QObject::QObject;

    DatabaseSession *session() const { return m_session; }
    void setSession(DatabaseSession *session);
    QVariantList nodes() const { return m_nodes; }
    bool hasPlan() const { return !m_nodes.isEmpty(); }
    int scans() const;
    QString raw() const { return m_raw; }
    QString error() const { return m_error; }

    /// Ask for `sql`'s plan. False (with error()) if the database can't say.
    Q_INVOKABLE bool explain(const QString &sql);
    Q_INVOKABLE void clear();

    /// The parsers, for tests: each turns the database's output into nodes.
    static QVariantList fromSqlite(const QList<QVariantList> &rows);       // id, parent, notused, detail
    static QVariantList fromPostgresJson(const QString &json);
    static QVariantList fromDuckDbJson(const QString &json);
    static QVariantList fromMysqlTree(const QString &tree);
    static QVariantList fromSqlServerXml(const QString &xml);

signals:
    void sessionChanged();
    void changed();

private:
    QPointer<DatabaseSession> m_session;
    QVariantList m_nodes;
    QString m_raw;
    QString m_error;
};

#endif // QUERYPLAN_H
