#ifndef REDISSESSION_H
#define REDISSESSION_H

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

#include "redisclient.h"

class SshTunnel;

/// A Redis server, as the Redis screens see it: its keys and their values, a
/// console, and what the server says about itself (INFO).
/**
  Read-only until changes are allowed: Redis has no read-only session, so the
  console asks the server about each command (COMMAND INFO) and refuses any
  that writes or administers. Commands that would block — MONITOR, SUBSCRIBE,
  BLPOP and the like — are always refused: they'd hold up the window.

\code
    Redis { id: redis }
    redis.connectTo({ host: "localhost", port: 6379, password: "…", database: 0 })
    const page = redis.scan("user:*", "0", 500)     // { cursor, keys: [{ key, type, ttl }] }
    const v = redis.value("user:1", 0, 200)          // by type: { kind, rows | text, total, next }
    redis.run("HGETALL user:1")                      // { ok, text, error, ms, refused }
\endcode
 */
class RedisSession : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Redis)
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY openChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY openChanged)
    Q_PROPERTY(QString location READ location NOTIFY openChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    /// The selected database number.
    Q_PROPERTY(int database READ database NOTIFY databaseChanged)
    /// `[{ index, keys, expires }]`, every database the server has.
    Q_PROPERTY(QVariantList databases READ databases NOTIFY infoChanged)
    /// The highlights of INFO: `{ version, mode, role, os, uptimeDays, clients,
    /// memory, peakMemory, maxMemory, opsPerSec, hitRate, … }`.
    Q_PROPERTY(QVariantMap info READ info NOTIFY infoChanged)
    /// All of INFO: `[{ name, rows: [{ key, value }] }]`.
    Q_PROPERTY(QVariantList infoSections READ infoSections NOTIFY infoChanged)
    Q_PROPERTY(bool changesAllowed READ changesAllowed NOTIFY changesAllowedChanged)

public:
    explicit RedisSession(QObject *parent = nullptr);
    ~RedisSession() override;

    bool isOpen() const;
    QString displayName() const { return m_displayName; }
    QString location() const { return m_location; }
    QString error() const { return m_error; }
    int database() const { return m_database; }
    QVariantList databases() const { return m_databases; }
    QVariantMap info() const { return m_info; }
    QVariantList infoSections() const { return m_infoSections; }
    bool changesAllowed() const { return m_changesAllowed; }

    /// How it was connected, without the password (for Recent).
    Q_INVOKABLE QVariantMap connectionSettings() const { return m_settings; }

    /// `{ host, port, user, password, database, ssl: { mode: "on"|"off", trust },
    /// ssh: { host, port, user, key } }`. False (with error()) if it can't.
    Q_INVOKABLE bool connectTo(const QVariantMap &settings);
    Q_INVOKABLE void close();
    Q_INVOKABLE bool selectDatabase(int index);
    /// Read INFO (and the databases' key counts) again.
    Q_INVOKABLE void refresh();

    /// One step of SCAN: keys matching `pattern` (glob), from `cursor` ("0" to
    /// start), about `count` of them, each with its type and TTL (ms; -1 none).
    /// `{ cursor ("0" when there are no more), keys: [{ key, type, ttl }] }`.
    Q_INVOKABLE QVariantMap scan(const QString &pattern, const QString &cursor, int count = 500);

    /// `{ key, type, ttl, size, encoding, memory }` (size: length, fields, members…).
    Q_INVOKABLE QVariantMap keyInfo(const QString &key);

    /// The value, a page at a time. `from`: an index (list, zset), a cursor
    /// (hash, set) or a stream id. Strings: `{ kind: "string", text, length,
    /// truncated, binary, json }`; the rest `{ kind, rows: [...], total, next }`
    /// with rows `{ field, value }` (hash), `{ index, value }` (list),
    /// `{ member }` (set), `{ member, score }` (zset), `{ id, fields }` (stream).
    Q_INVOKABLE QVariantMap value(const QString &key, const QVariant &from = QVariant(), int count = 200);

    /// Run a command typed in the console: `{ ok, text, error, ms, refused }`.
    Q_INVOKABLE QVariantMap run(const QString &line);

    /// Allow commands that change the data (or go back to reading only).
    Q_INVOKABLE void allowChanges(bool on);

    /// Whether `args` may run now, and if not why (for the console; tests).
    bool allowed(const QList<QByteArray> &args, QString *why);

    static constexpr int MaxStringPreview = 1024 * 1024;

signals:
    void openChanged();
    void errorChanged();
    void databaseChanged();
    void infoChanged();
    void changesAllowedChanged();

private:
    void setError(const QString &message);
    RedisClient::Reply call(const QList<QByteArray> &args);
    QVariantMap commandFlags(const QString &name);

    std::unique_ptr<RedisClient> m_client;
    std::unique_ptr<SshTunnel>   m_tunnel;
    QString      m_displayName;
    QString      m_location;
    QString      m_error;
    int          m_database = 0;
    QVariantList m_databases;
    QVariantMap  m_info;
    QVariantList m_infoSections;
    QVariantMap  m_settings;
    bool         m_changesAllowed = false;
    QHash<QString, QVariantMap> m_flags;          // COMMAND INFO, per command
};

#endif // REDISSESSION_H
