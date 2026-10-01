#include <QtTest>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>

#include "databasesession.h"
#include "querybuilder.h"
#include "sampledatabase.h"

/// The visual query builder: the SQL it writes runs and returns what was
/// asked for, and the Qivot C++ says the same thing.
class TestQueryBuilder : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir   m_dir;
    DatabaseSession m_db;

    QList<QVariantList> run(const QString &sql)
    {
        QList<QVariantList> rows;
        QSqlQuery q(QSqlDatabase::database(m_db.connectionName()));
        if (!q.exec(sql)) {
            qWarning().noquote() << q.lastError().text() << "\n" << sql;
            return rows;
        }
        while (q.next()) {
            QVariantList row;
            for (int i = 0; i < q.record().count(); ++i)
                row << q.value(i);
            rows << row;
        }
        return rows;
    }
    static int joinIndex(const QueryBuilder &b, const QString &table)
    {
        const QVariantList j = b.joinable();
        for (int i = 0; i < j.size(); ++i)
            if (j[i].toMap().value("table").toString() == table)
                return i;
        return -1;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        const QString file = m_dir.filePath("bookshop.db");
        QVERIFY(SampleDatabase::create(file));
        QVERIFY(m_db.open(file));
    }

    void wholeRows()
    {
        QueryBuilder b;
        b.setSession(&m_db);
        QVERIFY(b.sql().isEmpty());
        b.setFrom("book");
        QCOMPARE(b.sql(), QString("SELECT *\nFROM book\nLIMIT 100;"));
        QCOMPARE(run(b.sql()).size(), 100);
        // Whole rows of one table come back as typed models.
        QVERIFY2(b.cpp().startsWith("QiList<Book> rows = Book::objects()"), qPrintable(b.cpp()));
        QVERIFY(b.cpp().contains(".limit(100)"));
    }

    void filtersAndSorting()
    {
        QueryBuilder b;
        b.setSession(&m_db);
        b.setFrom("book");
        b.toggleColumn("book", "title");
        b.toggleColumn("book", "price");
        b.addFilter("book", "price");
        b.updateFilter(0, { { "op", ">" }, { "value", "30" } });
        b.addFilter("book", "title");
        b.updateFilter(1, { { "op", "contains" }, { "value", "the" } });
        b.addFilter("book", "isbn");
        b.updateFilter(2, { { "op", "is not empty" } });
        b.addSort("book.price", true);
        b.setLimit(5);
        QCOMPARE(b.sql(), QString("SELECT book.title, book.price\nFROM book\n"
                                  "WHERE book.price > 30\n  AND book.title LIKE '%the%'\n  AND book.isbn IS NOT NULL\n"
                                  "ORDER BY book.price DESC\nLIMIT 5;"));
        const QList<QVariantList> rows = run(b.sql());
        QVERIFY(!rows.isEmpty() && rows.size() <= 5);
        for (int i = 0; i < rows.size(); ++i) {
            QVERIFY(rows[i][1].toDouble() > 30);
            QVERIFY(rows[i][0].toString().contains("the", Qt::CaseInsensitive));
            if (i) QVERIFY(rows[i - 1][1].toDouble() >= rows[i][1].toDouble());
        }
        // A string value is quoted, a number isn't; quotes are escaped.
        b.updateFilter(1, { { "op", "=" }, { "value", "O'Brien" } });
        QVERIFY(b.sql().contains("book.title = 'O''Brien'"));
        // C++: chained filters, names as Qivot writes them.
        const QString cpp = b.cpp();
        QVERIFY2(cpp.contains(".select(QStringList{ \"title\", \"price\" })"), qPrintable(cpp));
        QVERIFY2(cpp.contains(".filter(QiWhere(\"price\") > 30)"), qPrintable(cpp));
        QVERIFY2(cpp.contains(".filter(QiWhere(\"isbn\").isNot(QVariant()))"), qPrintable(cpp));
        QVERIFY2(cpp.contains(".orderBy(\"price DESC\")"), qPrintable(cpp));
        // An empty value doesn't filter anything yet.
        b.addFilter("book", "pages");
        QVERIFY(!b.sql().contains("pages"));
    }

    // Join along the keys, count per group, sort by the count.
    void joinsAndAggregates()
    {
        QueryBuilder b;
        b.setSession(&m_db);
        b.setFrom("book");
        QVERIFY(joinIndex(b, "author") >= 0);
        QVERIFY(joinIndex(b, "review") >= 0);                 // children join too
        b.join(joinIndex(b, "author"));
        QCOMPARE(b.tables(), QStringList({ "book", "author" }));
        QCOMPARE(b.joins().first().toMap().value("on").toString(), QString("author.id = book.author_id"));
        QCOMPARE(joinIndex(b, "author"), -1);                 // joins once
        b.toggleColumn("author", "country");
        b.toggleColumn("book", "id");
        b.setAggregate(1, "count");
        QCOMPARE(b.columns().last().toMap().value("label").toString(), QString("count_id"));
        QVERIFY(b.sortKeys().first() == "count_id");
        b.addSort("count_id", true);
        b.setLimit(3);
        QCOMPARE(b.sql(), QString("SELECT author.country, COUNT(book.id) AS count_id\nFROM book\n"
                                  "JOIN author ON author.id = book.author_id\nGROUP BY author.country\n"
                                  "ORDER BY count_id DESC\nLIMIT 3;"));
        const QList<QVariantList> rows = run(b.sql());
        QCOMPARE(rows.size(), 3);
        QVERIFY(rows[0][1].toInt() >= rows[1][1].toInt());
        // Its total across all countries is every book.
        b.setLimit(0);
        int total = 0;
        for (const QVariantList &r : run(b.sql()))
            total += r[1].toInt();
        QCOMPARE(total, 1200);

        const QString cpp = b.cpp();
        QVERIFY2(cpp.contains(".join(QiJoin<Author>(QiWhere(\"author.id\") == QiWhere(\"book.author_id\")))"), qPrintable(cpp));
        QVERIFY2(cpp.contains(".select(QStringList{ \"author.country\", \"COUNT(book.id)\" })"), qPrintable(cpp));
        QVERIFY2(cpp.contains(".groupBy(\"author.country\")"), qPrintable(cpp));
        QVERIFY2(cpp.contains(".orderBy(\"COUNT(book.id) DESC\")"), qPrintable(cpp));
        QVERIFY2(cpp.contains("query.value(0) << query.value(1)"), qPrintable(cpp));

        // Removing a table takes what used it along.
        b.removeTable("author");
        QCOMPARE(b.tables(), QStringList({ "book" }));
        QCOMPARE(b.columns().size(), 1);
        QVERIFY(b.sorts().size() == 1);                       // count_id is still there
    }

    void leftJoinsAndChains()
    {
        QueryBuilder b;
        b.setSession(&m_db);
        b.setFrom("customer");
        b.join(joinIndex(b, "review"));                        // customer <- review
        b.setJoinLeft("review", true);                         // customers with no reviews too
        b.toggleColumn("customer", "name");
        b.toggleColumn("review", "id");
        b.setAggregate(1, "count");
        b.setLimit(0);
        const QList<QVariantList> rows = run(b.sql());
        QVERIFY(b.sql().contains("LEFT JOIN review ON review.customer_id = customer.id"));
        QVERIFY(!rows.isEmpty());
        QVERIFY(b.cpp().contains(", QiBaseJoin::Left)"));

        // Through a chain: order_item -> book -> author; removing book drops author.
        b.setFrom("order_item");
        b.join(joinIndex(b, "book"));
        b.join(joinIndex(b, "author"));
        QCOMPARE(b.tables(), QStringList({ "order_item", "book", "author" }));
        b.removeTable("book");
        QCOMPARE(b.tables(), QStringList({ "order_item" }));
    }
};

QTEST_GUILESS_MAIN(TestQueryBuilder)
#include "tst_querybuilder.moc"
