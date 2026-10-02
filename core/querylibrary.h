#ifndef QUERYLIBRARY_H
#define QUERYLIBRARY_H

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

/// The queries run on a database, and the ones saved by name, kept between
/// sessions (per database: `scope` says which).
/**
\code
    QueryLibrary { id: library; scope: "sqlite:/path/shop.db" }
    library.record("SELECT ...", rows, ms, true, "")
    library.save("Best sellers", "SELECT ...")
    library.history    // [{ sql, at, rows, ms, ok, error }], newest first
    library.saved      // [{ name, sql, at }], by name
\endcode
 */
class QueryLibrary : public QObject {
    Q_OBJECT
    QML_ELEMENT
    /// Which database: anything that names it ("" keeps nothing).
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY changed)
    Q_PROPERTY(QVariantList history READ history NOTIFY changed)
    Q_PROPERTY(QVariantList saved READ saved NOTIFY changed)

public:
    explicit QueryLibrary(QObject *parent = nullptr);

    static constexpr int MaxHistory = 200;

    QString scope() const { return m_scope; }
    void setScope(const QString &scope);
    QVariantList history() const { return m_history; }
    QVariantList saved() const { return m_saved; }

    /// A query that was run (the same text again moves to the top).
    Q_INVOKABLE void record(const QString &sql, int rows, int ms, bool ok, const QString &error = QString());
    /// Save `sql` as `name` (replacing one with that name).
    Q_INVOKABLE void save(const QString &name, const QString &sql);
    Q_INVOKABLE bool rename(const QString &from, const QString &to);
    Q_INVOKABLE void remove(const QString &name);
    Q_INVOKABLE void clearHistory();
    /// Whether a saved query has this name.
    Q_INVOKABLE bool hasName(const QString &name) const;
    /// A name for `sql`: "book by author", from its tables.
    Q_INVOKABLE QString suggestName(const QString &sql) const;

signals:
    void changed();

private:
    QString key(const char *what) const;
    void load();
    void store();

    QString m_scope;
    QVariantList m_history;
    QVariantList m_saved;
};

#endif // QUERYLIBRARY_H
