#ifndef SAMPLEDATABASE_H
#define SAMPLEDATABASE_H

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

class SampleSchema;

/// The sample databases: realistic schemas to explore (and to test against),
/// each generated deterministically so every run (and screenshot) is the same,
/// and each available on SQLite, PostgreSQL, MySQL and SQL Server.
///
///  - bookshop:   books, orders and reviews: single and composite keys,
///                delete rules, a view and a full-text table (SQLite)
///  - university: a circular reference (departments and their chairs), a
///                many-to-many of courses to themselves (prerequisites) and a
///                three-column composite reference (enrollments to sections)
///  - company:    an org chart (employees managing employees), a text natural
///                key (countries), and salary and job history keyed by date
///  - music:      a record store: artists, albums and tracks, playlists
///                (many-to-many), customers, invoices and their support reps
namespace SampleDatabase {

struct Info {
    QString id, title, summary;
    QStringList highlights;
    QStringList tables;               // what it holds (tst_samples keeps these honest)
    int rows = 0;
};
QList<Info> catalogue();

/// The sample `id` described for every database (nullptr-safe: an unknown id
/// gives an empty schema). Built fresh on each call.
SampleSchema build(const QString &id);
/// Bumped when a sample changes, so a cached copy gets rebuilt.
int version(const QString &id);

/// Write sample `id` to the SQLite file `path`, replacing any file there.
/// On failure returns false and, if `error` is given, says why.
bool createFile(const QString &id, const QString &path, QString *error = nullptr);
/// The bookshop, to `path`.
inline bool create(const QString &path, QString *error = nullptr) { return createFile(QStringLiteral("bookshop"), path, error); }
/// Fill an open, empty server database (dialect "postgres", "mysql" or "sqlserver").
bool load(const QString &id, QSqlDatabase db, const QString &dialect, QString *error = nullptr);

}

#endif // SAMPLEDATABASE_H
