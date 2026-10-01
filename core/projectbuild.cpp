#include "projectbuild.h"

#include "databasesession.h"

#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

namespace {

constexpr int MaxLog = 400 * 1024;      // keep the tail of a long build

QString qtPrefix()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QLibraryInfo::path(QLibraryInfo::PrefixPath);
#else
    return QLibraryInfo::location(QLibraryInfo::PrefixPath);
#endif
}

} // namespace

ProjectBuild::ProjectBuild(QObject *parent)
    : QObject(parent)
    , m_cmake(findCMake())
{
}

ProjectBuild::~ProjectBuild()
{
    stop();
}

QString ProjectBuild::findCMake()
{
    QString found = QStandardPaths::findExecutable(QStringLiteral("cmake"));
    if (found.isEmpty())
        found = QStandardPaths::findExecutable(QStringLiteral("cmake"),
                                               { QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"),
                                                 QStringLiteral("/Applications/CMake.app/Contents/bin"),
                                                 QStringLiteral("C:/Program Files/CMake/bin") });
    return found;
}

void ProjectBuild::setDirectory(const QString &directory)
{
    if (directory == m_directory)
        return;
    m_directory = directory;
    emit directoryChanged();
}

void ProjectBuild::setSession(DatabaseSession *session)
{
    if (session == m_session)
        return;
    m_session = session;
    emit sessionChanged();
}

int ProjectBuild::passedCount() const
{
    int n = 0;
    for (const QVariant &r : m_results)
        n += r.toMap().value(QStringLiteral("passed")).toBool() ? 1 : 0;
    return n;
}

int ProjectBuild::errorCount() const
{
    int n = 0;
    for (const QVariant &p : m_problems)
        n += p.toMap().value(QStringLiteral("severity")).toString() == QLatin1String("error") ? 1 : 0;
    return n;
}

QVariantMap ProjectBuild::parseProblem(const QString &line, const QString &directory)
{
    // clang / gcc:   src/main.cpp:12:5: error: use of undeclared identifier 'x'
    static const QRegularExpression gnu(
        QStringLiteral("^(.+?):(\\d+):(?:(\\d+):)?\\s*(fatal error|error|warning):\\s*(.*)$"));
    // MSVC:          C:\p\src\main.cpp(12,5): error C2065: 'x': undeclared identifier
    static const QRegularExpression msvc(
        QStringLiteral("^\\s*(.+?)\\((\\d+)(?:,(\\d+))?\\)\\s*:\\s*(fatal error|error|warning)\\s+(\\w+\\d+:.*)$"));
    // CMake:         CMake Error at CMakeLists.txt:12 (find_package):
    static const QRegularExpression cmake(
        QStringLiteral("^CMake (Error|Warning)(?: \\(dev\\))? at (.+?):(\\d+)"));

    QString file, severity, message;
    int lineNo = 0, column = 0;
    QRegularExpressionMatch m = cmake.match(line);
    if (m.hasMatch()) {
        file = m.captured(2);
        lineNo = m.captured(3).toInt();
        severity = m.captured(1).toLower();
        message = QStringLiteral("CMake ") + severity + QStringLiteral(" (see the log for details)");
    } else if ((m = msvc.match(line)).hasMatch()) {
        file = m.captured(1);
        lineNo = m.captured(2).toInt();
        column = m.captured(3).toInt();
        severity = m.captured(4).endsWith(QLatin1String("error")) ? QStringLiteral("error") : QStringLiteral("warning");
        message = m.captured(5).trimmed();
    } else if ((m = gnu.match(line)).hasMatch()) {
        file = m.captured(1);
        lineNo = m.captured(2).toInt();
        column = m.captured(3).toInt();
        severity = m.captured(4).endsWith(QLatin1String("error")) ? QStringLiteral("error") : QStringLiteral("warning");
        message = m.captured(5).trimmed();
    } else {
        return {};
    }
    if (file.startsWith(QLatin1String("ld")) || file.contains(QLatin1String("make")))
        return {};
    QFileInfo fi(file);
    if (fi.isRelative())
        fi = QFileInfo(QDir(directory).filePath(file));
    return { { QStringLiteral("file"), QDir::cleanPath(fi.absoluteFilePath()) },
             { QStringLiteral("line"), lineNo },
             { QStringLiteral("column"), column },
             { QStringLiteral("severity"), severity },
             { QStringLiteral("message"), message } };
}

QString ProjectBuild::findProgram(const QString &buildDirectory)
{
    // Single-config generators put it in build/, multi-config ones in build/Release/.
    for (const QString &dir : { buildDirectory, QDir(buildDirectory).filePath(QStringLiteral("Release")) }) {
        const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &fi : entries) {
            const QString name = fi.fileName();
            if (name.startsWith(QLatin1String("tst_")) || name.startsWith(QLatin1String("CMake")))
                continue;
            if (fi.isDir() && name.endsWith(QLatin1String(".app")))       // a macOS bundle
                return QDir(fi.absoluteFilePath()).filePath(QStringLiteral("Contents/MacOS/")
                                                            + fi.completeBaseName());
#ifdef Q_OS_WIN
            if (fi.isFile() && name.endsWith(QLatin1String(".exe")))
                return fi.absoluteFilePath();
#else
            if (fi.isFile() && fi.isExecutable() && !name.contains(QLatin1Char('.')))
                return fi.absoluteFilePath();
#endif
        }
    }
    return QString();
}

int ProjectBuild::failedCount() const
{
    return m_results.size() - passedCount();
}

void ProjectBuild::start(const QString &mode)
{
    if (running())
        return;
    m_mode = mode;
    m_log.clear();
    m_partialLine.clear();
    m_results.clear();
    m_problems.clear();
    emit problemsChanged();
    m_step.clear();
    m_elapsedMs = 0;
    emit logChanged();
    emit resultsChanged();
    m_clock.start();

    if (m_cmake.isEmpty()) {
        append(tr("CMake isn't installed (or isn't on the PATH). Get it from https://cmake.org/download/\n"));
        finish(QStringLiteral("failed"));
        return;
    }
    if (!QFileInfo::exists(QDir(m_directory).filePath(QStringLiteral("CMakeLists.txt")))) {
        append(tr("There's no CMakeLists.txt in %1.\n").arg(m_directory));
        finish(QStringLiteral("failed"));
        return;
    }

    QStringList args = { QStringLiteral("-S"), m_directory, QStringLiteral("-B"),
                         QDir(m_directory).filePath(QStringLiteral("build")),
                         QStringLiteral("-DCMAKE_BUILD_TYPE=Release"),
                         QStringLiteral("-DCMAKE_PREFIX_PATH=") + qtPrefix() };
    runStep(QStringLiteral("configure"), m_cmake, args);
}

void ProjectBuild::stop()
{
    if (!m_process)
        return;
    QProcess *p = m_process;
    m_process = nullptr;
    p->disconnect(this);
    p->kill();
    p->waitForFinished(2000);
    p->deleteLater();
    append(tr("\nStopped.\n"));
    finish(QStringLiteral("failed"));
}

void ProjectBuild::runStep(const QString &stage, const QString &program, const QStringList &arguments)
{
    m_stage = stage;
    m_step = stage;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setWorkingDirectory(m_directory);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (m_session && !m_session->password().isEmpty())
        env.insert(QStringLiteral("QIVOT_DB_PASSWORD"), m_session->password());
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    m_process->setProcessEnvironment(env);
    connect(m_process, &QProcess::readyRead, this, [this] {
        if (m_process)
            append(QString::fromLocal8Bit(m_process->readAll()));
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ProjectBuild::stepFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_process) {
            append(tr("Couldn't start %1.\n").arg(m_process->program()));
            stepFinished(-1, QProcess::CrashExit);
        }
    });
    append(QStringLiteral("$ %1 %2\n").arg(QFileInfo(program).fileName(), arguments.join(QLatin1Char(' '))).trimmed()
           + QLatin1Char('\n'));
    emit stateChanged();
    m_process->start(program, arguments);
}

void ProjectBuild::stepFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process)
        return;
    append(QString::fromLocal8Bit(m_process->readAll()));
    m_process->deleteLater();
    m_process = nullptr;
    const bool ok = status == QProcess::NormalExit && exitCode == 0;
    const QString build = QDir(m_directory).filePath(QStringLiteral("build"));

    if (m_stage == QLatin1String("configure") && ok) {
        runStep(QStringLiteral("build"), m_cmake,
                { QStringLiteral("--build"), build, QStringLiteral("--config"), QStringLiteral("Release"),
                  QStringLiteral("--parallel"), QString::number(QThread::idealThreadCount()) });
    } else if (m_stage == QLatin1String("build") && ok && m_mode == QLatin1String("build")) {
        finish(QStringLiteral("passed"));
    } else if (m_stage == QLatin1String("build") && ok && m_mode == QLatin1String("run")) {
        const QString program = findProgram(build);
        if (program.isEmpty()) {
            append(tr("The build didn't produce a program to run.\n"));
            finish(QStringLiteral("failed"));
            return;
        }
        runStep(QStringLiteral("run"), program, {});
    } else if (m_stage == QLatin1String("build") && ok) {
        const QString ctest = QFileInfo(m_cmake).dir().filePath(QStringLiteral("ctest"));
        runStep(QStringLiteral("test"), QFileInfo::exists(ctest) ? ctest : QStringLiteral("ctest"),
                { QStringLiteral("--test-dir"), build, QStringLiteral("-C"), QStringLiteral("Release"),
                  QStringLiteral("--output-on-failure") });
    } else {
        // A failing test is still a finished run: what matters is whether all passed.
        const bool passed = ok && (m_stage == QLatin1String("test") || m_stage == QLatin1String("run"));
        if (m_stage == QLatin1String("run"))
            append(ok ? tr("\nThe program exited normally.\n")
                      : tr("\nThe program exited with code %1.\n").arg(exitCode));
        finish(passed ? QStringLiteral("passed") : QStringLiteral("failed"));
    }
}

void ProjectBuild::append(const QString &text)
{
    if (text.isEmpty())
        return;
    m_log += text;
    if (m_log.size() > MaxLog)
        m_log = m_log.right(MaxLog);
    emit logChanged();

    // ctest's result lines, as they arrive.
    m_partialLine += text;
    int newline;
    while ((newline = m_partialLine.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = m_partialLine.left(newline);
        if (m_stage == QLatin1String("test"))
            parseTestLine(line);
        else if (m_stage == QLatin1String("configure") || m_stage == QLatin1String("build")) {
            const QVariantMap problem = parseProblem(line, m_directory);
            if (!problem.isEmpty() && !m_problems.contains(problem)) {
                m_problems << problem;
                emit problemsChanged();
            }
        }
        m_partialLine.remove(0, newline + 1);
    }
}

void ProjectBuild::parseTestLine(const QString &line)
{
    //  1/11 Test  #1: Album ............................   Passed    0.04 sec
    //  2/11 Test  #2: Track ............................***Failed    0.20 sec
    static const QRegularExpression re(
        QStringLiteral("Test\\s+#\\d+:\\s+(\\S+)\\s+\\.*\\s*(\\*{3})?(\\w[\\w ]*?)\\s+([\\d.]+)\\s+sec"));
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch())
        return;
    m_results << QVariantMap{ { QStringLiteral("name"), m.captured(1) },
                              { QStringLiteral("passed"), m.captured(3) == QLatin1String("Passed") },
                              { QStringLiteral("seconds"), m.captured(4).toDouble() } };
    emit resultsChanged();
}

void ProjectBuild::finish(const QString &stage)
{
    m_stage = stage;
    m_elapsedMs = m_clock.isValid() ? int(m_clock.elapsed()) : 0;
    emit stateChanged();
    emit finished(stage == QLatin1String("passed"));
}
