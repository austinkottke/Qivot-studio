#ifndef QUERYREPLAY_H
#define QUERYREPLAY_H

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

class DatabaseSession;

/// Replaying an app's recorded queries against a database before and after its
/// pending migrations: what breaks, what returns different rows, what slows down.
/**
  The recording comes from Qivot's QiRecorder (`QIVOT_RECORD=app.qrec`). Every
  query in it runs twice: on the database as it is, and on the database with
  the migrations in `dir` that haven't run yet. Nothing is changed:

  - SQLite: both runs are on copies (`VACUUM INTO`); the migrations really run
    on the second copy, with QiMigrator.
  - PostgreSQL and SQL Server (`sandbox`): one transaction that is never
    committed. The migrations' SQL runs inside it, and each query inside a
    savepoint. Schema changes lock the tables they touch until the end, so
    this belongs on a staging server or a restored copy, not production.
  - MySQL commits schema changes at once and DuckDB files are read-only here:
    neither can be replayed yet.

  Writes (INSERT, UPDATE, DELETE) run too, each rolled back as soon as it's
  timed. Other statements in the recording (CREATE, PRAGMA, …) are skipped.

\code
    QueryReplay::Options o;
    o.dir = "migrations";
    const QueryReplay::Report r = QueryReplay::run(session, "app.qrec", o);
    for (const QueryReplay::Item &i : r.items)
        if (i.verdict == "breaks") qDebug() << i.sql << i.after.error;
\endcode
 */
namespace QueryReplay {

struct Options {
    QString dir;                                      ///< the migrations folder
    QString history = QStringLiteral("qivot_migrations");
    bool    sandbox = false;                          ///< allow the transaction sandbox on a server
    double  slower = 2.0;                             ///< "slower" at this many times the time…
    double  minMs = 1.0;                              ///< …and at least this many ms more
    int     repeat = 3;                               ///< runs per sample; the fastest counts
    int     samples = 3;                              ///< recorded value sets per query to replay
    bool    plans = true;                             ///< compare plans (SQLite)
};

/// One query on one side.
struct Side {
    bool    ok = false;
    QString error;
    qint64  rows = -1;        ///< rows returned (SELECT) or changed (writes)
    quint64 digest = 0;       ///< of the rows returned, in any order
    QStringList columns;      ///< the result's column names (SELECT)
    double  ms = -1;          ///< the fastest run, averaged over the samples
    QString plan;             ///< the plan's steps, one per line ("" if not asked)
    int     scans = 0;        ///< steps that read a whole table
};

struct Item {
    QString sql;
    qint64  runs = 0;         ///< how often the app ran it
    int     replayed = 0;     ///< value sets replayed
    Side    before, after;
    /// "breaks", "columns" (runs, but its result's columns changed: a model's
    /// field no longer gets its value), "different", "slower", "faster",
    /// "fixed", "failing" or "same".
    QString verdict;
    QStringList columnsGone, columnsNew;   ///< for "columns"
    bool    planChanged = false;
};

struct Report {
    bool        ok = false;   ///< the replay ran (not: nothing broke)
    QString     error;
    QString     database, dialect;
    QStringList migrations;   ///< "0004 rename title", in order
    QString     migrationError;   ///< the migrations themselves failed (then nothing is replayed)
    qint64      recordedRuns = 0;
    int         skipped = 0;  ///< statements in the recording that aren't queries
    /// Queries recorded on another kind of database ("QSQLITE": 12), whose SQL may not run here.
    QMap<QString, int> otherDrivers;
    QVector<Item> items;      ///< worst first

    int count(const QString &verdict) const;
};

/// Replay `recording` against `session` before and after the migrations in `options.dir`.
Report run(DatabaseSession &session, const QString &recording, const Options &options);

/// Whether a recorded statement is a query or write to replay (not DDL, PRAGMA, …).
bool replayable(const QString &sql);

} // namespace QueryReplay

#endif // QUERYREPLAY_H
