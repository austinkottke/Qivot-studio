#ifndef ROWSMODEL_H
#define ROWSMODEL_H

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QQmlEngine>
#include <QVariantList>
#include <QVector>

#include "databasesession.h"

/// The rows of any table or view, for a QML TableView.
/**
  Counts once, then fetches only the pages the view actually shows
  (`LIMIT`/`OFFSET`), keeping a bounded number of them — a ten-million-row
  table costs the same memory as a ten-thousand-row one. Sorting and filtering
  are done by SQLite, so they're as fast as the database allows and never load
  the whole table.

\code
    Rows {
        id: rows
        session: db
        table: "book"
        filter: search.text          // matches any column, case-insensitive
    }
    TableView { model: rows }
    // header click:  rows.sortBy(column)   (ascending, then descending)
\endcode
 */
class RowsModel : public QAbstractTableModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(Rows)

    Q_PROPERTY(DatabaseSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QString table READ table WRITE setTable NOTIFY tableChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int sortColumn READ sortColumn NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending NOTIFY sortChanged)
    Q_PROPERTY(qint64 totalRows READ totalRows NOTIFY countsChanged)
    Q_PROPERTY(qint64 matchingRows READ matchingRows NOTIFY countsChanged)
    Q_PROPERTY(QVariantList columns READ columns NOTIFY columnsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    enum Role { NullRole = Qt::UserRole + 1, NumberRole, RawRole };

    explicit RowsModel(QObject *parent = nullptr);

    DatabaseSession *session() const { return m_session; }
    void setSession(DatabaseSession *session);
    QString table() const { return m_table; }
    void setTable(const QString &table);
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    int sortColumn() const { return m_sortColumn; }
    bool sortDescending() const { return m_sortDescending; }
    qint64 totalRows() const { return m_total; }
    qint64 matchingRows() const { return m_matching; }
    QString error() const { return m_error; }

    /// `{ name, type, primaryKey }` for each column, in table order.
    QVariantList columns() const;

    /// Sort by `column`: ascending first, then descending on a second call.
    Q_INVOKABLE void sortBy(int column);

    /// Back to the table's natural order.
    Q_INVOKABLE void clearSort();

    /// Every value in `row` as `{ column: value }` (nulls stay null) — for an inspector.
    Q_INVOKABLE QVariantMap rowAt(int row);

    /// How many pages are held right now (tests check the cache stays bounded).
    int cachedPages() const { return m_pages.size(); }

    static constexpr int PageSize = 200;
    static constexpr int MaxPages = 40;

    // QAbstractTableModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void sessionChanged();
    void tableChanged();
    void filterChanged();
    void sortChanged();
    void countsChanged();
    void columnsChanged();
    void errorChanged();

private:
    struct Column { QString name; QString type; bool primaryKey; };

    void reload();                         // re-read columns and counts, drop the cache
    void requery();                        // counts and cache only (filter/sort changed)
    QString fromClause() const;
    QString whereClause(QVariantList *binds) const;
    QString orderClause() const;
    const QVariantList &rowValues(int row) const;
    void setError(const QString &error);
    QString quoted(const QString &identifier) const;

    QPointer<DatabaseSession> m_session;
    QString        m_table;
    QString        m_filter;
    int            m_sortColumn = -1;
    bool           m_sortDescending = false;
    qint64         m_total = 0;
    qint64         m_matching = 0;
    QString        m_error;
    QVector<Column> m_columns;
    QStringList    m_primaryKey;

    // Page cache: page number -> its rows; m_recent holds pages oldest-first.
    mutable QHash<int, QVector<QVariantList>> m_pages;
    mutable QList<int> m_recent;
};

#endif // ROWSMODEL_H
