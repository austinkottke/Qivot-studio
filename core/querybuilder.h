#ifndef QUERYBUILDER_H
#define QUERYBUILDER_H

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>
#include <QVector>

#include "databasesession.h"

/// A query put together by pointing, not typing: a table, the tables joined
/// to it along their foreign keys, the columns to show (optionally counted,
/// summed, ...), filters, sorting and a row limit. It writes the SQL, in the
/// database's dialect, and the same query as Qivot C++.
/**
  Joins only follow foreign keys, so every join has its ON condition filled
  in and is one the schema means; a table joins once. Picking an aggregate
  groups by the other picked columns.

\code
    QueryBuilder { id: b; session: db }
    b.setFrom("book")
    b.join(0)                          // b.joinable[0]: author, via book.author_id
    b.toggleColumn("author", "country")
    b.toggleColumn("book", "id"); b.setAggregate(1, "count")
    b.sql    // SELECT author.country, COUNT(book.id) AS count_id FROM book JOIN author ON ...
\endcode
 */
class QueryBuilder : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(DatabaseSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QString from READ from NOTIFY changed)
    /// Every table in the query, the first being `from`.
    Q_PROPERTY(QStringList tables READ tables NOTIFY changed)
    /// `[{ table, on, left }]`: how each joined table is reached.
    Q_PROPERTY(QVariantList joins READ joins NOTIFY changed)
    /// `[{ table, on }]`: tables that can be joined next (through a foreign key either way).
    Q_PROPERTY(QVariantList joinable READ joinable NOTIFY changed)
    /// `[{ table, columns: [{ name, type, selected }] }]` for the tables in the query.
    Q_PROPERTY(QVariantList available READ available NOTIFY changed)
    /// `[{ table, column, aggregate, label }]`, in order; empty means every column of `from`.
    Q_PROPERTY(QVariantList columns READ columns NOTIFY changed)
    /// `[{ table, column, op, value }]`, all of which must hold.
    Q_PROPERTY(QVariantList filters READ filters NOTIFY changed)
    /// `[{ key, desc }]`: key is "table.column", or a picked aggregate's label.
    Q_PROPERTY(QVariantList sorts READ sorts NOTIFY changed)
    /// What can be sorted by: picked aggregates' labels, then every "table.column".
    Q_PROPERTY(QStringList sortKeys READ sortKeys NOTIFY changed)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY changed)
    Q_PROPERTY(bool distinct READ distinct WRITE setDistinct NOTIFY changed)
    Q_PROPERTY(QString sql READ sql NOTIFY changed)
    Q_PROPERTY(QString cpp READ cpp NOTIFY changed)
    /// The filter operators, as the UI shows them.
    Q_PROPERTY(QStringList operators READ operators CONSTANT)
    Q_PROPERTY(QStringList aggregates READ aggregates CONSTANT)

public:
    explicit QueryBuilder(QObject *parent = nullptr);

    DatabaseSession *session() const { return m_session; }
    void setSession(DatabaseSession *session);
    QString from() const { return m_from; }
    QStringList tables() const;
    QVariantList joins() const;
    QVariantList joinable() const;
    QVariantList available() const;
    QVariantList columns() const;
    QVariantList filters() const;
    QVariantList sorts() const;
    QStringList sortKeys() const;
    int limit() const { return m_limit; }
    void setLimit(int limit);
    bool distinct() const { return m_distinct; }
    void setDistinct(bool distinct);
    QString sql() const;
    QString cpp() const;
    QStringList operators() const;
    QStringList aggregates() const;

    /// Start a new query on `table` (clears everything else).
    Q_INVOKABLE void setFrom(const QString &table);
    /// Join `joinable[index]`.
    Q_INVOKABLE void join(int index);
    /// LEFT JOIN (keep rows with no match) instead of JOIN.
    Q_INVOKABLE void setJoinLeft(const QString &table, bool left);
    /// Take a joined table out, with the tables joined through it and
    /// anything that uses their columns.
    Q_INVOKABLE void removeTable(const QString &table);

    Q_INVOKABLE void toggleColumn(const QString &table, const QString &column);
    /// "", "count", "sum", "avg", "min" or "max" for the picked column at `index`.
    Q_INVOKABLE void setAggregate(int index, const QString &aggregate);
    Q_INVOKABLE void removeColumn(int index);

    Q_INVOKABLE void addFilter(const QString &table = QString(), const QString &column = QString());
    /// Change any of `{ table, column, op, value }`.
    Q_INVOKABLE void updateFilter(int index, const QVariantMap &changes);
    Q_INVOKABLE void removeFilter(int index);

    Q_INVOKABLE void addSort(const QString &key = QString(), bool desc = false);
    /// Change any of `{ key, desc }`.
    Q_INVOKABLE void updateSort(int index, const QVariantMap &changes);
    Q_INVOKABLE void removeSort(int index);

    Q_INVOKABLE void clear();

signals:
    void sessionChanged();
    void changed();

private:
    struct Join {
        QString     table;          // the joined table
        QString     to;             // the table it joins (already in the query)
        QStringList columns;        // on `table`
        QStringList toColumns;      // on `to`
        bool        left = false;
    };
    struct Column { QString table, column, aggregate; };
    struct Filter { QString table, column, op, value; };
    struct Sort { QString key; bool desc = false; };

    const QiTableInfo *info(const QString &table) const;
    QVector<Join> candidates() const;
    QString q(const QString &identifier) const;                     // quoted for the dialect
    QString ref(const QString &table, const QString &column) const; // table.column, quoted
    QString label(const Column &c) const;
    QString literal(const Filter &f) const;
    QString condition(const Filter &f) const;
    QString onClause(const Join &j) const;
    QString className(const QString &table) const;

    QPointer<DatabaseSession> m_session;
    QString                   m_from;
    QVector<Join>             m_joins;
    QVector<Column>           m_columns;
    QVector<Filter>           m_filters;
    QVector<Sort>             m_sorts;
    int                       m_limit = 100;
    bool                      m_distinct = false;
};

#endif // QUERYBUILDER_H
