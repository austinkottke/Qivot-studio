#include <QtTest>
#include <QTemporaryDir>

#include "databasesession.h"
#include "projectbuild.h"
#include "projectexport.h"
#include "sampledatabase.h"

/// Project export, end to end: the files it writes, and that the project
/// really configures, builds, and passes its own tests against the sample.
class TestProjectExport : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir   m_dir;
    QString         m_sample;
    DatabaseSession m_db;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_sample = m_dir.filePath("bookshop.db");
        QVERIFY(SampleDatabase::create(m_sample));
        QVERIFY(m_db.open(m_sample));
    }

    void generatesTheFiles()
    {
        const QMap<QString, QString> files = ProjectExport::generate(m_db, "Bookshop");
        QCOMPARE(files.keys(), QStringList({ "CMakeLists.txt", "README.md", "src/database.h", "src/main.cpp",
                                             "src/models.h", "tests/tst_models.cpp",
                                             "third_party/qivot/LICENSE.txt", "third_party/qivot/NOTICE.txt",
                                             "third_party/qivot/qivot.cpp", "third_party/qivot/qivot.hpp" }));
        // Qivot comes with it: the whole single header, built once by qivot.cpp.
        QVERIFY(files.value("third_party/qivot/qivot.hpp").contains("#define QIVOT_HPP"));
        QVERIFY(files.value("third_party/qivot/qivot.hpp").size() > 100000);
        QVERIFY(files.value("third_party/qivot/qivot.cpp").contains("#define QIVOT_IMPLEMENTATION"));
        QVERIFY(files.value("third_party/qivot/LICENSE.txt").contains("MIT"));

        const QString cmake = files.value("CMakeLists.txt");
        QVERIFY(cmake.contains("project(Bookshop LANGUAGES CXX)"));
        QVERIFY(cmake.contains("add_executable(bookshop "));
        QVERIFY(cmake.contains("add_library(qivot STATIC third_party/qivot/qivot.cpp"));
        QVERIFY(!cmake.contains("FetchContent"));                // nothing to download
        for (const char *model : { "Author", "Book", "OrderItem", "Review" })
            QVERIFY2(cmake.contains(QRegularExpression(QStringLiteral("\\n    %1\\b").arg(model))), model);

        // The database path is in database.h; a server password never is.
        QVERIFY(files.value("src/database.h").contains(QFileInfo(m_sample).absoluteFilePath()));
        QVERIFY(files.value("src/database.h").contains("QIVOT_DB"));

        // A test per model, named like it; foreign keys are followed.
        const QString tests = files.value("tests/tst_models.cpp");
        QVERIFY(tests.contains("    void OrderItem()"));
        QVERIFY(tests.contains("follows(row.author_id)"));

        // The example follows a foreign key.
        QVERIFY2(files.value("src/main.cpp").contains("->"), qPrintable(files.value("src/main.cpp")));
    }

    void suggestsAName()
    {
        ProjectExport project;
        project.setSession(&m_db);
        QCOMPARE(project.suggestedName(), QString("Bookshop"));
        QVERIFY(project.files().isEmpty());       // no name yet
        project.setName("Shop");
        QCOMPARE(project.files().size(), 10);
    }

    void buildsAndPassesItsTests()
    {
        if (ProjectBuild::findCMake().isEmpty())
            QSKIP("cmake isn't installed");

        ProjectExport project;
        project.setSession(&m_db);
        project.setDirectory(m_dir.path());
        project.setName("Bookshop");
        QVERIFY2(project.write(), qPrintable(project.error()));
        QVERIFY(project.exists());

        ProjectBuild build;
        build.setDirectory(project.projectPath());
        QSignalSpy done(&build, &ProjectBuild::finished);
        build.start();
        QVERIFY(build.running());
        QVERIFY(done.wait(540000));
        QVERIFY2(done.first().first().toBool(), qPrintable(build.log().right(4000)));
        QCOMPARE(build.stage(), QString("passed"));
        QCOMPARE(build.results().size(), 8);           // one per table and view
        QCOMPARE(build.failedCount(), 0);
        QVERIFY(build.passedCount() == 8);
    }
};

QTEST_GUILESS_MAIN(TestProjectExport)
#include "tst_projectexport.moc"
