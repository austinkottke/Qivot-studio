#include <QtTest>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "databasesession.h"
#include "sampledatabase.h"
#include "schemacompare.h"

/// Comparing two databases' structures, both ways, and applying the result.
class TestCompare : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_shop, m_changed;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_shop = m_dir.filePath("shop.db");
        QVERIFY(SampleDatabase::create(m_shop));
        // The same shop, moved on: a new column, a new table, one gone.
        m_changed = m_dir.filePath("shop-v2.db");
        QVERIFY(QFile::copy(m_shop, m_changed));
        {
            QSqlDatabase w = QSqlDatabase::addDatabase("QSQLITE", "v2");
            w.setDatabaseName(m_changed);
            QVERIFY(w.open());
            QSqlQuery q(w);
            QVERIFY(q.exec("ALTER TABLE book ADD COLUMN subtitle TEXT"));
            QVERIFY(q.exec("CREATE TABLE tag (id INTEGER PRIMARY KEY, label TEXT NOT NULL UNIQUE)"));
            QVERIFY(q.exec("DROP VIEW book_sales"));
            QVERIFY(q.exec("DROP TABLE review"));
            q = QSqlQuery();
            w.close();
        }
        QSqlDatabase::removeDatabase("v2");
    }

    void identicalMatch()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_shop));
        SchemaCompare c;
        c.setSession(&db);
        QVERIFY(c.other()->open(m_shop));
        QVERIFY(c.sameDialect());
        QVERIFY2(c.changes().isEmpty(), qPrintable(c.changes().join(" | ")));
        QVERIFY(c.migration().isEmpty());
    }

    void bothWays()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_shop));
        SchemaCompare c;
        c.setSession(&db);
        QVERIFY(c.other()->open(QUrl::fromLocalFile(m_changed)));

        // Make the other (v2) match this (v1): the column and table go, review comes back.
        QCOMPARE(c.direction(), QString("toOther"));
        QStringList ch = c.changes();
        QVERIFY2(ch.contains("Create table review"), qPrintable(ch.join(" | ")));
        QVERIFY2(ch.contains("Drop table tag"), qPrintable(ch.join(" | ")));
        QVERIFY2(ch.contains("Drop column book.subtitle"), qPrintable(ch.join(" | ")));
        QVERIFY(c.migration().contains("CREATE TABLE review"));

        // The other way: this (v1) to match v2.
        c.setDirection("toThis");
        ch = c.changes();
        QVERIFY2(ch.contains("Create table tag"), qPrintable(ch.join(" | ")));
        QVERIFY2(ch.contains("Drop table review"), qPrintable(ch.join(" | ")));
        QVERIFY2(ch.contains("Add column book.subtitle"), qPrintable(ch.join(" | ")));

        // Saving the SQL.
        QVERIFY(c.saveMigration(m_dir.filePath("v1-to-v2.sql")));
        QFile f(m_dir.filePath("v1-to-v2.sql"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()), c.migration());
    }

    void appliesToThis()
    {
        const QString file = m_dir.filePath("apply.db");
        QVERIFY(QFile::copy(m_shop, file));
        DatabaseSession db;
        QVERIFY(db.open(file));
        SchemaCompare c;
        c.setSession(&db);
        QVERIFY(c.other()->open(m_changed));
        c.setDirection("toThis");
        QVERIFY(!c.applyToThis().value("ok").toBool());          // changes not allowed yet
        // (review has rows: dropping it loses them, but that's what was asked for.)
        QVERIFY(db.allowChanges(true));
        const QVariantMap r = c.applyToThis();
        QVERIFY2(r.value("ok").toBool(), qPrintable(r.value("error").toString() + "\n" + r.value("failedStatement").toString()));
        QVERIFY(QFileInfo::exists(r.value("backup").toString()));
        QVERIFY2(c.changes().isEmpty(), qPrintable(c.changes().join(" | ")));   // now they match
        QVERIFY(db.table("tag").value("name") == "tag");
    }
};

QTEST_GUILESS_MAIN(TestCompare)
#include "tst_compare.moc"
