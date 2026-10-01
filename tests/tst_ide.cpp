#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include "projectbuild.h"
#include "workspace.h"

/// The IDE's pieces: the workspace tree and files, reading compiler
/// diagnostics, and building / running a real (tiny) CMake project.
class TestIde : public QObject
{
    Q_OBJECT

private:
    static void put(const QString &path, const QByteArray &text)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text);
    }
    static QStringList paths(const Workspace &ws)
    {
        QStringList out;
        for (int i = 0; i < ws.rowCount(); ++i)
            out << ws.data(ws.index(i), Workspace::PathRole).toString();
        return out;
    }

private slots:
    void workspaceTree()
    {
        QTemporaryDir dir;
        put(dir.filePath("CMakeLists.txt"), "project(x)\n");
        put(dir.filePath("src/main.cpp"), "int main() {}\n");
        put(dir.filePath("src/util/a.h"), "");
        put(dir.filePath("build/CMakeCache.txt"), "");      // build output: left out
        put(dir.filePath(".git/HEAD"), "");                  // hidden: left out

        Workspace ws;
        ws.setRoot(dir.path());
        QCOMPARE(ws.name(), QFileInfo(dir.path()).fileName());
        // Top-level folders start open; folders come before files.
        QCOMPARE(paths(ws), QStringList({ "src", "src/util", "src/main.cpp", "CMakeLists.txt" }));
        QCOMPARE(ws.fileCount(), 2);

        ws.toggle(1);                                       // open src/util
        QCOMPARE(paths(ws), QStringList({ "src", "src/util", "src/util/a.h", "src/main.cpp", "CMakeLists.txt" }));
        QCOMPARE(ws.data(ws.index(2), Workspace::DepthRole).toInt(), 2);
        ws.toggle(0);                                       // close src
        QCOMPARE(paths(ws), QStringList({ "src", "CMakeLists.txt" }));
    }

    void workspaceFiles()
    {
        QTemporaryDir dir;
        put(dir.filePath("a.txt"), "one");
        Workspace ws;
        ws.setRoot(dir.path());
        QCOMPARE(ws.read("a.txt"), QString("one"));
        QVERIFY(ws.write("a.txt", "two ✓"));
        QCOMPARE(ws.read("a.txt"), QString("two ✓"));
        QVERIFY(ws.exists("a.txt"));
        QVERIFY(!ws.exists("nope.txt"));
        QCOMPARE(ws.relativePath(dir.filePath("a.txt")), QString("a.txt"));
        QCOMPARE(ws.relativePath("/somewhere/else.cpp"), QString());

        // A new file on disk shows up by itself.
        QSignalSpy changed(&ws, &Workspace::treeChanged);
        put(dir.filePath("b.txt"), "");
        QVERIFY(changed.wait(3000));
        QVERIFY(paths(ws).contains("b.txt"));
    }

    void diagnostics_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("lineNo");
        QTest::addColumn<QString>("severity");
        QTest::addColumn<QString>("message");
        QTest::newRow("clang") << "/p/src/main.cpp:12:5: error: use of undeclared identifier 'x'"
                               << "/p/src/main.cpp" << 12 << "error" << "use of undeclared identifier 'x'";
        QTest::newRow("gcc relative") << "src/models.h:7:3: warning: unused variable 'y' [-Wunused-variable]"
                                      << "/p/src/models.h" << 7 << "warning" << "unused variable 'y' [-Wunused-variable]";
        QTest::newRow("gcc fatal") << "/p/a.cpp:1:10: fatal error: nope.h: No such file or directory"
                                   << "/p/a.cpp" << 1 << "error" << "nope.h: No such file or directory";
        QTest::newRow("msvc") << "C:/p/src/main.cpp(12,5): error C2065: 'x': undeclared identifier"
                              << "C:/p/src/main.cpp" << 12 << "error" << "C2065: 'x': undeclared identifier";
        QTest::newRow("cmake") << "CMake Error at CMakeLists.txt:9 (find_package):"
                               << "/p/CMakeLists.txt" << 9 << "error" << "CMake error (see the log for details)";
    }
    void diagnostics()
    {
        QFETCH(QString, line);
        QFETCH(QString, file);
        QFETCH(int, lineNo);
        QFETCH(QString, severity);
        QFETCH(QString, message);
        const QVariantMap p = ProjectBuild::parseProblem(line, "/p");
        QVERIFY2(!p.isEmpty(), qPrintable(line));
        // Compared as the platform resolves it: on Windows "/p/..." gains a drive
        // letter, and a "C:/..." path is only absolute there.
        const QFileInfo expected(file);
        if (!expected.isRelative())
            QCOMPARE(p.value("file").toString(), QDir::cleanPath(expected.absoluteFilePath()));
        QCOMPARE(p.value("line").toInt(), lineNo);
        QCOMPARE(p.value("severity").toString(), severity);
        QCOMPARE(p.value("message").toString(), message);
    }
    void notDiagnostics()
    {
        for (const char *line : { "[ 50%] Building CXX object CMakeFiles/x.dir/main.cpp.o",
                                  "/p/a.cpp:3:1: note: declared here",
                                  "1 error generated.",
                                  "-- Configuring done (0.3s)" })
            QVERIFY2(ProjectBuild::parseProblem(QString::fromLatin1(line), "/p").isEmpty(), line);
    }

    // A real build: an error lands in `problems`; fixed, the program runs.
    void buildsAndRuns()
    {
        if (ProjectBuild::findCMake().isEmpty())
            QSKIP("cmake isn't installed");
        QTemporaryDir dir;
        put(dir.filePath("CMakeLists.txt"),
            "cmake_minimum_required(VERSION 3.16)\nproject(hello LANGUAGES CXX)\nadd_executable(hello main.cpp)\n");
        put(dir.filePath("main.cpp"), "#include <cstdio>\nint main() {\n    std::puts(greeting);\n}\n");

        ProjectBuild build;
        build.setDirectory(dir.path());
        QSignalSpy done(&build, &ProjectBuild::finished);
        build.start("build");
        QVERIFY(done.wait(180000));
        QCOMPARE(done.takeFirst().first().toBool(), false);
        QCOMPARE(build.step(), QString("build"));
        QVERIFY2(build.errorCount() >= 1, qPrintable(build.log()));
        const QVariantMap problem = build.problems().first().toMap();
        QCOMPARE(QFileInfo(problem.value("file").toString()).fileName(), QString("main.cpp"));
        QCOMPARE(problem.value("line").toInt(), 3);

        put(dir.filePath("main.cpp"), "#include <cstdio>\nint main() {\n    std::puts(\"hello from the IDE\");\n}\n");
        build.start("run");
        QVERIFY(done.wait(180000));
        QVERIFY2(done.takeFirst().first().toBool(), qPrintable(build.log()));
        QCOMPARE(build.step(), QString("run"));
        QCOMPARE(build.errorCount(), 0);
        QVERIFY2(build.log().contains("hello from the IDE"), qPrintable(build.log()));
        QVERIFY(!ProjectBuild::findProgram(dir.filePath("build")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestIde)
#include "tst_ide.moc"
