#ifndef SCHEMACOMPARE_H
#define SCHEMACOMPARE_H

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QStringList>

#include "databasesession.h"

/// Comparing the open database's structure with another database's, and the
/// migration that would make one match the other.
/**
  The other database is a second session (`other`): open a file, a sample or
  a server in it. Tables and columns are matched by name; the differences come
  out as the designer words them ("Add column book.subtitle", "Drop table
  tag") and the SQL is in the dialect of the database being changed.

\code
    SchemaCompare { id: compare; session: db }
    compare.other.open(fileUrl)
    compare.direction = "toOther"     // make the other one match this one
    compare.changes                   // ["Create table tag", ...]
    compare.migration                 // the SQL, for the other database
\endcode
 */
class SchemaCompare : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(DatabaseSession *session READ session WRITE setSession NOTIFY sessionChanged)
    /// The database to compare with: open something in it.
    Q_PROPERTY(DatabaseSession *other READ other CONSTANT)
    /// "toOther": make the other database match this one (default);
    /// "toThis": make this one match the other.
    Q_PROPERTY(QString direction READ direction WRITE setDirection NOTIFY changed)
    /// The differences, in words, as changes to the database being changed.
    Q_PROPERTY(QStringList changes READ changes NOTIFY changed)
    /// The SQL that makes them, in that database's dialect ("" when they match).
    Q_PROPERTY(QString migration READ migration NOTIFY changed)
    /// Whether both databases are the same kind (types compare as written, so
    /// across kinds most columns differ in type).
    Q_PROPERTY(bool sameDialect READ sameDialect NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit SchemaCompare(QObject *parent = nullptr);

    DatabaseSession *session() const { return m_session; }
    void setSession(DatabaseSession *session);
    DatabaseSession *other() const { return m_other; }
    QString direction() const { return m_direction; }
    void setDirection(const QString &direction);
    QStringList changes() const { return m_changes; }
    QString migration() const { return m_migration; }
    bool sameDialect() const;
    QString error() const { return m_error; }

    /// Write the migration to a .sql file. False (with error()) if it can't.
    Q_INVOKABLE bool saveMigration(const QVariant &fileOrUrl);

    /// Run the migration on this database (direction "toThis"), once changes
    /// are allowed: backed up first if it's a file, in one transaction.
    /// `{ ok, error, failedStatement, backup }`.
    Q_INVOKABLE QVariantMap applyToThis();

signals:
    void sessionChanged();
    void changed();

private:
    void compute();

    QPointer<DatabaseSession> m_session;
    DatabaseSession *m_other;
    QString m_direction = QStringLiteral("toOther");
    QStringList m_changes;
    QString m_migration;
    QString m_error;
};

#endif // SCHEMACOMPARE_H
