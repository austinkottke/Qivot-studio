#ifndef QUERYMODEL_H
#define QUERYMODEL_H

#include <QAbstractTableModel>
#include <QElapsedTimer>
#include <QPointer>
#include <QQmlEngine>
#include <QThread>
#include <QVariantList>
#include <QVector>
#include <atomic>
#include <memory>

#include "databasesession.h"

/// Runs SQL typed by the user and holds the result, for the SQL console.
/**
  Nothing it runs can change the database: SQLite files are open read-only,
  PostgreSQL and MySQL sessions are read-only on the server, and on SQL Server
  (which has no such session setting) every statement runs inside a
  transaction that is always rolled back.

  start() runs the query on a worker thread with a connection of its own (as
  read-only as the session's), so a slow query doesn't freeze the window, and
  cancel() stops it: the server is asked to cancel it (PostgreSQL, MySQL, SQL
  Server), and reading rows stops. run() is the same, waiting for the result.

  At most MaxRows rows are kept; `truncated` says when there were more.

\code
    QueryResult { id: result; session: db }
    Button { text: result.running ? "Stop" : "Run"
             onClicked: result.running ? result.cancel() : result.start(editor.text) }
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
    /// True from start() until the result is in (or cancel()).
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    /// True when the last run was stopped with cancel().
    Q_PROPERTY(bool cancelled READ cancelled NOTIFY finished)

public:
    enum Role { NullRole = Qt::UserRole + 1, NumberRole, RawRole };
    static constexpr int MaxRows = 10000;

    explicit QueryModel(QObject *parent = nullptr);
    ~QueryModel() override;

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
    bool    running() const { return m_running; }
    bool    cancelled() const { return m_cancelled; }

    /// Run one statement and wait for it. Returns false (and sets `error`) if it failed.
    Q_INVOKABLE bool run(const QString &sql);

    /// Run one statement in the background; finished() when the result is in.
    Q_INVOKABLE void start(const QString &sql);

    /// Stop the query start() began. The result is dropped; finished() at once.
    Q_INVOKABLE void cancel();

    /// Forget the current result (stopping a query that's running).
    Q_INVOKABLE void clear();

    /// Every value of `row` in full, `{ column: value }`, for an inspector.
    Q_INVOKABLE QVariantMap rowAt(int row) const;

    /// Run the last query again and write all its rows (not just the first
    /// MaxRows) to a file: `format` "csv" or "json". `{ ok, rows, error, path }`.
    Q_INVOKABLE QVariantMap exportTo(const QVariant &fileOrUrl, const QString &format);

    /// Wait (up to `ms`) for queries still finishing in the background,
    /// including stopped ones. True when none are left. For tests and shutdown.
    bool waitForBackground(int ms);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void sessionChanged();
    void finished();
    void runningChanged();

private:
    struct Column { QString name; QString type; };
    struct Outcome {
        QVector<Column>       columns;
        QVector<QVariantList> rows;
        bool    truncated = false;
        int     rowsAffected = -1;
        QString error;
        QString notice;
        qint64  elapsed = 0;
    };
    // What a background run shares with the thread doing it.
    struct Shared {
        std::atomic_bool   cancel{ false };
        std::atomic<qint64> backend{ -1 };   // the server's id for the worker's session
    };

    QString prepare(const QString &text, QString *error) const;
    static Outcome execute(QSqlDatabase db, const QString &sql, const QString &dialect, const std::atomic_bool *cancel);
    void apply(const Outcome &outcome, bool cancelled = false);
    void setRunning(bool running);
    void deliver(int generation, const Outcome &outcome);

    QPointer<DatabaseSession> m_session;
    QVector<Column>       m_columns;
    QVector<QVariantList> m_rows;
    bool    m_truncated = false;
    qint64  m_elapsed = 0;
    int     m_rowsAffected = -1;
    QString m_error;
    QString m_notice;
    QString m_sql;
    bool    m_running = false;
    bool    m_cancelled = false;
    int     m_generation = 0;
    QElapsedTimer m_timer;
    std::shared_ptr<Shared>    m_shared;
    QList<QPointer<QThread>>   m_threads;
};

#endif // QUERYMODEL_H
