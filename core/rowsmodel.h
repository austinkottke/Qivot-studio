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

  Editing, once the user allows changes to the database: setCell(),
  toggleDelete() and addRow() collect changes (keyed by each row's primary
  key, so they survive paging, sorting and filtering, and show at once), and
  save() runs them all in one transaction on the writable connection —
  pendingSql() is what it would run. New rows sit after the table's own.
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
    /// Whether rows can be edited now: changes are allowed, it's a table (not
    /// a view) and it has a primary key to find each row by.
    Q_PROPERTY(bool editable READ editable NOTIFY editableChanged)
    /// Why not, when not editable ("" when it is).
    Q_PROPERTY(QString notEditableReason READ notEditableReason NOTIFY editableChanged)
    /// Rows changed, deleted or added, not yet saved.
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY pendingChanged)
    /// The statements save() would run, with the values written in.
    Q_PROPERTY(QStringList pendingSql READ pendingSql NOTIFY pendingChanged)

public:
    enum Role { NullRole = Qt::UserRole + 1, NumberRole, RawRole, EditedRole, DeletedRole, InsertedRole };

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

    /// Every row that matches the filter, in the current order, to a file:
    /// `format` "csv" or "json". `{ ok, rows, error, path }` (see DataTransfer::write).
    Q_INVOKABLE QVariantMap exportTo(const QVariant &fileOrUrl, const QString &format);

    /// Every value in `row` as `{ column: value }` (nulls stay null) — for an inspector.
    Q_INVOKABLE QVariantMap rowAt(int row);

    bool editable() const;
    QString notEditableReason() const;
    int pendingCount() const { return m_changes.size() + m_inserts.size(); }
    QStringList pendingSql() const;

    /// Set a cell (`value`: text, or null for NULL). Returns false if it can't be edited.
    Q_INVOKABLE bool setCell(int row, int column, const QVariant &value);
    /// Whether `row` is marked to be deleted.
    Q_INVOKABLE bool rowDeleted(int row) const { return data(index(row, 0), DeletedRole).toBool(); }
    /// Mark a row to be deleted, or not any more; a new row is just dropped.
    Q_INVOKABLE void toggleDelete(int row);
    /// A new, empty row at the end (its columns get their defaults); returns its row.
    Q_INVOKABLE int addRow();
    /// Forget every unsaved change.
    Q_INVOKABLE void discardChanges();
    /// Save every change in one transaction; on failure nothing is saved and
    /// error() says which change failed.
    Q_INVOKABLE bool save();

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
    void editableChanged();
    void pendingChanged();

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

    struct Change { QVariantList key; QHash<int, QVariant> values; bool deleted = false; };
    QVariantList keyOf(const QVariantList &row) const;
    static QString keyString(const QVariantList &key);
    QString literal(const QVariant &v, int column) const;
    QString keyCondition(const QVariantList &key, QVariantList *binds, bool literals) const;
    void dropPending();

    QPointer<DatabaseSession> m_session;
    bool           m_isTable = false;
    QHash<QString, Change> m_changes;           // existing rows, by their key
    QVector<QHash<int, QVariant>> m_inserts;    // new rows: column -> value
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
