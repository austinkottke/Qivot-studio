#include <QtTest>
#include <QTemporaryDir>

#include "databasesession.h"
#include "querylibrary.h"
#include "queryplan.h"
#include "sampledatabase.h"
#include "sqlcompleter.h"
#include "gridtools.h"
#include "connectionhistory.h"
#include "querymodel.h"

/// The query screen's helpers: history and saved queries, autocomplete, and plans.
class TestQueryTools : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    DatabaseSession m_db;

    static QStringList texts(const QVariantMap &completion)
    {
        QStringList out;
        for (const QVariant &v : completion.value("items").toList())
            out << v.toMap().value("text").toString();
        return out;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // As the app does: without an organisation, QSettings on Windows can
        // neither read nor write (elsewhere it falls back to a default).
        QCoreApplication::setOrganizationName(QStringLiteral("Qivot"));
        QCoreApplication::setApplicationName(QStringLiteral("Qivot Studio Tests"));
        QVERIFY(m_dir.isValid());
        QVERIFY(SampleDatabase::create(m_dir.filePath("shop.db")));
        QVERIFY(m_db.open(m_dir.filePath("shop.db")));
    }

    // ---- History and saved queries ----
    void library()
    {
        const QString scope = "test:" + m_dir.path();
        {
            QueryLibrary lib;
            lib.setScope(scope);
            lib.clearHistory();
            lib.record("SELECT 1", 1, 2, true);
            lib.record("SELECT * FROM nowhere", 0, 1, false, "no such table");
            lib.record("SELECT 1", 1, 3, true);                 // again: to the top, once
            QCOMPARE(lib.history().size(), 2);
            QCOMPARE(lib.history().first().toMap().value("sql").toString(), QString("SELECT 1"));
            QCOMPARE(lib.history().last().toMap().value("ok").toBool(), false);
            QCOMPARE(lib.history().last().toMap().value("error").toString(), QString("no such table"));

            QCOMPARE(lib.suggestName("SELECT * FROM book b JOIN author a ON a.id = b.author_id"), QString("book and author"));
            lib.save("Best sellers", "SELECT * FROM book_sales ORDER BY units DESC");
            lib.save("all authors", "SELECT * FROM author");
            QCOMPARE(lib.saved().first().toMap().value("name").toString(), QString("all authors"));   // by name
            QVERIFY(lib.hasName("best SELLERS"));
            QCOMPARE(lib.suggestName("SELECT * FROM author"), QString("author"));
            QVERIFY(lib.rename("all authors", "Authors"));
            QVERIFY(!lib.rename("Authors", "Best sellers"));     // taken
            lib.remove("Best sellers");
            QCOMPARE(lib.saved().size(), 1);
        }
        // Kept between sessions, per database.
        QueryLibrary again;
        again.setScope(scope);
        QCOMPARE(again.history().size(), 2);
        QCOMPARE(again.saved().first().toMap().value("name").toString(), QString("Authors"));
        QueryLibrary other;
        other.setScope(scope + "-elsewhere");
        QVERIFY(other.saved().isEmpty());
    }

    void historyIsBounded()
    {
        QueryLibrary lib;
        lib.setScope("test:bounded");
        lib.clearHistory();
        for (int i = 0; i < QueryLibrary::MaxHistory + 20; ++i)
            lib.record(QString("SELECT %1").arg(i), 1, 1, true);
        QCOMPARE(lib.history().size(), QueryLibrary::MaxHistory);
        QCOMPARE(lib.history().first().toMap().value("sql").toString(), QString("SELECT %1").arg(QueryLibrary::MaxHistory + 19));
    }

    // ---- Autocomplete ----
    void completes()
    {
        SqlCompleter c;
        c.setSession(&m_db);
        // After FROM: tables (and views).
        QString sql = "SELECT * FROM bo";
        QVariantMap r = c.complete(sql, sql.size(), false);
        QCOMPARE(r.value("start").toInt(), 14);
        QCOMPARE(r.value("prefix").toString(), QString("bo"));
        QCOMPARE(texts(r), QStringList({ "book", "book_sales", "book_search" }));
        // Nothing typed after FROM: every table.
        sql = "SELECT * FROM ";
        QVERIFY(texts(c.complete(sql, sql.size(), false)).contains("publisher"));
        // alias.: that table's columns.
        sql = "SELECT b.ti FROM book b";
        r = c.complete(sql, 11, false);
        QCOMPARE(texts(r), QStringList({ "title" }));
        sql = "SELECT a. FROM book b JOIN author AS a ON a.id = b.author_id";
        r = c.complete(sql, 9, false);
        QVERIFY2(texts(r).contains("last_name") && !texts(r).contains("title"), qPrintable(texts(r).join(",")));
        // A plain word: the statement's columns first, then tables, keywords, functions.
        sql = "SELECT pu FROM book";
        const QStringList pu = texts(c.complete(sql, 9, false));
        QCOMPARE(pu.mid(0, 2), QStringList({ "publisher_id", "published" }));   // book's columns, in its order, first
        QVERIFY(texts(c.complete(sql, 9, false)).contains("publisher"));     // a table
        sql = "sel";
        QCOMPARE(texts(c.complete(sql, 3, false)), QStringList({ "select" }));   // in the case typed
        sql = "SELECT CO";
        QVERIFY(texts(c.complete(sql, 9, false)).contains("COUNT"));
        // Nothing until something's started, unless asked.
        sql = "SELECT ";
        QVERIFY(texts(c.complete(sql, sql.size(), false)).isEmpty());
        QVERIFY(!texts(c.complete(sql, sql.size(), true)).isEmpty());
        // Only this statement's tables count.
        sql = "SELECT * FROM author; SELECT fi FROM book";
        QVERIFY(!texts(c.complete(sql, 31, false)).contains("first_name"));
    }

    // ---- Plans ----
    void sqlitePlan()
    {
        QueryPlan p;
        p.setSession(&m_db);
        QVERIFY2(p.explain("SELECT * FROM book WHERE author_id = 3"), qPrintable(p.error()));
        QCOMPARE(p.scans(), 0);                                  // idx_book_author
        QVERIFY(p.nodes().first().toMap().value("label").toString() == "SEARCH");
        QVERIFY(p.raw().contains("idx_book_author"));
        QVERIFY(p.explain("SELECT * FROM book WHERE pages > 300;"));
        QCOMPARE(p.scans(), 1);                                  // no index on pages: the whole table
        QCOMPARE(p.nodes().first().toMap().value("table").toString(), QString("book"));
        QVERIFY(!p.explain("SELECT * FROM nowhere"));
        QVERIFY(!p.error().isEmpty());
    }

    void postgresPlan()
    {
        const QString json = R"json([{"Plan":{"Node Type":"Hash Join","Join Type":"Inner","Total Cost":120.5,"Plan Rows":1200,
            "Hash Cond":"(b.author_id = a.id)","Plans":[
              {"Node Type":"Seq Scan","Relation Name":"book","Alias":"b","Total Cost":40.0,"Plan Rows":1200},
              {"Node Type":"Hash","Total Cost":5.5,"Plan Rows":150,"Plans":[
                {"Node Type":"Index Scan","Relation Name":"author","Alias":"a","Index Name":"author_pkey","Total Cost":5.0,"Plan Rows":150}]}]}}])json";
        const QVariantList n = QueryPlan::fromPostgresJson(json);
        QCOMPARE(n.size(), 4);
        QCOMPARE(n.at(0).toMap().value("label").toString(), QString("Hash Join (Inner)"));
        QCOMPARE(n.at(0).toMap().value("share").toDouble(), 1.0);
        QCOMPARE(n.at(1).toMap().value("depth").toInt(), 1);
        QVERIFY(n.at(1).toMap().value("scan").toBool());
        QVERIFY(n.at(1).toMap().value("detail").toString().startsWith("on book b"));
        QCOMPARE(n.at(3).toMap().value("depth").toInt(), 2);
        QVERIFY(n.at(3).toMap().value("detail").toString().contains("using author_pkey"));
    }

    void mysqlPlan()
    {
        const QString tree =
            "-> Nested loop inner join  (cost=544.25 rows=1200)\n"
            "    -> Table scan on b  (cost=124.25 rows=1200)\n"
            "    -> Single-row index lookup on a using PRIMARY (id=b.author_id)  (cost=0.25 rows=1)\n";
        const QVariantList n = QueryPlan::fromMysqlTree(tree);
        QCOMPARE(n.size(), 3);
        QCOMPARE(n.at(0).toMap().value("label").toString(), QString("Nested loop inner join"));
        QCOMPARE(n.at(1).toMap().value("depth").toInt(), 1);
        QVERIFY(n.at(1).toMap().value("scan").toBool());
        QCOMPARE(n.at(1).toMap().value("table").toString(), QString("b"));
        QCOMPARE(n.at(2).toMap().value("rows").toDouble(), 1.0);
        QVERIFY(!n.at(2).toMap().value("scan").toBool());
    }

    void sqlServerPlan()
    {
        const QString xml = R"(<ShowPlanXML><BatchSequence><Batch><Statements><StmtSimple><QueryPlan>
            <RelOp PhysicalOp="Hash Match" LogicalOp="Inner Join" EstimateRows="1200" EstimatedTotalSubtreeCost="0.9">
              <Hash><RelOp PhysicalOp="Clustered Index Scan" LogicalOp="Clustered Index Scan" EstimateRows="150" EstimatedTotalSubtreeCost="0.1">
                <IndexScan><Object Database="[shop]" Schema="[dbo]" Table="[author]" Index="[PK_author]"/></IndexScan></RelOp>
              <RelOp PhysicalOp="Index Seek" LogicalOp="Index Seek" EstimateRows="8" EstimatedTotalSubtreeCost="0.3">
                <IndexScan><Object Table="[book]" Index="[idx_book_author]"/></IndexScan></RelOp></Hash>
            </RelOp></QueryPlan></StmtSimple></Statements></Batch></BatchSequence></ShowPlanXML>)";
        const QVariantList n = QueryPlan::fromSqlServerXml(xml);
        QCOMPARE(n.size(), 3);
        QCOMPARE(n.at(0).toMap().value("label").toString(), QString("Hash Match"));
        QCOMPARE(n.at(1).toMap().value("depth").toInt(), 1);
        QCOMPARE(n.at(1).toMap().value("table").toString(), QString("author"));
        QVERIFY(n.at(1).toMap().value("scan").toBool());
        QVERIFY(n.at(2).toMap().value("detail").toString().contains("using idx_book_author"));
        QVERIFY(!n.at(2).toMap().value("scan").toBool());
    }

    // ---- Copying and summing a block of the grid ----
    void gridTools()
    {
        QueryModel q;
        q.setSession(&m_db);
        QVERIFY(q.run("SELECT 1 AS n, 'a,b' AS t, NULL AS z, 2.5 AS x UNION ALL "
                      "SELECT 3, 'say \"hi\"', NULL, '4.5'"));
        GridTools g;
        QCOMPARE(g.text(&q, 0, 0, 1, 3, "tsv", false),
                 QString("1\ta,b\t\t2.5\n3\t\"say \"\"hi\"\"\"\t\t4.5\n"));
        QCOMPARE(g.text(&q, 1, 3, 0, 0, "csv", true),            // corners either way round
                 QString("n,t,z,x\n1,\"a,b\",,2.5\n3,\"say \"\"hi\"\"\",,4.5\n"));
        QCOMPARE(g.text(&q, 0, 0, 0, 0), QString("1\n"));
        QVERIFY(g.text(&q, 5, 5, 6, 6).isEmpty());                 // outside

        const QVariantMap all = g.summary(&q, 0, 0, 1, 3);
        QCOMPARE(all.value("cells").toInt(), 8);
        QCOMPARE(all.value("nulls").toInt(), 2);
        QCOMPARE(all.value("numbers").toInt(), 4);                  // '4.5' as text counts
        QCOMPARE(all.value("sum").toDouble(), 11.0);
        QCOMPARE(all.value("avg").toDouble(), 2.75);
        QCOMPARE(all.value("min").toDouble(), 1.0);
        QCOMPARE(all.value("max").toDouble(), 4.5);
        QVERIFY(!g.summary(&q, 0, 1, 1, 2).contains("sum"));        // no numbers there

        QVERIFY(q.run("SELECT id FROM book ORDER BY id"));
        const QVariantMap ids = g.summary(&q, 0, 0, q.rowCount() - 1, 0);
        QCOMPARE(ids.value("numbers").toInt(), 1200);
        QCOMPARE(ids.value("sum").toLongLong(), 1200LL * 1201 / 2);
    }

    // ---- Recent and saved connections ----
    void connectionHistory()
    {
        {
            ConnectionHistory h;
            for (const QVariant &e : h.entries())
                h.forget(e.toMap().value("id").toString());
            QVERIFY(h.entries().isEmpty());

            const QVariantMap pg{ { "type", "postgres" }, { "host", "db.example.com" }, { "port", 5432 },
                                  { "database", "shop" }, { "user", "me" }, { "password", "secret" } };
            const QString pgId = h.remember(pg);
            const QString fileId = h.remember({ { "type", "sqlite" }, { "path", m_dir.filePath("shop.db") } });
            QCOMPARE(h.entries().size(), 2);
            QCOMPARE(h.entries().first().toMap().value("id").toString(), fileId);      // latest first
            const QVariantMap server = h.entry(pgId);
            QCOMPARE(server.value("kind").toString(), QString("server"));
            QCOMPARE(server.value("title").toString(), QString("shop"));
            QCOMPARE(server.value("detail").toString(), QString("PostgreSQL · me@db.example.com:5432"));
            QVERIFY(!server.value("settings").toMap().contains("password"));          // never kept
            QCOMPARE(h.entry(fileId).value("kind").toString(), QString("file"));
            QVERIFY(!h.entry(fileId).value("missing").toBool());

            // The same again moves to the top rather than repeating.
            h.remember(pg);
            QCOMPARE(h.entries().size(), 2);
            QCOMPARE(h.entries().first().toMap().value("id").toString(), pgId);

            // Saved ones stay, named, ahead of the rest, past the limit for recent ones.
            h.setSaved(fileId, true, "Local shop");
            for (int i = 0; i < ConnectionHistory::MaxRecent + 3; ++i)
                h.remember({ { "type", "sqlite" }, { "path", m_dir.filePath(QString("other%1.db").arg(i)) } });
            const QVariantList all = h.entries();
            QCOMPARE(all.size(), ConnectionHistory::MaxRecent + 1);
            QCOMPARE(all.first().toMap().value("title").toString(), QString("Local shop"));
            QVERIFY(all.first().toMap().value("saved").toBool());
            QVERIFY(h.entry(pgId).isEmpty());                                        // pushed out
            QVERIFY(all.last().toMap().value("missing").toBool());                   // no such file
        }
        // Kept between runs.
        ConnectionHistory again;
        QCOMPARE(again.entries().first().toMap().value("title").toString(), QString("Local shop"));
        again.rename(again.entries().first().toMap().value("id").toString(), "Shop");
        QCOMPARE(again.entries().first().toMap().value("title").toString(), QString("Shop"));
        for (const QVariant &e : again.entries())
            again.forget(e.toMap().value("id").toString());
        QVERIFY(again.entries().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestQueryTools)
#include "tst_querytools.moc"
