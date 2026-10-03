#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "qivotcli.h"
#include "sampledatabase.h"

/// qivot-cli's commands, run in-process on sample files.
class TestCli : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_shop, m_changed;

    struct Run { int code; QString out, err; };

    static Run cli(const QStringList &args)
    {
        QString out, err;
        QTextStream o(&out), e(&err);
        const int code = QivotCli::run(args, o, e);
        o.flush();
        e.flush();
        return { code, out, err };
    }

    QString path(const QString &name) const { return m_dir.filePath(name); }

    static bool exec(const QString &file, const QString &sql)
    {
        bool ok;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "tst_cli_exec");
            db.setDatabaseName(file);
            ok = db.open() && QSqlQuery(db).exec(sql);
        }
        QSqlDatabase::removeDatabase("tst_cli_exec");
        return ok;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_dir.isValid());
        m_shop = path("shop.db");
        QVERIFY(SampleDatabase::create(m_shop));
        m_changed = path("shop-v2.db");
        QVERIFY(QFile::copy(m_shop, m_changed));
        QVERIFY(exec(m_changed, "ALTER TABLE book ADD COLUMN subtitle TEXT"));
        QVERIFY(exec(m_changed, "CREATE TABLE tag (id INTEGER PRIMARY KEY, label TEXT NOT NULL UNIQUE)"));
    }

    void usage()
    {
        Run r = cli({});
        QCOMPARE(r.code, int(QivotCli::Failed));
        QVERIFY(r.out.contains("Usage: qivot-cli"));
        QCOMPARE(cli({ "--help" }).code, int(QivotCli::Ok));
        r = cli({ "frobnicate" });
        QCOMPARE(r.code, int(QivotCli::Failed));
        QVERIFY(r.err.contains("no command frobnicate"));
        r = cli({ "inspect", m_shop, "--colour" });
        QCOMPARE(r.code, int(QivotCli::Failed));
        QVERIFY(r.err.contains("unknown option --colour"));
        r = cli({ "inspect", path("missing.db") });
        QCOMPARE(r.code, int(QivotCli::Failed));
        QVERIFY(r.err.contains("no such file"));
        QVERIFY(!QFile::exists(path("missing.db")));     // only migrate up creates files
    }

    void inspect()
    {
        Run r = cli({ "inspect", m_shop, "--table", "book" });
        QCOMPARE(r.code, int(QivotCli::Ok));
        QVERIFY2(r.out.contains("book  (table, "), qPrintable(r.out));
        QVERIFY(r.out.contains("primary key, auto"));
        QVERIFY(r.out.contains("-> author(id) on delete cascade"));
        QVERIFY(!r.out.contains("publisher  (table"));

        r = cli({ "inspect", m_shop, "--json" });
        QCOMPARE(r.code, int(QivotCli::Ok));
        const QJsonObject o = QJsonDocument::fromJson(r.out.toUtf8()).object();
        QCOMPARE(o.value("dialect").toString(), QString("sqlite"));
        QStringList names;
        for (const QJsonValue &t : o.value("tables").toArray())
            names << t.toObject().value("name").toString();
        QVERIFY(names.contains("book") && names.contains("order_item"));

        QCOMPARE(cli({ "inspect", m_shop, "--table", "nope" }).code, int(QivotCli::Failed));
    }

    void models()
    {
        Run r = cli({ "models", m_shop, "--table", "book" });
        QCOMPARE(r.code, int(QivotCli::Ok));
        QVERIFY(r.out.contains("class Book : public QiModel"));
        QVERIFY(!r.out.contains("class Author"));

        r = cli({ "models", m_shop, "-o", path("models.h") });
        QCOMPARE(r.code, int(QivotCli::Ok));
        QFile f(path("models.h"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString header = QString::fromUtf8(f.readAll());
        QVERIFY(header.contains("MODELS_H"));
        QVERIFY(header.contains("class Author") && header.contains("class Book"));
    }

    void query()
    {
        Run r = cli({ "query", m_shop, "SELECT id, title FROM book ORDER BY id LIMIT 2", "--format", "csv" });
        QCOMPARE(r.code, int(QivotCli::Ok));
        QStringList lines = r.out.trimmed().split('\n');
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines.first(), QString("id,title"));
        QVERIFY(lines.at(1).startsWith("1,"));

        r = cli({ "query", m_shop, "SELECT 1 AS one, NULL AS missing", "--format", "json" });
        const QJsonArray rows = QJsonDocument::fromJson(r.out.toUtf8()).array();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toObject().value("one").toInt(), 1);
        QVERIFY(rows.first().toObject().value("missing").isNull());

        r = cli({ "query", m_shop, "SELECT 'a' AS x UNION ALL SELECT 'b'" });
        QVERIFY(r.out.contains("(2 rows)"));

        // Read-only: nothing is changed.
        r = cli({ "query", m_shop, "DELETE FROM review" });
        QCOMPARE(r.code, int(QivotCli::Failed));
        QVERIFY(cli({ "query", m_shop, "SELECT COUNT(*) FROM review", "--format", "csv" }).out.split('\n').at(1).toInt() > 0);
    }

    void diff()
    {
        Run r = cli({ "diff", m_shop, m_shop, "--exit-code" });
        QCOMPARE(r.code, int(QivotCli::Ok));
        QVERIFY(r.out.contains("No differences"));

        // Make shop.db match shop-v2.db.
        r = cli({ "diff", m_changed, m_shop, "--exit-code" });
        QCOMPARE(r.code, int(QivotCli::Differs));
        QVERIFY2(r.out.contains("Add column book.subtitle"), qPrintable(r.out));
        QVERIFY(r.out.contains("Create table tag"));
        QVERIFY(r.out.contains("ALTER TABLE book ADD COLUMN subtitle TEXT"));
        QCOMPARE(cli({ "diff", m_changed, m_shop }).code, int(QivotCli::Ok));    // differences aren't a failure

        r = cli({ "diff", m_changed, m_shop, "--sql" });
        QVERIFY(r.out.startsWith("BEGIN;"));
        QVERIFY(!r.out.contains("Add column book.subtitle\n"));
    }

    void project()
    {
        const QString dir = path("project");
        Run r = cli({ "project", m_shop, "-o", dir, "--name", "Shop" });
        QCOMPARE(r.code, int(QivotCli::Ok));
        QVERIFY(QFile::exists(dir + "/CMakeLists.txt"));
        QVERIFY(QFile::exists(dir + "/src/models.h"));
        QVERIFY(QFile::exists(dir + "/third_party/qivot/qivot.hpp"));
        r = cli({ "project", m_shop, "-o", dir });
        QCOMPARE(r.code, int(QivotCli::Failed));
        QVERIFY(r.err.contains("isn't empty"));
        QCOMPARE(cli({ "project", m_shop, "-o", dir, "--force" }).code, int(QivotCli::Ok));
    }

    void migrations()
    {
        const QString mig = path("migrations");
        const QString app = path("app.db");

        // The first migration: everything in shop.db, from nothing.
        Run r = cli({ "migrate", "new", "Create the shop", "--dir", mig, "--from", m_shop });
        QCOMPARE(r.code, int(QivotCli::Ok));
        QVERIFY2(QFile::exists(mig + "/0001_create_the_shop.up.sql"), qPrintable(r.out + r.err));
        QVERIFY(QFile::exists(mig + "/0001_create_the_shop.down.sql"));
        QFile up(mig + "/0001_create_the_shop.up.sql");
        QVERIFY(up.open(QIODevice::ReadOnly));
        const QString upSql = QString::fromUtf8(up.readAll());
        up.close();
        QVERIFY(upSql.contains("CREATE TABLE book"));
        QVERIFY(!upSql.contains("BEGIN;") && !upSql.contains("COMMIT;"));     // the migrator's own
        QFile down(mig + "/0001_create_the_shop.down.sql");
        QVERIFY(down.open(QIODevice::ReadOnly));
        const QString downSql = QString::fromUtf8(down.readAll());
        // Children before parents, or a server refuses the DROP.
        QVERIFY(downSql.indexOf("DROP TABLE book;") < downSql.indexOf("DROP TABLE author;"));
        QVERIFY(downSql.indexOf("DROP TABLE order_item;") < downSql.indexOf("DROP TABLE orders;"));
        QVERIFY(downSql.indexOf("DROP TABLE order_item;") < downSql.indexOf("DROP TABLE book;"));

        // Into a database that doesn't exist yet.
        r = cli({ "migrate", "up", app, "--dir", mig });
        QCOMPARE(r.code, int(QivotCli::Ok));
        QVERIFY2(r.out.contains("Applied 1: version 1."), qPrintable(r.out + r.err));
        QCOMPARE(cli({ "migrate", "status", app, "--dir", mig, "--exit-code" }).code, int(QivotCli::Ok));
        QCOMPARE(cli({ "diff", m_shop, app, "--exit-code" }).code, int(QivotCli::Ok));             // the tables match
        QCOMPARE(cli({ "diff", "migrations:" + mig, app, "--exit-code" }).code, int(QivotCli::Ok)); // and the folder

        // Nothing to write when nothing changed.
        r = cli({ "migrate", "new", "again", "--dir", mig, "--from", m_shop });
        QVERIFY(r.out.contains("No differences"));
        QVERIFY(!QFile::exists(mig + "/0002_again.up.sql"));

        // The next one, from shop-v2.db.
        r = cli({ "migrate", "new", "subtitles and tags", "--dir", mig, "--from", m_changed });
        QCOMPARE(r.code, int(QivotCli::Ok));
        QVERIFY(QFile::exists(mig + "/0002_subtitles_and_tags.up.sql"));
        r = cli({ "migrate", "status", app, "--dir", mig, "--exit-code" });
        QCOMPARE(r.code, int(QivotCli::Differs));
        QVERIFY(r.out.contains("1 pending"));
        r = cli({ "migrate", "up", app, "--dir", mig, "--dry-run" });
        QVERIFY(r.out.contains("ALTER TABLE book ADD COLUMN subtitle"));
        QCOMPARE(cli({ "migrate", "status", app, "--dir", mig, "--exit-code" }).code, int(QivotCli::Differs));  // dry
        QCOMPARE(cli({ "migrate", "up", app, "--dir", mig }).code, int(QivotCli::Ok));
        QCOMPARE(cli({ "diff", m_changed, app, "--exit-code" }).code, int(QivotCli::Ok));

        r = cli({ "migrate", "status", app, "--dir", mig, "--json" });
        const QJsonObject st = QJsonDocument::fromJson(r.out.toUtf8()).object();
        QCOMPARE(st.value("version").toInt(), 2);
        QCOMPARE(st.value("migrations").toArray().size(), 2);
        QCOMPARE(st.value("migrations").toArray().at(1).toObject().value("state").toString(), QString("applied"));

        // Editing a migration that ran.
        QFile edit(mig + "/0002_subtitles_and_tags.up.sql");
        QVERIFY(edit.open(QIODevice::Append));
        edit.write("-- a note added later\n");
        edit.close();
        r = cli({ "migrate", "status", app, "--dir", mig, "--exit-code" });
        QCOMPARE(r.code, int(QivotCli::Differs));
        QVERIFY(r.out.contains("CHANGED"));
        QCOMPARE(cli({ "migrate", "up", app, "--dir", mig }).code, int(QivotCli::Failed));
        QCOMPARE(cli({ "migrate", "accept", app, "--dir", mig }).code, int(QivotCli::Ok));
        QCOMPARE(cli({ "migrate", "status", app, "--dir", mig, "--exit-code" }).code, int(QivotCli::Ok));

        // Back down: v1 keeps the rows, v0 is empty.
        QVERIFY(exec(app, "INSERT INTO publisher (name) VALUES ('Kept')"));
        QCOMPARE(cli({ "migrate", "down", app, "--dir", mig }).code, int(QivotCli::Failed));   // needs --to
        r = cli({ "migrate", "down", app, "--dir", mig, "--to", "1" });
        QCOMPARE(r.code, int(QivotCli::Ok));
        QVERIFY(r.out.contains("version 1"));
        QCOMPARE(cli({ "diff", m_shop, app, "--exit-code" }).code, int(QivotCli::Ok));
        QVERIFY(cli({ "query", app, "SELECT COUNT(*) FROM publisher WHERE name = 'Kept'", "--format", "csv" }).out.contains("\n1\n"));
        QCOMPARE(cli({ "migrate", "down", app, "--dir", mig, "--to", "0" }).code, int(QivotCli::Ok));
        r = cli({ "inspect", app, "--json" });
        QCOMPARE(QJsonDocument::fromJson(r.out.toUtf8()).object().value("tables").toArray().size(), 1);  // just the history
    }

    void drivers()
    {
        const Run r = cli({ "drivers" });
        QCOMPARE(r.code, int(QivotCli::Ok));
        QVERIFY(r.out.contains("QSQLITE   loads"));
    }
};

QTEST_GUILESS_MAIN(TestCli)
#include "tst_cli.moc"
