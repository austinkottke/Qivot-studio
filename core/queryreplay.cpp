#include "queryreplay.h"
#include "databasesession.h"
#include "queryplan.h"
#include <qivot.hpp>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <algorithm>
#include <memory>

namespace QueryReplay {
namespace {

int g_connections = 0;

QString nextName()
{
    return QStringLiteral("query-replay-%1").arg(++g_connections);
}

// The statement's first word, past comments and space.
QString firstWord(const QString &sql)
{
    static const QRegularExpression lead(QStringLiteral("^(?:\\s+|--[^\\n]*\\n?|/\\*.*?\\*/)*(\\w+)"),
                                         QRegularExpression::DotMatchesEverythingOption);
    return lead.match(sql).captured(1).toUpper();
}

bool isRead(const QString &sql)
{
    const QString w = firstWord(sql);
    return w == QLatin1String("SELECT") || w == QLatin1String("WITH") || w == QLatin1String("VALUES");
}

// Transaction control in a migration file: the sandbox is the transaction.
bool isControl(const QString &statement)
{
    static const QRegularExpression control(
        QStringLiteral("^(BEGIN( (TRANSACTION|TRAN|WORK|DEFERRED|IMMEDIATE|EXCLUSIVE))?|START TRANSACTION|"
                       "COMMIT( (TRANSACTION|TRAN|WORK))?|END( TRANSACTION)?|PRAGMA FOREIGN_KEYS ?= ?(ON|OFF))$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression comments(QStringLiteral("--[^\\n]*|/\\*.*?\\*/"),
                                             QRegularExpression::DotMatchesEverythingOption);
    return control.match(QString(statement).remove(comments).simplified()).hasMatch();
}

/// Where queries run: a connection, and how each one is undone afterwards.
struct Runner {
    QSqlDatabase db;
    enum Undo { Transaction, PgSavepoint, MsSavepoint } undo = Transaction;

    bool begin()
    {
        if (undo == Transaction)
            return db.transaction();
        return QSqlQuery(db).exec(undo == PgSavepoint ? QStringLiteral("SAVEPOINT qivot_replay")
                                                      : QStringLiteral("SAVE TRANSACTION qivot_replay"));
    }
    void undoIt()
    {
        if (undo == Transaction) {
            db.rollback();
        } else if (undo == PgSavepoint) {
            QSqlQuery(db).exec(QStringLiteral("ROLLBACK TO SAVEPOINT qivot_replay"));
            QSqlQuery(db).exec(QStringLiteral("RELEASE SAVEPOINT qivot_replay"));
        } else {
            QSqlQuery(db).exec(QStringLiteral("ROLLBACK TRANSACTION qivot_replay"));
        }
    }

    // One run of one value set: rows, digest and time. False (with error) if it fails.
    bool once(const QString &sql, const QVariantList &values, qint64 &rows, quint64 &digest, double &ms, QString &error,
              QStringList *columnNames = nullptr)
    {
        if (!begin()) {
            error = QStringLiteral("couldn't start: %1").arg(db.lastError().text().trimmed());
            return false;
        }
        bool ok = false;
        {
            QSqlQuery q(db);
            q.setForwardOnly(true);
            QElapsedTimer timer;
            timer.start();
            if (q.prepare(sql)) {
                for (const QVariant &v : values)
                    q.addBindValue(v);
                ok = q.exec();
            }
            if (ok) {
                rows = 0;
                digest = 0;
                if (q.isSelect()) {
                    const QSqlRecord record = q.record();
                    const int columns = record.count();
                    if (columnNames) {
                        columnNames->clear();
                        for (int c = 0; c < columns; ++c)
                            *columnNames << record.fieldName(c);
                    }
                    while (q.next()) {
                        QString row;
                        for (int c = 0; c < columns; ++c)
                            row += (q.value(c).isNull() ? QStringLiteral("\x1e") : q.value(c).toString()) + QChar(0x1f);
                        const quint64 h = quint64(qHash(row));
                        digest += h * 0x9E3779B97F4A7C15ULL + (h >> 13);   // a sum: the rows' order doesn't matter
                        if (++rows >= 200000)
                            break;
                    }
                } else {
                    rows = q.numRowsAffected();
                }
                ms = timer.nsecsElapsed() / 1.0e6;
            } else {
                error = q.lastError().text().trimmed();
            }
        }
        undoIt();
        return ok;
    }

    Side run(const QiRecorder::Query &query, const Options &o)
    {
        Side side;
        side.ok = true;
        side.rows = 0;
        double total = 0;
        int counted = 0;
        const int samples = qMax(1, qMin(o.samples, int(query.samples.size())));
        for (int s = 0; s < samples; ++s) {
            const QVariantList values = query.samples.isEmpty() ? QVariantList() : query.samples.at(s).values;
            double best = -1;
            qint64 rows = -1;
            quint64 digest = 0;
            for (int r = 0; r < qMax(1, o.repeat); ++r) {
                double ms = -1;
                QString error;
                if (!once(query.sql, values, rows, digest, ms, error, s == 0 && r == 0 ? &side.columns : nullptr)) {
                    side.ok = false;
                    side.error = error;
                    side.rows = -1;
                    return side;
                }
                best = best < 0 ? ms : qMin(best, ms);
            }
            side.rows += rows;
            side.digest += digest * quint64(2 * s + 1);
            total += best;
            ++counted;
        }
        side.ms = counted ? total / counted : -1;
        return side;
    }
};

// SQLite's plan for `sql` with the recorded values bound (an unbound ? is refused).
void explainSqlite(QSqlDatabase db, Side &side, const QString &sql, const QVariantList &values)
{
    QSqlQuery q(db);
    if (!q.prepare(QStringLiteral("EXPLAIN QUERY PLAN ") + sql))
        return;
    for (const QVariant &v : values)
        q.addBindValue(v);
    if (!q.exec())
        return;
    QList<QVariantList> rows;
    while (q.next())
        rows << QVariantList{ q.value(0), q.value(1), q.value(2), q.value(3) };
    QStringList steps;
    side.scans = 0;
    for (const QVariant &n : QueryPlan::fromSqlite(rows)) {
        const QVariantMap m = n.toMap();
        const QString detail = m.value(QStringLiteral("detail")).toString();
        steps << QString(m.value(QStringLiteral("depth")).toInt() * 2, QLatin1Char(' '))
                     + m.value(QStringLiteral("label")).toString()
                     + (detail.isEmpty() ? QString() : QStringLiteral(" ") + detail);
        side.scans += m.value(QStringLiteral("scan")).toBool();
    }
    side.plan = steps.join(QLatin1Char('\n'));
}

QString verdictOf(const Item &i, const Options &o)
{
    if (i.before.ok && !i.after.ok) return QStringLiteral("breaks");
    if (!i.before.ok && i.after.ok) return QStringLiteral("fixed");
    if (!i.before.ok) return QStringLiteral("failing");
    if (i.before.columns != i.after.columns) return QStringLiteral("columns");
    if (i.before.rows != i.after.rows || i.before.digest != i.after.digest) return QStringLiteral("different");
    if (i.after.ms > i.before.ms * o.slower && i.after.ms - i.before.ms >= o.minMs) return QStringLiteral("slower");
    if (i.before.ms > i.after.ms * o.slower && i.before.ms - i.after.ms >= o.minMs) return QStringLiteral("faster");
    return QStringLiteral("same");
}

int rank(const QString &verdict)
{
    static const QStringList order = { "breaks", "columns", "different", "slower", "failing", "fixed", "faster", "same" };
    const int i = order.indexOf(verdict);
    return i < 0 ? order.size() : i;
}

// The migrations still to run on `db`, read with QiMigrator.
bool pendingMigrations(QSqlDatabase db, const Options &o, QVector<QiMigrator::Migration> &out, QString &error)
{
    QiConnection conn;
    if (!conn.open(db, false)) {
        error = QStringLiteral("Qivot can't use this database");
        return false;
    }
    QiMigrator m(conn);
    m.setUseUserVersion(false);   // user_version may mean anything here
    m.setTable(o.history);
    if (m.addDirectory(o.dir) < 0) {
        error = m.lastError();
        return false;
    }
    if (!m.changed().isEmpty()) {
        error = QStringLiteral("migration %1 was changed after it ran; settle that first (migrate accept)")
                    .arg(m.changed().first().version);
        return false;
    }
    out = m.pending();
    return true;
}

QString label(const QiMigrator::Migration &m)
{
    return QStringLiteral("%1 %2").arg(m.version, 4, 10, QLatin1Char('0')).arg(m.name);
}

} // namespace

int Report::count(const QString &verdict) const
{
    int n = 0;
    for (const Item &i : items)
        n += i.verdict == verdict;
    return n;
}

bool replayable(const QString &sql)
{
    static const QStringList words = { "SELECT", "WITH", "VALUES", "INSERT", "UPDATE", "DELETE", "REPLACE", "MERGE" };
    return words.contains(firstWord(sql));
}

Report run(DatabaseSession &session, const QString &recording, const Options &o)
{
    Report report;
    report.database = session.displayName();
    report.dialect = session.dialect();

    QString error;
    const QVector<QiRecorder::Query> recorded = QiRecorder::read(recording, &error);
    if (!error.isEmpty()) { report.error = error; return report; }

    const QString dialect = session.dialect();
    const QString driverHere = dialect == QLatin1String("sqlite") ? QStringLiteral("QSQLITE")
                             : dialect == QLatin1String("postgres") ? QStringLiteral("QPSQL")
                             : dialect == QLatin1String("mysql") ? QStringLiteral("QMYSQL")
                             : dialect == QLatin1String("sqlserver") ? QStringLiteral("QODBC") : QString();
    QVector<QiRecorder::Query> queries;
    for (const QiRecorder::Query &q : recorded) {
        report.recordedRuns += q.runs;
        if (!replayable(q.sql)) {
            ++report.skipped;
            continue;
        }
        queries << q;
        if (!q.driver.isEmpty() && !driverHere.isEmpty() && q.driver != driverHere)
            ++report.otherDrivers[q.driver];
    }

    const bool sqlite = dialect == QLatin1String("sqlite");
    const bool server = dialect == QLatin1String("postgres") || dialect == QLatin1String("sqlserver");
    if (!sqlite && !server) {
        report.error = dialect == QLatin1String("mysql")
            ? QStringLiteral("MySQL commits schema changes as it goes, so there's no sandbox to replay in yet")
            : QStringLiteral("replaying on %1 isn't supported yet").arg(session.dialectName());
        return report;
    }
    if (server && !o.sandbox) {
        report.error = QStringLiteral(
            "On a server the replay runs inside a transaction that is never committed, and the migrations' "
            "schema changes lock their tables until it ends. Run it on a staging server or a restored copy, "
            "and pass --sandbox to say so.");
        return report;
    }

    QVector<QiMigrator::Migration> pending;
    if (!pendingMigrations(QSqlDatabase::database(session.connectionName()), o, pending, error)) {
        report.error = error;
        return report;
    }
    for (const QiMigrator::Migration &m : pending)
        report.migrations << label(m);
    if (pending.isEmpty()) {
        report.error = QStringLiteral("no pending migrations in %1: nothing to compare").arg(o.dir);
        return report;
    }

    QVector<Item> items(queries.size());
    for (int i = 0; i < queries.size(); ++i) {
        items[i].sql = queries.at(i).sql;
        items[i].runs = queries.at(i).runs;
        items[i].replayed = qMax(1, qMin(o.samples, int(queries.at(i).samples.size())));
    }

    if (sqlite) {
        // Two copies: as it is, and migrated.
        QTemporaryDir dir;
        if (!dir.isValid()) { report.error = QStringLiteral("can't make a scratch folder"); return report; }
        const QString beforePath = dir.filePath(QStringLiteral("before.db"));
        const QString afterPath = dir.filePath(QStringLiteral("after.db"));
        for (const QString &path : { beforePath, afterPath }) {
            QSqlQuery copy(QSqlDatabase::database(session.connectionName()));
            if (!copy.exec(QStringLiteral("VACUUM INTO '%1'").arg(QString(path).replace(QLatin1Char('\''), QLatin1String("''"))))) {
                report.error = QStringLiteral("couldn't copy the database: %1").arg(copy.lastError().text().trimmed());
                return report;
            }
        }

        const QString beforeName = nextName(), afterName = nextName();
        {
            QSqlDatabase before = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), beforeName);
            before.setDatabaseName(beforePath);
            QSqlDatabase after = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), afterName);
            after.setDatabaseName(afterPath);
            if (!before.open() || !after.open()) {
                report.error = QStringLiteral("couldn't open the copies");
            } else {
                {
                    QiConnection conn;
                    if (conn.open(after, false)) {
                        QiMigrator m(conn);
                        m.setUseUserVersion(false);   // user_version may mean anything here
                        m.setTable(o.history);
                        m.addDirectory(o.dir);
                        if (m.migrate() < 0)
                            report.migrationError = m.lastError();
                    }
                }
                if (report.migrationError.isEmpty()) {
                    // As Qivot runs them: foreign keys enforced.
                    QSqlQuery(before).exec(QStringLiteral("PRAGMA foreign_keys = ON"));
                    QSqlQuery(after).exec(QStringLiteral("PRAGMA foreign_keys = ON"));
                    Runner b{ before }, a{ after };
                    for (int i = 0; i < queries.size(); ++i) {
                        items[i].before = b.run(queries.at(i), o);
                        items[i].after = a.run(queries.at(i), o);
                        if (o.plans && isRead(queries.at(i).sql)) {
                            const QVariantList values = queries.at(i).samples.isEmpty() ? QVariantList()
                                                                                        : queries.at(i).samples.first().values;
                            explainSqlite(before, items[i].before, queries.at(i).sql, values);
                            explainSqlite(after, items[i].after, queries.at(i).sql, values);
                            items[i].planChanged = items[i].before.plan != items[i].after.plan;
                        }
                    }
                }
            }
            before.close();
            after.close();
        }
        QSqlDatabase::removeDatabase(beforeName);
        QSqlDatabase::removeDatabase(afterName);

    } else {
        // One transaction, never committed: the database as it is, then migrated.
        if (!session.changesAllowed() && !session.allowChanges(true)) {
            report.error = session.error();
            return report;
        }
        QSqlDatabase db = session.writeDatabase();
        const bool pg = dialect == QLatin1String("postgres");
        if (!db.transaction()) {
            report.error = QStringLiteral("couldn't start the sandbox transaction: %1").arg(db.lastError().text().trimmed());
            return report;
        }
        // Don't wait long on a lock, or run away with a slow query.
        if (pg) {
            QSqlQuery(db).exec(QStringLiteral("SET LOCAL lock_timeout = '5s'"));
            QSqlQuery(db).exec(QStringLiteral("SET LOCAL statement_timeout = '60s'"));
        } else {
            QSqlQuery(db).exec(QStringLiteral("SET LOCK_TIMEOUT 5000"));
        }
        Runner r{ db, pg ? Runner::PgSavepoint : Runner::MsSavepoint };
        for (int i = 0; i < queries.size(); ++i)
            items[i].before = r.run(queries.at(i), o);

        const QString driver = db.driverName();
        for (const QiMigrator::Migration &m : pending) {
            if (m.sql.trimmed().isEmpty()) {
                report.migrationError = QStringLiteral("%1 is a code migration: only SQL migrations can be replayed").arg(label(m));
                break;
            }
            for (const QString &statement : QiMigrator::splitStatements(m.sql, driver)) {
                if (isControl(statement))
                    continue;
                QSqlQuery q(db);
                if (!q.exec(statement)) {
                    report.migrationError = QStringLiteral("%1: %2").arg(label(m), q.lastError().text().trimmed());
                    break;
                }
            }
            if (!report.migrationError.isEmpty())
                break;
        }
        if (report.migrationError.isEmpty())
            for (int i = 0; i < queries.size(); ++i)
                items[i].after = r.run(queries.at(i), o);
        db.rollback();
    }

    if (report.migrationError.isEmpty()) {
        for (Item &i : items) {
            i.verdict = verdictOf(i, o);
            for (const QString &c : i.before.columns)
                if (!i.after.columns.contains(c)) i.columnsGone << c;
            for (const QString &c : i.after.columns)
                if (!i.before.columns.contains(c)) i.columnsNew << c;
        }
        std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
            return rank(a.verdict) != rank(b.verdict) ? rank(a.verdict) < rank(b.verdict) : a.runs > b.runs;
        });
        report.items = items;
    }
    report.ok = true;
    return report;
}

} // namespace QueryReplay
