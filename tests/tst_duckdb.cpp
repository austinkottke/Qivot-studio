#include <QtTest>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "connectionhistory.h"
#include "databasesession.h"
#include "querymodel.h"
#include "queryplan.h"
#include "rowsmodel.h"
#include "tableprofile.h"

/// DuckDB files: opened read-only through Qivot's QDUCKDB driver, and read
/// like any other database (structure, keys, rows, queries, profiles).
class TestDuckDb : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString       m_file;

    // A small shop, written through the driver with the file open for writing.
    static bool make(const QString &path, QString *why)
    {
        bool ok = true;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QDUCKDB"), QStringLiteral("make"));
            db.setDatabaseName(path);
            if (!db.open()) { *why = db.lastError().text(); ok = false; }
            const QStringList script = {
                "CREATE TABLE author (id INTEGER PRIMARY KEY, name VARCHAR NOT NULL, country VARCHAR)",
                "CREATE TABLE book (id INTEGER PRIMARY KEY, title VARCHAR NOT NULL, "
                "author_id INTEGER NOT NULL REFERENCES author(id), price DECIMAL(8,2), published DATE)",
                "INSERT INTO author SELECT i, 'Author ' || i, CASE WHEN i % 3 = 0 THEN 'UK' ELSE 'US' END "
                "FROM range(1, 51) t(i)",
                "INSERT INTO book SELECT i, 'Book ' || i, 1 + (i % 50), 5 + (i % 30), DATE '2020-01-01' + (i % 365)::INTEGER "
                "FROM range(1, 1001) t(i)",
                "CREATE VIEW cheap AS SELECT * FROM book WHERE price < 10",
            };
            QSqlQuery q(db);
            for (const QString &s : script) {
                if (ok && !q.exec(s)) { *why = s + ": " + q.lastError().text(); ok = false; }
            }
            q = QSqlQuery();
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("make"));
        return ok;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("Qivot"));
        QCoreApplication::setApplicationName(QStringLiteral("Qivot Studio Tests"));
        DatabaseSession registersTheDriver;
        Q_UNUSED(registersTheDriver);
        QVERIFY(QSqlDatabase::drivers().contains("QDUCKDB"));
        QVERIFY(m_dir.isValid());
        m_file = m_dir.filePath("shop.duckdb");
        QString why;
        QVERIFY2(make(m_file, &why), qPrintable(why));
        // A copy for the app's smoke tests (tests/CMakeLists.txt).
        const QString fixture = qEnvironmentVariable("STUDIO_DUCKDB_FIXTURE");
        if (!fixture.isEmpty()) {
            QFile::remove(fixture);
            QVERIFY(QFile::copy(m_file, fixture));
        }
    }

    void recognisesTheFile()
    {
        QVERIFY(DatabaseSession::isDuckDbFile(m_file));
        // By its header, whatever it's called.
        const QString renamed = m_dir.filePath("shop.data");
        QVERIFY(QFile::copy(m_file, renamed));
        QVERIFY(DatabaseSession::isDuckDbFile(renamed));
        // And not an SQLite file or a text file.
        QFile text(m_dir.filePath("notes.db"));
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("just some text, long enough to have a header");
        text.close();
        QVERIFY(!DatabaseSession::isDuckDbFile(text.fileName()));
    }

    void readsTheStructure()
    {
        DatabaseSession db;
        QVERIFY2(db.open(m_file), qPrintable(db.error()));
        QCOMPARE(db.dialect(), QString("duckdb"));
        QCOMPARE(db.dialectName(), QString("DuckDB"));
        QVERIFY(db.readOnly());
        QCOMPARE(db.connectionSettings().value("type").toString(), QString("duckdb"));

        QStringList names;
        for (const QVariant &t : db.tables())
            names << t.toMap().value("name").toString();
        QVERIFY2(names.contains("author") && names.contains("book") && names.contains("cheap"), qPrintable(names.join(",")));

        const QVariantMap book = db.table("book");
        QCOMPARE(book.value("rows").toLongLong(), 1000LL);
        QCOMPARE(book.value("primaryKey").toStringList(), QStringList{ "id" });
        const QVariantList fks = book.value("foreignKeys").toList();
        QCOMPARE(fks.size(), 1);
        QCOMPARE(fks.first().toMap().value("refTable").toString(), QString("author"));
        QCOMPARE(db.table("author").value("referencedBy").toList().size(), 1);
        QCOMPARE(db.diagram().value("links").toList().size(), 1);
    }

    void readsRowsAndQueries()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_file));

        // A table's rows, a page at a time.
        RowsModel rows;
        rows.setSession(&db);
        rows.setTable("book");
        QCOMPARE(rows.rowCount(), 1000);
        QCOMPARE(rows.data(rows.index(999, 0), RowsModel::RawRole).toInt(), 1000);

        // The console, waiting and in the background.
        QueryModel q;
        q.setSession(&db);
        QVERIFY2(q.run("SELECT a.country, COUNT(*) AS books, SUM(b.price) AS total FROM book b "
                       "JOIN author a ON a.id = b.author_id GROUP BY 1 ORDER BY 1"), qPrintable(q.error()));
        QCOMPARE(q.rowCount(), 2);
        QCOMPARE(q.data(q.index(0, 0), QueryModel::RawRole).toString(), QString("UK"));
        QSignalSpy done(&q, &QueryModel::finished);
        q.start("SELECT * FROM range(100000)");
        QVERIFY(done.wait(20000));
        QVERIFY2(q.error().isEmpty(), qPrintable(q.error()));
        QCOMPARE(q.rowCount(), QueryModel::MaxRows);
        QVERIFY(q.truncated());
        QVERIFY(q.waitForBackground(10000));

        // Read-only: nothing changes the file.
        QVERIFY(!q.run("DELETE FROM book"));
        QVERIFY(q.run("SELECT COUNT(*) FROM book"));
        QCOMPARE(q.data(q.index(0, 0), QueryModel::RawRole).toInt(), 1000);
        QVERIFY(!db.allowChanges(true));
        QVERIFY(db.error().contains("DuckDB"));
    }

    void profiles()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_file));
        TableProfile p;
        p.setSession(&db);
        QSignalSpy done(&p, &TableProfile::finished);
        p.setTable("book");
        QVERIFY(done.wait(20000));
        QCOMPARE(p.rows(), 1000LL);
        for (const QVariant &c : p.columns())
            QVERIFY2(c.toMap().value("error").toString().isEmpty(),
                     qPrintable(c.toMap().value("name").toString() + ": " + c.toMap().value("error").toString()));
    }

    void plans()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_file));
        QueryPlan plan;
        plan.setSession(&db);
        QVERIFY2(plan.explain("SELECT a.name, COUNT(*) FROM book b JOIN author a ON a.id = b.author_id GROUP BY 1"),
                 qPrintable(plan.error() + " | " + plan.raw().left(600)));
        QStringList labels;
        bool readsBook = false;
        for (const QVariant &n : plan.nodes()) {
            labels << n.toMap().value("label").toString();
            readsBook = readsBook || n.toMap().value("table").toString() == QLatin1String("book");
        }
        QVERIFY2(labels.join(" ").contains("JOIN") && readsBook, qPrintable(labels.join(" > ") + " | " + plan.raw().left(600)));
        QVERIFY(plan.scans() >= 1);                       // no filter: every row of both
        QVERIFY(plan.explain("SELECT * FROM book WHERE price < 7"));
        QCOMPARE(plan.scans(), 0);                        // filtered as it's read
    }

    void isAFileInRecent()
    {
        ConnectionHistory h;
        const QString id = h.remember({ { "type", "duckdb" }, { "path", m_file } });
        const QVariantMap e = h.entry(id);
        QCOMPARE(e.value("kind").toString(), QString("file"));
        QCOMPARE(e.value("title").toString(), QString("shop.duckdb"));
        h.forget(id);
    }
};

QTEST_GUILESS_MAIN(TestDuckDb)
#include "tst_duckdb.moc"
