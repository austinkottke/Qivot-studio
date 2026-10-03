#include "redissession.h"
#include "sshtunnel.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>

namespace {

constexpr int ConnectTimeout = 8000;
constexpr int CommandTimeout = 15000;

QByteArray b(const QString &s) { return s.toUtf8(); }

// Text, and whether it's binary (not valid UTF-8, or has NULs).
QString textOf(const QByteArray &bytes, bool *binary = nullptr)
{
    const QString s = QString::fromUtf8(bytes);
    const bool bin = bytes.contains('\0') || s.contains(QChar::ReplacementCharacter);
    if (binary)
        *binary = bin;
    return bin ? QString::fromLatin1(bytes.toHex(' ')) : s;
}

// Commands that would wait for something to happen, holding up the window.
const QSet<QString> kNeverRun = {
    "MONITOR", "SUBSCRIBE", "PSUBSCRIBE", "SSUBSCRIBE", "SYNC", "PSYNC", "QUIT", "RESET",
    "BLPOP", "BRPOP", "BRPOPLPUSH", "BLMOVE", "BLMPOP", "BZPOPMIN", "BZPOPMAX", "BZMPOP", "WAIT", "WAITAOF",
};
// When the server won't say (COMMAND is denied by its ACL): what's known to only read.
const QSet<QString> kReadOnly = {
    "GET", "MGET", "STRLEN", "GETRANGE", "EXISTS", "TYPE", "TTL", "PTTL", "EXPIRETIME", "PEXPIRETIME",
    "KEYS", "SCAN", "DBSIZE", "RANDOMKEY", "HGET", "HMGET", "HGETALL", "HKEYS", "HVALS", "HLEN", "HEXISTS",
    "HSCAN", "HSTRLEN", "HRANDFIELD", "LRANGE", "LLEN", "LINDEX", "LPOS", "SMEMBERS", "SCARD", "SISMEMBER",
    "SMISMEMBER", "SSCAN", "SRANDMEMBER", "SINTER", "SUNION", "SDIFF", "SINTERCARD", "ZRANGE", "ZRANGEBYSCORE",
    "ZREVRANGE", "ZREVRANGEBYSCORE", "ZRANGEBYLEX", "ZCARD", "ZSCORE", "ZMSCORE", "ZRANK", "ZREVRANK", "ZCOUNT",
    "ZLEXCOUNT", "ZSCAN", "ZRANDMEMBER", "XRANGE", "XREVRANGE", "XLEN", "XREAD", "XINFO", "XPENDING",
    "PING", "ECHO", "INFO", "TIME", "LASTSAVE", "SELECT", "AUTH", "HELLO", "COMMAND", "OBJECT", "MEMORY",
    "BITCOUNT", "BITPOS", "GETBIT", "PFCOUNT", "GEOPOS", "GEODIST", "GEOHASH", "GEOSEARCH", "DUMP",
    "JSON.GET", "JSON.TYPE", "JSON.STRLEN", "JSON.ARRLEN", "JSON.OBJKEYS", "JSON.OBJLEN", "JSON.MGET",
};

// Administration commands that only look: Redis files them under @admin, but
// they're what you'd want to read in a read-only session.
const QSet<QString> kAdminReads = {
    "CONFIG|GET", "CLIENT|LIST", "CLIENT|INFO", "CLIENT|GETNAME", "CLIENT|ID", "SLOWLOG|GET", "SLOWLOG|LEN",
    "LATENCY|LATEST", "LATENCY|HISTORY", "LATENCY|DOCTOR", "MEMORY|STATS", "MEMORY|DOCTOR", "MEMORY|USAGE",
    "ACL|WHOAMI", "ACL|CAT", "ACL|USERS", "CLUSTER|INFO", "CLUSTER|NODES", "CLUSTER|SLOTS", "CLUSTER|SHARDS",
    "MODULE|LIST", "FUNCTION|LIST", "SCRIPT|EXISTS",
};

} // namespace

RedisSession::RedisSession(QObject *parent) : QObject(parent) {}

RedisSession::~RedisSession()
{
    close();
}

bool RedisSession::isOpen() const
{
    return m_client && m_client->isOpen();
}

void RedisSession::setError(const QString &message)
{
    if (message == m_error)
        return;
    m_error = message;
    emit errorChanged();
}

RedisClient::Reply RedisSession::call(const QList<QByteArray> &args)
{
    if (!m_client)
        return RedisClient::Reply{ RedisClient::Reply::Error, "Not connected", 0, 0, {} };
    const RedisClient::Reply r = m_client->command(args, CommandTimeout);
    if (!m_client->isOpen()) {                // it went away: say so, everywhere
        setError(tr("The connection to %1 was lost: %2").arg(m_location, r.text()));
        emit openChanged();
    }
    return r;
}

bool RedisSession::connectTo(const QVariantMap &settings)
{
    close();
    const QString host = settings.value(QStringLiteral("host"), QStringLiteral("localhost")).toString().trimmed();
    const int port = settings.value(QStringLiteral("port"), 6379).toInt() > 0 ? settings.value(QStringLiteral("port"), 6379).toInt() : 6379;
    const QString user = settings.value(QStringLiteral("user")).toString().trimmed();
    const QString password = settings.value(QStringLiteral("password")).toString();
    const int database = settings.value(QStringLiteral("database")).toInt();
    const QVariantMap ssl = settings.value(QStringLiteral("ssl")).toMap();
    const bool tls = ssl.value(QStringLiteral("mode")).toString() == QLatin1String("on");
    const bool verify = !ssl.value(QStringLiteral("trust"), false).toBool();

    QString connectHost = host;
    int connectPort = port;
    const QVariantMap ssh = settings.value(QStringLiteral("ssh")).toMap();
    if (!ssh.value(QStringLiteral("host")).toString().trimmed().isEmpty()) {
        m_tunnel = std::make_unique<SshTunnel>();
        SshTunnel::Options o;
        o.host = ssh.value(QStringLiteral("host")).toString().trimmed();
        o.port = ssh.value(QStringLiteral("port"), 22).toInt();
        o.user = ssh.value(QStringLiteral("user")).toString().trimmed();
        o.keyFile = ssh.value(QStringLiteral("key")).toString().trimmed();
        o.targetHost = host;
        o.targetPort = port;
        if (!m_tunnel->start(o)) {
            setError(tr("Couldn't open the SSH tunnel through %1 — %2").arg(o.host, m_tunnel->error()));
            m_tunnel.reset();
            return false;
        }
        connectHost = QStringLiteral("127.0.0.1");
        connectPort = m_tunnel->localPort();
    }

    m_client = std::make_unique<RedisClient>();
    QString why;
    if (!m_client->open(connectHost, connectPort, tls, verify, ConnectTimeout, &why)) {
        setError(tr("Couldn't connect to Redis at %1:%2 — %3").arg(host).arg(port).arg(why));
        close();
        return false;
    }
    auto fail = [&](const RedisClient::Reply &r) {
        setError(tr("Couldn't connect to Redis at %1:%2 — %3").arg(host).arg(port).arg(r.text()));
        close();
        return false;
    };
    if (!password.isEmpty()) {
        const RedisClient::Reply r = user.isEmpty() ? m_client->command({ "AUTH", b(password) }, ConnectTimeout)
                                                    : m_client->command({ "AUTH", b(user), b(password) }, ConnectTimeout);
        if (r.isError())
            return fail(r);
    }
    const RedisClient::Reply pong = m_client->command({ "PING" }, ConnectTimeout);
    if (pong.isError())
        return fail(pong);
    if (database != 0) {
        const RedisClient::Reply r = m_client->command({ "SELECT", QByteArray::number(database) }, ConnectTimeout);
        if (r.isError())
            return fail(r);
    }

    m_database = database;
    m_location = host + QLatin1Char(':') + QString::number(port);
    if (m_tunnel)
        m_location += tr(" via %1").arg(ssh.value(QStringLiteral("host")).toString().trimmed());
    m_displayName = host + QLatin1Char(':') + QString::number(port);
    m_settings = { { QStringLiteral("type"), QStringLiteral("redis") }, { QStringLiteral("host"), host },
                   { QStringLiteral("port"), port }, { QStringLiteral("user"), user },
                   { QStringLiteral("database"), database } };
    if (!ssl.isEmpty())
        m_settings.insert(QStringLiteral("ssl"), ssl);
    if (m_tunnel)
        m_settings.insert(QStringLiteral("ssh"), ssh);
    setError(QString());
    refresh();
    emit databaseChanged();
    emit openChanged();
    return true;
}

void RedisSession::close()
{
    const bool was = isOpen();
    m_client.reset();
    m_tunnel.reset();
    m_flags.clear();
    m_databases.clear();
    m_info.clear();
    m_infoSections.clear();
    m_settings.clear();
    m_displayName.clear();
    m_location.clear();
    if (m_changesAllowed) {
        m_changesAllowed = false;
        emit changesAllowedChanged();
    }
    if (was)
        emit openChanged();
}

void RedisSession::refresh()
{
    if (!isOpen())
        return;
    const RedisClient::Reply r = call({ "INFO", "everything" });
    m_infoSections.clear();
    QHash<QString, QString> all;
    QVariantMap section;
    QVariantList rows;
    auto flush = [&] {
        if (!section.isEmpty()) {
            section.insert(QStringLiteral("rows"), rows);
            m_infoSections << section;
        }
        rows.clear();
    };
    for (const QString &line : r.text().split(QStringLiteral("\r\n"))) {
        if (line.startsWith(QLatin1String("# "))) {
            flush();
            section = { { QStringLiteral("name"), line.mid(2).trimmed() } };
        } else if (line.contains(QLatin1Char(':'))) {
            const QString key = line.section(QLatin1Char(':'), 0, 0);
            const QString value = line.section(QLatin1Char(':'), 1);
            all.insert(key, value);
            rows << QVariantMap{ { QStringLiteral("key"), key }, { QStringLiteral("value"), value } };
        }
    }
    flush();

    const double hits = all.value(QStringLiteral("keyspace_hits")).toDouble();
    const double misses = all.value(QStringLiteral("keyspace_misses")).toDouble();
    m_info = {
        { QStringLiteral("version"), all.value(QStringLiteral("redis_version"), all.value(QStringLiteral("valkey_version"))) },
        { QStringLiteral("mode"), all.value(QStringLiteral("redis_mode")) },
        { QStringLiteral("role"), all.value(QStringLiteral("role")) },
        { QStringLiteral("os"), all.value(QStringLiteral("os")) },
        { QStringLiteral("uptimeDays"), all.value(QStringLiteral("uptime_in_days")).toInt() },
        { QStringLiteral("clients"), all.value(QStringLiteral("connected_clients")).toInt() },
        { QStringLiteral("memory"), all.value(QStringLiteral("used_memory_human")) },
        { QStringLiteral("peakMemory"), all.value(QStringLiteral("used_memory_peak_human")) },
        { QStringLiteral("maxMemory"), all.value(QStringLiteral("maxmemory_human")) },
        { QStringLiteral("evictionPolicy"), all.value(QStringLiteral("maxmemory_policy")) },
        { QStringLiteral("opsPerSec"), all.value(QStringLiteral("instantaneous_ops_per_sec")).toInt() },
        { QStringLiteral("commands"), all.value(QStringLiteral("total_commands_processed")).toLongLong() },
        { QStringLiteral("hitRate"), hits + misses > 0 ? hits / (hits + misses) : -1.0 },
        { QStringLiteral("persistence"), all.value(QStringLiteral("aof_enabled")) == QLatin1String("1") ? tr("AOF")
                                       : all.value(QStringLiteral("rdb_last_save_time")).isEmpty() ? QString() : tr("RDB snapshots") },
    };

    // The databases: their key counts from INFO's keyspace; how many from CONFIG.
    QHash<int, QPair<qint64, qint64>> counts;
    for (auto it = all.cbegin(); it != all.cend(); ++it) {
        if (!it.key().startsWith(QLatin1String("db")))
            continue;
        bool ok = false;
        const int index = it.key().mid(2).toInt(&ok);
        if (!ok)
            continue;
        qint64 keys = 0, expires = 0;
        for (const QString &part : it.value().split(QLatin1Char(','))) {
            if (part.startsWith(QLatin1String("keys="))) keys = part.mid(5).toLongLong();
            else if (part.startsWith(QLatin1String("expires="))) expires = part.mid(8).toLongLong();
        }
        counts.insert(index, { keys, expires });
    }
    int howMany = 16;
    const RedisClient::Reply cfg = call({ "CONFIG", "GET", "databases" });
    if (cfg.type == RedisClient::Reply::Array && cfg.items.size() >= 2)
        howMany = cfg.items.at(1).text().toInt();
    else if (cfg.type == RedisClient::Reply::Map && cfg.items.size() >= 2)
        howMany = cfg.items.at(1).text().toInt();
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        howMany = qMax(howMany, it.key() + 1);
    if (m_info.value(QStringLiteral("mode")).toString() == QLatin1String("cluster"))
        howMany = 1;                            // a cluster has only db 0
    m_databases.clear();
    for (int i = 0; i < howMany; ++i)
        m_databases << QVariantMap{ { QStringLiteral("index"), i }, { QStringLiteral("keys"), counts.value(i).first },
                                    { QStringLiteral("expires"), counts.value(i).second } };
    emit infoChanged();
}

bool RedisSession::selectDatabase(int index)
{
    if (!isOpen())
        return false;
    const RedisClient::Reply r = call({ "SELECT", QByteArray::number(index) });
    if (r.isError()) {
        setError(r.text());
        return false;
    }
    m_database = index;
    m_settings.insert(QStringLiteral("database"), index);
    emit databaseChanged();
    return true;
}

QVariantMap RedisSession::scan(const QString &pattern, const QString &cursor, int count)
{
    QVariantMap out{ { QStringLiteral("cursor"), QStringLiteral("0") }, { QStringLiteral("keys"), QVariantList() } };
    if (!isOpen())
        return out;
    QList<QByteArray> args{ "SCAN", b(cursor.isEmpty() ? QStringLiteral("0") : cursor) };
    if (!pattern.trimmed().isEmpty() && pattern.trimmed() != QLatin1String("*"))
        args << "MATCH" << b(pattern.trimmed());
    args << "COUNT" << QByteArray::number(qMax(10, count));
    const RedisClient::Reply r = call(args);
    if (r.isError() || r.items.size() < 2) {
        setError(r.text());
        return out;
    }
    out.insert(QStringLiteral("cursor"), r.items.at(0).text());
    // Each key's type and TTL, in one round trip.
    QList<QList<QByteArray>> asks;
    for (const RedisClient::Reply &k : r.items.at(1).items) {
        asks << QList<QByteArray>{ "TYPE", k.str };
        asks << QList<QByteArray>{ "PTTL", k.str };
    }
    const QVector<RedisClient::Reply> answers = asks.isEmpty() ? QVector<RedisClient::Reply>() : m_client->pipeline(asks, CommandTimeout);
    QVariantList keys;
    for (int i = 0; i < r.items.at(1).items.size(); ++i) {
        const QByteArray name = r.items.at(1).items.at(i).str;
        keys << QVariantMap{ { QStringLiteral("key"), textOf(name) },
                             { QStringLiteral("type"), answers.value(2 * i).text() },
                             { QStringLiteral("ttl"), answers.value(2 * i + 1).integer } };
    }
    out.insert(QStringLiteral("keys"), keys);
    return out;
}

QVariantMap RedisSession::keyInfo(const QString &key)
{
    if (!isOpen())
        return {};
    const QByteArray k = b(key);
    const QString type = call({ "TYPE", k }).text();
    // (Each branch a QByteArray: MSVC won't choose between a literal and one.)
    const QByteArray sizeCommand = type == QLatin1String("string") ? QByteArray("STRLEN")
                                 : type == QLatin1String("hash")   ? QByteArray("HLEN")
                                 : type == QLatin1String("list")   ? QByteArray("LLEN")
                                 : type == QLatin1String("set")    ? QByteArray("SCARD")
                                 : type == QLatin1String("zset")   ? QByteArray("ZCARD")
                                 : type == QLatin1String("stream") ? QByteArray("XLEN") : QByteArray();
    QList<QList<QByteArray>> asks{ { "PTTL", k }, { "OBJECT", "ENCODING", k }, { "MEMORY", "USAGE", k } };
    if (!sizeCommand.isEmpty())
        asks << QList<QByteArray>{ sizeCommand, k };
    const QVector<RedisClient::Reply> a = m_client->pipeline(asks, CommandTimeout);
    return { { QStringLiteral("key"), key }, { QStringLiteral("type"), type },
             { QStringLiteral("exists"), type != QLatin1String("none") },
             { QStringLiteral("ttl"), a.value(0).integer },
             { QStringLiteral("encoding"), a.value(1).isError() ? QString() : a.value(1).text() },
             { QStringLiteral("memory"), a.value(2).isError() ? -1 : a.value(2).integer },
             { QStringLiteral("size"), sizeCommand.isEmpty() ? -1 : a.value(3).integer } };
}

QVariantMap RedisSession::value(const QString &key, const QVariant &from, int count)
{
    QVariantMap out;
    if (!isOpen())
        return out;
    const QByteArray k = b(key);
    const QString type = call({ "TYPE", k }).text();
    out.insert(QStringLiteral("kind"), type);
    count = qBound(1, count, 10000);
    QVariantList rows;

    if (type == QLatin1String("string")) {
        const qint64 length = call({ "STRLEN", k }).integer;
        const RedisClient::Reply v = length > MaxStringPreview
            ? call({ "GETRANGE", k, "0", QByteArray::number(MaxStringPreview - 1) }) : call({ "GET", k });
        bool binary = false;
        const QString text = textOf(v.str, &binary);
        QString pretty;
        if (!binary) {
            const QJsonDocument doc = QJsonDocument::fromJson(v.str);
            if (doc.isObject() || doc.isArray())
                pretty = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
        }
        out.insert(QStringLiteral("text"), pretty.isEmpty() ? text : pretty);
        out.insert(QStringLiteral("raw"), text);
        out.insert(QStringLiteral("json"), !pretty.isEmpty());
        out.insert(QStringLiteral("binary"), binary);
        out.insert(QStringLiteral("length"), length);
        out.insert(QStringLiteral("truncated"), length > MaxStringPreview);
        return out;
    }
    if (type == QLatin1String("ReJSON-RL")) {             // RedisJSON
        const RedisClient::Reply v = call({ "JSON.GET", k });
        const QJsonDocument doc = QJsonDocument::fromJson(v.str);
        out.insert(QStringLiteral("kind"), QStringLiteral("json"));
        out.insert(QStringLiteral("text"), doc.isNull() ? v.text() : QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
        out.insert(QStringLiteral("json"), true);
        return out;
    }
    if (type == QLatin1String("hash") || type == QLatin1String("set")) {
        const bool hash = type == QLatin1String("hash");
        const QString cursor = from.toString().isEmpty() ? QStringLiteral("0") : from.toString();
        const QVector<RedisClient::Reply> a = m_client->pipeline(
            { { QByteArray(hash ? "HLEN" : "SCARD"), k },
              { QByteArray(hash ? "HSCAN" : "SSCAN"), k, b(cursor), "COUNT", QByteArray::number(count) } }, CommandTimeout);
        out.insert(QStringLiteral("total"), a.value(0).integer);
        const RedisClient::Reply page = a.value(1);
        out.insert(QStringLiteral("next"), page.items.size() >= 2 ? page.items.at(0).text() : QStringLiteral("0"));
        const QVector<RedisClient::Reply> items = page.items.size() >= 2 ? page.items.at(1).items : QVector<RedisClient::Reply>();
        if (hash) {
            for (int i = 0; i + 1 < items.size(); i += 2)
                rows << QVariantMap{ { QStringLiteral("field"), textOf(items.at(i).str) },
                                     { QStringLiteral("value"), textOf(items.at(i + 1).str) } };
        } else {
            for (const RedisClient::Reply &m : items)
                rows << QVariantMap{ { QStringLiteral("member"), textOf(m.str) } };
        }
    } else if (type == QLatin1String("list") || type == QLatin1String("zset")) {
        const bool list = type == QLatin1String("list");
        const qint64 start = from.toLongLong();
        const QByteArray stop = QByteArray::number(start + count - 1);
        QList<QByteArray> range = list ? QList<QByteArray>{ "LRANGE", k, QByteArray::number(start), stop }
                                       : QList<QByteArray>{ "ZRANGE", k, QByteArray::number(start), stop, "WITHSCORES" };
        const QVector<RedisClient::Reply> a = m_client->pipeline({ { QByteArray(list ? "LLEN" : "ZCARD"), k }, range }, CommandTimeout);
        const qint64 total = a.value(0).integer;
        out.insert(QStringLiteral("total"), total);
        const QVector<RedisClient::Reply> items = a.value(1).items;
        if (list) {
            for (int i = 0; i < items.size(); ++i)
                rows << QVariantMap{ { QStringLiteral("index"), start + i }, { QStringLiteral("value"), textOf(items.at(i).str) } };
        } else {
            // RESP2: member, score, member, score…; RESP3: [member, score] pairs.
            for (int i = 0; i < items.size(); ++i) {
                const RedisClient::Reply &m = items.at(i);
                if (m.type == RedisClient::Reply::Array && m.items.size() == 2)
                    rows << QVariantMap{ { QStringLiteral("member"), textOf(m.items.at(0).str) },
                                         { QStringLiteral("score"), m.items.at(1).str.toDouble() } };
                else if (i + 1 < items.size()) {
                    rows << QVariantMap{ { QStringLiteral("member"), textOf(m.str) },
                                         { QStringLiteral("score"), items.at(i + 1).str.toDouble() } };
                    ++i;
                }
            }
        }
        out.insert(QStringLiteral("next"), start + rows.size() < total ? QVariant(start + rows.size()) : QVariant());
    } else if (type == QLatin1String("stream")) {
        const QString start = from.toString().isEmpty() ? QStringLiteral("-") : from.toString();
        const QVector<RedisClient::Reply> a = m_client->pipeline(
            { { "XLEN", k }, { "XRANGE", k, b(start), "+", "COUNT", QByteArray::number(count) } }, CommandTimeout);
        out.insert(QStringLiteral("total"), a.value(0).integer);
        QString last;
        for (const RedisClient::Reply &e : a.value(1).items) {
            if (e.items.size() < 2)
                continue;
            last = e.items.at(0).text();
            QStringList fields;
            const QVector<RedisClient::Reply> &kv = e.items.at(1).items;
            for (int i = 0; i + 1 < kv.size(); i += 2)
                fields << textOf(kv.at(i).str) + QStringLiteral(" = ") + textOf(kv.at(i + 1).str);
            rows << QVariantMap{ { QStringLiteral("id"), last }, { QStringLiteral("fields"), fields.join(QStringLiteral(", ")) } };
        }
        // The next page starts just after the last id seen.
        out.insert(QStringLiteral("next"), rows.size() == count && !last.isEmpty() ? QVariant(QStringLiteral("(") + last) : QVariant());
    } else if (type == QLatin1String("none")) {
        out.insert(QStringLiteral("text"), tr("There's no key %1 (any more).").arg(key));
        return out;
    } else {
        out.insert(QStringLiteral("text"), tr("Studio can't show %1 values yet; the console can (e.g. DUMP).").arg(type));
        return out;
    }
    out.insert(QStringLiteral("rows"), rows);
    return out;
}

QVariantMap RedisSession::commandFlags(const QString &name)
{
    const QString lower = name.toLower();
    if (m_flags.contains(lower))
        return m_flags.value(lower);
    QVariantMap f;
    const RedisClient::Reply r = call({ "COMMAND", "INFO", b(lower) });
    if (r.isError()) {
        f.insert(QStringLiteral("unknown"), true);
        f.insert(QStringLiteral("denied"), true);         // the server won't say: go by the list
    } else if (r.items.isEmpty() || r.items.first().isNil()) {
        f.insert(QStringLiteral("unknown"), true);
    } else {
        const RedisClient::Reply &info = r.items.first();
        QStringList flags, categories;
        if (info.items.size() > 2)
            for (const RedisClient::Reply &x : info.items.at(2).items)
                flags << x.text().toLower();
        if (info.items.size() > 6)
            for (const RedisClient::Reply &x : info.items.at(6).items)
                categories << x.text().toLower();
        f.insert(QStringLiteral("flags"), flags);
        f.insert(QStringLiteral("categories"), categories);
    }
    m_flags.insert(lower, f);
    return f;
}

bool RedisSession::allowed(const QList<QByteArray> &args, QString *why)
{
    if (args.isEmpty())
        return false;
    const QString name = QString::fromUtf8(args.first()).toUpper();
    if (kNeverRun.contains(name)) {
        *why = tr("%1 waits for something to happen, which would hold up Studio; run it from redis-cli.").arg(name);
        return false;
    }
    if ((name == QLatin1String("XREAD") || name == QLatin1String("XREADGROUP"))
        && std::any_of(args.cbegin(), args.cend(), [](const QByteArray &a) { return a.toUpper() == "BLOCK"; })) {
        *why = tr("%1 … BLOCK would hold up Studio; leave out BLOCK.").arg(name);
        return false;
    }
    if (m_changesAllowed)
        return true;
    if (args.size() > 1 && kAdminReads.contains(name + QLatin1Char('|') + QString::fromUtf8(args.at(1)).toUpper()))
        return true;

    // A container command (CONFIG GET, CLIENT LIST…) is judged by its subcommand.
    QVariantMap f;
    if (args.size() > 1) {
        f = commandFlags(name + QLatin1Char('|') + QString::fromUtf8(args.at(1)));
        if (f.value(QStringLiteral("unknown")).toBool() && !f.value(QStringLiteral("denied")).toBool())
            f = commandFlags(name);
    } else {
        f = commandFlags(name);
    }
    if (f.value(QStringLiteral("denied")).toBool()) {
        if (kReadOnly.contains(name))
            return true;
        *why = tr("Read-only: Studio can't tell whether %1 changes anything (the server won't say), so it isn't run. "
                  "Allow changes to run it.").arg(name);
        return false;
    }
    if (f.value(QStringLiteral("unknown")).toBool())
        return true;                                       // the server will say it doesn't know it
    const QStringList flags = f.value(QStringLiteral("flags")).toStringList();
    const QStringList cats = f.value(QStringLiteral("categories")).toStringList();
    const bool writes = flags.contains(QLatin1String("write")) || cats.contains(QLatin1String("@write"));
    const bool admin = flags.contains(QLatin1String("admin")) || cats.contains(QLatin1String("@admin"));
    const bool publishes = flags.contains(QLatin1String("pubsub")) && name == QLatin1String("PUBLISH");
    if (writes || admin || publishes) {
        *why = tr("Read-only: %1 %2. Allow changes to run it.")
                   .arg(name, writes ? tr("changes the data") : publishes ? tr("sends a message") : tr("administers the server"));
        return false;
    }
    return true;
}

QVariantMap RedisSession::run(const QString &line)
{
    QVariantMap out{ { QStringLiteral("ok"), false }, { QStringLiteral("refused"), false } };
    if (!isOpen()) {
        out.insert(QStringLiteral("error"), tr("Not connected."));
        return out;
    }
    QString why;
    const QList<QByteArray> args = RedisClient::split(line, &why);
    if (args.isEmpty()) {
        out.insert(QStringLiteral("error"), why.isEmpty() ? tr("Type a command.") : why);
        return out;
    }
    if (!allowed(args, &why)) {
        out.insert(QStringLiteral("refused"), true);
        out.insert(QStringLiteral("error"), why);
        return out;
    }
    QElapsedTimer timer;
    timer.start();
    const RedisClient::Reply r = call(args);
    out.insert(QStringLiteral("ms"), timer.elapsed());
    out.insert(QStringLiteral("text"), RedisClient::format(r));
    out.insert(QStringLiteral("ok"), !r.isError());
    if (r.isError())
        out.insert(QStringLiteral("error"), r.text());
    const QString name = QString::fromUtf8(args.first()).toUpper();
    if (name == QLatin1String("SELECT") && !r.isError() && args.size() > 1) {
        m_database = args.at(1).toInt();
        m_settings.insert(QStringLiteral("database"), m_database);
        emit databaseChanged();
    }
    return out;
}

void RedisSession::allowChanges(bool on)
{
    if (on == m_changesAllowed)
        return;
    m_changesAllowed = on;
    emit changesAllowedChanged();
}
