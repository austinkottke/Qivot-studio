#include "workspace.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

Workspace::Workspace(QObject *parent)
    : QAbstractListModel(parent)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(250);
    connect(&m_settle, &QTimer::timeout, this, &Workspace::rebuild);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_settle.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &absolute) {
        const QString path = relativePath(absolute);
        if (m_justWritten.remove(path))
            return;                                  // our own save
        if (!path.isEmpty())
            emit fileChanged(path);
        if (QFileInfo::exists(absolute))
            m_watcher.addPath(absolute);             // editors that save by replacing drop the watch
    });
}

bool Workspace::isIgnored(const QString &name, bool isDir)
{
    if (name.startsWith(QLatin1Char('.')))
        return true;                                 // .git, .DS_Store, .vscode, ...
    if (isDir)
        return name == QLatin1String("build") || name.startsWith(QLatin1String("build-"))
            || name.startsWith(QLatin1String("cmake-build-")) || name == QLatin1String("CMakeFiles")
            || name == QLatin1String("node_modules");
    return name.endsWith(QLatin1String(".user")) || name.endsWith(QLatin1String(".o"));
}

void Workspace::setRoot(const QString &root)
{
    QString dir = root;
    if (dir.startsWith(QLatin1String("file:")))
        dir = QUrl(dir).toLocalFile();
    dir = dir.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
    if (dir == m_root)
        return;
    m_root = dir;
    m_expanded.clear();
    // Start with the top-level folders open: src/ and tests/ are where the work is.
    if (!m_root.isEmpty())
        for (const QFileInfo &fi : QDir(m_root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (!isIgnored(fi.fileName(), true))
                m_expanded.insert(fi.fileName());
    emit rootChanged();
    rebuild();
}

QString Workspace::name() const
{
    return m_root.isEmpty() ? QString() : QFileInfo(m_root).fileName();
}

int Workspace::fileCount() const
{
    int n = 0;
    for (const Entry &e : m_rows)
        n += e.isDir ? 0 : 1;
    return n;
}

int Workspace::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant Workspace::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Entry &e = m_rows.at(index.row());
    switch (role) {
    case NameRole:     return e.name;
    case PathRole:     return e.path;
    case DepthRole:    return e.depth;
    case IsDirRole:    return e.isDir;
    case ExpandedRole: return e.isDir && m_expanded.contains(e.path);
    }
    return {};
}

QHash<int, QByteArray> Workspace::roleNames() const
{
    return { { NameRole, "name" }, { PathRole, "path" }, { DepthRole, "depth" },
             { IsDirRole, "isDir" }, { ExpandedRole, "expanded" } };
}

void Workspace::toggle(int row)
{
    if (row < 0 || row >= m_rows.size() || !m_rows.at(row).isDir)
        return;
    const QString path = m_rows.at(row).path;
    if (!m_expanded.remove(path))
        m_expanded.insert(path);
    rebuild();
}

void Workspace::refresh()
{
    rebuild();
}

// Folders first, then files, each alphabetical; only into expanded folders.
void Workspace::list(const QString &dir, int depth, QVector<Entry> &out, QStringList &dirs) const
{
    const QString absolute = dir.isEmpty() ? m_root : QDir(m_root).filePath(dir);
    dirs << absolute;
    const QFileInfoList entries = QDir(absolute).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fi : entries) {
        if (isIgnored(fi.fileName(), fi.isDir()))
            continue;
        Entry e;
        e.name = fi.fileName();
        e.path = dir.isEmpty() ? e.name : dir + QLatin1Char('/') + e.name;
        e.depth = depth;
        e.isDir = fi.isDir();
        out << e;
        if (e.isDir && m_expanded.contains(e.path))
            list(e.path, depth + 1, out, dirs);
    }
}

void Workspace::rebuild()
{
    QVector<Entry> rows;
    QStringList dirs;
    if (!m_root.isEmpty())
        list(QString(), 0, rows, dirs);

    beginResetModel();
    m_rows = rows;
    endResetModel();

    // Watch the shown folders (for new and deleted files) and the shown files (for edits).
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    QStringList files;
    for (const Entry &e : m_rows)
        if (!e.isDir)
            files << QDir(m_root).filePath(e.path);
    if (!dirs.isEmpty())
        m_watcher.addPaths(dirs);
    if (!files.isEmpty())
        m_watcher.addPaths(files);
    emit treeChanged();
}

QString Workspace::absolutePath(const QString &path) const
{
    return m_root.isEmpty() ? QString() : QDir(m_root).filePath(path);
}

QString Workspace::relativePath(const QString &absolute) const
{
    if (m_root.isEmpty())
        return QString();
    const QString clean = QDir::cleanPath(absolute);
    if (!clean.startsWith(m_root + QLatin1Char('/')))
        return QString();
    return clean.mid(m_root.size() + 1);
}

bool Workspace::exists(const QString &path) const
{
    return !m_root.isEmpty() && QFileInfo(absolutePath(path)).isFile();
}

QString Workspace::read(const QString &path)
{
    QFile file(absolutePath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        setError(tr("Couldn't open %1: %2").arg(path, file.errorString()));
        return QString();
    }
    setError(QString());
    return QString::fromUtf8(file.readAll());
}

bool Workspace::write(const QString &path, const QString &text)
{
    QFile file(absolutePath(path));
    m_justWritten.insert(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0) {
        m_justWritten.remove(path);
        setError(tr("Couldn't save %1: %2").arg(path, file.errorString()));
        return false;
    }
    setError(QString());
    return true;
}

void Workspace::setError(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}
