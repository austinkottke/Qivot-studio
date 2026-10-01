#ifndef TABLEPROFILE_H
#define TABLEPROFILE_H

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QThread>
#include <QVariantList>
#include <atomic>
#include <memory>

#include "databasesession.h"

/// What's in each column of a table: how much is empty, how much repeats,
/// the range, and the shape (a histogram for numbers, the commonest values
/// otherwise).
/**
  The queries run on a worker thread with its own connection (a clone of the
  session's), so a table of millions of rows profiles without freezing the
  UI. Columns arrive one at a time through `columns` / columnReady(); setting
  another table cancels the run.

  Each column: `{ name, type, kind: "number"|"date"|"text"|"other", done,
  rows, nulls, distinct, min, max, avg, top: [{ value, count }],
  histogram: [{ from, to, count }], error }`.

\code
    Profile { id: p; session: db; table: "book" }
    Repeater { model: p.columns; delegate: Text { text: modelData.name + ": " + modelData.nulls + " empty" } }
\endcode
 */
class TableProfile : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Profile)
    Q_PROPERTY(DatabaseSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QString table READ table WRITE setTable NOTIFY tableChanged)
    Q_PROPERTY(QVariantList columns READ columns NOTIFY columnsChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int done READ done NOTIFY columnsChanged)
    Q_PROPERTY(qint64 rows READ rows NOTIFY columnsChanged)

public:
    explicit TableProfile(QObject *parent = nullptr);
    ~TableProfile() override;

    DatabaseSession *session() const { return m_session; }
    void setSession(DatabaseSession *session);
    QString table() const { return m_table; }
    void setTable(const QString &table);
    QVariantList columns() const { return m_columns; }
    bool running() const { return m_thread != nullptr; }
    int done() const;
    qint64 rows() const { return m_rows; }

    /// Profile again (e.g. after the data changed).
    Q_INVOKABLE void refresh();

    /// Buckets in a histogram; values for the commonest list.
    static constexpr int Buckets = 12;
    static constexpr int TopValues = 6;

    /// The kind of a declared column type: "number", "date", "text" or "other".
    static QString kindOf(const QString &type);

signals:
    void sessionChanged();
    void tableChanged();
    void columnsChanged();
    void runningChanged();
    void columnReady(int index);
    void finished();

private:
    void start();
    void stop();
    Q_INVOKABLE void deliver(int generation, int index, const QVariantMap &column, qint64 rows);
    Q_INVOKABLE void threadDone(int generation);

    QPointer<DatabaseSession>          m_session;
    QString                            m_table;
    QVariantList                       m_columns;
    qint64                             m_rows = -1;
    QThread                           *m_thread = nullptr;
    std::shared_ptr<std::atomic_bool>  m_cancel;
    int                                m_generation = 0;
};

#endif // TABLEPROFILE_H
