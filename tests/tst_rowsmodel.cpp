#include <QtTest>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "databasesession.h"
#include "rowsmodel.h"
#include "sampledatabase.h"

/// RowsModel against the bookshop sample plus a small hand-made table with
/// awkward values (NULLs, blobs, newlines, LIKE wildcards).
class TestRowsModel : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir   m_dir;
    QString         m_sample;
    QString         m_odd;
    DatabaseSession m_db;

    QString cell(RowsModel &m, int row, int col, int role = Qt::DisplayRole)
    {
        return m.data(m.index(row, col), role).toString();
    }
    int columnOf(RowsModel &m, const QString &name)
    {
        for (int i = 0; i < m.columnCount(); ++i)
            if (m.headerData(i, Qt::Horizontal, Qt::DisplayRole).toString() == name)
                return i;
        return -1;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_sample = m_dir.filePath("bookshop.db");
        QVERIFY(SampleDatabase::create(m_sample));

        // A tiny table of edge cases, written before the session opens it read-only.
        m_odd = m_dir.filePath("odd.db");
        {
            QSqlDatabase w = QSqlDatabase::addDatabase("QSQLITE", "odd_writer");
            w.setDatabaseName(m_odd);
            QVERIFY(w.open());
            QSqlQuery q(w);
            QVERIFY(q.exec("CREATE TABLE note (id INTEGER PRIMARY KEY, body TEXT, data BLOB, score REAL)"));
            QVERIFY(q.exec("INSERT INTO note (body, data, score) VALUES "
                           "('first line\nsecond line', x'00010203', 1.5),"
                           "(NULL, NULL, NULL),"
                           "('50% off', NULL, 2),"
                           "('5000 off', NULL, 3),"
                           "('under_score', NULL, 4)"));
            q = QSqlQuery();
            w.close();
        }
        QSqlDatabase::removeDatabase("odd_writer");
    }

    void init() { QVERIFY(m_db.open(m_sample)); }

    void countsAndColumns()
    {
        RowsModel m;
        m.setSession(&m_db);
        m.setTable("book");
        QCOMPARE(m.totalRows(), qint64(1200));
        QCOMPARE(m.matchingRows(), qint64(1200));
        QCOMPARE(m.rowCount(), 1200);
        QCOMPARE(m.columnCount(), 8);
        QCOMPARE(m.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString(), QString("id"));
        QCOMPARE(m.columns().at(0).toMap().value("primaryKey").toBool(), true);
        QVERIFY(m.error().isEmpty());
    }

    void pagesAcrossBoundaries()
    {
        RowsModel m;
        m.setSession(&m_db);
        m.setTable("book");
        // Natural order is by primary key, so row N is id N+1 — on every page.
        for (int row : { 0, 199, 200, 201, 999, 1199 })
            QCOMPARE(cell(m, row, 0), QString::number(row + 1));
        QVERIFY(!m.data(m.index(1200, 0), Qt::DisplayRole).isValid());   // past the end
    }

    void cacheStaysBounded()
    {
        RowsModel m;
        m.setSession(&m_db);
        m.setTable("order_item");                    // ~22,000 rows = 100+ pages
        QVERIFY(m.rowCount() > RowsModel::PageSize * (RowsModel::MaxPages + 10));
        for (int row = 0; row < m.rowCount(); row += RowsModel::PageSize)
            m.data(m.index(row, 0), Qt::DisplayRole);
        QVERIFY(m.cachedPages() <= RowsModel::MaxPages);
        // An evicted page comes back correctly.
        QCOMPARE(m.data(m.index(0, 0), Qt::DisplayRole).toInt(), 1);
    }

    void sortsBothWays()
    {
        RowsModel m;
        m.setSession(&m_db);
        m.setTable("book");
        const int price = columnOf(m, "price");
        QVERIFY(price >= 0);

        m.sortBy(price);
        QCOMPARE(m.sortColumn(), price);
        QVERIFY(!m.sortDescending());
        const double lowest = m.data(m.index(0, price), RowsModel::RawRole).toDouble();
        QVERIFY(lowest <= m.data(m.index(600, price), RowsModel::RawRole).toDouble());
        QVERIFY(lowest <= m.data(m.index(1199, price), RowsModel::RawRole).toDouble());

        m.sortBy(price);                              // second click: descending
        QVERIFY(m.sortDescending());
        QVERIFY(m.data(m.index(0, price), RowsModel::RawRole).toDouble()
                >= m.data(m.index(1199, price), RowsModel::RawRole).toDouble());

        m.clearSort();
        QCOMPARE(m.sortColumn(), -1);
        QCOMPARE(cell(m, 0, 0), QString("1"));
    }

    void filtersAnyColumn()
    {
        RowsModel m;
        m.setSession(&m_db);
        m.setTable("publisher");
        QCOMPARE(m.rowCount(), 12);
        m.setFilter("press");                          // "Ashgrove Press", case-insensitive
        QCOMPARE(m.matchingRows(), qint64(1));
        QCOMPARE(m.totalRows(), qint64(12));
        QVERIFY(cell(m, 0, columnOf(m, "name")).contains("Press"));
        m.setFilter("no such publisher");
        QCOMPARE(m.rowCount(), 0);
        m.setFilter("");
        QCOMPARE(m.rowCount(), 12);
    }

    void filterTreatsWildcardsLiterally()
    {
        QVERIFY(m_db.open(m_odd));
        RowsModel m;
        m.setSession(&m_db);
        m.setTable("note");
        m.setFilter("50%");
        QCOMPARE(m.matchingRows(), qint64(1));        // not "5000 off"
        m.setFilter("5_00");
        QCOMPARE(m.matchingRows(), qint64(0));        // "_" isn't "any character" (would match "5000")
        m.setFilter("under_");
        QCOMPARE(m.matchingRows(), qint64(1));
    }

    void displaysOddValues()
    {
        QVERIFY(m_db.open(m_odd));
        RowsModel m;
        m.setSession(&m_db);
        m.setTable("note");
        const int body = columnOf(m, "body"), data = columnOf(m, "data"), score = columnOf(m, "score");

        QCOMPARE(cell(m, 0, body), QString("first line ↵ second line"));   // one line in a cell
        QCOMPARE(cell(m, 0, data), QString("BLOB · 4 bytes"));
        QVERIFY(m.data(m.index(0, score), RowsModel::NumberRole).toBool());

        QCOMPARE(cell(m, 1, body), QString("NULL"));
        QVERIFY(m.data(m.index(1, body), RowsModel::NullRole).toBool());
        QVERIFY(!m.data(m.index(0, body), RowsModel::NullRole).toBool());

        // The inspector gets the real value, newline and all.
        QCOMPARE(m.rowAt(0).value("body").toString(), QString("first line\nsecond line"));
        QVERIFY(m.rowAt(1).value("body").isNull());
    }

    void worksOnViews()
    {
        RowsModel m;
        m.setSession(&m_db);
        m.setTable("book_sales");
        QCOMPARE(m.rowCount(), 1200);
        QVERIFY(m.columnCount() > 0);
        m.sortBy(columnOf(m, "revenue"));
        QVERIFY(m.error().isEmpty());
    }

    void newTableResetsSortAndFilter()
    {
        RowsModel m;
        m.setSession(&m_db);
        m.setTable("book");
        m.sortBy(1);
        m.setFilter("the");
        m.setTable("author");
        QCOMPARE(m.sortColumn(), -1);
        QVERIFY(m.filter().isEmpty());
        QCOMPARE(m.rowCount(), 150);
    }

    void emptiesWhenDatabaseCloses()
    {
        RowsModel m;
        m.setSession(&m_db);
        m.setTable("book");
        QCOMPARE(m.rowCount(), 1200);
        m_db.close();
        QCOMPARE(m.rowCount(), 0);
        QCOMPARE(m.columnCount(), 0);
        QVERIFY(m_db.open(m_sample));
        QCOMPARE(m.rowCount(), 1200);                 // same table, back again
    }
};

QTEST_GUILESS_MAIN(TestRowsModel)
#include "tst_rowsmodel.moc"
