#ifndef SQLSCRIPT_H
#define SQLSCRIPT_H

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

/// Running a SQL script one statement at a time (QSqlQuery runs one).
namespace SqlScript {

/// The statements of `script`, split at the semicolons that end them — not
/// at ones inside 'strings', "identifiers", [brackets], `backticks`, or
/// comments (which are dropped). A line holding only GO (SQL Server's batch
/// separator) also ends a statement.
QStringList split(const QString &script);

/// The outcome of run(): how many statements succeeded, and the one that failed.
struct Result {
    bool ok = true;
    int done = 0;
    QString failedStatement;
    QString error;
};

/// Run `statements` on `db`, stopping at the first that fails. With
/// `transaction`, they run inside one (BEGIN/COMMIT statements in the script
/// are then skipped) and a failure rolls everything back.
Result run(QSqlDatabase db, const QStringList &statements, bool transaction);

}

#endif // SQLSCRIPT_H
