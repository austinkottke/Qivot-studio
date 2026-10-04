#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "databasesession.h"
#include "qivotcli.h"
#include "queryreplay.h"
#include "sampledatabase.h"

/// Replaying recorded queries before and after pending migrations.
class TestReplay : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_shop, m_record, m_migrations;

    void write(const QString &path, const QByteArray &text)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text);
    }

    static QByteArray line(const QString &sql, const QJsonArray &values, int more = 0)
    {
        QJsonObject o{ { "driver", "QSQLITE" }, { "sql", sql }, { "values", values }, { "ms", 0.1 } };
        QByteArray text = QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n";
        if (more)
            text += QJsonDocument(QJsonObject{ { "driver", "QSQLITE" }, { "sql", sql }, { "more", more } }).toJson(QJsonDocument::Compact) + "\n";
        return text;
    }

    static const QueryReplay::Item *find(const QueryReplay::Report &r, const QString &start)
    {
        for (const QueryReplay::Item &i : r.items)
            if (i.sql.startsWith(start))
                return &i;
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_shop = m_dir.filePath("shop.db");
        QVERIFY(SampleDatabase::create(m_shop));

        // What the app ran (as QiRecorder writes it).
        m_record = m_dir.filePath("app.qrec");
        write(m_record,
              line("SELECT title, isbn FROM book WHERE id = :id", { 1 }, 311)
              + line("SELECT COUNT(*) FROM book WHERE author_id = ?", { 3 }, 40)
              + line("SELECT price FROM book WHERE id = ?", { 2 })
              + line("SELECT * FROM book WHERE id = ?", { 5 })
              + line("INSERT INTO publisher (name) VALUES (?)", { "Replay Press" })
              + line("SELECT * FROM no_such_table", {})
              + line("CREATE TABLE IF NOT EXISTS scratch (id INTEGER)", {}));

        // What's about to ship.
        m_migrations = m_dir.filePath("migrations");
        QVERIFY(QDir().mkpath(m_migrations));
        write(m_migrations + "/0001_isbn13.sql", "ALTER TABLE book RENAME COLUMN isbn TO isbn13;\n");
        write(m_migrations + "/0002_reprice.sql", "BEGIN;\nDROP INDEX idx_book_author;\nUPDATE book SET price = price * 2;\nCOMMIT;\n");
    }

    void sqlite()
    {
        DatabaseSession s;
        QVERIFY(s.open(m_shop));
        QueryReplay::Options o;
        o.dir = m_migrations;
        o.repeat = 1;
        const QueryReplay::Report r = QueryReplay::run(s, m_record, o);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY2(r.migrationError.isEmpty(), qPrintable(r.migrationError));
        QCOMPARE(r.migrations, QStringList({ "0001 isbn13", "0002 reprice" }));
        QCOMPARE(r.items.size(), 6);
        QCOMPARE(r.skipped, 1);                          // the CREATE TABLE
        QCOMPARE(r.recordedRuns, qint64(312 + 41 + 1 + 1 + 1 + 1 + 1));

        // SELECT * still runs, but a model's isbn field would come back empty.
        const QueryReplay::Item *star = find(r, "SELECT * FROM book");
        QVERIFY(star);
        QCOMPARE(star->verdict, QString("columns"));
        QCOMPARE(star->columnsGone, QStringList({ "isbn" }));
        QCOMPARE(star->columnsNew, QStringList({ "isbn13" }));

        const QueryReplay::Item *isbn = find(r, "SELECT title, isbn");
        QVERIFY(isbn);
        QCOMPARE(isbn->verdict, QString("breaks"));
        QVERIFY(isbn->after.error.contains("isbn"));
        QCOMPARE(isbn->runs, qint64(312));
        QCOMPARE(r.items.first().verdict, QString("breaks"));     // worst first

        const QueryReplay::Item *price = find(r, "SELECT price");
        QVERIFY(price);
        QCOMPARE(price->verdict, QString("different"));
        QCOMPARE(price->before.rows, price->after.rows);

        const QueryReplay::Item *author = find(r, "SELECT COUNT(*) FROM book WHERE author_id");
        QVERIFY(author);
        QVERIFY(author->planChanged);                    // the index it used is gone
        QVERIFY2(author->before.plan.contains("idx_book_author"), qPrintable(author->before.plan));
        QVERIFY(!author->after.plan.contains("idx_book_author"));
        QVERIFY(author->after.scans > author->before.scans);

        const QueryReplay::Item *insert = find(r, "INSERT INTO publisher");
        QVERIFY(insert);
        QCOMPARE(insert->verdict, QString("same"));
        QCOMPARE(insert->before.rows, qint64(1));

        const QueryReplay::Item *missing = find(r, "SELECT * FROM no_such_table");
        QVERIFY(missing);
        QCOMPARE(missing->verdict, QString("failing"));

        // Nothing changed: the original still has isbn, its index and its prices, and no new publisher.
        QSqlDatabase db = QSqlDatabase::database(s.connectionName());
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT isbn FROM book LIMIT 1"));
        QVERIFY(q.exec("SELECT COUNT(*) FROM sqlite_master WHERE name = 'idx_book_author'") && q.next());
        QCOMPARE(q.value(0).toInt(), 1);
        QVERIFY(q.exec("SELECT COUNT(*) FROM publisher WHERE name = 'Replay Press'") && q.next());
        QCOMPARE(q.value(0).toInt(), 0);
        QVERIFY(!db.tables().contains("qivot_migrations"));
    }

    void brokenMigration()
    {
        const QString dir = m_dir.filePath("broken");
        QVERIFY(QDir().mkpath(dir));
        write(dir + "/0001_oops.sql", "ALTER TABLE no_such_table ADD COLUMN x TEXT;\n");
        DatabaseSession s;
        QVERIFY(s.open(m_shop));
        QueryReplay::Options o;
        o.dir = dir;
        const QueryReplay::Report r = QueryReplay::run(s, m_record, o);
        QVERIFY(r.ok);
        QVERIFY(r.migrationError.contains("no_such_table"));
        QVERIFY(r.items.isEmpty());
    }

    void refusals()
    {
        DatabaseSession s;
        QVERIFY(s.open(m_shop));
        QueryReplay::Options o;
        o.dir = m_dir.filePath("empty");
        QVERIFY(QDir().mkpath(o.dir));
        QueryReplay::Report r = QueryReplay::run(s, m_record, o);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("no pending migrations"));

        o.dir = m_migrations;
        r = QueryReplay::run(s, m_dir.filePath("missing.qrec"), o);
        QVERIFY(!r.ok);

        QVERIFY(QueryReplay::replayable("  -- a note\nSELECT 1"));
        QVERIFY(QueryReplay::replayable("with x as (select 1) select * from x"));
        QVERIFY(!QueryReplay::replayable("PRAGMA foreign_keys = ON"));
        QVERIFY(!QueryReplay::replayable("CREATE TABLE t (id INT)"));
    }

    void cli()
    {
        QString out, err;
        QTextStream o(&out), e(&err);
        const int code = QivotCli::run({ "replay", m_shop, "--record", m_record, "--dir", m_migrations, "--exit-code" }, o, e);
        o.flush(); e.flush();
        QCOMPARE(code, int(QivotCli::Differs));
        QVERIFY2(out.contains("x 1 break after the migrations"), qPrintable(out + err));
        QVERIFY(out.contains("! 1 return different rows"));
        QVERIFY(out.contains("0001 isbn13"));
        QVERIFY(out.contains("3 of the app's queries won't work as they did"));
        QVERIFY(out.contains("gone: isbn"));

        out.clear();
        QivotCli::run({ "replay", m_shop, "--record", m_record, "--dir", m_migrations, "--json" }, o, e);
        o.flush();
        const QJsonObject j = QJsonDocument::fromJson(out.toUtf8()).object();
        QCOMPARE(j.value("queries").toArray().size(), 6);
        QCOMPARE(j.value("queries").toArray().first().toObject().value("verdict").toString(), QString("breaks"));
    }
};

QTEST_GUILESS_MAIN(TestReplay)
#include "tst_replay.moc"
