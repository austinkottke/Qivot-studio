#ifndef QUERYMODEL_H
#define QUERYMODEL_H

#include <QAbstractTableModel>
#include <QPointer>
#include <QQmlEngine>
#include <QVariantList>
#include <QVector>

#include "databasesession.h"

/// Runs SQL typed by the user and holds the result, for the SQL console.
/**
  Nothing it runs can change the database: SQLite files are open read-only,
  PostgreSQL and MySQL sessions are read-only on the server, and on SQL Server
  (which has no such session setting) every statement runs inside a
  transaction that is always rolled back.

  At most MaxRows rows are kept; `truncated` says when there were more.

\code
    QueryResult { id: result; session: db }
    Button { onClicked: result.run(editor.text) }
    TableView { model: result }
    Text { text: result.status }       // "16,044 rows · 12 ms"
\endcode
 */
class QueryModel : public QAbstractTableModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(QueryResult)

    Q_PROPERTY(DatabaseSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList columns READ columns NOTIFY finished)
    Q_PROPERTY(int resultRows READ resultRows NOTIFY finished)
    Q_PROPERTY(bool truncated READ truncated NOTIFY finished)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY finished)
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY finished)
    Q_PROPERTY(int rowsAffected READ rowsAffected NOTIFY finished)
    Q_PROPERTY(QString error READ error NOTIFY finished)
    Q_PROPERTY(QString notice READ notice NOTIFY finished)
    Q_PROPERTY(QString lastSql READ lastSql NOTIFY finished)

public:
    enum Role { NullRole = Qt::UserRole + 1, NumberRole, RawRole };
    static constexpr int MaxRows = 10000;

    explicit QueryModel(QObject *parent = nullptr);

    DatabaseSession *session() const { return m_session; }
    void setSession(DatabaseSession *session);

    /// `{ name, type }` per result column; `type` is the Qt type ("int", "QString", …).
    QVariantList columns() const;
    int     resultRows() const { return m_rows.size(); }
    bool    truncated() const { return m_truncated; }
    bool    hasResult() const { return !m_columns.isEmpty(); }
    qint64  elapsedMs() const { return m_elapsed; }
    int     rowsAffected() const { return m_rowsAffected; }
    QString error() const { return m_error; }
    /// Not an error, but worth saying (e.g. "rolled back").
    QString notice() const { return m_notice; }
    QString lastSql() const { return m_sql; }

    /// Run one statement. Returns false (and sets `error`) if it failed.
    Q_INVOKABLE bool run(const QString &sql);

    /// Forget the current result.
    Q_INVOKABLE void clear();

    /// Every value of `row` in full, `{ column: value }`, for an inspector.
    Q_INVOKABLE QVariantMap rowAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void sessionChanged();
    void finished();

private:
    struct Column { QString name; QString type; };

    QPointer<DatabaseSession> m_session;
    QVector<Column>       m_columns;
    QVector<QVariantList> m_rows;
    bool    m_truncated = false;
    qint64  m_elapsed = 0;
    int     m_rowsAffected = -1;
    QString m_error;
    QString m_notice;
    QString m_sql;
};

#endif // QUERYMODEL_H
