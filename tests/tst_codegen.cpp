#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "codegen.h"
#include "databasesession.h"
#include "sampledatabase.h"

/// The Qivot model generator, against the bookshop sample and a schema full of
/// awkward cases.
class TestCodeGen : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString       m_sample;
    QString       m_awkward;

    static QVector<QiTableInfo> read(const QString &path)
    {
        QVector<QiTableInfo> tables;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("codegen"));
            db.setDatabaseName(path);
            if (db.open())
                tables = QiSchema(db).tables(/*includeViews=*/true);
        }
        QSqlDatabase::removeDatabase(QStringLiteral("codegen"));
        return tables;
    }

    static CodeGen::Warning warningFor(const CodeGen::Model &m, const QString &column)
    {
        for (const CodeGen::Warning &w : m.warnings)
            if (w.column == column)
                return w;
        return {};
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_sample = m_dir.filePath("bookshop.db");
        QVERIFY(SampleDatabase::create(m_sample));

        m_awkward = m_dir.filePath("awkward.db");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("setup"));
            db.setDatabaseName(m_awkward);
            QVERIFY(db.open());
            QSqlQuery q(db);
            for (const char *sql : {
                     "CREATE TABLE tag (slug TEXT PRIMARY KEY, label VARCHAR(80) NOT NULL DEFAULT 'untitled')",
                     "CREATE TABLE person (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE,"
                     " manager_id INTEGER REFERENCES person(id), team_id INTEGER REFERENCES team(id),"
                     " \"Start Date\" DATE, class TEXT, created DATETIME DEFAULT CURRENT_TIMESTAMP,"
                     " score NUMERIC(5,2) DEFAULT 0, avatar BLOB, active BOOLEAN)",
                     "CREATE TABLE team (id INTEGER PRIMARY KEY, lead_id INTEGER REFERENCES person(id) ON DELETE SET NULL)",
                     "CREATE TABLE person_tag (person_id INTEGER REFERENCES person(id) ON DELETE CASCADE,"
                     " tag TEXT REFERENCES tag(slug), PRIMARY KEY (person_id, tag))",
                     "CREATE TABLE \"order\" (id INTEGER PRIMARY KEY, note TEXT)",
                     "CREATE TABLE log (at DATETIME, message)",
                     "CREATE TABLE Artist (ArtistId INTEGER PRIMARY KEY, Name NVARCHAR(120))",
                     "CREATE TABLE Album (AlbumId INTEGER PRIMARY KEY, Title NVARCHAR(160) NOT NULL,"
                     " ArtistId INTEGER NOT NULL REFERENCES Artist(ArtistId))",
                     "CREATE VIEW leads AS SELECT id, name FROM person" })
                QVERIFY2(q.exec(QString::fromLatin1(sql)), sql);
        }
        QSqlDatabase::removeDatabase(QStringLiteral("setup"));
    }

    void cppTypes_data()
    {
        QTest::addColumn<QString>("sql");
        QTest::addColumn<QString>("cpp");
        const QList<QPair<const char *, const char *>> cases = {
            { "INTEGER", "int" }, { "int(11) unsigned", "int" }, { "smallint", "int" }, { "serial", "int" },
            { "BIGINT", "qint64" }, { "bigserial", "qint64" },
            { "tinyint(1)", "bool" }, { "boolean", "bool" }, { "bit", "bool" },
            { "REAL", "double" }, { "numeric(10,2)", "double" }, { "double precision", "double" }, { "money", "double" },
            { "DATE", "QDate" }, { "DATETIME", "QDateTime" }, { "timestamp without time zone", "QDateTime" },
            { "timestamptz", "QDateTime" }, { "datetime2", "QDateTime" }, { "time", "QTime" },
            { "BLOB", "QByteArray" }, { "bytea", "QByteArray" }, { "varbinary(max)", "QByteArray" },
            { "TEXT", "QString" }, { "character varying(80)", "QString" }, { "uuid", "QString" },
            { "interval", "QString" }, { "point", "QString" }, { "", "QString" },
        };
        for (const auto &c : cases)
            QTest::newRow(c.first) << QString::fromLatin1(c.first) << QString::fromLatin1(c.second);
    }
    void cppTypes()
    {
        QFETCH(QString, sql);
        QFETCH(QString, cpp);
        QCOMPARE(CodeGen::cppType(sql), cpp);
    }

    void fieldNames()
    {
        QVERIFY(CodeGen::isFieldName("customer_id"));
        QVERIFY(CodeGen::isFieldName("AlbumId"));
        QVERIFY(!CodeGen::isFieldName("Start Date"));
        QVERIFY(!CodeGen::isFieldName("2fa"));
        QVERIFY(!CodeGen::isFieldName("class"));
        QVERIFY(!CodeGen::isFieldName("save"));         // a QiModel method
        QVERIFY(!CodeGen::isFieldName("_Reserved"));
    }

    void sampleModels()
    {
        CodeGen gen(read(m_sample), QStringLiteral("sqlite"));

        const CodeGen::Model book = gen.model("book");
        QCOMPARE(book.className, QString("Book"));
        QVERIFY2(book.code.contains("QI_DECLARE_MODEL(Book, \"book\""), qPrintable(book.code));
        QVERIFY2(!book.code.contains(" id;"), "the built-in id is QiModel's");
        QVERIFY2(book.code.contains("QiForeignKey<Author"), qPrintable(book.code));
        QCOMPARE(book.dependsOn.contains("Author"), true);

        // Composite key: no built-in id, both key columns marked.
        const CodeGen::Model item = gen.model("order_item");
        QCOMPARE(item.className, QString("OrderItem"));
        QVERIFY2(item.code.contains("QI_DECLARE_MODEL_NOID(OrderItem"), qPrintable(item.code));
        QCOMPARE(item.code.count("QiPrimary"), 2);

        // Views come out read-only, with a note.
        const CodeGen::Model sales = gen.model("book_sales");
        QVERIFY(sales.isValid());
        QVERIFY(sales.code.contains("QI_DECLARE_MODEL_NOID"));
        QVERIFY(!sales.warnings.isEmpty());
    }

    void headerOrdersParentsFirst()
    {
        CodeGen gen(read(m_sample), QStringLiteral("sqlite"));
        const QString h = gen.header();
        QVERIFY(h.startsWith("// Qivot models"));
        QVERIFY(h.contains("#include <qivot.hpp>"));
        for (const CodeGen::Model &m : gen.models())
            for (const QString &parent : m.dependsOn)
                QVERIFY2(h.indexOf("class " + parent + " ") < h.indexOf("class " + m.className + " "),
                         qPrintable(parent + " must come before " + m.className));
    }

    void awkwardSchema()
    {
        CodeGen gen(read(m_awkward), QStringLiteral("sqlite"));

        // String key: NOID + QiPrimary; VARCHAR(80) kept; string default quoted.
        const CodeGen::Model tag = gen.model("tag");
        QVERIFY2(tag.code.contains("QI_FIELD(slug, QiPrimary | QiNotNull)"), qPrintable(tag.code));
        QVERIFY2(tag.code.contains("QI_FIELD_AS(label, \"VARCHAR(80)\", QiNotNull | QiDefault(\"'untitled'\"))"),
                 qPrintable(tag.code));

        const CodeGen::Model person = gen.model("person");
        // Columns that can't be fields are left out, each with a reason.
        QVERIFY(!person.code.contains("Start Date"));
        QVERIFY(!warningFor(person, "Start Date").message.isEmpty());
        QVERIFY(!person.code.contains(" class;"));
        QVERIFY(!warningFor(person, "class").message.isEmpty());
        // Clauses and types.
        QVERIFY2(person.code.contains("QI_FIELD(name, QiNotNull | QiUnique)"), qPrintable(person.code));
        QVERIFY2(person.code.contains("QiDefault(\"CURRENT_TIMESTAMP\")"), qPrintable(person.code));
        QVERIFY2(person.code.contains("QI_FIELD_AS(score, \"NUMERIC(5,2)\", QiDefault(\"0\"))"), qPrintable(person.code));
        QVERIFY(person.code.contains("QiField<QByteArray>"));
        QVERIFY(person.code.contains("QiField<bool>"));
        // A self-reference stays a plain integer with the reference in a comment.
        QVERIFY2(person.code.contains(QRegularExpression("QiField<int>\\s+manager_id;\\s+// -> person\\(id\\)")),
                 qPrintable(person.code));

        // person <-> team point at each other: exactly one side keeps QiForeignKey.
        const QString h = gen.header();
        const bool personFollows = h.contains(QRegularExpression("QiForeignKey<Team>\\s+team_id;"));
        const bool teamFollows = h.contains(QRegularExpression("QiForeignKey<Person, QiFkSetNull>\\s+lead_id;"));
        QVERIFY2(personFollows != teamFollows, qPrintable(h));
        QVERIFY(h.contains("refer to each other"));

        // A key column can also be a foreign key; one to a string key stays plain.
        const CodeGen::Model pt = gen.model("person_tag");
        QVERIFY2(pt.code.contains("QiForeignKey<Person, QiFkCascade>"), qPrintable(pt.code));
        QVERIFY2(pt.code.contains(QRegularExpression("QiField<QString>\\s+tag;\\s+// -> tag\\(slug\\)")),
                 qPrintable(pt.code));

        // A foreign key to a QiPrimary integer key (Chinook style) is followed too.
        const CodeGen::Model album = gen.model("Album");
        QVERIFY2(album.code.contains("QI_DECLARE_MODEL_NOID(Album"), qPrintable(album.code));
        QVERIFY2(album.code.contains(QRegularExpression("QiForeignKey<Artist>\\s+ArtistId;")), qPrintable(album.code));
        QVERIFY2(album.code.contains("QI_FIELD(AlbumId, QiPrimary | QiNotNull)"), qPrintable(album.code));

        // A reserved word as a table name, and a table with no key.
        QVERIFY(!gen.model("order").warnings.isEmpty());
        QCOMPARE(gen.model("order").className, QString("Order"));
        const CodeGen::Model log = gen.model("log");
        QVERIFY(!log.warnings.isEmpty());
        QVERIFY2(log.code.contains("QI_FIELD_AS(message, \"\")"), qPrintable(log.code));   // typeless column
    }

    // A table that is only its key (a lookup a foreign key points at).
    void idOnlyTable()
    {
        QiTableInfo t;
        t.name = "category";
        QiColumnInfo id;
        id.name = "id";
        id.type = "INTEGER";
        id.primaryKey = true;
        id.nullable = false;
        t.columns << id;
        t.primaryKey << "id";
        const CodeGen::Model m = CodeGen({ t }, "sqlite").model("category");
        QVERIFY2(m.code.contains("QI_DECLARE_MODEL_NOID(Category, \"category\", QI_FIELD(id));"), qPrintable(m.code));
    }

    void throughTheSession()
    {
        DatabaseSession db;
        QVERIFY(db.open(m_sample));
        const QVariantMap m = db.cppModel("customer");
        QCOMPARE(m.value("className").toString(), QString("Customer"));
        QVERIFY(m.value("code").toString().contains("class Customer : public QiModel"));
        QVERIFY(db.cppModel("nope").isEmpty());
        QVERIFY(db.cppHeader().contains("#endif // MODELS_H"));
    }
};

QTEST_GUILESS_MAIN(TestCodeGen)
#include "tst_codegen.moc"
