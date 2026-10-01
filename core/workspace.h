#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QQmlEngine>
#include <QSet>
#include <QTimer>

/// A project folder, as the IDE's explorer and editor see it.
/**
  The folder tree is a flat list model (one row per visible file or folder,
  with a depth), so the explorer is a plain ListView on Qt 5 and Qt 6 alike.
  Folders expand and collapse; the tree follows changes on disk. Build
  output, version control and hidden files are left out.

  Paths in and out are relative to `root` ("src/models.h").

\code
    Workspace { id: ws; root: "/Users/me/Projects/Bookshop" }
    ListView { model: ws; delegate: Text { text: "  ".repeat(depth) + name } }
    editor.text = ws.read("src/models.h")
\endcode
 */
class Workspace : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString root READ root WRITE setRoot NOTIFY rootChanged)
    Q_PROPERTY(QString name READ name NOTIFY rootChanged)
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY rootChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY treeChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole, DepthRole, IsDirRole, ExpandedRole };

    explicit Workspace(QObject *parent = nullptr);

    QString root() const { return m_root; }
    void setRoot(const QString &root);
    QString name() const;
    bool isOpen() const { return !m_root.isEmpty(); }
    int fileCount() const;
    QString error() const { return m_error; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Expand or collapse the folder on `row`.
    Q_INVOKABLE void toggle(int row);
    /// Re-read the folder tree (it also follows the disk by itself).
    Q_INVOKABLE void refresh();

    /// A file's text; empty (and `error` set) if it can't be read.
    Q_INVOKABLE QString read(const QString &path);
    /// Replace a file's text. False (and `error` set) if it can't be written.
    Q_INVOKABLE bool write(const QString &path, const QString &text);
    Q_INVOKABLE bool exists(const QString &path) const;
    Q_INVOKABLE QString absolutePath(const QString &path) const;
    /// `path` relative to root, or "" if it is outside the project.
    Q_INVOKABLE QString relativePath(const QString &absolute) const;

    /// Folders and files the explorer leaves out.
    static bool isIgnored(const QString &name, bool isDir);

signals:
    void rootChanged();
    void treeChanged();
    void errorChanged();
    /// A file changed on disk (not through write()).
    void fileChanged(const QString &path);

private:
    struct Entry {
        QString name;
        QString path;       // relative
        int     depth = 0;
        bool    isDir = false;
    };
    void rebuild();
    void list(const QString &dir, int depth, QVector<Entry> &out, QStringList &dirs) const;
    void setError(const QString &error);

    QString            m_root;
    QVector<Entry>     m_rows;
    QSet<QString>      m_expanded;   // relative folder paths
    QFileSystemWatcher m_watcher;
    QTimer             m_settle;     // coalesce bursts of disk changes
    QSet<QString>      m_justWritten;
    QString            m_error;
};

#endif // WORKSPACE_H
