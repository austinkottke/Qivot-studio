#ifndef PROJECTBUILD_H
#define PROJECTBUILD_H

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQmlEngine>
#include <QVariantList>

#include "databasesession.h"   // QPointer<DatabaseSession> needs the full type

/// Configures, builds, tests or runs a CMake project, streaming the output.
/**
  Runs `cmake` (configure), `cmake --build`, then, by mode, `ctest` or the
  project's program — each step only if the one before succeeded — against
  the Qt Studio itself was built with. Each ctest result becomes an entry in
  `results`, and each compiler or CMake error/warning an entry in `problems`
  (clang, gcc and MSVC formats), so the UI can show them while the log scrolls.

  When `session` is set, its password goes to the tests as
  $QIVOT_DB_PASSWORD (exported projects never store it).

\code
    ProjectBuild { id: build; directory: project.projectPath; session: db }
    ActionButton { text: "Build & test"; onClicked: build.start() }
\endcode
 */
class ProjectBuild : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY directoryChanged)
    Q_PROPERTY(DatabaseSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    /// "", "configure", "build", "test", "run", "passed" or "failed".
    Q_PROPERTY(QString stage READ stage NOTIFY stateChanged)
    /// The step that ran last: "configure", "build", "test" or "run" (the failing one, after a failure).
    Q_PROPERTY(QString step READ step NOTIFY stateChanged)
    /// What start() was asked for: "build", "test" or "run".
    Q_PROPERTY(QString mode READ mode NOTIFY stateChanged)
    Q_PROPERTY(QString log READ log NOTIFY logChanged)
    /// Compiler and CMake diagnostics: `{ file (absolute), line, column, severity: "error"|"warning", message }`.
    Q_PROPERTY(QVariantList problems READ problems NOTIFY problemsChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY problemsChanged)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY problemsChanged)
    /// One per ctest test: `{ name, passed, seconds }`.
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(int passedCount READ passedCount NOTIFY resultsChanged)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY resultsChanged)
    Q_PROPERTY(int elapsedMs READ elapsedMs NOTIFY stateChanged)
    /// The cmake found on this machine; empty if there is none.
    Q_PROPERTY(QString cmake READ cmake CONSTANT)

public:
    explicit ProjectBuild(QObject *parent = nullptr);
    ~ProjectBuild() override;

    QString directory() const { return m_directory; }
    void setDirectory(const QString &directory);
    DatabaseSession *session() const { return m_session; }
    void setSession(DatabaseSession *session);
    bool running() const { return m_process != nullptr; }
    QString stage() const { return m_stage; }
    QString step() const { return m_step; }
    QString mode() const { return m_mode; }
    QVariantList problems() const { return m_problems; }
    int errorCount() const;
    int warningCount() const { return m_problems.size() - errorCount(); }
    QString log() const { return m_log; }
    QVariantList results() const { return m_results; }
    int passedCount() const;
    int failedCount() const;
    int elapsedMs() const { return m_elapsedMs; }
    QString cmake() const { return m_cmake; }

    /// Configure and build, then `mode` "test" runs ctest and "run" runs the
    /// project's program; "build" stops after building. Does nothing while running.
    Q_INVOKABLE void start(const QString &mode = QStringLiteral("test"));
    /// Stop whatever step is running.
    Q_INVOKABLE void stop();

    /// Where cmake is: PATH, then the usual install locations.
    static QString findCMake();

    /// Parse one line of build output as a diagnostic: `{ file, line, column,
    /// severity, message }`, or empty if it isn't one. `directory` resolves
    /// relative paths.
    static QVariantMap parseProblem(const QString &line, const QString &directory);

    /// The program a build produced (not a test), or "" if there's none.
    static QString findProgram(const QString &buildDirectory);

signals:
    void directoryChanged();
    void sessionChanged();
    void stateChanged();
    void logChanged();
    void resultsChanged();
    void problemsChanged();
    void finished(bool passed);

private:
    void runStep(const QString &stage, const QString &program, const QStringList &arguments);
    void stepFinished(int exitCode, QProcess::ExitStatus status);
    void append(const QString &text);
    void parseTestLine(const QString &line);
    void finish(const QString &stage);

    QString                   m_directory;
    QPointer<DatabaseSession> m_session;
    QString                   m_cmake;
    QString                   m_stage;
    QString                   m_step;
    QString                   m_mode;
    QVariantList              m_problems;
    QString                   m_log;
    QString                   m_partialLine;
    QVariantList              m_results;
    QProcess                 *m_process = nullptr;
    QElapsedTimer             m_clock;
    int                       m_elapsedMs = 0;
};

#endif // PROJECTBUILD_H
