#include <QtTest>
#include <QSqlError>
#include <QSqlQuery>

#include "databasesession.h"
#include "rowsmodel.h"
#include "querymodel.h"
#include "schemadesign.h"
#include "querybuilder.h"
#include "tableprofile.h"
#include "sampledatabase.h"
#include "sampleschema.h"
#include "datatransfer.h"
#include "queryplan.h"
#include "sqlcompleter.h"
#include "sshtunnel.h"
#include <QProcess>
#include <QTemporaryDir>

/// Studio against real servers holding the public sample databases
/// (Pagila on PostgreSQL, Sakila and Employees on MySQL, Chinook on SQL Server).
/// Each test skips unless its server is configured:
///
///   STUDIO_TEST_PG=host:port      STUDIO_TEST_PG_USER / _PASS      (default qivot / qivot)
///   STUDIO_TEST_MYSQL=host:port   STUDIO_TEST_MYSQL_USER / _PASS   (default root / qivot)
///   STUDIO_TEST_MSSQL=host:port   STUDIO_TEST_MSSQL_USER / _PASS   (default sa / Qivot_Test1)
class TestServers : public QObject
{
    Q_OBJECT

private:
    static QVariantMap settings(const QString &type, const QString &env, const QString &database,
                                const QString &defUser, const QString &defPass)
    {
        const QString where = qEnvironmentVariable(qPrintable(env));
        return { { "type", type },
                 { "host", where.section(':', 0, 0) },
                 { "port", where.section(':', 1, 1).toInt() },
                 { "database", database },
                 { "user", qEnvironmentVariable(qPrintable(env + "_USER"), defUser) },
                 { "password", qEnvironmentVariable(qPrintable(env + "_PASS"), defPass) } };
    }
    static int columnOf(RowsModel &m, const QString &name)
    {
        for (int i = 0; i < m.columnCount(); ++i)
            if (m.headerData(i, Qt::Horizontal, Qt::DisplayRole).toString() == name)
                return i;
        return -1;
    }
    static qint64 scalar(DatabaseSession &db, const QString &sql)
    {
        QSqlQuery q(QSqlDatabase::database(db.connectionName()));
        return q.exec(sql) && q.next() ? q.value(0).toLongLong() : -1;
    }
    // Every row the filter returned really contains the text in some column.
    static bool allRowsContain(RowsModel &m, const QString &needle, int limit = 400)
    {
        for (int r = 0; r < qMin(m.rowCount(), limit); ++r) {
            bool hit = false;
            for (int c = 0; c < m.columnCount() && !hit; ++c)
                hit = m.data(m.index(r, c), RowsModel::RawRole).toString().contains(needle, Qt::CaseInsensitive);
            if (!hit)
                return false;
        }
        return true;
    }
    static int linkCount(DatabaseSession &db) { return db.diagram().value("links").toList().size(); }


    // ---- Designer migrations against a real server ----
    // A writable connection (the sessions are read-only): to `database` on the
    // server behind `env`, as connection `name`.
    static QSqlDatabase writable(const QString &type, const QString &env, const QString &database,
                                 const QString &defUser, const QString &defPass, const QString &name)
    {
        const QVariantMap s = settings(type, env, database, defUser, defPass);
        QSqlDatabase db;
        if (type == "sqlserver") {
            db = QSqlDatabase::addDatabase("QODBC", name);
            db.setDatabaseName(QString("Driver={ODBC Driver 18 for SQL Server};Server=%1,%2;Database=%3;Uid=%4;Pwd=%5;"
                                       "TrustServerCertificate=yes;")
                                   .arg(s["host"].toString()).arg(s["port"].toInt())
                                   .arg(database, s["user"].toString(), s["password"].toString()));
        } else {
            db = QSqlDatabase::addDatabase(type == "postgres" ? "QPSQL"
                                           : QSqlDatabase::isDriverAvailable("QMYSQL") ? "QMYSQL" : "QMARIADB", name);
            db.setHostName(s["host"].toString());
            db.setPort(s["port"].toInt());
            db.setDatabaseName(database);
            db.setUserName(s["user"].toString());
            db.setPassword(s["password"].toString());
        }
        db.open();
        return db;
    }
    // Run a script one statement at a time; "" or the failing statement and its error.
    static QString run(QSqlDatabase db, const QString &script)
    {
        QString text;
        for (const QString &line : script.split('\n'))
            if (!line.trimmed().startsWith("--"))
                text += line + '\n';
        QSqlQuery q(db);
        for (const QString &statement : text.split(';')) {
            if (statement.trimmed().isEmpty())
                continue;
            if (!q.exec(statement))
                return statement.trimmed() + "\n  -> " + q.lastError().text();
        }
        return QString();
    }

    // Make a scratch database, design a set of changes to it, run the
    // migration Studio writes, and read the result back: nothing may be left
    // to change, and the rows must still be there.
    void migrationRoundTrip(const QString &type, const QString &env, const QString &adminDb,
                            const QString &defUser, const QString &defPass)
    {
        const QString scratch = "studio_migrate";
        {
            QSqlDatabase admin = writable(type, env, adminDb, defUser, defPass, "admin");
            QVERIFY2(admin.isOpen(), qPrintable(admin.lastError().text()));
            QSqlQuery q(admin);
            q.exec(QString("DROP DATABASE IF EXISTS %1").arg(scratch));
            QVERIFY2(q.exec(QString("CREATE DATABASE %1").arg(scratch)), qPrintable(q.lastError().text()));
        }
        QSqlDatabase::removeDatabase("admin");

        // The starting point: three tables with a few rows, in the server's own types.
        const bool pg = type == "postgres", my = type == "mysql";
        const QString id = pg ? "id serial PRIMARY KEY" : my ? "id INT AUTO_INCREMENT PRIMARY KEY" : "id INT IDENTITY(1,1) PRIMARY KEY";
        const QString text = pg ? "text" : my ? "VARCHAR(200)" : "NVARCHAR(200)";
        const QString money = pg ? "numeric(10,2)" : "DECIMAL(10,2)";
        {
            QSqlDatabase w = writable(type, env, scratch, defUser, defPass, "setup");
            QVERIFY2(w.isOpen(), qPrintable(w.lastError().text()));
            const QString failed = run(w, QString(
                "CREATE TABLE author (%1, name %2 NOT NULL, country %2);"
                "CREATE TABLE book (%1, title %2 NOT NULL, author_id INT NOT NULL, price %3,"
                "  CONSTRAINT fk_book_author FOREIGN KEY (author_id) REFERENCES author (id));"
                "CREATE TABLE orders (%1, book_id INT, qty INT,"
                "  CONSTRAINT fk_orders_book FOREIGN KEY (book_id) REFERENCES book (id));"
                "INSERT INTO author (name, country) VALUES ('Ada', 'UK');"
                "INSERT INTO author (name, country) VALUES ('Grace', 'US');"
                "INSERT INTO book (title, author_id, price) VALUES ('Notes', 1, 9.50);"
                "INSERT INTO book (title, author_id, price) VALUES ('Compilers', 2, 12.00);"
                "INSERT INTO orders (book_id, qty) VALUES (1, 3);"
                "INSERT INTO orders (book_id, qty) VALUES (2, 1);").arg(id, text, money));
            QVERIFY2(failed.isEmpty(), qPrintable(failed));
        }
        QSqlDatabase::removeDatabase("setup");

        DatabaseSession db;
        QVERIFY2(db.connectTo(settings(type, env, scratch, defUser, defPass)), qPrintable(db.error()));
        SchemaDesign d;
        d.setAutosaveEnabled(false);
        d.setSession(&db);
        auto col = [&](const QString &table, const QString &name) {
            const QVariantList cols = d.table(table).value("columns").toList();
            for (int i = 0; i < cols.size(); ++i)
                if (cols[i].toMap().value("name").toString() == name)
                    return i;
            return -1;
        };
        const QString intType = pg ? "integer" : my ? "int" : "int";
        d.addColumn("book", "subtitle", text);                                                   // add
        QVERIFY(d.updateColumn("author", col("author", "country"), { { "name", "nationality" } })); // rename column
        QVERIFY(d.updateColumn("orders", col("orders", "qty"), { { "type", pg ? "bigint" : "BIGINT" } })); // retype
        d.removeColumn("book", col("book", "price"));                                             // drop column
        QVERIFY(d.renameTable("orders", "purchase"));                                             // rename table
        QVERIFY(d.setReference("book", "author_id", ""));                                         // drop a foreign key
        QVERIFY(d.updateColumn("author", col("author", "name"), { { "unique", true } }));         // unique
        d.addTable("tag");
        d.addColumn("tag", "label", text);
        QVERIFY(d.updateColumn("tag", 1, { { "unique", true }, { "nullable", false } }));
        d.addTable("book_tag");                                                                   // many-to-many
        d.addColumn("book_tag", "book_id", intType);
        d.addColumn("book_tag", "tag_id", intType);
        d.removeColumn("book_tag", 0);
        QVERIFY(d.updateColumn("book_tag", 0, { { "primaryKey", true } }));
        QVERIFY(d.updateColumn("book_tag", 1, { { "primaryKey", true } }));
        QVERIFY(d.setReference("book_tag", "book_id", "book", "CASCADE"));
        QVERIFY(d.setReference("book_tag", "tag_id", "tag", "CASCADE"));
        QCOMPARE(d.errorCount(), 0);
        const QString script = d.migration();

        {
            QSqlDatabase w = writable(type, env, scratch, defUser, defPass, "migrate");
            const QString failed = run(w, script);
            QVERIFY2(failed.isEmpty(), qPrintable(failed + "\n\n" + script));
        }
        QSqlDatabase::removeDatabase("migrate");

        // Read back: the database is now the design.
        DatabaseSession after;
        QVERIFY2(after.connectTo(settings(type, env, scratch, defUser, defPass)), qPrintable(after.error()));
        SchemaDesign readBack;
        readBack.setAutosaveEnabled(false);
        readBack.reset(after.tableInfos(), type);
        QVector<DesignTable> mine = d.designTables();
        for (DesignTable &t : mine) {
            t.origin = t.info.name;
            t.columnOrigins.clear();
            for (const QiColumnInfo &c : t.info.columns)
                t.columnOrigins << c.name;
        }
        const QStringList left = Migration::changes(readBack.infos(), mine);
        QVERIFY2(left.isEmpty(), qPrintable(left.join('\n') + "\n\n" + script));
        QCOMPARE(scalar(after, "SELECT COUNT(*) FROM purchase"), qint64(2));
        QCOMPARE(scalar(after, "SELECT COUNT(*) FROM author WHERE nationality = 'UK'"), qint64(1));

        after.close();
        db.close();
        {
            QSqlDatabase admin = writable(type, env, adminDb, defUser, defPass, "admin");
            QSqlQuery q(admin);
            if (type == "sqlserver")
                q.exec(QString("ALTER DATABASE %1 SET SINGLE_USER WITH ROLLBACK IMMEDIATE").arg(scratch));
            q.exec(QString("DROP DATABASE %1").arg(scratch));
        }
        QSqlDatabase::removeDatabase("admin");
    }


    // The query builder's SQL, on each server: join along a key, count per
    // group, biggest first, five rows (TOP on SQL Server, LIMIT elsewhere).
    void builderRuns(const QVariantMap &settings, const QString &from, const QString &parent, const QString &via,
                     const QString &groupColumn, const QString &countColumn)
    {
        DatabaseSession db;
        QVERIFY2(db.connectTo(settings), qPrintable(db.error()));
        QueryBuilder b;
        b.setSession(&db);
        b.setFrom(from);
        int j = -1;
        const QVariantList joinable = b.joinable();
        for (int i = 0; i < joinable.size(); ++i)
            if (joinable[i].toMap().value("table").toString() == parent
                && joinable[i].toMap().value("on").toString().contains(via))     // film has two keys to language
                j = i;
        QVERIFY2(j >= 0, qPrintable(parent));
        b.join(j);
        b.toggleColumn(parent, groupColumn);
        b.toggleColumn(from, countColumn);
        b.setAggregate(1, "count");
        b.addSort(b.columns().at(1).toMap().value("label").toString(), true);
        b.setLimit(5);
        QSqlQuery q(QSqlDatabase::database(db.connectionName()));
        QVERIFY2(q.exec(b.sql()), qPrintable(q.lastError().text() + "\n" + b.sql()));
        int rows = 0;
        qint64 last = std::numeric_limits<qint64>::max();
        while (q.next()) {
            QVERIFY(q.value(1).toLongLong() <= last);
            last = q.value(1).toLongLong();
            ++rows;
        }
        QVERIFY2(rows > 0 && rows <= 5, qPrintable(QString("%1 rows from\n%2").arg(rows).arg(b.sql())));
    }


    // Load every sample into a fresh database on the server, then read it back:
    // the same rows as the description, every view runs, and Studio sees the
    // same relationships as on SQLite.
    void samplesLoad(const QString &type, const QString &env, const QString &adminDb,
                     const QString &defUser, const QString &defPass)
    {
        for (const SampleDatabase::Info &info : SampleDatabase::catalogue()) {
            const QString name = "studio_sample_" + info.id;
            {
                QSqlDatabase admin = writable(type, env, adminDb, defUser, defPass, "admin");
                QVERIFY2(admin.isOpen(), qPrintable(admin.lastError().text()));
                QSqlQuery q(admin);
                if (type == "sqlserver")      // in use by a previous run's session: take it back first
                    q.exec(QString("IF DB_ID('%1') IS NOT NULL ALTER DATABASE %1 SET SINGLE_USER WITH ROLLBACK IMMEDIATE").arg(name));
                q.exec(QString("DROP DATABASE IF EXISTS %1").arg(name));
                QVERIFY2(q.exec(QString("CREATE DATABASE %1").arg(name)), qPrintable(q.lastError().text()));
            }
            QSqlDatabase::removeDatabase("admin");

            const SampleSchema schema = SampleDatabase::build(info.id);
            {
                QSqlDatabase w = writable(type, env, name, defUser, defPass, "load");
                QVERIFY2(w.isOpen(), qPrintable(w.lastError().text()));
                QString why;
                QElapsedTimer timer;
                timer.start();
                QVERIFY2(SampleDatabase::load(info.id, w, type, &why), qPrintable(info.id + " on " + type + ": " + why));
                qInfo().noquote() << info.id << "on" << type << "loaded in" << timer.elapsed() << "ms";
            }
            QSqlDatabase::removeDatabase("load");

            DatabaseSession db;
            QVERIFY2(db.connectTo(settings(type, env, name, defUser, defPass)), qPrintable(db.error()));
            for (const SampleSchema::Table &t : schema.tables())
                QCOMPARE(scalar(db, "SELECT COUNT(*) FROM " + t.name), qint64(t.rows.size()));
            int views = 0;
            for (const QVariant &v : db.tables())
                if (v.toMap().value("kind").toString() == "view") {
                    ++views;
                    QVERIFY2(scalar(db, "SELECT COUNT(*) FROM " + v.toMap().value("name").toString()) > 0,
                             qPrintable(info.id + "." + v.toMap().value("name").toString()));
                }
            QVERIFY2(views >= 1, qPrintable(info.id));

            DatabaseSession lite;
            QTemporaryDir dir;
            QVERIFY(SampleDatabase::createFile(info.id, dir.filePath("s.db")));
            QVERIFY(lite.open(dir.filePath("s.db")));
            QCOMPARE(linkCount(db), linkCount(lite));
        }
    }

    // Changing a server database through Studio: allow changes, edit rows,
    // import a CSV, apply a design — on a fresh copy of the bookshop sample.
    void changesOn(const QString &type, const QString &env, const QString &adminDb,
                   const QString &defUser, const QString &defPass)
    {
        const QString name = "studio_changes";
        {
            QSqlDatabase admin = writable(type, env, adminDb, defUser, defPass, "admin");
            QVERIFY2(admin.isOpen(), qPrintable(admin.lastError().text()));
            QSqlQuery q(admin);
            if (type == "sqlserver")
                q.exec(QString("IF DB_ID('%1') IS NOT NULL ALTER DATABASE %1 SET SINGLE_USER WITH ROLLBACK IMMEDIATE").arg(name));
            q.exec(QString("DROP DATABASE IF EXISTS %1").arg(name));
            QVERIFY2(q.exec(QString("CREATE DATABASE %1").arg(name)), qPrintable(q.lastError().text()));
        }
        QSqlDatabase::removeDatabase("admin");
        {
            QSqlDatabase w = writable(type, env, name, defUser, defPass, "load");
            QString why;
            QVERIFY2(SampleDatabase::load("bookshop", w, type, &why), qPrintable(why));
        }
        QSqlDatabase::removeDatabase("load");

        DatabaseSession db;
        QVERIFY2(db.connectTo(settings(type, env, name, defUser, defPass)), qPrintable(db.error()));
        QVERIFY2(db.allowChanges(true), qPrintable(db.error()));

        // Rows: an edit, a delete, a new one; then a failure that saves nothing.
        RowsModel m;
        m.setSession(&db);
        m.setTable("publisher");
        QVERIFY(m.editable());
        m.sortBy(0);                                            // by id: row 0 is publisher 1
        QVERIFY(m.setCell(0, columnOf(m, "name"), "Northwind Press"));
        m.toggleDelete(11);                                     // publisher 12
        const int added = m.addRow();
        QVERIFY(m.setCell(added, columnOf(m, "name"), "Quill & Ink"));
        QVERIFY2(m.save(), qPrintable(m.error()));
        QCOMPARE(scalar(db, "SELECT COUNT(*) FROM publisher"), qint64(12));
        QCOMPARE(scalar(db, "SELECT COUNT(*) FROM publisher WHERE name = 'Northwind Press'"), qint64(1));
        QVERIFY(m.setCell(1, columnOf(m, "name"), "Northwind Press"));   // name is UNIQUE
        QVERIFY(!m.save());
        m.discardChanges();

        // Undo a save: an edit, a delete and a new row on review (nothing refers to it).
        m.setTable("review");
        m.sortBy(0);
        const qint64 reviews = scalar(db, "SELECT COUNT(*) FROM review");
        const qint64 ratingSum = scalar(db, "SELECT SUM(rating) FROM review");
        QVERIFY(m.setCell(0, columnOf(m, "rating"), "1"));
        m.toggleDelete(1);
        const int fresh = m.addRow();
        QVERIFY(m.setCell(fresh, columnOf(m, "book_id"), "7"));
        QVERIFY(m.setCell(fresh, columnOf(m, "rating"), "5"));
        QVERIFY(m.setCell(fresh, columnOf(m, "created"), "2025-01-01"));
        QVERIFY2(m.save(), qPrintable(m.error()));
        QVERIFY2(m.canUndo(), qPrintable(type + ": " + m.undoNote()));    // the new row's id was read back
        QVERIFY2(m.undoSave(), qPrintable(type + ": " + m.error()));
        QCOMPARE(scalar(db, "SELECT COUNT(*) FROM review"), reviews);
        QCOMPARE(scalar(db, "SELECT SUM(rating) FROM review"), ratingSum);

        // A CSV import, all in one transaction.
        QTemporaryDir dir;
        QFile csv(dir.filePath("p.csv"));
        QVERIFY(csv.open(QIODevice::WriteOnly));
        csv.write("name,country\nBramble House,Ireland\nOakleaf,\n");
        csv.close();
        CsvImport imp;
        imp.setSession(&db);
        imp.setTable("publisher");
        QVERIFY(imp.load(csv.fileName()));
        QVERIFY2(imp.run(), qPrintable(imp.error()));
        QCOMPARE(scalar(db, "SELECT COUNT(*) FROM publisher"), qint64(14));

        // A design applied to the server itself.
        SchemaDesign d;
        d.setAutosaveEnabled(false);
        d.setSession(&db);
        d.addColumn("book", "subtitle", type == "postgres" ? "text" : type == "mysql" ? "VARCHAR(200)" : "NVARCHAR(200)");
        const QString tag = d.addTable("tag");
        d.addColumn(tag, "label", type == "postgres" ? "text" : "VARCHAR(80)");
        const QVariantMap r = d.applyToDatabase();
        QVERIFY2(r.value("ok").toBool(), qPrintable(r.value("error").toString() + "\n" + r.value("failedStatement").toString()));
        QVERIFY2(d.changes().isEmpty(), qPrintable(d.changes().join(" | ")));
        QVERIFY(db.table("tag").value("name") == "tag");
        QCOMPARE(scalar(db, "SELECT COUNT(*) FROM book"), qint64(1200));
        db.allowChanges(false);
    }

private slots:
    void postgresPagila()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_PG")) QSKIP("STUDIO_TEST_PG not set");
        DatabaseSession db;
        QVERIFY2(db.connectTo(settings("postgres", "STUDIO_TEST_PG", "pagila", "qivot", "qivot")), qPrintable(db.error()));
        QCOMPARE(db.dialect(), QString("postgres"));
        QCOMPARE(db.dialectName(), QString("PostgreSQL"));
        QVERIFY(db.isServer());
        QVERIFY(db.readOnly());
        QCOMPARE(db.displayName(), QString("pagila"));

        // Structure, including payment's foreign keys from its partitions.
        QCOMPARE(linkCount(db), 21);
        QCOMPARE(db.table("payment").value("foreignKeys").toList().size(), 3);
        QCOMPARE(db.table("rental").value("rows").toLongLong(), qint64(16044));

        // The session really is read-only, on the server.
        QSqlQuery w(QSqlDatabase::database(db.connectionName()));
        QVERIFY(!w.exec("DELETE FROM rental"));
        QVERIFY(w.lastError().text().contains("read-only", Qt::CaseInsensitive));

        RowsModel m;
        m.setSession(&db);
        m.setTable("rental");
        QCOMPARE(m.rowCount(), 16044);
        QVERIFY(m.data(m.index(16043, 0), Qt::DisplayRole).isValid());     // last page
        const int date = columnOf(m, "rental_date");
        m.sortBy(date);
        m.sortBy(date);                                                     // descending
        QVERIFY(m.data(m.index(0, date), RowsModel::RawRole).toDateTime()
                >= m.data(m.index(16043, date), RowsModel::RawRole).toDateTime());

        // Case-insensitive filter: sound, and at least everything a title match finds.
        m.setTable("film");
        m.setFilter("LOVE");
        QVERIFY(m.matchingRows() > 0);
        QVERIFY(allRowsContain(m, "love"));
        QVERIFY(m.matchingRows() >= scalar(db, "SELECT COUNT(*) FROM film WHERE title ILIKE '%love%'"));
        m.setFilter("50%");                                                 // literal, not a wildcard
        QVERIFY(allRowsContain(m, "50%"));
        QVERIFY(m.error().isEmpty());
    }

    void mysqlSakilaAndEmployees()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_MYSQL")) QSKIP("STUDIO_TEST_MYSQL not set");
        DatabaseSession db;
        QVERIFY2(db.connectTo(settings("mysql", "STUDIO_TEST_MYSQL", "sakila", "root", "qivot")), qPrintable(db.error()));
        QCOMPARE(db.dialect(), QString("mysql"));
        QVERIFY(db.readOnly());
        QCOMPARE(linkCount(db), 22);
        QSqlQuery w(QSqlDatabase::database(db.connectionName()));
        QVERIFY(!w.exec("DELETE FROM rental"));

        RowsModel m;
        m.setSession(&db);
        m.setTable("film");
        m.setFilter("love");
        QVERIFY(allRowsContain(m, "love"));
        QVERIFY(m.matchingRows() >= scalar(db, "SELECT COUNT(*) FROM film WHERE title LIKE '%love%'"));

        // A big table: 2.8 million rows, paged deep without loading it.
        QVERIFY2(db.connectTo(settings("mysql", "STUDIO_TEST_MYSQL", "employees", "root", "qivot")), qPrintable(db.error()));
        QCOMPARE(m.rowCount(), 0);                        // the old table is gone with the old database
        m.setTable("salaries");
        QCOMPARE(m.totalRows(), qint64(2844047));
        const int salary = columnOf(m, "salary");
        QVERIFY(m.data(m.index(2000000, salary), RowsModel::RawRole).toInt() > 0);
        QVERIFY(m.cachedPages() <= RowsModel::MaxPages);
        QCOMPARE(linkCount(db), 6);
    }

    void sqlServerChinook()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_MSSQL")) QSKIP("STUDIO_TEST_MSSQL not set");
        DatabaseSession db;
        QVERIFY2(db.connectTo(settings("sqlserver", "STUDIO_TEST_MSSQL", "Chinook", "sa", "Qivot_Test1")), qPrintable(db.error()));
        QCOMPARE(db.dialect(), QString("sqlserver"));
        QCOMPARE(db.dialectName(), QString("SQL Server"));
        QVERIFY(!db.readOnly());                          // no per-session switch on SQL Server
        QCOMPARE(linkCount(db), 11);

        RowsModel m;
        m.setSession(&db);
        m.setTable("Track");
        QCOMPARE(m.rowCount(), 3503);
        // OFFSET ... FETCH paging, including the short last page.
        QCOMPARE(m.data(m.index(0, 0), Qt::DisplayRole).toInt(), 1);
        QCOMPARE(m.data(m.index(3502, 0), Qt::DisplayRole).toInt(), 3503);
        const int name = columnOf(m, "Name");
        m.sortBy(name);
        QVERIFY(m.data(m.index(0, name), Qt::DisplayRole).toString().compare(
                    m.data(m.index(3502, name), Qt::DisplayRole).toString(), Qt::CaseInsensitive) <= 0);
        m.clearSort();

        m.setFilter("LOVE");
        QVERIFY(allRowsContain(m, "love"));
        QVERIFY(m.matchingRows() >= scalar(db, "SELECT COUNT(*) FROM Track WHERE Name LIKE '%love%'"));
        m.setFilter("[");                                  // '[' is a wildcard set in SQL Server's LIKE
        QVERIFY(m.error().isEmpty());
        QVERIFY(allRowsContain(m, "["));

        // A view with no key still pages (ORDER BY (SELECT NULL)).
        QVERIFY(m.error().isEmpty());
    }

    // The SQL console can't change anything on any server.
    void consoleIsReadOnlyEverywhere()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_PG") || qEnvironmentVariableIsEmpty("STUDIO_TEST_MYSQL")
            || qEnvironmentVariableIsEmpty("STUDIO_TEST_MSSQL"))
            QSKIP("needs all three STUDIO_TEST_* servers");

        DatabaseSession pg;
        QVERIFY(pg.connectTo(settings("postgres", "STUDIO_TEST_PG", "pagila", "qivot", "qivot")));
        QueryModel q;
        q.setSession(&pg);
        QVERIFY(q.run("SELECT c.name AS category, COUNT(*) AS films FROM film_category fc "
                      "JOIN category c USING (category_id) GROUP BY c.name ORDER BY films DESC"));
        QCOMPARE(q.rowCount(), 16);
        QVERIFY(!q.run("DELETE FROM rental"));
        QVERIFY(q.error().contains("read-only", Qt::CaseInsensitive));

        DatabaseSession my;
        QVERIFY(my.connectTo(settings("mysql", "STUDIO_TEST_MYSQL", "sakila", "root", "qivot")));
        q.setSession(&my);
        QVERIFY(!q.run("UPDATE film SET title = 'X'"));
        QVERIFY(q.error().contains("READ ONLY", Qt::CaseInsensitive));

        // SQL Server: the statement runs, and is rolled back.
        DatabaseSession ms;
        QVERIFY(ms.connectTo(settings("sqlserver", "STUDIO_TEST_MSSQL", "Chinook", "sa", "Qivot_Test1")));
        q.setSession(&ms);
        QVERIFY(q.run("SELECT COUNT(*) FROM Track WHERE Name = 'renamed by test'"));
        QCOMPARE(q.data(q.index(0, 0), QueryModel::RawRole).toInt(), 0);
        QVERIFY2(q.run("UPDATE Track SET Name = 'renamed by test' WHERE TrackId <= 10"), qPrintable(q.error()));
        QVERIFY(q.notice().contains("Rolled back"));
        QVERIFY(q.run("SELECT COUNT(*) FROM Track WHERE Name = 'renamed by test'"));
        QCOMPARE(q.data(q.index(0, 0), QueryModel::RawRole).toInt(), 0);     // still none: undone
    }

    // A query in the background stays read-only, and Stop has the server cancel
    // it: the worker comes back within moments, not after the 30 s it asked for.
    void stopsOnServers()
    {
        struct Case { const char *env, *type, *db, *user, *pass, *sleepSql, *writeSql; };
        const Case cases[] = {
            { "STUDIO_TEST_PG", "postgres", "pagila", "qivot", "qivot", "SELECT pg_sleep(30)", "DELETE FROM rental" },
            { "STUDIO_TEST_MYSQL", "mysql", "sakila", "root", "qivot", "SELECT SLEEP(30)", "UPDATE film SET title = 'X'" },
            { "STUDIO_TEST_MSSQL", "sqlserver", "Chinook", "sa", "Qivot_Test1", "WAITFOR DELAY '00:00:30'", nullptr },
        };
        bool ran = false;
        for (const Case &c : cases) {
            if (qEnvironmentVariableIsEmpty(c.env))
                continue;
            ran = true;
            DatabaseSession db;
            QVERIFY2(db.connectTo(settings(c.type, c.env, c.db, c.user, c.pass)), qPrintable(db.error()));
            QueryModel q;
            q.setSession(&db);
            QSignalSpy done(&q, &QueryModel::finished);
            if (c.writeSql) {
                q.start(c.writeSql);
                QVERIFY2(done.wait(20000), c.type);
                QVERIFY2(q.error().contains("read", Qt::CaseInsensitive), qPrintable(QString(c.type) + ": " + q.error()));
            }
            q.start(c.sleepSql);
            QTest::qWait(1500);                // long enough to be under way on the server
            QVERIFY2(q.running(), c.type);
            QElapsedTimer t;
            t.start();
            q.cancel();
            QVERIFY(q.cancelled());
            QVERIFY2(q.waitForBackground(10000), c.type);
            QVERIFY2(t.elapsed() < 10000, qPrintable(QString("%1 took %2 ms to stop").arg(c.type).arg(t.elapsed())));
            // And the session is fine afterwards.
            QVERIFY2(q.run("SELECT 1"), qPrintable(QString(c.type) + ": " + q.error()));
        }
        if (!ran) QSKIP("no STUDIO_TEST_* server set");
    }

    // The TLS settings reach each driver: shown by what a server without TLS
    // (or with a self-signed certificate) makes of them.
    void sslOptions()
    {
        bool ran = false;
        if (!qEnvironmentVariableIsEmpty("STUDIO_TEST_PG")) {
            ran = true;
            DatabaseSession db;
            QVariantMap s = settings("postgres", "STUDIO_TEST_PG", "pagila", "qivot", "qivot");
            s["ssl"] = QVariantMap{ { "mode", "disable" } };
            QVERIFY2(db.connectTo(s), qPrintable(db.error()));
            QCOMPARE(db.connectionSettings().value("ssl").toMap().value("mode").toString(), QString("disable"));
            s["ssl"] = QVariantMap{ { "mode", "require" } };            // the test server has no TLS
            QVERIFY(!db.connectTo(s));
            QVERIFY2(db.error().contains("SSL", Qt::CaseInsensitive), qPrintable(db.error()));
        }
        if (!qEnvironmentVariableIsEmpty("STUDIO_TEST_MYSQL")) {
            ran = true;
            DatabaseSession db;
            QVariantMap s = settings("mysql", "STUDIO_TEST_MYSQL", "sakila", "root", "qivot");
            s["ssl"] = QVariantMap{ { "mode", "on" }, { "ca", "/no/such/ca.pem" } };
            QVERIFY(!db.connectTo(s));                                  // asked to check with a CA it can't read
            s["ssl"] = QVariantMap{ { "mode", "off" }, { "ca", "/no/such/ca.pem" } };
            QVERIFY2(db.connectTo(s), qPrintable(db.error()));          // off: the file isn't used
        }
        if (!qEnvironmentVariableIsEmpty("STUDIO_TEST_MSSQL")) {
            ran = true;
            DatabaseSession db;
            QVariantMap s = settings("sqlserver", "STUDIO_TEST_MSSQL", "Chinook", "sa", "Qivot_Test1");
            s["ssl"] = QVariantMap{ { "mode", "off" } };
            QVERIFY2(db.connectTo(s), qPrintable(db.error()));
            s["ssl"] = QVariantMap{ { "mode", "strict" } };            // a self-signed certificate: refused
            QVERIFY(!db.connectTo(s));
            s["ssl"] = QVariantMap{ { "mode", "on" }, { "trust", false } };
            QVERIFY(!db.connectTo(s));
            s["ssl"] = QVariantMap{ { "mode", "on" }, { "trust", true } };
            QVERIFY2(db.connectTo(s), qPrintable(db.error()));
        }
        if (!ran) QSKIP("no STUDIO_TEST_* server set");
    }

    // Through SSH: STUDIO_TEST_SSH is the SSH server (host:port), which can
    // reach STUDIO_TEST_PG; STUDIO_TEST_SSH_KEY its key (user STUDIO_TEST_SSH_USER, root).
    void throughSsh()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_SSH") || qEnvironmentVariableIsEmpty("STUDIO_TEST_PG"))
            QSKIP("STUDIO_TEST_SSH and STUDIO_TEST_PG not set");
        if (SshTunnel::sshProgram().isEmpty())
            QSKIP("no ssh here");
        const QString sshWhere = qEnvironmentVariable("STUDIO_TEST_SSH");
        QVariantMap s = settings("postgres", "STUDIO_TEST_PG", "pagila", "qivot", "qivot");
        s["ssh"] = QVariantMap{ { "host", sshWhere.section(':', 0, 0) }, { "port", sshWhere.section(':', 1, 1).toInt() },
                                { "user", qEnvironmentVariable("STUDIO_TEST_SSH_USER", "root") },
                                { "key", qEnvironmentVariable("STUDIO_TEST_SSH_KEY") } };
        {
            DatabaseSession db;
            QVERIFY2(db.connectTo(s), qPrintable(db.error()));
            QVERIFY(db.location().contains(" via "));
            QCOMPARE(db.connectionSettings().value("ssh").toMap().value("host").toString(), sshWhere.section(':', 0, 0));
            QVERIFY(scalar(db, "SELECT COUNT(*) FROM film") == 1000);
            // A query in the background, and a writing connection, go the same way.
            QueryModel q;
            q.setSession(&db);
            QSignalSpy done(&q, &QueryModel::finished);
            q.start("SELECT COUNT(*) FROM actor");
            QVERIFY(done.wait(20000));
            QVERIFY2(q.error().isEmpty(), qPrintable(q.error()));
            QCOMPARE(q.data(q.index(0, 0), QueryModel::RawRole).toInt(), 200);
            QVERIFY2(db.allowChanges(true), qPrintable(db.error()));
            QSqlQuery w(db.writeDatabase());
            QVERIFY2(w.exec("SELECT 1"), qPrintable(w.lastError().text()));
            w = QSqlQuery();
            db.close();
        }
        // A key the server doesn't know: told why.
        QTemporaryDir dir;
        const QString badKey = dir.filePath("bad");
        QVERIFY(QProcess::execute("ssh-keygen", { "-q", "-t", "ed25519", "-N", "", "-f", badKey }) == 0);
        QVariantMap ssh = s.value("ssh").toMap();
        ssh["key"] = badKey;
        s["ssh"] = ssh;
        DatabaseSession db;
        QVERIFY(!db.connectTo(s));
        QVERIFY2(db.error().contains("SSH tunnel") && db.error().contains("Permission denied"), qPrintable(db.error()));
    }

    void friendlyErrors()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_PG")) QSKIP("STUDIO_TEST_PG not set");
        DatabaseSession db;
        QVariantMap s = settings("postgres", "STUDIO_TEST_PG", "pagila", "qivot", "wrong-password");
        QVERIFY(!db.connectTo(s));
        QVERIFY(db.error().startsWith("Couldn't connect to PostgreSQL"));
        QVERIFY(!db.isOpen());
        s["password"] = qEnvironmentVariable("STUDIO_TEST_PG_PASS", "qivot");
        s["database"] = "no_such_database";
        QVERIFY(!db.connectTo(s));
        QVERIFY(db.error().contains("no_such_database"));
        s["database"] = "";
        QVERIFY(!db.connectTo(s));
        QCOMPARE(db.error(), QString("Enter the name of the database to open."));
        QVERIFY(!db.connectTo({ { "type", "oracle" } }));
        QVERIFY(db.error().contains("Unknown database type"));
    }
    // Plans from the real servers, read into the same tree as SQLite's.
    void plansOnServers()
    {
        struct Case { const char *env, *type, *db, *user, *pass, *scanSql, *indexSql; };
        const Case cases[] = {
            { "STUDIO_TEST_PG", "postgres", "pagila", "qivot", "qivot",
              "SELECT * FROM film WHERE length > 100",
              "SELECT f.title, l.name FROM film f JOIN language l ON l.language_id = f.language_id WHERE f.film_id = 7" },
            { "STUDIO_TEST_MYSQL", "mysql", "sakila", "root", "qivot",
              "SELECT * FROM film WHERE length > 100",
              "SELECT rental_id FROM rental WHERE customer_id = 7" },        // idx_fk_customer_id
            { "STUDIO_TEST_MSSQL", "sqlserver", "Chinook", "sa", "Qivot_Test1",
              "SELECT * FROM Track WHERE Milliseconds > 300000",
              "SELECT Name FROM Track WHERE AlbumId = 7" } };                 // IFK_TrackAlbumId
        int ran = 0;
        for (const Case &c : cases) {
            if (qEnvironmentVariableIsEmpty(c.env))
                continue;
            DatabaseSession db;
            QVERIFY2(db.connectTo(settings(c.type, c.env, c.db, c.user, c.pass)), qPrintable(db.error()));
            QueryPlan p;
            p.setSession(&db);
            QVERIFY2(p.explain(c.scanSql), qPrintable(QString(c.type) + ": " + p.error() + " | raw "
                                                      + QString::number(p.raw().size()) + ": " + p.raw().left(400)));
            QVERIFY2(p.scans() >= 1, qPrintable(QString(c.type) + " scan: " + p.raw().left(600)));
            QVERIFY2(p.explain(c.indexSql), qPrintable(QString(c.type) + ": " + p.error()));
            QVERIFY2(!p.nodes().isEmpty(), qPrintable(QString(c.type) + ": " + p.raw().left(600)));
            QVERIFY2(p.scans() == 0, qPrintable(QString(c.type) + " index: " + p.raw().left(600)));
            // The session is still fine for an ordinary query afterwards (SQL Server's SHOWPLAN is off again).
            QueryModel q;
            q.setSession(&db);
            QVERIFY2(q.run(c.indexSql), qPrintable(q.error()));
            QVERIFY(q.resultRows() > 0);
            // Autocomplete knows the server's tables.
            SqlCompleter comp;
            comp.setSession(&db);
            const QString typed = QString("SELECT * FROM ") + QString(c.type == QString("sqlserver") ? "Tra" : "fil");
            const QVariantList items = comp.complete(typed, typed.size(), false).value("items").toList();
            QVERIFY2(!items.isEmpty(), c.type);
            ++ran;
        }
        if (!ran) QSKIP("no STUDIO_TEST_* server set");
    }
    void changesOnPostgres()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_PG")) QSKIP("STUDIO_TEST_PG not set");
        changesOn("postgres", "STUDIO_TEST_PG", "postgres", "qivot", "qivot");
    }
    void changesOnMysql()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_MYSQL")) QSKIP("STUDIO_TEST_MYSQL not set");
        changesOn("mysql", "STUDIO_TEST_MYSQL", "mysql", "root", "qivot");
    }
    void changesOnSqlServer()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_MSSQL")) QSKIP("STUDIO_TEST_MSSQL not set");
        changesOn("sqlserver", "STUDIO_TEST_MSSQL", "master", "sa", "Qivot_Test1");
    }
    void samplesOnPostgres()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_PG")) QSKIP("STUDIO_TEST_PG not set");
        samplesLoad("postgres", "STUDIO_TEST_PG", "postgres", "qivot", "qivot");
    }
    void samplesOnMysql()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_MYSQL")) QSKIP("STUDIO_TEST_MYSQL not set");
        samplesLoad("mysql", "STUDIO_TEST_MYSQL", "mysql", "root", "qivot");
    }
    void samplesOnSqlServer()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_MSSQL")) QSKIP("STUDIO_TEST_MSSQL not set");
        samplesLoad("sqlserver", "STUDIO_TEST_MSSQL", "master", "sa", "Qivot_Test1");
    }
    void migrationOnPostgres()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_PG")) QSKIP("STUDIO_TEST_PG not set");
        migrationRoundTrip("postgres", "STUDIO_TEST_PG", "postgres", "qivot", "qivot");
    }
    void migrationOnMysql()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_MYSQL")) QSKIP("STUDIO_TEST_MYSQL not set");
        migrationRoundTrip("mysql", "STUDIO_TEST_MYSQL", "mysql", "root", "qivot");
    }
    void migrationOnSqlServer()
    {
        if (qEnvironmentVariableIsEmpty("STUDIO_TEST_MSSQL")) QSKIP("STUDIO_TEST_MSSQL not set");
        migrationRoundTrip("sqlserver", "STUDIO_TEST_MSSQL", "master", "sa", "Qivot_Test1");
    }
    void builderOnServers()
    {
        int ran = 0;
        if (!qEnvironmentVariableIsEmpty("STUDIO_TEST_PG")) {
            builderRuns(settings("postgres", "STUDIO_TEST_PG", "pagila", "qivot", "qivot"), "film", "language", "film.language_id", "name", "film_id");
            ++ran;
        }
        if (!qEnvironmentVariableIsEmpty("STUDIO_TEST_MYSQL")) {
            builderRuns(settings("mysql", "STUDIO_TEST_MYSQL", "sakila", "root", "qivot"), "film", "language", "film.language_id", "name", "film_id");
            ++ran;
        }
        if (!qEnvironmentVariableIsEmpty("STUDIO_TEST_MSSQL")) {
            builderRuns(settings("sqlserver", "STUDIO_TEST_MSSQL", "Chinook", "sa", "Qivot_Test1"), "Track", "Album", "AlbumId", "Title", "TrackId");
            ++ran;
        }
        if (!ran) QSKIP("no server configured");
    }
    // Column profiles on each server: every column profiled, none failed,
    // numbers with a spread get a full histogram (FLOOR and TOP per dialect).
    void profileOnServers()
    {
        struct Case { const char *env, *type, *db, *user, *pass, *table; };
        const Case cases[] = { { "STUDIO_TEST_PG", "postgres", "pagila", "qivot", "qivot", "film" },
                               { "STUDIO_TEST_MYSQL", "mysql", "sakila", "root", "qivot", "payment" },
                               { "STUDIO_TEST_MSSQL", "sqlserver", "Chinook", "sa", "Qivot_Test1", "Track" } };
        int ran = 0;
        for (const Case &c : cases) {
            if (qEnvironmentVariableIsEmpty(c.env))
                continue;
            DatabaseSession db;
            QVERIFY2(db.connectTo(settings(c.type, c.env, c.db, c.user, c.pass)), qPrintable(db.error()));
            TableProfile p;
            p.setSession(&db);
            p.setTable(c.table);
            QSignalSpy finished(&p, &TableProfile::finished);
            QVERIFY(finished.wait(60000));
            QCOMPARE(p.done(), p.columns().size());
            int histograms = 0;
            for (const QVariant &v : p.columns()) {
                const QVariantMap m = v.toMap();
                QVERIFY2(m.value("error").toString().isEmpty(),
                         qPrintable(QString("%1.%2: %3").arg(c.table, m.value("name").toString(), m.value("error").toString())));
                if (!m.value("histogram").toList().isEmpty()) {
                    qint64 sum = 0;
                    for (const QVariant &b : m.value("histogram").toList())
                        sum += b.toMap().value("count").toLongLong();
                    QCOMPARE(sum, m.value("rows").toLongLong() - m.value("nulls").toLongLong());
                    ++histograms;
                }
            }
            QVERIFY2(histograms > 0, c.table);
            ++ran;
        }
        if (!ran) QSKIP("no server configured");
    }
};

QTEST_GUILESS_MAIN(TestServers)
#include "tst_servers.moc"
