#include <QtTest>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "databasesession.h"
#include "sampledatabase.h"

/// DatabaseSession is everything the UI knows about a database, so these
/// checks are written against what the screens show.
class TestDatabaseSession : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString       m_sample;

    static QVariantMap find(const QVariantList &list, const QString &key, const QVariant &value)
    {
        for (const QVariant &v : list)
            if (v.toMap().value(key) == value)
                return v.toMap();
        return {};
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_sample = m_dir.filePath("bookshop.db");
        QString why;
        QVERIFY2(SampleDatabase::create(m_sample, &why), qPrintable(why));
    }

    void opensSample()
    {
        DatabaseSession db;
        QSignalSpy opened(&db, &DatabaseSession::openChanged);
        QVERIFY(!db.isOpen());
        QVERIFY(db.open(m_sample));
        QVERIFY(db.isOpen());
        QCOMPARE(opened.count(), 1);
        QCOMPARE(db.displayName(), QString("bookshop.db"));
        QVERIFY(db.fileSize() > 0);
        QVERIFY(db.error().isEmpty());
    }

    void listsTablesWithRowCounts()
    {
        DatabaseSession db;
        QVERIFY(db.open(QUrl::fromLocalFile(m_sample)));          // file: URLs work too

        const QVariantList tables = db.tables();
        QStringList names;
        for (const QVariant &t : tables)
            if (t.toMap().value("kind") == "table")
                names << t.toMap().value("name").toString();
        QCOMPARE(names, QStringList({ "author", "book", "customer", "order_item", "orders",
                                      "publisher", "review" }));

        QCOMPARE(find(tables, "name", "author").value("rows").toLongLong(), qint64(150));
        QCOMPARE(find(tables, "name", "book").value("rows").toLongLong(), qint64(1200));
        QCOMPARE(find(tables, "name", "book_sales").value("kind").toString(), QString("view"));
        // FTS shadow tables are storage, not something to browse.
        QVERIFY(find(tables, "name", "book_search_data").isEmpty());
    }

    void describesColumnsAndKeys()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_sample));
        const QVariantMap book = db.table("book");
        QCOMPARE(book.value("name").toString(), QString("book"));
        QCOMPARE(book.value("primaryKey").toStringList(), QStringList({ "id" }));

        const QVariantList columns = book.value("columns").toList();
        const QVariantMap id = find(columns, "name", "id");
        QVERIFY(id.value("primaryKey").toBool());
        QVERIFY(id.value("autoIncrement").toBool());

        const QVariantMap author = find(columns, "name", "author_id");
        QCOMPARE(author.value("references").toString(), QString("author.id"));
        QCOMPARE(author.value("refTable").toString(), QString("author"));
        QVERIFY(!author.value("nullable").toBool());

        QCOMPARE(find(columns, "name", "price").value("defaultValue").toString(), QString("9.99"));
    }

    void foreignKeysBothWays()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_sample));

        const QVariantList fks = db.table("book").value("foreignKeys").toList();
        QCOMPARE(fks.size(), 2);
        QCOMPARE(find(fks, "refTable", "author").value("onDelete").toString(), QString("CASCADE"));
        QCOMPARE(find(fks, "refTable", "publisher").value("onDelete").toString(), QString("SET NULL"));

        // author is pointed at by book.author_id — the "Referenced by" card.
        const QVariantList refs = db.table("author").value("referencedBy").toList();
        QCOMPARE(refs.size(), 1);
        QCOMPARE(refs.at(0).toMap().value("table").toString(), QString("book"));
        QCOMPARE(refs.at(0).toMap().value("columns").toStringList(), QStringList({ "author_id" }));
    }

    void indexes()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_sample));
        const QVariantList ix = db.table("orders").value("indexes").toList();
        const QVariantMap composite = find(ix, "name", "idx_orders_customer_placed");
        QCOMPARE(composite.value("columns").toStringList(), QStringList({ "customer_id", "placed_at" }));
        QVERIFY(!composite.value("unique").toBool());

        // The composite primary key of order_item is backed by an automatic unique index.
        const QVariantList itemIx = db.table("order_item").value("indexes").toList();
        QCOMPARE(itemIx.size(), 1);
        QVERIFY(itemIx.at(0).toMap().value("implicit").toBool());
    }

    void opensReadOnly()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_sample));
        QSqlQuery q(QSqlDatabase::database(db.connectionName()));
        QVERIFY(!q.exec("DELETE FROM review"));
        QVERIFY(q.lastError().text().contains("readonly", Qt::CaseInsensitive));
    }

    void rejectsMissingAndNonDatabaseFiles()
    {
        DatabaseSession db;
        QVERIFY(!db.open(m_dir.filePath("nope.db")));
        QVERIFY(db.error().contains("No such file"));
        QVERIFY(!db.isOpen());

        const QString text = m_dir.filePath("notes.txt");
        QFile f(text);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not a database, just some text that happens to be long enough");
        f.close();
        QVERIFY(!db.open(text));
        QVERIFY(db.error().contains("isn't an SQLite database"));
        QVERIFY(!db.isOpen());
    }

    void closeAndReopen()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_sample));
        db.close();
        QVERIFY(!db.isOpen());
        QVERIFY(db.tables().isEmpty());
        QVERIFY(db.table("book").isEmpty());
        QVERIFY(db.open(m_sample));                  // the connection name is reusable
        QCOMPARE(db.table("book").value("name").toString(), QString("book"));
    }

    void unknownTable()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_sample));
        QVERIFY(db.table("no_such_table").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDatabaseSession)
#include "tst_databasesession.moc"
