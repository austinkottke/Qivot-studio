#include <QtTest>
#include <QCryptographicHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "databasesession.h"
#include "sampledatabase.h"
#include "sampleschema.h"

/// The sample databases: each builds on SQLite with every constraint holding,
/// comes out the same every time, and has the relationships it promises.
/// (tst_servers loads them into PostgreSQL, MySQL and SQL Server.)
class TestSamples : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString file(const QString &id) const { return m_dir.filePath(id + ".db"); }

    static qint64 scalar(QSqlDatabase db, const QString &sql)
    {
        QSqlQuery q(db);
        return q.exec(sql) && q.next() ? q.value(0).toLongLong() : -1;
    }

    // The foreign keys of `table` as "cols->ref(refcols) ACTION".
    static QStringList keysOf(DatabaseSession &db, const QString &table)
    {
        QStringList out;
        for (const QiTableInfo &t : db.tableInfos())
            if (t.name == table)
                for (const QiForeignKeyInfo &fk : t.foreignKeys)
                    out << fk.columns.join(',') + "->" + fk.refTable + "(" + fk.refColumns.join(',') + ")";
        return out;
    }

    void checkData(const QString &connection);

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
    }

    void catalogue()
    {
        QStringList ids;
        for (const SampleDatabase::Info &s : SampleDatabase::catalogue()) {
            ids << s.id;
            QVERIFY(!s.title.isEmpty() && !s.summary.isEmpty() && s.highlights.size() >= 2);
        }
        QCOMPARE(ids, QStringList({ "bookshop", "university", "company", "music" }));
        // What the lists say each one holds is what it holds.
        for (const SampleDatabase::Info &s : SampleDatabase::catalogue()) {
            const SampleSchema schema = SampleDatabase::build(s.id);
            int rows = 0;
            QStringList names;
            for (const SampleSchema::Table &t : schema.tables()) {
                rows += t.rows.size();
                names << t.name;
            }
            QCOMPARE(names, s.tables);
            QCOMPARE(rows, s.rows);
        }
        QVERIFY(SampleDatabase::build("nope").tables().isEmpty());
        QString why;
        QVERIFY(!SampleDatabase::createFile("nope", file("nope"), &why));
        QVERIFY(why.contains("nope"));
    }

    void buildsWithEveryConstraint_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<int>("tables");
        QTest::addColumn<int>("minRows");
        QTest::newRow("bookshop")   << "bookshop"   << 7  << 40000;
        QTest::newRow("university") << "university" << 9  << 15000;
        QTest::newRow("company")    << "company"    << 10 << 10000;
        QTest::newRow("music")      << "music"      << 11 << 30000;
    }
    void buildsWithEveryConstraint()
    {
        QFETCH(QString, id);
        QFETCH(int, tables);
        QFETCH(int, minRows);
        QString why;
        QVERIFY2(SampleDatabase::createFile(id, file(id), &why), qPrintable(why));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "check");
            db.setDatabaseName(file(id));
            QVERIFY(db.open());
            // NOT NULL, UNIQUE and CHECK held as the rows went in; the references
            // (loaded with foreign_keys off) are checked here.
            QSqlQuery q(db);
            QVERIFY(q.exec("PRAGMA foreign_key_check"));
            if (q.next())
                QFAIL(qPrintable(QString("%1.%2 row %3 points at no %4")
                                     .arg(id, q.value(0).toString(), q.value(1).toString(), q.value(2).toString())));
            q.finish();
            QCOMPARE(scalar(db, "PRAGMA user_version"), qint64(SampleDatabase::version(id)));
            qint64 rows = 0;
            int count = 0;
            const SampleSchema schema = SampleDatabase::build(id);
            for (const SampleSchema::Table &t : schema.tables()) {
                const qint64 n = scalar(db, "SELECT COUNT(*) FROM " + t.name);
                QCOMPARE(n, qint64(t.rows.size()));
                QVERIFY2(n > 0, qPrintable(t.name + " is empty"));
                rows += n;
                ++count;
            }
            QCOMPARE(count, tables);
            QVERIFY2(rows >= minRows, qPrintable(QString::number(rows)));
            // Every view runs.
            QSqlQuery views(db);
            QVERIFY(views.exec("SELECT name FROM sqlite_master WHERE type = 'view'"));
            int nViews = 0;
            while (views.next()) {
                ++nViews;
                QVERIFY2(scalar(db, "SELECT COUNT(*) FROM " + views.value(0).toString()) > 0, qPrintable(views.value(0).toString()));
            }
            QVERIFY(nViews >= 1);
            views = QSqlQuery();
            q = QSqlQuery();
            db.close();
        }
        QSqlDatabase::removeDatabase("check");
    }

    // The same rows, every time, everywhere.
    void deterministic()
    {
        for (const SampleDatabase::Info &s : SampleDatabase::catalogue())
            for (const QString &dialect : { "sqlite", "postgres", "mysql", "sqlserver" }) {
                const QByteArray a = SampleDatabase::build(s.id).statements(dialect).join('\n').toUtf8();
                const QByteArray b = SampleDatabase::build(s.id).statements(dialect).join('\n').toUtf8();
                QVERIFY2(a == b, qPrintable(s.id + " on " + dialect));
            }
    }

    // The relationships each sample is there to show.
    void relationships()
    {
        DatabaseSession db;
        QVERIFY2(db.open(file("university")), qPrintable(db.error()));
        // Circular: departments have chairs, instructors have departments.
        QVERIFY(keysOf(db, "department").contains("chair_id->instructor(id)"));
        QVERIFY(keysOf(db, "instructor").contains("department_id->department(id)"));
        // A course needing courses: two references from one table to the same one.
        QCOMPARE(keysOf(db, "course_prereq").size(), 2);
        // Enrollments refer to a section by its three-column key.
        QVERIFY2(keysOf(db, "enrollment").contains("course_id,term_id,section_no->section(course_id,term_id,section_no)"),
                 qPrintable(keysOf(db, "enrollment").join(" | ")));
        db.close();

        QVERIFY2(db.open(file("company")), qPrintable(db.error()));
        QVERIFY(keysOf(db, "employee").contains("manager_id->employee(id)"));          // to itself
        QVERIFY(keysOf(db, "office").contains("country_code->country(code)"));          // a text key
        db.close();

        QVERIFY2(db.open(file("music")), qPrintable(db.error()));
        QVERIFY(keysOf(db, "playlist_track").contains("playlist_id->playlist(id)"));
        QVERIFY(keysOf(db, "playlist_track").contains("track_id->track(id)"));
        QVERIFY(keysOf(db, "employee").contains("reports_to->employee(id)"));
        db.close();
    }

    // The data hangs together beyond the keys.
    void dataMakesSense()
    {
        { checkData("sense"); }
        QSqlDatabase::removeDatabase("sense");
    }
    // The server scripts: what tools/sample-servers loads.
    void serverScripts()
    {
        const SampleSchema s = SampleDatabase::build("university");
        const QString pg = s.script("postgres", "university");
        QVERIFY(pg.contains("CREATE DATABASE university;\n\\connect university"));
        QVERIFY(pg.contains("GENERATED BY DEFAULT AS IDENTITY"));
        QVERIFY(pg.contains("SELECT setval(pg_get_serial_sequence('student', 'id')"));
        QVERIFY(pg.contains("ALTER TABLE enrollment ADD CONSTRAINT fk_enrollment_section FOREIGN KEY "
                            "(course_id, term_id, section_no) REFERENCES section (course_id, term_id, section_no) ON DELETE CASCADE"));
        QVERIFY(pg.contains("FALSE"));
        const QString my = s.script("mysql", "university");
        QVERIFY(my.contains("INT AUTO_INCREMENT PRIMARY KEY"));
        QVERIFY(my.contains("USE university;"));
        const QString ms = s.script("sqlserver", "university");
        QVERIFY(ms.contains("INT IDENTITY(1,1) PRIMARY KEY"));
        QVERIFY(ms.contains("SET IDENTITY_INSERT student ON;\nGO"));
        QVERIFY(ms.contains("DATETIME2") || ms.contains("DATE"));
        QVERIFY(!ms.contains("RESTRICT"));
        // No statement too long for SQL Server's 1000-row VALUES limit.
        for (const QString &st : s.statements("sqlserver"))
            QVERIFY(st.count("),\n(") < 1000);
    }

    // The app's way in: made once, kept, rebuilt when the sample changes.
    void openSample()
    {
        QStandardPaths::setTestModeEnabled(true);
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QFile::remove(dir + "/music.db");
        DatabaseSession db;
        QVERIFY2(db.openSample("music"), qPrintable(db.error()));
        QCOMPARE(db.displayName(), QString("music.db"));
        QCOMPARE(db.sampleId(), QString("music"));
        QVERIFY(db.readOnly());
        db.close();
        // An out-of-date copy (an older version) is rebuilt.
        {
            QSqlDatabase w = QSqlDatabase::addDatabase("QSQLITE", "age");
            w.setDatabaseName(dir + "/music.db");
            QVERIFY(w.open());
            QSqlQuery(w).exec("PRAGMA user_version = 0");
            QSqlQuery(w).exec("DELETE FROM genre WHERE id NOT IN (SELECT genre_id FROM track WHERE genre_id IS NOT NULL)");
            w.close();
        }
        QSqlDatabase::removeDatabase("age");
        QVERIFY(db.openSample("music"));
        QSqlQuery q(QSqlDatabase::database(db.connectionName()));
        QVERIFY(q.exec("PRAGMA user_version") && q.next());
        QCOMPARE(q.value(0).toInt(), SampleDatabase::version("music"));
        q = QSqlQuery();
        db.close();
        QCOMPARE(db.sampleId(), QString());            // closed: not a sample any more
        QVERIFY(!db.openSample("nope"));
        QVERIFY(db.error().contains("nope"));
        QCOMPARE(db.samples().size(), 4);
    }
};

void TestSamples::checkData(const QString &connection)
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(file("company"));
    QVERIFY(db.open());
    // One chief executive; everyone else has a manager hired... somewhere in the tree.
    QCOMPARE(scalar(db, "SELECT COUNT(*) FROM employee WHERE manager_id IS NULL"), qint64(1));
    QCOMPARE(scalar(db, "SELECT COUNT(*) FROM employee WHERE manager_id >= id"), qint64(0));   // a tree: no loops
    // At most one current salary each.
    QCOMPARE(scalar(db, "SELECT COUNT(*) FROM (SELECT employee_id FROM salary WHERE to_date IS NULL "
                        "GROUP BY employee_id HAVING COUNT(*) > 1) x"), qint64(0));
    db.close();
    db.setDatabaseName(file("music"));
    QVERIFY(db.open());
    // Invoice totals are the sum of their lines.
    QCOMPARE(scalar(db, "SELECT COUNT(*) FROM invoice i WHERE ABS(i.total - (SELECT SUM(unit_price * quantity) "
                        "FROM invoice_line l WHERE l.invoice_id = i.id)) > 0.001"), qint64(0));
    db.close();
    db.setDatabaseName(file("university"));
    QVERIFY(db.open());
    // Only the current term is ungraded.
    QCOMPARE(scalar(db, "SELECT COUNT(DISTINCT term_id) FROM enrollment WHERE grade IS NULL"), qint64(1));
    db.close();
}

QTEST_GUILESS_MAIN(TestSamples)
#include "tst_samples.moc"
