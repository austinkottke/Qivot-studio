#include <QtTest>

#include "redisclient.h"
#include "redissession.h"

/// Redis: the protocol on its own, and (with STUDIO_TEST_REDIS=host:port,
/// STUDIO_TEST_REDIS_PASS) a real server, through the Redis screens' session.
class TestRedis : public QObject
{
    Q_OBJECT

private:
    static QVariantMap settings()
    {
        const QString where = qEnvironmentVariable("STUDIO_TEST_REDIS");
        return { { "host", where.section(':', 0, 0) }, { "port", where.section(':', 1, 1).toInt() },
                 { "password", qEnvironmentVariable("STUDIO_TEST_REDIS_PASS") }, { "database", 7 } };
    }

private slots:
    // ---- The protocol ----
    void parsesReplies()
    {
        RedisClient::Reply r;
        QCOMPARE(RedisClient::parse("+OK\r\n", 0, &r), 5);
        QCOMPARE(r.type, RedisClient::Reply::Status);
        QCOMPARE(r.text(), QString("OK"));
        QCOMPARE(RedisClient::parse(":42\r\n", 0, &r), 5);
        QCOMPARE(r.integer, 42LL);
        QCOMPARE(RedisClient::parse("$-1\r\n", 0, &r), 5);
        QVERIFY(r.isNil());
        QCOMPARE(RedisClient::parse("$5\r\nhe\r\no\r\n", 0, &r), 11);         // a CRLF inside the value
        QCOMPARE(r.str, QByteArray("he\r\no"));
        const QByteArray nested = "*2\r\n$3\r\nfoo\r\n*2\r\n:1\r\n-ERR no\r\n";
        QCOMPARE(RedisClient::parse(nested, 0, &r), nested.size());
        QCOMPARE(r.items.size(), 2);
        QCOMPARE(r.items.at(1).items.at(1).type, RedisClient::Reply::Error);
        // RESP3: a map, a double, a boolean, verbatim text.
        QCOMPARE(RedisClient::parse("%1\r\n+a\r\n,1.5\r\n", 0, &r), 14);
        QCOMPARE(r.type, RedisClient::Reply::Map);
        QCOMPARE(r.items.at(1).number, 1.5);
        QCOMPARE(RedisClient::parse("#t\r\n", 0, &r), 4);
        QCOMPARE(r.integer, 1LL);
        QCOMPARE(RedisClient::parse("=8\r\ntxt:abcd\r\n", 0, &r), 14);
        QCOMPARE(r.str, QByteArray("abcd"));
        // Not all there yet: wait for more. Not RESP: say so.
        QCOMPARE(RedisClient::parse("$5\r\nhel", 0, &r), 0);
        QCOMPARE(RedisClient::parse("*2\r\n:1\r\n", 0, &r), 0);
        QCOMPARE(RedisClient::parse("hello\r\n", 0, &r), -1);
        QCOMPARE(RedisClient::encode({ "SET", "k", "a b" }), QByteArray("*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$3\r\na b\r\n"));
    }

    void splitsAndFormats()
    {
        QCOMPARE(RedisClient::split("SET \"my key\" 'it''s' \"a\\\"b\\n\\x41\" plain"),
                 (QList<QByteArray>{ "SET", "my key", "it", "s", "a\"b\nA", "plain" }));
        QCOMPARE(RedisClient::split("GET 'it\\'s'"), (QList<QByteArray>{ "GET", "it's" }));
        QString why;
        QVERIFY(RedisClient::split("GET \"open", &why).isEmpty());
        QVERIFY(!why.isEmpty());

        RedisClient::Reply r;
        RedisClient::parse("*3\r\n$1\r\na\r\n:2\r\n*2\r\n$1\r\nb\r\n$-1\r\n", 0, &r);
        QCOMPARE(RedisClient::format(r), QString("1) \"a\"\n2) (integer) 2\n3) 1) \"b\"\n   2) (nil)"));
        RedisClient::parse("*0\r\n", 0, &r);
        QCOMPARE(RedisClient::format(r), QString("(empty array)"));
        RedisClient::parse("-WRONGTYPE no\r\n", 0, &r);
        QCOMPARE(RedisClient::format(r), QString("(error) WRONGTYPE no"));
    }

    // ---- A real server ----
    void browsesAServer()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_REDIS"))
            QSKIP("STUDIO_TEST_REDIS not set");
        // Data of every kind, in database 7, written with a plain client.
        {
            RedisClient w;
            QString why;
            const QVariantMap s = settings();
            QVERIFY2(w.open(s["host"].toString(), s["port"].toInt(), false, false, 5000, &why), qPrintable(why));
            if (!s["password"].toString().isEmpty())
                QVERIFY(!w.command({ "AUTH", s["password"].toString().toUtf8() }).isError());
            QList<QList<QByteArray>> setup{ { "SELECT", "7" }, { "FLUSHDB" },
                { "SET", "user:1:name", "Ada" }, { "SET", "config", "{\"theme\":\"dark\",\"size\":3}" },
                { "SET", "blob", QByteArray("\x00\x01\xff", 3) }, { "SET", "session:x", "1", "EX", "600" },
                { "HSET", "user:1", "name", "Ada", "born", "1815" },
                { "RPUSH", "queue", "a", "b", "c", "d", "e" },
                { "SADD", "tags", "red", "green" },
                { "ZADD", "scores", "3", "carol", "1", "alice", "2", "bob" },
                { "XADD", "events", "1-1", "kind", "login" }, { "XADD", "events", "2-1", "kind", "logout" } };
            for (int i = 0; i < 300; ++i)
                setup << QList<QByteArray>{ "SET", "bulk:" + QByteArray::number(i), "v" };
            for (const RedisClient::Reply &r : w.pipeline(setup))
                QVERIFY2(!r.isError(), qPrintable(r.text()));
        }

        RedisSession redis;
        QVERIFY2(redis.connectTo(settings()), qPrintable(redis.error()));
        QVERIFY(redis.isOpen());
        QCOMPARE(redis.database(), 7);
        QVERIFY(redis.info().value("version").toString().startsWith("7"));
        QVERIFY(!redis.connectionSettings().contains("password"));
        const QVariantMap db7 = redis.databases().value(7).toMap();
        QCOMPARE(db7.value("keys").toLongLong(), 309LL);
        QVERIFY(db7.value("expires").toLongLong() >= 1);

        // Every key, a page at a time, with types and TTLs.
        QHash<QString, QString> types;
        QString cursor = "0";
        int rounds = 0;
        do {
            const QVariantMap page = redis.scan("*", cursor, 100);
            for (const QVariant &k : page.value("keys").toList())
                types.insert(k.toMap().value("key").toString(), k.toMap().value("type").toString());
            cursor = page.value("cursor").toString();
        } while (cursor != "0" && ++rounds < 100);
        QCOMPARE(types.size(), 309);
        QCOMPARE(types.value("user:1"), QString("hash"));
        QCOMPARE(types.value("scores"), QString("zset"));
        QCOMPARE(types.value("events"), QString("stream"));
        const QVariantList users = redis.scan("user:*", "0", 1000).value("keys").toList();
        QVERIFY(!users.isEmpty());
        for (const QVariant &k : users)
            QVERIFY(k.toMap().value("key").toString().startsWith("user:"));

        // Values, by type.
        QVariantMap v = redis.value("config");
        QVERIFY(v.value("json").toBool());
        QVERIFY(v.value("text").toString().contains("\"theme\": \"dark\""));
        v = redis.value("blob");
        QVERIFY(v.value("binary").toBool());
        QCOMPARE(v.value("text").toString(), QString("00 01 ff"));
        v = redis.value("user:1");
        QCOMPARE(v.value("total").toInt(), 2);
        QCOMPARE(v.value("rows").toList().size(), 2);
        v = redis.value("queue", 2, 2);
        QCOMPARE(v.value("total").toInt(), 5);
        QCOMPARE(v.value("rows").toList().first().toMap().value("value").toString(), QString("c"));
        QCOMPARE(v.value("next").toInt(), 4);
        v = redis.value("scores");
        QCOMPARE(v.value("rows").toList().first().toMap().value("member").toString(), QString("alice"));
        QCOMPARE(v.value("rows").toList().last().toMap().value("score").toDouble(), 3.0);
        v = redis.value("events", QVariant(), 1);
        QCOMPARE(v.value("rows").toList().first().toMap().value("fields").toString(), QString("kind = login"));
        QCOMPARE(redis.value("events", v.value("next"), 1).value("rows").toList().first().toMap().value("id").toString(), QString("2-1"));
        const QVariantMap info = redis.keyInfo("session:x");
        QVERIFY(info.value("ttl").toLongLong() > 0);
        QCOMPARE(info.value("size").toInt(), 1);

        // The console: reads run, writes and waits don't.
        QVariantMap r = redis.run("HGET user:1 born");
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(r.value("text").toString(), QString("\"1815\""));
        r = redis.run("SET user:1:name Grace");
        QVERIFY(r.value("refused").toBool());
        QCOMPARE(redis.run("GET user:1:name").value("text").toString(), QString("\"Ada\""));
        QVERIFY(redis.run("FLUSHDB").value("refused").toBool());
        QVERIFY(redis.run("CONFIG SET maxmemory 1").value("refused").toBool());
        QVERIFY(redis.run("CONFIG GET maxmemory").value("ok").toBool());
        QVERIFY(redis.run("BLPOP queue 0").value("refused").toBool());
        QVERIFY(redis.run("MONITOR").value("refused").toBool());
        QVERIFY(redis.run("XREAD COUNT 1 STREAMS events 0").value("ok").toBool());
        QVERIFY(redis.run("XREAD BLOCK 0 STREAMS events $").value("refused").toBool());
        QVERIFY(redis.run("INFO memory").value("ok").toBool());
        QVERIFY(!redis.run("NOSUCHCOMMAND").value("ok").toBool());

        // With changes allowed.
        redis.allowChanges(true);
        QVERIFY(redis.run("SET user:1:name Grace").value("ok").toBool());
        QCOMPARE(redis.run("GET user:1:name").value("text").toString(), QString("\"Grace\""));
        QVERIFY(redis.run("MONITOR").value("refused").toBool());          // still never
        redis.allowChanges(false);

        // Another database, from the console too.
        QVERIFY(redis.selectDatabase(0));
        QCOMPARE(redis.database(), 0);
        QVERIFY(redis.run("SELECT 7").value("ok").toBool());
        QCOMPARE(redis.database(), 7);
        redis.close();
        QVERIFY(!redis.isOpen());

        // A wrong password says so.
        QVariantMap bad = settings();
        bad["password"] = "wrong";
        QVERIFY(!redis.connectTo(bad));
        QVERIFY2(redis.error().contains("WRONGPASS") || redis.error().contains("invalid"), qPrintable(redis.error()));
    }
};

QTEST_GUILESS_MAIN(TestRedis)
#include "tst_redis.moc"
