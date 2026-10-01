#ifndef SCHEMADESIGN_H
#define SCHEMADESIGN_H

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <qivot.hpp>

#include "databasesession.h"

/// One table of a design: the table as it would be, and where it came from.
struct DesignTable {
    QiTableInfo info;
    QString     origin;          ///< its name in the database; empty for a new table
    QStringList columnOrigins;   ///< parallel to info.columns; "" for a new column
};

/// The SQL that turns one schema into another.
/**
  Tables and columns are matched by their origin, so a rename is a rename
  (not a drop and a create, which would lose the rows). Statements are in
  the database's own dialect; SQLite, which can't alter a column, gets the
  usual rebuild: create the new table, copy the rows, drop the old one,
  rename the new one into place.
 */
namespace Migration {

/// The script (with comments), or "" when the design matches the database.
QString sql(const QVector<QiTableInfo> &original, const QVector<DesignTable> &design, const QString &dialect);

/// The changes in words, one per line: "Add column book.subtitle", ...
QStringList changes(const QVector<QiTableInfo> &original, const QVector<DesignTable> &design);

/// The design's tables that exist in the database but differ from it.
QStringList changedTables(const QVector<QiTableInfo> &original, const QVector<DesignTable> &design);

/// CREATE TABLE for one table in `dialect`.
QString createTable(const QiTableInfo &table, const QString &dialect);

/// An identifier as `dialect` needs it: bare when it can be, quoted when not.
QString quoted(const QString &identifier, const QString &dialect);

}

/// A schema being designed: starts as the open database's tables, edited
/// from the designer, and turned into Qivot models (C++) and a migration (SQL).
/**
  Every edit can be undone. QML gets plain maps and lists, and `changed()`
  after each edit; the diagram, the C++ and the SQL are read back on demand.

\code
    Design { id: design; session: db }
    design.addColumn("book", "subtitle", "TEXT")
    design.migration     // ALTER TABLE book ADD COLUMN subtitle TEXT;
\endcode
 */
class SchemaDesign : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Design)
    Q_PROPERTY(DatabaseSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QString dialect READ dialect NOTIFY designChanged)
    /// `[{ name, isNew, columns }]`, in the design's order.
    Q_PROPERTY(QVariantList tables READ tables NOTIFY designChanged)
    Q_PROPERTY(QStringList changes READ changes NOTIFY designChanged)
    Q_PROPERTY(QString migration READ migration NOTIFY designChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY designChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY designChanged)
    /// Types to offer for a column, in the database's dialect.
    Q_PROPERTY(QStringList typeSuggestions READ typeSuggestions NOTIFY designChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    /// What would go wrong: `[{ table, column, severity: "error"|"warning", message }]`,
    /// errors first. Checks the design itself and, against the database's data,
    /// what the migration would run into (NULLs, duplicates, orphaned keys, ...).
    Q_PROPERTY(QVariantList problems READ problems NOTIFY problemsChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY problemsChanged)
    /// True when the design was picked up from where it was left (autosave).
    Q_PROPERTY(bool restored READ restored NOTIFY designChanged)
    /// Keep edits for the open database between runs, and pick them up again
    /// on open (on by default; demos and tests turn it off).
    Q_PROPERTY(bool autosave READ autosaveEnabled WRITE setAutosaveEnabled NOTIFY autosaveChanged)
    /// The file it was last saved to or opened from ("" if none).
    Q_PROPERTY(QString filePath READ filePath NOTIFY designChanged)

public:
    explicit SchemaDesign(QObject *parent = nullptr);

    DatabaseSession *session() const { return m_session; }
    void setSession(DatabaseSession *session);
    QString dialect() const { return m_dialect; }
    QVariantList tables() const;
    QStringList changes() const { return Migration::changes(m_original, m_tables); }
    QString migration() const { return Migration::sql(m_original, m_tables, m_dialect); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QStringList typeSuggestions() const;
    QString error() const { return m_error; }
    QVariantList problems() const { return m_problems; }
    bool restored() const { return m_restored; }
    bool autosaveEnabled() const { return m_autosave; }
    void setAutosaveEnabled(bool on);
    QString filePath() const { return m_filePath; }

    /// Save to a .qivotdesign file (a `file:` URL or a path). The file holds
    /// the design and the schema it was made against.
    Q_INVOKABLE bool save(const QVariant &fileOrUrl);
    /// Open a .qivotdesign file. Its migration compares against the schema
    /// saved in it; `error` says so if the open database has changed since.
    Q_INVOKABLE bool load(const QVariant &fileOrUrl);

    /// The whole design as JSON (what save() writes), and back.
    QByteArray toJson() const;
    bool fromJson(const QByteArray &json, QString *why = nullptr);

    /// Where edits are saved automatically for the open database ("" if none).
    QString autosavePath() const;
    int errorCount() const;
    /// The problems for one table.
    Q_INVOKABLE QVariantList problemsFor(const QString &table) const;

    /// Start over from `original` (what the database has) in `dialect`.
    void reset(const QVector<QiTableInfo> &original, const QString &dialect);
    /// Start over from the session's database (keeps no autosave).
    Q_INVOKABLE void reset();
    /// Start over and forget the autosaved design too.
    Q_INVOKABLE void discard();

    /// One table for the inspector: `{ name, isNew, origin, columns: [{ name,
    /// type, nullable, primaryKey, autoIncrement, unique, defaultValue,
    /// reference, onDelete, isNew }], referencedBy: [names] }`.
    Q_INVOKABLE QVariantMap table(const QString &name) const;

    /// Add a table with an `id` key; returns its (unique) name.
    Q_INVOKABLE QString addTable(const QString &name = QString());
    Q_INVOKABLE bool renameTable(const QString &name, const QString &newName);
    /// Remove a table and every reference to it.
    Q_INVOKABLE void removeTable(const QString &name);

    /// Add a column; returns its (unique) name.
    Q_INVOKABLE QString addColumn(const QString &table, const QString &name = QString(),
                                  const QString &type = QString());
    /// Change a column: any of `{ name, type, nullable, primaryKey, unique, defaultValue }`.
    Q_INVOKABLE bool updateColumn(const QString &table, int column, const QVariantMap &changes);
    Q_INVOKABLE void removeColumn(const QString &table, int column);
    Q_INVOKABLE void moveColumn(const QString &table, int from, int to);
    /// Make `column` reference `refTable`'s key ("" removes the reference).
    Q_INVOKABLE bool setReference(const QString &table, const QString &column, const QString &refTable,
                                  const QString &onDelete = QString());

    /// Give `table` a new column that references `refTable`'s key, typed to
    /// hold it, and named after it ("category_id"; the key's own name when it
    /// already says which table, e.g. a key called category_id). Returns the
    /// column's name, or "" if `refTable` has no single-column key.
    Q_INVOKABLE QString addReference(const QString &table, const QString &refTable,
                                     const QString &onDelete = QString());
    /// What happens to `column`'s rows when the row they reference is deleted:
    /// "" (no action), "CASCADE", "SET NULL", "RESTRICT".
    Q_INVOKABLE bool setOnDelete(const QString &table, const QString &column, const QString &onDelete);

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    /// The diagram of the design (see DiagramData).
    Q_INVOKABLE QVariantMap diagram() const;
    /// The Qivot model for one table: `{ className, code, warnings }` (see DatabaseSession::cppModel).
    Q_INVOKABLE QVariantMap cppModel(const QString &table) const;
    Q_INVOKABLE QString cppHeader() const;

    /// SQLite: copy the database file, run the migration on the copy, and
    /// return the copy's path ("" and `error` set on failure). The original
    /// is never touched.
    Q_INVOKABLE QString applyToCopy();

    /// The design as QiSchema would read it back.
    QVector<QiTableInfo> infos() const;
    const QVector<DesignTable> &designTables() const { return m_tables; }

signals:
    void sessionChanged();
    void designChanged();
    void errorChanged();
    void problemsChanged();
    void autosaveChanged();

private:
    DesignTable *find(const QString &name);
    const DesignTable *find(const QString &name) const;
    QString uniqueTableName(const QString &base) const;
    void edit();                       // remember the state before an edit
    void setError(const QString &error);
    void validate();                   // recompute m_problems
    void autosave();                   // after every edit
    void openSession();                // the session opened a database: reset, then restore
    bool restoreAutosave();            // after reset() from the session
    qint64 scalar(const QString &sql) const;   // -1 if it can't be asked

    QPointer<DatabaseSession> m_session;
    QString                   m_dialect = QStringLiteral("sqlite");
    QVector<QiTableInfo>      m_original;
    QVector<DesignTable>      m_tables;
    QVector<QVector<DesignTable>> m_undo, m_redo;
    QString                   m_error;
    QVariantList              m_problems;
    bool                      m_restored = false;
    QString                   m_filePath;
    bool                      m_loading = false;
    bool                      m_autosave = true;
};

#endif // SCHEMADESIGN_H
