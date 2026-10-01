#include <QtTest>
#include <QTemporaryDir>

#include "databasesession.h"
#include "querymodel.h"
#include "sampledatabase.h"

/// The SQL console's model, against the bookshop sample.
class TestQueryModel : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir   m_dir;
    QString         m_sample;
    DatabaseSession m_db;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_sample = m_dir.filePath("bookshop.db");
        QVERIFY(SampleDatabase::create(m_sample));
    }
    void init() { QVERIFY(m_db.open(m_sample)); }

    void runsASelect()
    {
        QueryModel q;
        q.setSession(&m_db);
        QSignalSpy done(&q, &QueryModel::finished);
        QVERIFY(q.run("SELECT id, title, price FROM book WHERE price > 30 ORDER BY price DESC;"));
        QCOMPARE(done.count(), 1);
        QVERIFY(q.hasResult());
        QCOMPARE(q.columnCount(), 3);
        QCOMPARE(q.headerData(1, Qt::Horizontal, Qt::DisplayRole).toString(), QString("title"));
        QVERIFY(q.rowCount() > 0);
        QVERIFY(q.data(q.index(0, 2), QueryModel::RawRole).toDouble()
                >= q.data(q.index(q.rowCount() - 1, 2), QueryModel::RawRole).toDouble());
        QVERIFY(q.data(q.index(0, 0), QueryModel::NumberRole).toBool());
        QVERIFY(q.error().isEmpty());
        QVERIFY(!q.truncated());
        QCOMPARE(q.lastSql(), QString("SELECT id, title, price FROM book WHERE price > 30 ORDER BY price DESC"));
        QCOMPARE(q.columns().at(1).toMap().value("name").toString(), QString("title"));
    }

    void aggregatesAndJoins()
    {
        QueryModel q;
        q.setSession(&m_db);
        QVERIFY(q.run("SELECT a.country, COUNT(*) AS books FROM book b JOIN author a ON a.id = b.author_id "
                      "GROUP BY a.country ORDER BY books DESC"));
        QVERIFY(q.rowCount() > 1);
        int total = 0;
        for (int r = 0; r < q.rowCount(); ++r)
            total += q.data(q.index(r, 1), QueryModel::RawRole).toInt();
        QCOMPARE(total, 1200);
    }

    void capsLargeResults()
    {
        QueryModel q;
        q.setSession(&m_db);
        QVERIFY(q.run("SELECT * FROM order_item"));            // ~22,000 rows
        QCOMPARE(q.rowCount(), QueryModel::MaxRows);
        QVERIFY(q.truncated());
    }

    void reportsErrors()
    {
        QueryModel q;
        q.setSession(&m_db);
        QVERIFY(!q.run("SELEC nonsense"));
        QVERIFY(!q.error().isEmpty());
        QVERIFY(!q.hasResult());
        QVERIFY(!q.run("SELECT * FROM no_such_table"));
        QVERIFY(q.error().contains("no_such_table"));
        QVERIFY(!q.run("   "));
        QCOMPARE(q.error(), QString("Type a query to run."));
    }

    void cannotWrite()
    {
        // The file is open read-only, so even a direct DELETE is refused.
        QueryModel q;
        q.setSession(&m_db);
        QVERIFY(!q.run("DELETE FROM review"));
        QVERIFY(q.error().contains("readonly", Qt::CaseInsensitive));
        QVERIFY(q.run("SELECT COUNT(*) FROM review"));
        QCOMPARE(q.data(q.index(0, 0), QueryModel::RawRole).toInt(), 6000);
    }

    void nullsAndInspector()
    {
        QueryModel q;
        q.setSession(&m_db);
        QVERIFY(q.run("SELECT first_name, last_name FROM author WHERE first_name IS NULL LIMIT 1"));
        QCOMPARE(q.rowCount(), 1);
        QCOMPARE(q.data(q.index(0, 0), Qt::DisplayRole).toString(), QString("NULL"));
        QVERIFY(q.data(q.index(0, 0), QueryModel::NullRole).toBool());
        QVERIFY(q.rowAt(0).value("first_name").isNull());
        QVERIFY(!q.rowAt(0).value("last_name").toString().isEmpty());
    }

    void clearsWhenDatabaseCloses()
    {
        QueryModel q;
        q.setSession(&m_db);
        QVERIFY(q.run("SELECT 1"));
        QVERIFY(q.hasResult());
        m_db.close();
        QVERIFY(!q.hasResult());
        QCOMPARE(q.rowCount(), 0);
        QVERIFY(!q.run("SELECT 1"));
        QCOMPARE(q.error(), QString("Open a database first."));
    }
};

QTEST_GUILESS_MAIN(TestQueryModel)
#include "tst_querymodel.moc"
