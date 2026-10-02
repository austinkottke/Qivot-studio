/** Qivot Studio — explore any SQLite database.

    qivot-studio [file.db]           open a file
    qivot-studio --sample            open the bundled bookshop sample
    qivot-studio --connect postgres://user:pass@host:5432/pagila
                                     connect to a server (postgres://, mysql://, sqlserver://)
    qivot-studio --connect-dialog    start with the "Connect to a server" dialog open
    qivot-studio --view query --query "SELECT …"   open the SQL console and run a query
    qivot-studio --shot out.png      render once, save a screenshot, quit
    qivot-studio --smoke             load, then quit: exit 1 if any QML warning was logged (CI)
    qivot-studio --table book        select a table once the file is open
    qivot-studio --view data         start on the Data tab (or: profile, cpp, diagram, design, export, structure)
    qivot-studio --view export --build    export the project and build it right away
    qivot-studio --sample --view design   the class designer (--design-demo, --design-tab sql)
    qivot-studio --sample --project ~/Projects/Shop   open a project folder in the IDE (add --build to test it)
    qivot-studio --dark / --light    force the colour scheme (screenshots)
    qivot-studio --size 1440x900     window size (screenshots)
    qivot-studio --find Track        in the diagram, fly to a table and highlight it
    qivot-studio --select-row 3      on the Data tab, open a row in the inspector
    qivot-studio db.sqlite --models models.h   write Qivot model classes for every table, quit
    qivot-studio db.sqlite --export ~/Projects/Shop   write a buildable project (models, example, tests), quit
 */
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <QtQml/QQmlExtensionPlugin>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "databasesession.h"
#include "projectexport.h"
#include "sampledatabase.h"
#include "sampleschema.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
// The QML modules are static libraries; pull their plugins into the binary.
Q_IMPORT_QML_PLUGIN(QivotStudio_CorePlugin)
Q_IMPORT_QML_PLUGIN(QivotUIPlugin)
#else
#include "diagramgeometry.h"
#include "prefs.h"
#include "projectbuild.h"
#include "querybuilder.h"
#include "tableprofile.h"
#include "datatransfer.h"
#include "schemacompare.h"
#include "diagramexport.h"
#include "querylibrary.h"
#include "queryplan.h"
#include "sqlcompleter.h"
#include "schemadesign.h"
#include "querymodel.h"
#include "rowsmodel.h"
#include "syntaxhighlighter.h"
#include "workspace.h"
#endif

namespace {
// postgres://user:pass@host:port/database  ->  DatabaseSession::connectTo() settings.
QVariantMap parseConnectUrl(const QString &text)
{
    const QUrl url(text);
    const QString scheme = url.scheme().toLower();
    const QString type = scheme == QLatin1String("postgres") || scheme == QLatin1String("postgresql") ? QStringLiteral("postgres")
                       : scheme == QLatin1String("mysql") || scheme == QLatin1String("mariadb")      ? QStringLiteral("mysql")
                       : scheme == QLatin1String("sqlserver") || scheme == QLatin1String("mssql")    ? QStringLiteral("sqlserver")
                       : scheme;
    return { { QStringLiteral("type"), type },
             { QStringLiteral("host"), url.host() },
             { QStringLiteral("port"), url.port(0) },
             { QStringLiteral("database"), url.path().mid(1) },
             { QStringLiteral("user"), url.userName() },
             { QStringLiteral("password"), url.password() } };
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
// Qt 5 has no generated type registration: register QivotStudio.Core here.
// QivotUI and the screens come from the resource file (compat/qt5/).
template <typename T>
QObject *createSingleton(QQmlEngine *, QJSEngine *) { return new T; }

void registerQt5Types()
{
    const char *uri = "QivotStudio.Core";
    qmlRegisterType<DatabaseSession>(uri, 1, 0, "Database");
    qmlRegisterType<RowsModel>(uri, 1, 0, "Rows");
    qmlRegisterType<QueryModel>(uri, 1, 0, "QueryResult");
    qmlRegisterType<SyntaxHighlighter>(uri, 1, 0, "SyntaxHighlighter");
    qmlRegisterType<ProjectExport>(uri, 1, 0, "ProjectExport");
    qmlRegisterType<ProjectBuild>(uri, 1, 0, "ProjectBuild");
    qmlRegisterType<Workspace>(uri, 1, 0, "Workspace");
    qmlRegisterType<SchemaDesign>(uri, 1, 0, "Design");
    qmlRegisterType<QueryBuilder>(uri, 1, 0, "QueryBuilder");
    qmlRegisterType<TableProfile>(uri, 1, 0, "Profile");
    qmlRegisterType<CsvImport>(uri, 1, 0, "CsvImport");
    qmlRegisterType<SchemaCompare>(uri, 1, 0, "SchemaCompare");
    qmlRegisterType<DiagramExport>(uri, 1, 0, "DiagramExport");
    qmlRegisterType<QueryLibrary>(uri, 1, 0, "QueryLibrary");
    qmlRegisterType<SqlCompleter>(uri, 1, 0, "SqlCompleter");
    qmlRegisterType<QueryPlan>(uri, 1, 0, "QueryPlan");
    qmlRegisterSingletonType<DiagramGeometry>(uri, 1, 0, "DiagramGeometry", createSingleton<DiagramGeometry>);
    qmlRegisterSingletonType<Prefs>(uri, 1, 0, "Prefs", createSingleton<Prefs>);
}
#endif

int              qmlWarnings = 0;
QtMessageHandler previousHandler = nullptr;

// Count QML errors and warnings so --smoke can fail on them.
void countingHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type >= QtWarningMsg && (message.contains(QLatin1String(".qml:"))
                                 || qstrcmp(context.category, "qml") == 0))
        ++qmlWarnings;
    previousHandler(type, context, message);
}
} // namespace

int main(int argc, char *argv[])
{
    previousHandler = qInstallMessageHandler(countingHandler);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Qivot Studio"));
    app.setOrganizationName(QStringLiteral("Qivot"));
    app.setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/qivot-studio-256.png")));

    // Studio draws its own controls; Basic keeps platform styles from restyling them.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QQuickStyle::setStyle(QStringLiteral("Basic"));
#else
    QQuickStyle::setStyle(QStringLiteral("Default"));   // Qt 5's name for Basic
    registerQt5Types();
#endif

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Explore any SQLite database."));
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addPositionalArgument(QStringLiteral("file"), QStringLiteral("SQLite database to open."));
    const QCommandLineOption sample(QStringLiteral("sample"), QStringLiteral("Open the bookshop sample database."));
    const QCommandLineOption openSample(QStringLiteral("open-sample"),
                                        QStringLiteral("Open sample <bookshop|university|company|music>."), QStringLiteral("id"));
    const QCommandLineOption sampleSql(QStringLiteral("sample-sql"),
                                       QStringLiteral("Write every sample as PostgreSQL, MySQL and SQL Server scripts to <folder>, and quit."),
                                       QStringLiteral("folder"));
    const QCommandLineOption shot(QStringLiteral("shot"), QStringLiteral("Save a screenshot to <png> and quit."),
                                  QStringLiteral("png"));
    const QCommandLineOption smoke(QStringLiteral("smoke"), QStringLiteral("Load, then quit; fail on QML warnings."));
    const QCommandLineOption table(QStringLiteral("table"), QStringLiteral("Select <name> once the file is open."),
                                   QStringLiteral("name"));
    const QCommandLineOption view(QStringLiteral("view"), QStringLiteral("Start on <structure|data|cpp|diagram|query>."),
                                  QStringLiteral("tab"));
    const QCommandLineOption connect(QStringLiteral("connect"),
                                     QStringLiteral("Connect to a server, e.g. postgres://user:pass@host/db."),
                                     QStringLiteral("url"));
    const QCommandLineOption connectDialog(QStringLiteral("connect-dialog"),
                                           QStringLiteral("Start with the connect dialog open."));
    const QCommandLineOption queryText(QStringLiteral("query"), QStringLiteral("Run <sql> in the SQL console."),
                                       QStringLiteral("sql"));
    const QCommandLineOption size(QStringLiteral("size"), QStringLiteral("Window size, e.g. 1440x900."),
                                  QStringLiteral("WxH"));
    const QCommandLineOption find(QStringLiteral("find"), QStringLiteral("Highlight <table> in the diagram."),
                                  QStringLiteral("table"));
    const QCommandLineOption selectRow(QStringLiteral("select-row"), QStringLiteral("Inspect row <n> on the Data tab."),
                                       QStringLiteral("n"));
    const QCommandLineOption dark(QStringLiteral("dark"), QStringLiteral("Force dark mode."));
    const QCommandLineOption light(QStringLiteral("light"), QStringLiteral("Force light mode."));
    const QCommandLineOption models(QStringLiteral("models"),
                                    QStringLiteral("Write Qivot models for every table to <file> and quit."),
                                    QStringLiteral("file"));
    const QCommandLineOption exportTo(QStringLiteral("export"),
                                      QStringLiteral("Write a CMake project for the database to <folder> and quit."),
                                      QStringLiteral("folder"));
    const QCommandLineOption queryBuilder(QStringLiteral("query-builder"),
                                          QStringLiteral("Open the query builder (with --view query)."));
    const QCommandLineOption builderDemo(QStringLiteral("builder-demo"),
                                         QStringLiteral("Open the query builder with an example query."));
    const QCommandLineOption buildProject(QStringLiteral("build"),
                                          QStringLiteral("On the Export screen, export and build the project at once."));
    const QCommandLineOption projectFolder(QStringLiteral("project"),
                                           QStringLiteral("Open <folder> in the IDE."), QStringLiteral("folder"));
    const QCommandLineOption designDemo(QStringLiteral("design-demo"),
                                        QStringLiteral("In the designer, start with a few example edits."));
    const QCommandLineOption designMenu(QStringLiteral("design-menu"),
                                        QStringLiteral("In the designer, open the right-click menu of <table[.column]>."),
                                        QStringLiteral("target"));
    const QCommandLineOption designTab(QStringLiteral("design-tab"),
                                       QStringLiteral("In the designer, show <table|cpp|sql>."), QStringLiteral("tab"));
    const QCommandLineOption projectsDir(QStringLiteral("projects-dir"),
                                         QStringLiteral("Put exported projects in <folder> (default ~/Documents/Qivot Projects)."),
                                         QStringLiteral("folder"));
    const QCommandLineOption allowChanges(QStringLiteral("allow-changes"),
                                          QStringLiteral("Allow changes to the database once it's open."));
    const QCommandLineOption editDemo(QStringLiteral("edit-demo"),
                                      QStringLiteral("On the Data tab, start with a few unsaved edits (with --allow-changes)."));
    const QCommandLineOption compareWith(QStringLiteral("compare-with"),
                                         QStringLiteral("On the Compare screen, compare with <file> (or sample:<id>)."),
                                         QStringLiteral("file"));
    const QCommandLineOption exportDiagram(QStringLiteral("export-diagram"),
                                           QStringLiteral("Save the diagram to <file> (.png, .svg or .pdf) once it's laid out."),
                                           QStringLiteral("file"));
    const QCommandLineOption explainOption(QStringLiteral("explain"),
                                           QStringLiteral("With --query: show its plan instead of running it."));
    const QCommandLineOption completeDemo(QStringLiteral("complete-demo"),
                                          QStringLiteral("On the SQL tab, start typing a query with suggestions open."));
    const QCommandLineOption wheel(QStringLiteral("wheel"),
                                   QStringLiteral("Scroll a mouse wheel <notches> (e.g. 3 or -2) at the middle of the window (tests)."),
                                   QStringLiteral("notches"));
    const QCommandLineOption wheelPixels(QStringLiteral("wheel-pixels"),
                                         QStringLiteral("Scroll a trackpad <dx,dy> pixels at the middle of the window (tests)."),
                                         QStringLiteral("dx,dy"));
    const QCommandLineOption shotDelay(QStringLiteral("shot-delay"),
                                       QStringLiteral("Wait <ms> before --shot / --smoke quit (default 1500)."),
                                       QStringLiteral("ms"));
    cli.addOptions({ models, exportTo, queryBuilder, builderDemo, buildProject, projectFolder, designDemo, designMenu, designTab, projectsDir, shotDelay, allowChanges, editDemo, compareWith, exportDiagram, explainOption, completeDemo, wheel, wheelPixels, sample, openSample, sampleSql, shot, smoke, table, view, connect, connectDialog, queryText, size, find, selectRow,
                    dark, light });
    cli.process(app);

    // The server copies of the samples (tools/sample-servers): <folder>/<dialect>/NN-<id>.sql
    if (cli.isSet(sampleSql)) {
        const QString folder = cli.value(sampleSql);
        int n = 0;
        for (const SampleDatabase::Info &info : SampleDatabase::catalogue()) {
            ++n;
            const SampleSchema schema = SampleDatabase::build(info.id);
            for (const QString &dialect : { QStringLiteral("postgres"), QStringLiteral("mysql"), QStringLiteral("sqlserver") }) {
                QDir().mkpath(folder + QLatin1Char('/') + dialect);
                QFile f(QStringLiteral("%1/%2/%3-%4.sql").arg(folder, dialect).arg(n, 2, 10, QLatin1Char('0')).arg(info.id));
                if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    qCritical().noquote() << "Couldn't write" << f.fileName() << f.errorString();
                    return 1;
                }
                f.write(schema.script(dialect, info.id).toUtf8());
            }
        }
        return 0;
    }

    if (cli.isSet(models) || cli.isSet(exportTo)) {
        DatabaseSession db;
        const QVariantMap server = cli.isSet(connect) ? parseConnectUrl(cli.value(connect)) : QVariantMap();
        const bool opened = !server.isEmpty() ? db.connectTo(server)
                          : cli.isSet(sample) || cli.isSet(openSample) ? db.openSample(cli.value(openSample))
                          : db.open(cli.positionalArguments().value(0));
        if (!opened) {
            qCritical().noquote() << db.error();
            return 1;
        }
        if (cli.isSet(exportTo)) {
            const QFileInfo folder(cli.value(exportTo));
            ProjectExport project;
            project.setSession(&db);
            project.setDirectory(folder.absolutePath());
            project.setName(folder.fileName());
            if (!project.write()) {
                qCritical().noquote() << project.error();
                return 1;
            }
            qInfo().noquote() << "Wrote" << project.projectPath();
            return 0;
        }
        QFile out(cli.value(models));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qCritical().noquote() << out.errorString();
            return 1;
        }
        out.write(db.cppHeader().toUtf8());
        return 0;
    }

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        { QStringLiteral("startupFile"),   cli.positionalArguments().value(0) },
        { QStringLiteral("startupSample"), cli.isSet(sample) || cli.isSet(openSample) },
        { QStringLiteral("startupSampleId"), cli.value(openSample) },
        { QStringLiteral("startupAllowChanges"), cli.isSet(allowChanges) },
        { QStringLiteral("startupEditDemo"), cli.isSet(editDemo) },
        { QStringLiteral("startupCompare"), cli.value(compareWith) },
        { QStringLiteral("startupExportDiagram"), cli.value(exportDiagram) },
        { QStringLiteral("startupExplain"), cli.isSet(explainOption) },
        { QStringLiteral("startupCompleteDemo"), cli.isSet(completeDemo) },
        { QStringLiteral("startupTable"),  cli.value(table) },
        { QStringLiteral("detailTab"),     cli.value(view) == QLatin1String("data") ? 1
                                         : cli.value(view) == QLatin1String("profile") ? 2
                                         : cli.value(view) == QLatin1String("cpp") ? 3 : 0 },
        { QStringLiteral("startupView"),   cli.isSet(queryText) || cli.isSet(queryBuilder) || cli.isSet(builderDemo) ? QStringLiteral("query") : cli.value(view) },
        { QStringLiteral("startupQuery"),  cli.value(queryText) },
        { QStringLiteral("startupBuild"),  cli.isSet(buildProject) },
        { QStringLiteral("startupQueryBuilder"), cli.isSet(queryBuilder) },
        { QStringLiteral("startupBuilderDemo"), cli.isSet(builderDemo) },
        { QStringLiteral("startupProject"), cli.isSet(projectFolder) ? QFileInfo(cli.value(projectFolder)).absoluteFilePath() : QString() },
        { QStringLiteral("startupDesignDemo"), cli.isSet(designDemo) },
        { QStringLiteral("startupDesignTab"), cli.value(designTab) },
        { QStringLiteral("startupDesignMenu"), cli.value(designMenu) },
        { QStringLiteral("startupDesignTable"), cli.value(table) },
        { QStringLiteral("projectsDir"),   cli.isSet(projectsDir) ? QFileInfo(cli.value(projectsDir)).absoluteFilePath() : QString() },
        { QStringLiteral("startupFind"),   cli.value(find) },
        { QStringLiteral("screenshotMode"), cli.isSet(shot) },
        { QStringLiteral("startupRow"),    cli.isSet(selectRow) ? cli.value(selectRow).toInt() - 1 : -1 },
        { QStringLiteral("startupConnection"),
          cli.isSet(connect) ? QVariant(parseConnectUrl(cli.value(connect))) : QVariant(QVariantMap()) },
    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    engine.loadFromModule("QivotStudio", "Main");
#else
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    engine.load(QUrl(QStringLiteral("qrc:/qml/QivotStudio/Main.qml")));
#endif
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    // The window comes back the size and place it was left, unless the command
    // line says otherwise (screenshots and tests keep theirs, and save nothing).
    const bool scripted = cli.isSet(size) || cli.isSet(shot) || cli.isSet(smoke);
    if (window && cli.isSet(size)) {
        const QStringList wh = cli.value(size).split(QLatin1Char('x'));
        if (wh.size() == 2)
            window->resize(wh[0].toInt(), wh[1].toInt());
    } else if (window && !scripted) {
        QSettings settings;
        const QRect saved = settings.value(QStringLiteral("window/geometry")).toRect();
        // Only where it's still on a screen (a monitor may have gone).
        bool onScreen = false;
        for (const QScreen *s : QGuiApplication::screens())
            onScreen = onScreen || s->availableGeometry().intersects(saved.adjusted(40, 40, -40, -40));
        if (saved.isValid() && onScreen)
            window->setGeometry(saved);
        if (settings.value(QStringLiteral("window/maximized")).toBool())
            window->showMaximized();
    }
    if (window && !scripted) {
        QObject::connect(&app, &QGuiApplication::aboutToQuit, window, [window] {
            QSettings settings;
            const bool maximized = window->visibility() == QWindow::Maximized
                                   || window->visibility() == QWindow::FullScreen;
            settings.setValue(QStringLiteral("window/maximized"), maximized);
            if (!maximized)                 // keep the last normal size to come back to
                settings.setValue(QStringLiteral("window/geometry"), window->geometry());
        });
    }
    if (window && cli.isSet(connectDialog))
        QMetaObject::invokeMethod(window, "showConnectDialog");
    if (window && (cli.isSet(dark) || cli.isSet(light))) {
        // Override `dark` on the Theme singleton from a one-line QML snippet:
        // the one way to reach a QML-file singleton on both Qt 5 and Qt 6.
        QQmlComponent setter(&engine);
        setter.setData(QByteArray("import QtQml 2.15; import QivotUI 1.0\n"
                                  "QtObject { Component.onCompleted: Theme.dark = ")
                           + (cli.isSet(dark) ? "true" : "false") + " }",
                       QUrl(QStringLiteral("qrc:/theme-override.qml")));
        delete setter.create();
    }

    // Simulated scrolling, for checking what the wheel does (with --shot).
    if (window && (cli.isSet(wheel) || cli.isSet(wheelPixels))) {
        const int notches = cli.value(wheel).toInt();
        const QStringList px = cli.value(wheelPixels).split(QLatin1Char(','));
        const QPoint pixels(px.value(0).toInt(), px.value(1).toInt());
        // The trackpad first, then (after it settles) the wheel, each in three
        // events as they'd arrive.
        // As macOS sends them: a wheel notch has a pixel delta too (a line's
        // worth) but no phase; a trackpad scrolls in phases.
        const auto scroll = [window](QPoint pixels, QPoint angle, Qt::ScrollPhase phase) {
            const QPointF at(window->width() * 0.6, window->height() * 0.5);
            for (int i = 0; i < 3; ++i) {
                QWheelEvent e(at, window->mapToGlobal(at.toPoint()), pixels / 3, angle / 3, Qt::NoButton,
                              Qt::NoModifier, phase, false);
                QCoreApplication::sendEvent(window, &e);
            }
        };
        if (notches)
            QTimer::singleShot(1000, window, [scroll, notches] {
                scroll(QPoint(0, notches * 10), QPoint(0, notches * 120), Qt::NoScrollPhase);
            });
        if (!pixels.isNull())
            QTimer::singleShot(700, window, [scroll, pixels] { scroll(pixels, pixels * 2, Qt::ScrollUpdate); });
    }

    if (cli.isSet(shot) || cli.isSet(smoke)) {
        const QString shotPath = cli.value(shot);
        auto *done = new QTimer(&app);
        done->setSingleShot(true);
        QObject::connect(done, &QTimer::timeout, &app, [&app, window, shotPath] {
            if (window && !shotPath.isEmpty())
                window->grabWindow().save(shotPath);
            app.exit(qmlWarnings > 0 ? 1 : 0);
        });
        if (cli.isSet(buildProject) && window) {
            // Wait for the build, then give the console a moment to settle.
            done->setInterval(800);
            QObject::connect(window, SIGNAL(buildFinished()), done, SLOT(start()));
        } else {
            done->start(cli.isSet(shotDelay) ? cli.value(shotDelay).toInt() : 1500);
        }
    }
    return app.exec();
}
