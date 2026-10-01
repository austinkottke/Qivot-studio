#ifndef PROJECTEXPORT_H
#define PROJECTEXPORT_H

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QStringList>

#include "databasesession.h"   // QPointer<DatabaseSession> needs the full type

/// A buildable C++ project for the open database: Qivot models for every
/// table, an example program, and a test per model that loads all its rows.
/**
  The files are generated in memory as soon as `session` and `name` are set,
  so the UI can preview them; write() puts them on disk at `projectPath`.

  The project builds with Qt 6 or Qt 5.15 and gets Qivot from GitHub, or from
  a local checkout with `-DQIVOT_SOURCE_DIR=…`. Connection details come from
  the session; the password is never written — it is read from
  $QIVOT_DB_PASSWORD at run time.

\code
    ProjectExport { id: project; session: db; name: "Bookshop"; directory: "/Users/me/Projects" }
    // project.files -> ["CMakeLists.txt", "README.md", "src/database.h", ...]
    // project.content("src/models.h"), project.write()
\endcode
 */
class ProjectExport : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(DatabaseSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY directoryChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY pathChanged)
    /// projectPath with the home folder shown as ~.
    Q_PROPERTY(QString displayPath READ displayPath NOTIFY pathChanged)
    Q_PROPERTY(QStringList files READ files NOTIFY filesChanged)
    Q_PROPERTY(bool exists READ exists NOTIFY pathChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString suggestedName READ suggestedName NOTIFY sessionChanged)
    Q_PROPERTY(QString defaultDirectory READ defaultDirectory CONSTANT)

public:
    explicit ProjectExport(QObject *parent = nullptr);

    DatabaseSession *session() const { return m_session; }
    void setSession(DatabaseSession *session);
    QString name() const { return m_name; }
    void setName(const QString &name);
    QString directory() const { return m_directory; }
    void setDirectory(const QString &directory);
    /// directory/name.
    QString projectPath() const;
    QString displayPath() const;
    QStringList files() const { return m_files.keys(); }
    /// True once a project has been written at projectPath (it has a CMakeLists.txt).
    bool exists() const;
    QString error() const { return m_error; }
    /// A project name from the database's name: "bookshop.db" -> "Bookshop".
    QString suggestedName() const;
    /// ~/Documents/Qivot Projects
    static QString defaultDirectory();

    /// The text of one generated file.
    Q_INVOKABLE QString content(const QString &file) const { return m_files.value(file); }

    /// Write every file under projectPath, creating folders. Existing files
    /// are replaced; anything else in the folder is left alone.
    Q_INVOKABLE bool write();

    /// Write just one of the files (e.g. "src/models.h" after the schema changed).
    Q_INVOKABLE bool writeFile(const QString &file);

    /// The files for a session, by relative path. What the properties wrap;
    /// public so the tests can check the output directly.
    static QMap<QString, QString> generate(const DatabaseSession &session, const QString &name);

signals:
    void sessionChanged();
    void nameChanged();
    void directoryChanged();
    void pathChanged();
    void filesChanged();
    void errorChanged();
    void written();

private:
    void regenerate();
    void setError(const QString &error);

    QPointer<DatabaseSession> m_session;
    QString                   m_name;
    QString                   m_directory;
    QMap<QString, QString>    m_files;
    QString                   m_error;
};

#endif // PROJECTEXPORT_H
