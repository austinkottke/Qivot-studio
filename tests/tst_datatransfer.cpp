#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "databasesession.h"
#include "datatransfer.h"
#include "querymodel.h"
#include "rowsmodel.h"
#include "sampledatabase.h"

/// Rows to files and back: CSV and JSON export of a table (as filtered and
/// sorted) and of a query (all of it), and CSV import into a table.
class TestDataTransfer : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_sample;

    QString read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
    void write(const QString &path, const QByteArray &text)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_sample = m_dir.filePath("bookshop.db");
        QVERIFY(SampleDatabase::create(m_sample));
    }

    void parsesCsv()
    {
        const auto r = DataTransfer::parseCsv(QString::fromUtf8("\xEF\xBB\xBF" "a,b,c\r\n1,\"x, y\",\"say \"\"hi\"\"\"\n2,\"two\nlines\",\n\n"), ',');
        QCOMPARE(r.size(), 3);
        QCOMPARE(r.at(0), QStringList({ "a", "b", "c" }));
        QCOMPARE(r.at(1), QStringList({ "1", "x, y", "say \"hi\"" }));
        QCOMPARE(r.at(2), QStringList({ "2", "two\nlines", "" }));
        QCOMPARE(DataTransfer::guessDelimiter("a;b;c\n1;2;3"), QChar(';'));
        QCOMPARE(DataTransfer::guessDelimiter("a\tb\n"), QChar('\t'));
        QCOMPARE(DataTransfer::guessDelimiter("a,b\n"), QChar(','));
    }

    void exportsATable()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_sample));
        RowsModel m;
        m.setSession(&db);
        m.setTable("publisher");
        m.setFilter("press");                 // Ashgrove Press
        const QString csv = m_dir.filePath("p.csv");
        QVariantMap r = m.exportTo(QUrl::fromLocalFile(csv), "csv");
        QVERIFY2(r.value("ok").toBool(), qPrintable(r.value("error").toString()));
        QCOMPARE(r.value("rows").toInt(), 1);
        QCOMPARE(read(csv), QString("id,name,country\r\n1,Ashgrove Press,Germany\r\n"));

        m.setFilter("");
        m.sortBy(1);
        m.sortBy(1);                          // name, descending
        const QString json = m_dir.filePath("p.json");
        r = m.exportTo(json, "json");
        QVERIFY(r.value("ok").toBool());
        const QJsonArray a = QJsonDocument::fromJson(read(json).toUtf8()).array();
        QCOMPARE(a.size(), 12);
        QCOMPARE(a.first().toObject().value("name").toString(), QString("Lantern Row"));
        QVERIFY(a.first().toObject().value("id").isDouble());     // a number, not "12"

        // Quotes and NULLs in CSV: authors with no first name.
        m.setTable("author");
        m.setFilter("");
        r = m.exportTo(m_dir.filePath("a.csv"), "csv");
        QCOMPARE(r.value("rows").toInt(), 150);
        QVERIFY(read(m_dir.filePath("a.csv")).contains("\r\n1,,"));   // author 1: first_name NULL -> empty
    }

    void exportsAWholeQuery()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_sample));
        QueryModel q;
        q.setSession(&db);
        QVERIFY(q.run("SELECT order_id, book_id FROM order_item ORDER BY order_id, book_id"));
        QVERIFY(q.truncated());                                  // the grid stops at MaxRows...
        const QString csv = m_dir.filePath("items.csv");
        const QVariantMap r = q.exportTo(csv, "csv");
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(r.value("rows").toInt(), 22760);                // ...the file has them all
        QCOMPARE(read(csv).count('\n'), 22761);
    }

    void importsCsv()
    {
        const QString file = m_dir.filePath("imp.db");
        QFile::remove(file);
        QVERIFY(QFile::copy(m_sample, file));
        DatabaseSession db;
        QVERIFY(db.open(file));
        const QString csv = m_dir.filePath("new.csv");
        write(csv, "Name;Country;Notes\n\"Bramble; Thorn\";Ireland;x\nOakleaf;;y\n");

        CsvImport imp;
        imp.setSession(&db);
        imp.setTable("publisher");
        QVERIFY(imp.load(QUrl::fromLocalFile(csv)));
        QCOMPARE(imp.delimiterName(), QString("semicolons"));
        QCOMPARE(imp.fileColumns(), QStringList({ "Name", "Country", "Notes" }));
        QCOMPARE(imp.mapping(), QStringList({ "name", "country", "" }));   // by name; Notes has nowhere to go
        QCOMPARE(imp.rowCount(), 2);
        QCOMPARE(imp.preview().first().toStringList().first(), QString("Bramble; Thorn"));

        QVERIFY(!imp.run());                                    // not allowed yet
        QVERIFY(imp.error().contains("Allow changes"));
        QVERIFY(db.allowChanges(true));
        QVERIFY2(imp.run(), qPrintable(imp.error()));
        QCOMPARE(imp.imported(), 2);
        QCOMPARE(db.table("publisher").value("rows").toLongLong(), qint64(14));
        QSqlQuery q(QSqlDatabase::database(db.connectionName()));
        QVERIFY(q.exec("SELECT country FROM publisher WHERE name = 'Oakleaf'") && q.next());
        QVERIFY(q.value(0).isNull());                           // empty -> NULL
        q.finish();                                             // (a read left open would hold SQLite's lock)

        // A bad row: nothing is imported. (name is UNIQUE.)
        write(csv, "name\nFresh One\nOakleaf\n");
        QVERIFY(imp.load(csv));
        QVERIFY(!imp.run());
        QVERIFY2(imp.error().contains("line 3"), qPrintable(imp.error()));
        QVERIFY(q.exec("SELECT COUNT(*) FROM publisher WHERE name = 'Fresh One'") && q.next());
        QCOMPARE(q.value(0).toInt(), 0);
        q.finish();

        // Mapping by hand, and no header row.
        write(csv, "Willow House,Wales\n");
        QVERIFY(imp.load(csv));
        imp.setHeaderRow(false);
        QCOMPARE(imp.fileColumns(), QStringList({ "Column 1", "Column 2" }));
        imp.setMapping(0, "name");
        imp.setMapping(1, "country");
        QVERIFY2(imp.run(), qPrintable(imp.error()));
        QVERIFY(q.exec("SELECT country FROM publisher WHERE name = 'Willow House'") && q.next());
        QCOMPARE(q.value(0).toString(), QString("Wales"));
    }
};

QTEST_GUILESS_MAIN(TestDataTransfer)
#include "tst_datatransfer.moc"
