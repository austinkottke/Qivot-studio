#include <QtTest>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "databasesession.h"
#include "sampledatabase.h"
#include "tableprofile.h"

/// Column profiles on the bookshop sample, checked against plain SQL.
class TestProfile : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir   m_dir;
    DatabaseSession m_db;

    qint64 scalar(const QString &sql)
    {
        QSqlQuery q(QSqlDatabase::database(m_db.connectionName()));
        return q.exec(sql) && q.next() ? q.value(0).toLongLong() : -1;
    }
    static QVariantMap column(const TableProfile &p, const QString &name)
    {
        for (const QVariant &c : p.columns())
            if (c.toMap().value("name").toString() == name)
                return c.toMap();
        return {};
    }
    static bool waitFor(TableProfile &p)
    {
        if (!p.running())
            return p.done() == p.columns().size();
        QSignalSpy finished(&p, &TableProfile::finished);
        return finished.wait(30000);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        const QString file = m_dir.filePath("bookshop.db");
        QVERIFY(SampleDatabase::create(file));
        QVERIFY(m_db.open(file));
    }

    void kinds()
    {
        QCOMPARE(TableProfile::kindOf("INTEGER"), QString("number"));
        QCOMPARE(TableProfile::kindOf("numeric(10,2)"), QString("number"));
        QCOMPARE(TableProfile::kindOf("DATE"), QString("date"));
        QCOMPARE(TableProfile::kindOf("varchar(80)"), QString("text"));
        QCOMPARE(TableProfile::kindOf("BLOB"), QString("other"));
    }

    void profilesATable()
    {
        TableProfile p;
        p.setSession(&m_db);
        p.setTable("book");
        QCOMPARE(p.columns().size(), 8);                 // placeholders straight away
        QVERIFY(waitFor(p));
        QCOMPARE(p.done(), 8);
        QCOMPARE(p.rows(), qint64(1200));

        // Counts match SQL.
        const QVariantMap price = column(p, "price");
        QCOMPARE(price.value("kind").toString(), QString("number"));
        QCOMPARE(price.value("nulls").toLongLong(), scalar("SELECT COUNT(*) FROM book WHERE price IS NULL"));
        QCOMPARE(price.value("distinct").toLongLong(), scalar("SELECT COUNT(DISTINCT price) FROM book"));
        QVERIFY(qAbs(price.value("avg").toDouble() - scalar("SELECT CAST(AVG(price) * 1000 AS INTEGER) FROM book") / 1000.0) < 0.01);
        // The histogram holds every non-empty value.
        qint64 inBars = 0;
        for (const QVariant &b : price.value("histogram").toList())
            inBars += b.toMap().value("count").toLongLong();
        QCOMPARE(price.value("histogram").toList().size(), int(TableProfile::Buckets));
        QCOMPARE(inBars, 1200 - price.value("nulls").toLongLong());

        // isbn: unique, so as many distinct values as filled ones.
        const QVariantMap isbn = column(p, "isbn");
        QCOMPARE(isbn.value("distinct").toLongLong(), 1200 - isbn.value("nulls").toLongLong());
        QVERIFY(isbn.value("error").toString().isEmpty());
    }

    void commonestValues()
    {
        TableProfile p;
        p.setSession(&m_db);
        p.setTable("orders");
        QVERIFY(waitFor(p));
        const QVariantList top = column(p, "status").value("top").toList();
        QVERIFY(!top.isEmpty() && top.size() <= TableProfile::TopValues);
        const QString first = top.first().toMap().value("value").toString();
        QCOMPARE(top.first().toMap().value("count").toLongLong(),
                 scalar(QString("SELECT COUNT(*) FROM orders WHERE status = '%1'").arg(first)));
        for (int i = 1; i < top.size(); ++i)
            QVERIFY(top[i - 1].toMap().value("count").toLongLong() >= top[i].toMap().value("count").toLongLong());

        // A number with few values (a 1-5 rating) gets its values, not a histogram.
        p.setTable("review");
        QVERIFY(waitFor(p));
        const QVariantMap rating = column(p, "rating");
        QVERIFY(rating.value("histogram").toList().isEmpty());
        QVERIFY(rating.value("top").toList().size() <= 5);
        // Dates: a range.
        QVERIFY(!column(p, "created").value("min").toString().isEmpty());
    }

    // Switching tables part-way drops the old run; nothing of it arrives late.
    void switchingCancels()
    {
        TableProfile p;
        p.setSession(&m_db);
        p.setTable("order_item");
        p.setTable("publisher");
        QVERIFY(waitFor(p));
        QCOMPARE(p.table(), QString("publisher"));
        QCOMPARE(p.rows(), qint64(12));
        QCOMPARE(column(p, "name").value("distinct").toLongLong(), qint64(12));
        QVERIFY(column(p, "quantity").isEmpty());            // order_item's
        p.setTable("nope");
        QVERIFY(p.columns().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestProfile)
#include "tst_profile.moc"
