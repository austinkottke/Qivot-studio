#ifndef CONNECTIONHISTORY_H
#define CONNECTIONHISTORY_H

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

/// The databases opened lately, and the connections kept for good: what the
/// welcome screen lists and the Connect dialog offers. Never a password.
/**
  Each entry: `{ id, kind: "file"|"server", title, detail, settings, saved,
  label, at, missing }`. `settings` is what DatabaseSession::connectionSettings()
  gave (a file's path; a server's type, host, port, database, user, and its SSL
  and SSH options) and goes back to open() / connectTo() as it is, with a
  password added for a server. Saved entries come first, by name; then the
  rest, latest first, at most MaxRecent of them.

\code
    ConnectionHistory.remember(db.connectionSettings)
    Repeater { model: ConnectionHistory.entries; delegate: Text { text: modelData.title } }
    ConnectionHistory.setSaved(id, true, "Production")
\endcode
 */
class ConnectionHistory : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)

public:
    explicit ConnectionHistory(QObject *parent = nullptr);

    static constexpr int MaxRecent = 12;

    QVariantList entries() const;

    /// Note that `settings` were just opened: to the top of the recent ones
    /// (a saved entry stays saved). The password, if any, is left out.
    Q_INVOKABLE QString remember(const QVariantMap &settings);
    /// Keep an entry for good (`saved`), under `label` (blank: its usual title).
    Q_INVOKABLE void setSaved(const QString &id, bool saved, const QString &label = QString());
    Q_INVOKABLE void rename(const QString &id, const QString &label);
    Q_INVOKABLE void forget(const QString &id);
    /// One entry, or empty.
    Q_INVOKABLE QVariantMap entry(const QString &id) const;
    /// The entry `settings` would be (the same file, or server + database + user).
    static QString idOf(const QVariantMap &settings);
    Q_INVOKABLE QString idFor(const QVariantMap &settings) const { return idOf(settings); }

signals:
    void changed();

private:
    void load();
    void store();
    QVariantMap decorate(const QVariantMap &e) const;

    QVariantList m_entries;                 // as stored: { id, settings, saved, label, at }
};

#endif // CONNECTIONHISTORY_H
