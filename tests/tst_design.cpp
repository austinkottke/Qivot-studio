#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "databasesession.h"
#include "sampledatabase.h"
#include "schemadesign.h"
#include "sqlscript.h"

/// The class designer's model: edits, undo, the generated C++, and the
/// migration — which is applied to a copy of the sample and read back.
class TestDesign : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir   m_dir;
    QString         m_sample;
    DatabaseSession m_db;

    static QVector<QiTableInfo> read(const QString &path)
    {
        QVector<QiTableInfo> tables;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("readback"));
            db.setDatabaseName(path);
            if (db.open())
                tables = QiSchema(db).tables();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("readback"));
        return tables;
    }
    static qint64 count(const QString &path, const QString &table)
    {
        qint64 n = -1;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("count"));
            db.setDatabaseName(path);
            QSqlQuery q(db);
            if (db.open() && q.exec(QStringLiteral("SELECT COUNT(*) FROM ") + table) && q.next())
                n = q.value(0).toLongLong();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("count"));
        return n;
    }
    static int columnIndex(const SchemaDesign &d, const QString &table, const QString &column)
    {
        const QVariantList cols = d.table(table).value("columns").toList();
        for (int i = 0; i < cols.size(); ++i)
            if (cols[i].toMap().value("name").toString() == column)
                return i;
        return -1;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);       // autosaves go to a test folder
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));   // the wording checked below: "1,200 rows"
        QVERIFY(m_dir.isValid());
        m_sample = m_dir.filePath("bookshop.db");
        QVERIFY(SampleDatabase::create(m_sample));
        QVERIFY(m_db.open(m_sample));
    }
    // Each test starts without a design left over from the one before.
    void init()
    {
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/designs").removeRecursively();
    }

    void startsAsTheDatabase()
    {
        SchemaDesign d;
        d.setSession(&m_db);
        QCOMPARE(d.dialect(), QString("sqlite"));
        QCOMPARE(d.tables().size(), 7);                 // tables only, no views
        QVERIFY(d.migration().isEmpty());
        QVERIFY(d.changes().isEmpty());
        QVERIFY(!d.canUndo());
        const QVariantMap book = d.table("book");
        QCOMPARE(book.value("isNew").toBool(), false);
        QVERIFY(book.value("referencedBy").toStringList().contains("review"));
    }

    void editsAndUndo()
    {
        SchemaDesign d;
        d.setSession(&m_db);
        QSignalSpy changed(&d, &SchemaDesign::designChanged);
        const QString name = d.addColumn("book", "subtitle", "TEXT");
        QCOMPARE(name, QString("subtitle"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(d.changes(), QStringList({ "Add column book.subtitle" }));
        QVERIFY(d.migration().contains("ALTER TABLE book ADD COLUMN subtitle TEXT;"));
        QVERIFY(d.cppModel("book").value("code").toString().contains("subtitle"));

        QVERIFY(d.renameTable("book", "books"));
        QCOMPARE(d.table("review").value("columns").toList().isEmpty(), false);
        // References follow the rename.
        bool follows = false;
        for (const QVariant &c : d.table("review").value("columns").toList())
            follows = follows || c.toMap().value("reference").toString() == "books";
        QVERIFY(follows);
        QVERIFY(!d.renameTable("books", "author"));      // taken
        QVERIFY(!d.error().isEmpty());

        d.undo();
        QVERIFY(d.table("book").value("name").toString() == "book");
        d.undo();
        QVERIFY(d.migration().isEmpty());
        QVERIFY(d.canRedo());
        d.redo();
        QCOMPARE(d.changes(), QStringList({ "Add column book.subtitle" }));
    }

    void newTableAndReferences()
    {
        SchemaDesign d;
        d.setSession(&m_db);
        const QString tag = d.addTable("tag");
        QCOMPARE(tag, QString("tag"));
        QCOMPARE(d.addTable("tag"), QString("tag2"));   // names stay unique
        d.removeTable("tag2");
        d.addColumn("tag", "label", "TEXT");
        QVERIFY(d.updateColumn("tag", 1, { { "unique", true }, { "nullable", false } }));
        QVERIFY(d.setReference("book", "publisher_id", ""));          // drop a reference
        const QString sql = d.migration();
        QVERIFY2(sql.contains("CREATE TABLE tag (\n    id INTEGER PRIMARY KEY AUTOINCREMENT,\n    label TEXT NOT NULL UNIQUE\n);"),
                 qPrintable(sql));
        QVERIFY2(sql.contains("rebuild book"), qPrintable(sql));     // SQLite drops a foreign key by rebuilding
        QVERIFY(d.changes().contains("Remove reference book.publisher_id → publisher"));
        // The new class is in the C++ too.
        QVERIFY(d.cppHeader().contains("class Tag : public QiModel"));
    }

    // The real test: apply a migration to a copy, read it back, and find
    // nothing left to change.
    void migrationRoundTrip()
    {
        SchemaDesign d;
        d.setSession(&m_db);
        d.addColumn("book", "subtitle", "TEXT");                                       // add
        QVERIFY(d.updateColumn("author", columnIndex(d, "author", "country"), { { "name", "nationality" } }));  // rename
        QVERIFY(d.updateColumn("review", columnIndex(d, "review", "body"), { { "type", "VARCHAR(500)" } }));    // retype
        d.removeColumn("publisher", columnIndex(d, "publisher", "country"));           // drop
        QVERIFY(d.renameTable("orders", "purchase"));                                  // rename a referenced table
        QVERIFY(d.updateColumn("publisher", columnIndex(d, "publisher", "name"), { { "unique", false } }));
        QVERIFY(d.setReference("review", "customer_id", ""));                          // drop a reference
        d.addTable("tag");
        d.addColumn("tag", "label", "TEXT");
        const QString link = d.addTable("book_tag");                                   // many-to-many
        d.addColumn(link, "book_id", "INTEGER");
        d.addColumn(link, "tag_id", "INTEGER");
        d.removeColumn(link, 0);                                                       // no id
        QVERIFY(d.updateColumn(link, 0, { { "primaryKey", true } }));
        QVERIFY(d.updateColumn(link, 1, { { "primaryKey", true } }));
        QVERIFY(d.setReference(link, "book_id", "book", "CASCADE"));
        QVERIFY(d.setReference(link, "tag_id", "tag", "CASCADE"));

        const QString copy = d.applyToCopy();
        QVERIFY2(!copy.isEmpty(), qPrintable(d.error() + "\n\n" + d.migration()));
        QVERIFY(copy != m_sample);

        // The rows came through.
        QCOMPARE(count(copy, "purchase"), count(m_sample, "orders"));
        QCOMPARE(count(copy, "order_item"), count(m_sample, "order_item"));
        QCOMPARE(count(copy, "review"), count(m_sample, "review"));

        // Read back, the copy is the design: no changes left.
        SchemaDesign readBack;
        readBack.reset(read(copy), "sqlite");
        QVector<DesignTable> mine = d.designTables();
        for (DesignTable &t : mine) {
            t.origin = t.info.name;
            t.columnOrigins.clear();
            for (const QiColumnInfo &c : t.info.columns)
                t.columnOrigins << c.name;
        }
        const QStringList left = Migration::changes(readBack.infos(), mine);
        QVERIFY2(left.isEmpty(), qPrintable(left.join('\n') + "\n\n" + d.migration()));

        // And the original is untouched.
        QCOMPARE(read(m_sample).size(), 7 + 0);
        QFile::remove(copy);
    }

    // A migration the data can't take (duplicate names made unique) fails
    // cleanly: the reason is reported and no half-migrated copy is left.
    void failsCleanly()
    {
        SchemaDesign d;
        d.setSession(&m_db);
        QVERIFY(d.updateColumn("customer", columnIndex(d, "customer", "name"), { { "unique", true } }));
        const int before = QDir(m_dir.path()).entryList(QDir::Files).size();
        QVERIFY(d.applyToCopy().isEmpty());
        QVERIFY2(d.error().contains("UNIQUE"), qPrintable(d.error()));
        QCOMPARE(QDir(m_dir.path()).entryList(QDir::Files).size(), before);
    }

    static QStringList messages(const SchemaDesign &d, const QString &severity)
    {
        QStringList out;
        for (const QVariant &v : d.problems()) {
            const QVariantMap p = v.toMap();
            if (p.value("severity").toString() == severity)
                out << p.value("table").toString() + "." + p.value("column").toString() + ": " + p.value("message").toString();
        }
        return out;
    }

    // The checks against the data: what the migration would run into.
    void problems()
    {
        SchemaDesign d;
        d.setSession(&m_db);
        QCOMPARE(d.errorCount(), 0);

        // A lookup table, and a required reference to it on a table with rows.
        const QString category = d.addTable("Category");
        d.addColumn("book", "category_id", "INTEGER");
        const int col = columnIndex(d, "book", "category_id");
        QVERIFY(d.setReference("book", "category_id", category));
        QVERIFY(d.updateColumn("book", col, { { "nullable", false } }));
        QStringList errors = messages(d, "error");
        QVERIFY2(errors.size() == 1 && errors.first().startsWith("book.category_id: Required, with no default, but book already has 1,200 rows"),
                 qPrintable(errors.join('\n')));
        QCOMPARE(d.problemsFor("book").size(), 1);
        // A default only helps if it's a row of the referenced table: the new
        // Category has none.
        QVERIFY(d.updateColumn("book", col, { { "defaultValue", "0" } }));
        errors = messages(d, "error");
        QVERIFY2(errors.size() == 1 && errors.first().startsWith("book.category_id: Defaults to 0, but Category has no row 0 "
                                                                 "(it's a new, empty table): all 1,200 rows would point at nothing"),
                 qPrintable(errors.join('\n')));
        QCOMPARE(d.problemsFor("book").first().toMap().value("fix").toString(), QString("allowEmpty"));
        // The diagram's card carries the message for its ERROR tag.
        bool tagged = false;
        for (const QVariant &t : d.diagram().value("tables").toList())
            tagged = tagged || (t.toMap().value("name") == "book" && t.toMap().value("status") == "error"
                                && t.toMap().value("errors").toStringList().first().startsWith("category_id: Defaults to 0"));
        QVERIFY(tagged);
        // Against an existing table it's checked in the data: publisher 1 exists, 999 doesn't.
        const QString pub = d.addColumn("book", "imprint_id", "INTEGER");
        const int pubCol = columnIndex(d, "book", pub);
        QVERIFY(d.setReference("book", pub, "publisher"));
        QVERIFY(d.updateColumn("book", pubCol, { { "nullable", false }, { "defaultValue", "1" } }));
        QVERIFY2(!messages(d, "error").join('\n').contains("book.imprint_id"), qPrintable(messages(d, "error").join('\n')));
        QVERIFY(d.updateColumn("book", pubCol, { { "defaultValue", "999" } }));
        QVERIFY(messages(d, "error").join('\n').contains("book.imprint_id: Defaults to 999, but publisher has no row 999: all 1,200 rows"));
        d.removeColumn("book", pubCol);
        // Letting it be empty (the fix) clears it.
        QVERIFY(d.updateColumn("book", col, { { "nullable", true }, { "defaultValue", "" } }));
        QCOMPARE(d.errorCount(), 0);

        // The reference's type has to hold the key.
        QVERIFY(d.updateColumn("book", col, { { "type", "TEXT" } }));
        QVERIFY2(messages(d, "error").join('\n').contains("book.category_id: Is TEXT but references Category.id"),
                 qPrintable(messages(d, "error").join('\n')));
        d.undo();
        QCOMPARE(d.errorCount(), 0);

        // NOT NULL on a column with NULLs; UNIQUE on one with repeats.
        QVERIFY(d.updateColumn("customer", columnIndex(d, "customer", "city"), { { "nullable", false } }));
        QVERIFY(d.updateColumn("customer", columnIndex(d, "customer", "name"), { { "unique", true } }));
        errors = messages(d, "error");
        QVERIFY2(errors.join('\n').contains(QRegularExpression("customer\\.city: Required, but [0-9,]+ rows have no value")),
                 qPrintable(errors.join('\n')));
        QVERIFY2(errors.join('\n').contains(QRegularExpression("customer\\.name: Unique, but [0-9,]+ rows repeat")),
                 qPrintable(errors.join('\n')));

        // A reference with values that point nowhere.
        QVERIFY(d.setReference("review", "book_id", "publisher"));
        QVERIFY2(messages(d, "error").join('\n').contains(QRegularExpression("review\\.book_id: [0-9,]+ rows point at no publisher row")),
                 qPrintable(messages(d, "error").join('\n')));

        // Dropping things with data in them is allowed, but said out loud.
        d.removeTable("publisher");
        QVERIFY(messages(d, "warning").join('\n').contains("publisher.: Dropped, with its 12 rows."));
    }

    // Drawing a relationship: a new column that references a table's key.
    void addReference()
    {
        SchemaDesign d;
        d.setAutosaveEnabled(false);
        d.setSession(&m_db);
        const QString category = d.addTable("category");
        QCOMPARE(d.addReference("book", category), QString("category_id"));
        QVariantMap col = d.table("book").value("columns").toList().last().toMap();
        QCOMPARE(col.value("reference").toString(), QString("category"));
        QCOMPARE(col.value("type").toString(), QString("INTEGER"));       // the key's type
        QVERIFY(d.cppModel("book").value("code").toString().contains(QRegularExpression("QiForeignKey<Category>\\s+category_id;")));
        d.undo();                                                         // one step undoes both
        QVERIFY(d.table("book").value("columns").toList().last().toMap().value("name").toString() != "category_id");
        d.redo();
        // A key already named after its table keeps its name; a second one is numbered.
        QVERIFY(d.updateColumn(category, 0, { { "name", "category_id" } }));
        QCOMPARE(d.addReference("review", category), QString("category_id"));
        QCOMPARE(d.addReference("review", category), QString("category_id2"));
        // On delete.
        QVERIFY(d.setOnDelete("book", "category_id", "CASCADE"));
        col = d.table("book").value("columns").toList().last().toMap();
        QCOMPARE(col.value("onDelete").toString(), QString("CASCADE"));
        QVERIFY(d.migration().contains("ON DELETE CASCADE"));
        QVERIFY(!d.setOnDelete("book", "title", "CASCADE"));             // not a reference
        // A table with a two-column key can't be referenced this way.
        QVERIFY(d.addReference("book", "order_item").isEmpty());
        QVERIFY(!d.error().isEmpty());
    }

    // Scripts split at the semicolons that end statements, and nowhere else.
    void splitsScripts()
    {
        const QStringList s = SqlScript::split(
            "-- a comment; with a semicolon\n"
            "CREATE TABLE t (a TEXT DEFAULT 'x;y', b TEXT);\n"
            "INSERT INTO t VALUES ('it''s; fine', \"q;q\");/* c; c */\n"
            "SELECT [odd;name] FROM t\nGO\n"
            "SELECT 1;;\n");
        QCOMPARE(s.size(), 4);
        QCOMPARE(s.at(0), QString("CREATE TABLE t (a TEXT DEFAULT 'x;y', b TEXT)"));
        QCOMPARE(s.at(1), QString("INSERT INTO t VALUES ('it''s; fine', \"q;q\")"));
        QCOMPARE(s.at(2), QString("SELECT [odd;name] FROM t"));
        QCOMPARE(s.at(3), QString("SELECT 1"));
        QVERIFY(SqlScript::split("SELECT 'GO'\nGOTO").size() == 1);   // not a batch separator
    }

    // Applying to the database itself: only once changes are allowed, with a
    // backup, and the design starts again from the result.
    void appliesToTheDatabase()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("shop.db");
        QVERIFY(QFile::copy(m_sample, file));
        DatabaseSession db;
        QVERIFY(db.open(file));
        SchemaDesign d;
        d.setAutosaveEnabled(false);
        d.setSession(&db);
        d.addColumn("book", "subtitle", "TEXT");
        const int col = columnIndex(d, "book", "price");
        QVERIFY(d.updateColumn("book", col, { { "defaultValue", "12.5" } }));     // a rebuild (SQLite)

        QVariantMap r = d.applyToDatabase();
        QVERIFY(!r.value("ok").toBool());                                         // not allowed yet
        QVERIFY(r.value("error").toString().contains("Allow changes"));
        QVERIFY(!db.changesAllowed());

        QVERIFY2(db.allowChanges(true), qPrintable(db.error()));
        r = d.applyToDatabase();
        QVERIFY2(r.value("ok").toBool(), qPrintable(r.value("error").toString() + "\n" + r.value("failedStatement").toString()));
        QVERIFY(QFileInfo::exists(r.value("backup").toString()));                 // the copy beside it
        QVERIFY(r.value("backup").toString().contains("shop.backup-"));
        QVERIFY(d.changes().isEmpty());                                           // the design is the database now
        QVERIFY(db.table("book").value("columns").toList().last().toMap().value("name") == "subtitle");
        // The rows survived the rebuild, and the reading connection is still read-only.
        QCOMPARE(db.table("book").value("rows").toLongLong(), qint64(1200));
        QSqlQuery q(QSqlDatabase::database(db.connectionName()));
        QVERIFY(!q.exec("DELETE FROM review"));

        // A failure changes nothing.
        d.addColumn("review", "score", "INTEGER");
        QVERIFY(d.updateColumn("review", columnIndex(d, "review", "score"), { { "nullable", false } }));
        QVERIFY(d.updateColumn("review", columnIndex(d, "review", "score"), { { "defaultValue", "1" } }));
        QVERIFY(d.addTable("tag").length());
        QSqlQuery w(db.writeDatabase());
        QVERIFY(w.exec("CREATE TABLE tag (x INTEGER)"));                          // the design's new table is taken
        r = d.applyToDatabase();
        QVERIFY(!r.value("ok").toBool());
        QVERIFY2(r.value("error").toString().contains("nothing was changed"), qPrintable(r.value("error").toString()));
        db.refresh();
        bool hasScore = false;
        for (const QVariant &c : db.table("review").value("columns").toList())
            hasScore = hasScore || c.toMap().value("name") == "score";
        QVERIFY(!hasScore);
        db.allowChanges(false);
        QVERIFY(!db.writeDatabase().isValid());
    }

    void saveAndOpen()
    {
        const QString file = m_dir.filePath("shop.qivotdesign");
        {
            SchemaDesign d;
            d.setSession(&m_db);
            d.addTable("tag");
            d.addColumn("tag", "label", "TEXT");
            QVERIFY(d.renameTable("orders", "purchase"));
            QVERIFY(d.updateColumn("author", columnIndex(d, "author", "country"), { { "name", "nationality" } }));
            QVERIFY(d.save(file));
            QCOMPARE(d.filePath(), file);
        }
        SchemaDesign d;
        d.setAutosaveEnabled(false);       // (the design above is autosaved too; this checks the file)
        d.setSession(&m_db);
        QVERIFY(d.changes().isEmpty());
        QVERIFY(d.load(QUrl::fromLocalFile(file)));
        QVERIFY2(d.error().isEmpty(), qPrintable(d.error()));
        QCOMPARE(d.changes(), QStringList({ "Create table tag", "Rename column author.country → nationality",
                                            "Rename table orders → purchase" }));
        // A rename is still a rename after the round trip.
        QVERIFY(d.migration().contains("ALTER TABLE author RENAME COLUMN country TO nationality;"));
        QVERIFY(!d.canUndo());

        // Not a design.
        QFile junk(m_dir.filePath("junk.qivotdesign"));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("{\"hello\": 1}");
        junk.close();
        QVERIFY(!d.load(junk.fileName()));
        QVERIFY(d.error().contains("isn't a Qivot Studio design"));
    }

    // Edits survive closing: the next time the database opens, they're back.
    void autosaveRestores()
    {
        {
            SchemaDesign d;
            d.setSession(&m_db);
            QVERIFY(!d.restored());
            d.addColumn("book", "subtitle", "TEXT");
            QVERIFY(QFileInfo::exists(d.autosavePath()));
        }
        {
            SchemaDesign d;
            d.setSession(&m_db);
            QVERIFY(d.restored());
            QCOMPARE(d.changes(), QStringList({ "Add column book.subtitle" }));
            // Start over forgets it.
            d.reset();
            QVERIFY(d.changes().isEmpty());
            QVERIFY(!QFileInfo::exists(d.autosavePath()));
        }
        SchemaDesign d;
        d.setSession(&m_db);
        QVERIFY(!d.restored());
        QVERIFY(d.changes().isEmpty());

        // Off (demos), nothing is kept.
        d.setAutosaveEnabled(false);
        d.addColumn("book", "subtitle", "TEXT");
        QVERIFY(!QFileInfo::exists(d.autosavePath()));
    }

    // An autosave made against a database that has changed since isn't restored.
    void autosaveSkipsAChangedDatabase()
    {
        const QString copy = m_dir.filePath("changing.db");
        QFile::remove(copy);
        QVERIFY(QFile::copy(m_sample, copy));
        {
            DatabaseSession db;
            QVERIFY(db.open(copy));
            SchemaDesign d;
            d.setSession(&db);
            d.addColumn("book", "subtitle", "TEXT");
        }
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase("QSQLITE", "alter");
            raw.setDatabaseName(copy);
            QVERIFY(raw.open());
            QSqlQuery q(raw);
            QVERIFY(q.exec("ALTER TABLE author ADD COLUMN website TEXT"));
        }
        QSqlDatabase::removeDatabase("alter");
        DatabaseSession db;
        QVERIFY(db.open(copy));
        SchemaDesign d;
        d.setSession(&db);
        QVERIFY(!d.restored());
        QVERIFY(d.changes().isEmpty());
    }

    void otherDialects()
    {
        SchemaDesign d;
        d.setSession(&m_db);
        QVector<QiTableInfo> tables = m_db.tableInfos();
        d.reset(tables, "postgres");
        QVERIFY(d.updateColumn("review", columnIndex(d, "review", "body"), { { "type", "varchar(500)" }, { "nullable", false } }));
        QVERIFY(d.renameTable("orders", "purchase"));
        QString sql = d.migration();
        QVERIFY2(sql.startsWith("BEGIN;"), qPrintable(sql));
        QVERIFY2(sql.contains("ALTER TABLE orders RENAME TO purchase;"), qPrintable(sql));
        QVERIFY2(sql.contains("ALTER TABLE review ALTER COLUMN body TYPE varchar(500) USING body::varchar(500);"), qPrintable(sql));
        QVERIFY2(sql.contains("ALTER TABLE review ALTER COLUMN body SET NOT NULL;"), qPrintable(sql));

        d.reset(tables, "mysql");
        QVERIFY(d.updateColumn("review", columnIndex(d, "review", "body"), { { "type", "VARCHAR(500)" } }));
        QVERIFY(d.renameTable("orders", "purchase"));
        sql = d.migration();
        QVERIFY2(sql.contains("RENAME TABLE orders TO purchase;"), qPrintable(sql));
        QVERIFY2(sql.contains("ALTER TABLE review MODIFY COLUMN body VARCHAR(500);"), qPrintable(sql));

        d.reset(tables, "sqlserver");
        d.addTable("order");                                    // a reserved word: quoted
        QVERIFY(d.renameTable("orders", "purchase"));
        sql = d.migration();
        QVERIFY2(sql.contains("CREATE TABLE [order] (\n    id INT IDENTITY(1,1) PRIMARY KEY\n);"), qPrintable(sql));
        QVERIFY2(sql.contains("EXEC sp_rename 'orders', 'purchase';"), qPrintable(sql));
    }
};

QTEST_GUILESS_MAIN(TestDesign)
#include "tst_design.moc"
