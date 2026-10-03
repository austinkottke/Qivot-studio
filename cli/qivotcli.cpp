#include "qivotcli.h"
#include "codegen.h"
#include "databasesession.h"
#include "datatransfer.h"
#include "projectexport.h"
#include "schemacompare.h"
#include <qivot.hpp>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <memory>

namespace QivotCli {
namespace {

const char *const kUsage =
R"(qivot-cli: Qivot Studio's engine without the window.

Usage: qivot-cli <command> [arguments] [options]

Databases are given as
  path/to/file.db             an SQLite or DuckDB file
  sample:<id>                 a Studio sample (bookshop, music, company, university)
  postgres://user@host:port/database     also mysql://, sqlserver://
                              (the password can come from QIVOT_PASSWORD)
  migrations:<dir>            an SQLite database built from a folder of migrations

Commands
  inspect <db> [--table T]... [--json]
      Tables, columns, keys, references and indexes.
  models <db> [--table T]... [-o models.h]
      Qivot model classes for the tables (the code Studio's C++ tab shows).
  project <db> -o <dir> [--name Name] [--force]
      A complete Qt project around the models: CMake, qmake, main.cpp, README.
  diff <source> <target> [--sql] [-o file.sql] [--exit-code]
      What differs, and the SQL that makes <target> match <source>.
  migrate new <name> --dir <dir> --from <desired> [--to <current>]
      Write the next migration (NNNN_name.up.sql and .down.sql) taking <current>
      to <desired>. <current> defaults to migrations:<dir>, the folder so far.
  migrate status <db> --dir <dir> [--json] [--exit-code]
  migrate up <db> --dir <dir> [--to N] [--dry-run]
  migrate down <db> --dir <dir> --to N
  migrate accept <db> --dir <dir>
      Run a folder of migrations with Qivot's QiMigrator: what has run, run the
      rest, undo back to version N, or accept edits to migrations that already ran.
      An SQLite file that doesn't exist yet is created by `up`.
  query <db> <sql> [--format table|csv|tsv|json] [-o file]
      Run a query, read-only, and print the rows.
  drivers
      Which database drivers load.

Options
  --history <table>   the migrations table (default qivot_migrations)
  --exit-code         exit 1 when there are differences or pending migrations
  -h, --help          this text
  -v, --version       the version

Exit codes: 0 done, 1 differences or pending (with --exit-code), 2 failed.
)";

// --- Arguments --------------------------------------------------------------

struct Args {
    QStringList positional;
    QMap<QString, QStringList> values;
    QSet<QString> flags;

    bool has(const QString &name) const { return flags.contains(name) || values.contains(name); }
    QString value(const QString &name, const QString &fallback = QString()) const
    {
        return values.contains(name) ? values.value(name).constLast() : fallback;
    }
    QStringList all(const QString &name) const { return values.value(name); }
};

const QStringList kValueOptions = { "output", "dir", "from", "to", "name", "table", "format", "history" };
const QStringList kFlags = { "json", "exit-code", "dry-run", "force", "sql", "help", "version" };

bool parse(const QStringList &in, Args &args, QString &error)
{
    for (int i = 0; i < in.size(); ++i) {
        QString a = in.at(i);
        if (a == QLatin1String("--")) {
            args.positional += in.mid(i + 1);
            break;
        }
        if (a == QLatin1String("-o")) a = QStringLiteral("--output");
        if (a == QLatin1String("-h")) a = QStringLiteral("--help");
        if (a == QLatin1String("-v")) a = QStringLiteral("--version");
        if (!a.startsWith(QLatin1String("--")) || a.size() == 2) {
            args.positional << a;
            continue;
        }
        QString name = a.mid(2), value;
        bool inlineValue = false;
        const int eq = name.indexOf(QLatin1Char('='));
        if (eq > 0) { value = name.mid(eq + 1); name = name.left(eq); inlineValue = true; }

        if (kFlags.contains(name)) {
            if (inlineValue) { error = QStringLiteral("--%1 takes no value").arg(name); return false; }
            args.flags << name;
        } else if (kValueOptions.contains(name)) {
            if (!inlineValue) {
                if (i + 1 >= in.size()) { error = QStringLiteral("--%1 needs a value").arg(name); return false; }
                value = in.at(++i);
            }
            args.values[name] << value;
        } else {
            error = QStringLiteral("unknown option --%1").arg(name);
            return false;
        }
    }
    return true;
}

// --- Output -----------------------------------------------------------------

// Text to a file (-o) or to out.
bool writeOut(const QString &text, const QString &path, QTextStream &out, QTextStream &err)
{
    if (path.isEmpty() || path == QLatin1String("-")) {
        out << text;
        if (!text.endsWith(QLatin1Char('\n')))
            out << '\n';
        return true;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        err << "qivot-cli: can't write " << path << ": " << f.errorString() << '\n';
        return false;
    }
    f.write(text.toUtf8());
    err << "wrote " << QDir::toNativeSeparators(path) << '\n';
    return true;
}

// Columns padded to line up; the last one isn't padded.
QString table(const QStringList &header, const QVector<QStringList> &rows, int indent = 0)
{
    int columns = header.size();
    for (const QStringList &r : rows)
        columns = qMax(columns, int(r.size()));
    QVector<int> width(columns, 0);
    for (int c = 0; c < header.size(); ++c)
        width[c] = header.at(c).size();
    for (const QStringList &r : rows)
        for (int c = 0; c < r.size(); ++c)
            width[c] = qMax(width[c], int(r.at(c).size()));

    QString text;
    auto line = [&](const QStringList &cells) {
        QString l(indent, QLatin1Char(' '));
        for (int c = 0; c < cells.size(); ++c)
            l += c + 1 < cells.size() ? cells.at(c).leftJustified(width[c] + 2) : cells.at(c);
        while (l.endsWith(QLatin1Char(' ')))
            l.chop(1);
        text += l + QLatin1Char('\n');
    };
    if (!header.isEmpty())
        line(header);
    for (const QStringList &r : rows)
        line(r);
    return text;
}

QString json(const QJsonValue &v)
{
    return QString::fromUtf8(v.isArray() ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Indented)
                                         : QJsonDocument(v.toObject()).toJson(QJsonDocument::Indented));
}

// A database as it can be shown: no password.
QString shown(const QString &spec)
{
    if (!spec.contains(QLatin1String("://")))
        return spec;
    return QUrl(spec).toDisplayString(QUrl::RemovePassword);
}

// --- Opening databases ------------------------------------------------------

/// Keeps what an opened database needs alive (the scratch file of migrations:<dir>).
struct Opened {
    std::unique_ptr<QTemporaryDir> scratch;
};

int g_connections = 0;

QString nextConnectionName()
{
    return QStringLiteral("qivot-cli-%1").arg(++g_connections);
}

/// A folder of migrations run into a new SQLite file; its path, or "" (with error).
QString buildFromMigrations(const QString &dir, const QString &history, Opened &keep, QString &error)
{
    keep.scratch = std::make_unique<QTemporaryDir>();
    if (!keep.scratch->isValid()) {
        error = QStringLiteral("can't make a scratch folder");
        return QString();
    }
    const QString path = keep.scratch->filePath(QStringLiteral("migrations.db"));
    const QString name = nextConnectionName();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (!db.open()) {
            error = db.lastError().text();
        } else {
            QiConnection conn;
            if (!conn.open(db, false)) {
                error = QStringLiteral("can't open the scratch database");
            } else {
                QiMigrator m(conn);
                m.setTable(history);
                if (m.addDirectory(dir) < 0 || m.migrate() < 0)
                    error = QStringLiteral("migrations:%1: %2").arg(dir, m.lastError());
                conn.close();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return error.isEmpty() ? path : QString();
}

/// Open `spec` in `s`. `create`: an SQLite file that doesn't exist is created.
bool openDb(const QString &spec, DatabaseSession &s, Opened &keep, const QString &history, QString &error, bool create = false)
{
    bool ok = false;
    if (spec.contains(QLatin1String("://"))) {
        QVariantMap settings = DatabaseSession::settingsFromUrl(spec);
        if (settings.value(QStringLiteral("password")).toString().isEmpty() && qEnvironmentVariableIsSet("QIVOT_PASSWORD"))
            settings.insert(QStringLiteral("password"), qEnvironmentVariable("QIVOT_PASSWORD"));
        if (settings.value(QStringLiteral("type")) == QLatin1String("redis")) {
            error = QStringLiteral("Redis has no tables; qivot-cli works with SQL databases");
            return false;
        }
        ok = s.connectTo(settings);
    } else if (spec.startsWith(QLatin1String("sample:"))) {
        ok = s.openSample(spec.mid(7));
    } else if (spec.startsWith(QLatin1String("migrations:"))) {
        const QString path = buildFromMigrations(spec.mid(11), history, keep, error);
        if (path.isEmpty())
            return false;
        ok = s.open(path);
    } else {
        if (!QFileInfo::exists(spec)) {
            if (!create) {
                error = QStringLiteral("%1: no such file").arg(spec);
                return false;
            }
            QFile f(spec);
            if (!f.open(QIODevice::WriteOnly)) {
                error = QStringLiteral("can't create %1: %2").arg(spec, f.errorString());
                return false;
            }
        }
        ok = s.open(spec);
    }
    if (!ok)
        error = s.error().isEmpty() ? QStringLiteral("can't open %1").arg(shown(spec)) : s.error();
    return ok;
}

// --- inspect ----------------------------------------------------------------

QString kindName(QiTableInfo::Kind k)
{
    return k == QiTableInfo::View ? QStringLiteral("view") : k == QiTableInfo::Virtual ? QStringLiteral("virtual") : QStringLiteral("table");
}

QVector<QiTableInfo> chosen(const DatabaseSession &s, const QStringList &names, QString &error)
{
    if (names.isEmpty())
        return s.tableInfos();
    QVector<QiTableInfo> out;
    for (const QString &n : names) {
        bool found = false;
        for (const QiTableInfo &t : s.tableInfos())
            if (t.name.compare(n, Qt::CaseInsensitive) == 0) { out << t; found = true; break; }
        if (!found) {
            error = QStringLiteral("no table %1").arg(n);
            return {};
        }
    }
    return out;
}

int inspect(const Args &a, QTextStream &out, QTextStream &err)
{
    if (a.positional.size() != 2) { err << "usage: qivot-cli inspect <db> [--table T]... [--json]\n"; return Failed; }
    DatabaseSession s; Opened keep; QString error;
    if (!openDb(a.positional.at(1), s, keep, a.value("history", "qivot_migrations"), error)) { err << "qivot-cli: " << error << '\n'; return Failed; }
    const QVector<QiTableInfo> tables = chosen(s, a.all("table"), error);
    if (!error.isEmpty()) { err << "qivot-cli: " << error << '\n'; return Failed; }

    QHash<QString, qint64> rows;
    for (const QVariant &v : s.tables()) {
        const QVariantMap m = v.toMap();
        rows.insert(m.value("name").toString(), m.value("rows").toLongLong());
    }

    if (a.has("json")) {
        QJsonArray list;
        for (const QiTableInfo &t : tables) {
            QJsonArray cols, fks, idx;
            for (const QiColumnInfo &c : t.columns)
                cols << QJsonObject{ { "name", c.name }, { "type", c.type }, { "nullable", c.nullable },
                                     { "primaryKey", c.primaryKey }, { "autoIncrement", c.autoIncrement },
                                     { "default", c.defaultValue.isNull() ? QJsonValue() : QJsonValue(c.defaultValue.toString()) } };
            for (const QiForeignKeyInfo &f : t.foreignKeys)
                fks << QJsonObject{ { "name", f.name }, { "columns", QJsonArray::fromStringList(f.columns) },
                                    { "refTable", f.refTable }, { "refColumns", QJsonArray::fromStringList(f.refColumns) },
                                    { "onDelete", f.onDelete }, { "onUpdate", f.onUpdate } };
            for (const QiIndexInfo &i : t.indexes)
                idx << QJsonObject{ { "name", i.name }, { "columns", QJsonArray::fromStringList(i.columns) },
                                    { "unique", i.unique }, { "implicit", i.implicit } };
            list << QJsonObject{ { "name", t.name }, { "kind", kindName(t.kind) }, { "rows", double(rows.value(t.name, -1)) },
                                 { "primaryKey", QJsonArray::fromStringList(t.primaryKey) },
                                 { "columns", cols }, { "foreignKeys", fks }, { "indexes", idx } };
        }
        out << json(QJsonObject{ { "database", s.displayName() }, { "dialect", s.dialect() }, { "tables", list } });
        return Ok;
    }

    out << s.displayName() << " (" << s.dialectName() << "), " << tables.size()
        << (tables.size() == 1 ? " table\n" : " tables\n");
    for (const QiTableInfo &t : tables) {
        const qint64 n = rows.value(t.name, -1);
        out << '\n' << t.name << "  (" << kindName(t.kind);
        if (n >= 0)
            out << ", " << n << (n == 1 ? " row" : " rows");
        out << ")\n";

        QHash<QString, QString> refs;
        for (const QiForeignKeyInfo &f : t.foreignKeys) {
            QString r = QStringLiteral("-> %1(%2)").arg(f.refTable, f.refColumns.join(", "));
            if (!f.onDelete.isEmpty() && f.onDelete != QLatin1String("NO ACTION"))
                r += QStringLiteral(" on delete %1").arg(f.onDelete.toLower());
            if (f.columns.size() == 1)
                refs.insert(f.columns.first(), r);
        }
        QVector<QStringList> lines;
        for (const QiColumnInfo &c : t.columns) {
            QStringList notes;
            if (c.primaryKey) notes << (c.autoIncrement ? "primary key, auto" : "primary key");
            else if (!c.nullable) notes << "not null";
            if (!c.defaultValue.isNull()) notes << "default " + c.defaultValue.toString();
            if (refs.contains(c.name)) notes << refs.value(c.name);
            lines << QStringList{ c.name, c.type.isEmpty() ? QStringLiteral("-") : c.type, notes.join(", ") };
        }
        out << table({}, lines, 2);
        for (const QiForeignKeyInfo &f : t.foreignKeys)
            if (f.columns.size() > 1)
                out << "  (" << f.columns.join(", ") << ") -> " << f.refTable << '(' << f.refColumns.join(", ") << ")\n";
        for (const QiIndexInfo &i : t.indexes)
            if (!i.implicit)
                out << "  index " << i.name << " (" << i.columns.join(", ") << ')' << (i.unique ? " unique" : "") << '\n';
    }
    return Ok;
}

// --- models / project -------------------------------------------------------

int models(const Args &a, QTextStream &out, QTextStream &err)
{
    if (a.positional.size() != 2) { err << "usage: qivot-cli models <db> [--table T]... [-o models.h]\n"; return Failed; }
    DatabaseSession s; Opened keep; QString error;
    if (!openDb(a.positional.at(1), s, keep, a.value("history", "qivot_migrations"), error)) { err << "qivot-cli: " << error << '\n'; return Failed; }

    QString text;
    const QStringList only = a.all("table");
    if (only.isEmpty()) {
        QString guard = QFileInfo(a.value("output")).fileName().toUpper();
        guard.replace(QRegularExpression(QStringLiteral("[^A-Z0-9]")), QStringLiteral("_"));
        text = CodeGen(s.tableInfos(), s.dialect()).header(guard.isEmpty() ? QStringLiteral("MODELS_H") : guard);
    } else {
        const CodeGen gen(s.tableInfos(), s.dialect());
        for (const QString &t : only) {
            const CodeGen::Model m = gen.model(t);
            if (!m.isValid()) { err << "qivot-cli: no table " << t << '\n'; return Failed; }
            text += m.code + QLatin1Char('\n');
        }
    }
    return writeOut(text, a.value("output"), out, err) ? Ok : Failed;
}

int project(const Args &a, QTextStream &, QTextStream &err)
{
    const QString dir = a.value("output");
    if (a.positional.size() != 2 || dir.isEmpty()) { err << "usage: qivot-cli project <db> -o <dir> [--name Name] [--force]\n"; return Failed; }
    DatabaseSession s; Opened keep; QString error;
    if (!openDb(a.positional.at(1), s, keep, a.value("history", "qivot_migrations"), error)) { err << "qivot-cli: " << error << '\n'; return Failed; }

    ProjectExport pe;
    pe.setSession(&s);
    const QString name = a.value("name", pe.suggestedName());
    const QDir target(dir);
    if (target.exists() && !target.isEmpty() && !a.has("force")) {
        err << "qivot-cli: " << dir << " isn't empty (--force writes over it)\n";
        return Failed;
    }
    const QMap<QString, QString> files = ProjectExport::generate(s, name);
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QString path = target.filePath(it.key());
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            err << "qivot-cli: can't write " << path << ": " << f.errorString() << '\n';
            return Failed;
        }
        f.write(it.value().toUtf8());
    }
    err << "wrote " << files.size() << " files for " << name << " to " << QDir::toNativeSeparators(target.absolutePath()) << '\n';
    return Ok;
}

// --- diff -------------------------------------------------------------------

/// `source` opened as the session, `target` in compare.other().
bool compareOpen(const QString &source, const QString &target, const QString &history,
                 DatabaseSession &s, SchemaCompare &compare, Opened &keepS, Opened &keepT, QString &error)
{
    if (!openDb(source, s, keepS, history, error))
        return false;
    compare.setIgnored({ history });     // the migrations' bookkeeping isn't the schema
    compare.setSession(&s);
    if (!openDb(target, *compare.other(), keepT, history, error))
        return false;
    if (!compare.error().isEmpty()) {
        error = compare.error();
        return false;
    }
    return true;
}

int diff(const Args &a, QTextStream &out, QTextStream &err)
{
    if (a.positional.size() != 3) { err << "usage: qivot-cli diff <source> <target> [--sql] [-o file.sql] [--exit-code]\n"; return Failed; }
    DatabaseSession s; SchemaCompare compare; Opened k1, k2; QString error;
    if (!compareOpen(a.positional.at(1), a.positional.at(2), a.value("history", "qivot_migrations"), s, compare, k1, k2, error)) {
        err << "qivot-cli: " << error << '\n';
        return Failed;
    }
    const QStringList changes = compare.changes();
    const QString sql = compare.migration();

    if (!a.value("output").isEmpty() || a.has("sql")) {
        if (!changes.isEmpty() && !writeOut(sql, a.value("output"), out, err))
            return Failed;
    } else if (changes.isEmpty()) {
        out << "No differences: " << shown(a.positional.at(2)) << " matches " << shown(a.positional.at(1)) << ".\n";
    } else {
        out << changes.size() << (changes.size() == 1 ? " difference" : " differences")
            << " to make " << shown(a.positional.at(2)) << " match " << shown(a.positional.at(1)) << ":\n";
        for (const QString &c : changes)
            out << "  " << c << '\n';
        if (!compare.sameDialect())
            out << "(" << s.dialectName() << " and " << compare.other()->dialectName()
                << " write types differently, so most columns differ in type.)\n";
        out << "\n" << sql;
        if (!sql.endsWith(QLatin1Char('\n')))
            out << '\n';
    }
    if (changes.isEmpty() && (a.has("sql") || !a.value("output").isEmpty()))
        err << "No differences.\n";
    return a.has("exit-code") && !changes.isEmpty() ? Differs : Ok;
}

// --- migrate ----------------------------------------------------------------

// Studio's SQL as a migration file: without the BEGIN / COMMIT and the SQLite
// foreign-key pragmas around it, which QiMigrator does itself.
QString asMigration(const QString &sql)
{
    static const QRegularExpression wrapper(
        QStringLiteral("^(BEGIN|BEGIN TRANSACTION|COMMIT|PRAGMA foreign_keys = (ON|OFF));[^\\n]*\\n?"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression blankRuns(QStringLiteral("\\n{3,}"));
    QString out = QString(sql).remove(wrapper);
    out.replace(blankRuns, QStringLiteral("\n\n"));
    return out.trimmed() + QLatin1Char('\n');
}

QString slug(const QString &name)
{
    QString s = name.trimmed().toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    s.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    return s;
}

int migrateNew(const Args &a, QTextStream &out, QTextStream &err)
{
    const QString dir = a.value("dir");
    const QString name = a.positional.size() == 3 ? slug(a.positional.at(2)) : QString();
    if (name.isEmpty() || dir.isEmpty() || !a.has("from")) {
        err << "usage: qivot-cli migrate new <name> --dir <dir> --from <desired> [--to <current>]\n";
        return Failed;
    }
    if (!QDir().mkpath(dir)) { err << "qivot-cli: can't make " << dir << '\n'; return Failed; }

    const QString history = a.value("history", "qivot_migrations");
    const QString desired = a.value("from");
    const QString current = a.value("to", QStringLiteral("migrations:") + dir);

    // Session: what we want. Other: what is. "toOther" makes what-is match
    // (the up step); "toThis" goes back (the down step, in the desired one's dialect).
    DatabaseSession s; SchemaCompare compare; Opened k1, k2; QString error;
    if (!compareOpen(desired, current, history, s, compare, k1, k2, error)) {
        err << "qivot-cli: " << error << '\n';
        return Failed;
    }
    const QStringList changes = compare.changes();
    if (changes.isEmpty()) {
        out << "No differences: " << shown(current) << " already matches " << shown(desired) << ". Nothing written.\n";
        return Ok;
    }
    const QString up = asMigration(compare.migration());
    QString down;
    if (compare.sameDialect()) {
        compare.setDirection(QStringLiteral("toThis"));
        down = asMigration(compare.migration());
    }

    // The next version, as wide as the ones there (at least 4 digits).
    int next = 1, width = 4;
    static const QRegularExpression numbered(QStringLiteral("^(\\d+)"));
    for (const QString &f : QDir(dir).entryList({ QStringLiteral("*.sql") }, QDir::Files)) {
        const QRegularExpressionMatch m = numbered.match(f);
        if (m.hasMatch()) {
            next = qMax(next, m.captured(1).toInt() + 1);
            width = qMax(width, m.captured(1).size());
        }
    }
    const QString base = QStringLiteral("%1_%2").arg(next, width, 10, QLatin1Char('0')).arg(name);
    const QString header = QStringLiteral("-- %1\n-- Generated by qivot-cli on %2: %3 to match %4.\n")
                               .arg(a.positional.at(2).trimmed(),
                                    QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd")),
                                    shown(current).startsWith(QLatin1String("migrations:")) ? QStringLiteral("the migrations so far") : shown(current),
                                    shown(desired));
    QString changeList;
    for (const QString &c : changes)
        changeList += QStringLiteral("--   %1\n").arg(c);

    auto write = [&](const QString &file, const QString &text) {
        QFile f(QDir(dir).filePath(file));
        if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            err << "qivot-cli: can't write " << f.fileName() << ": " << f.errorString() << '\n';
            return false;
        }
        f.write(text.toUtf8());
        out << "wrote " << QDir::toNativeSeparators(f.fileName()) << '\n';
        return true;
    };
    const QString upText = header + changeList + QLatin1Char('\n') + up;
    if (down.trimmed().isEmpty()) {
        if (!write(base + QStringLiteral(".sql"), upText))
            return Failed;
        if (!compare.sameDialect())
            out << "No down step: " << shown(desired) << " and " << shown(current) << " are different kinds of database.\n";
    } else {
        if (!write(base + QStringLiteral(".up.sql"), upText))
            return Failed;
        if (!write(base + QStringLiteral(".down.sql"),
                   QStringLiteral("-- %1, undone.\n\n").arg(a.positional.at(2).trimmed()) + down))
            return Failed;
    }
    out << changes.size() << (changes.size() == 1 ? " change" : " changes") << ":\n";
    for (const QString &c : changes)
        out << "  " << c << '\n';
    return Ok;
}

QString stateOf(const QiMigrator::Migration &m)
{
    if (!m.known) return QStringLiteral("not in folder");
    if (m.changed) return QStringLiteral("CHANGED");
    return m.applied ? QStringLiteral("applied") : QStringLiteral("pending");
}

int migrate(const Args &a, QTextStream &out, QTextStream &err)
{
    const QString action = a.positional.value(1);
    if (action == QLatin1String("new"))
        return migrateNew(a, out, err);
    static const QStringList actions = { "status", "up", "down", "accept" };
    if (!actions.contains(action) || a.positional.size() != 3 || a.value("dir").isEmpty()) {
        err << "usage: qivot-cli migrate <status|up|down|accept> <db> --dir <dir>   (or: migrate new <name> …)\n";
        return Failed;
    }
    const bool writes = action != QLatin1String("status") && !a.has("dry-run");
    if (action == QLatin1String("down") && !a.has("to")) {
        err << "qivot-cli: migrate down needs --to <version> (0 undoes everything)\n";
        return Failed;
    }

    DatabaseSession s; Opened keep; QString error;
    if (!openDb(a.positional.at(2), s, keep, a.value("history", "qivot_migrations"), error, action == QLatin1String("up") && writes)) {
        err << "qivot-cli: " << error << '\n';
        return Failed;
    }
    if (writes && !s.allowChanges(true)) {
        err << "qivot-cli: " << s.error() << '\n';
        return Failed;
    }

    int result = Ok;
    {
        QSqlDatabase db = writes ? s.writeDatabase() : QSqlDatabase::database(s.connectionName());
        QiConnection conn;
        if (!conn.open(db, false)) {
            err << "qivot-cli: Qivot can't use this database: " << conn.lastError().text() << '\n';
            return Failed;
        }
        QiMigrator m(conn);
        m.setTable(a.value("history", "qivot_migrations"));
        if (m.addDirectory(a.value("dir")) < 0) {
            err << "qivot-cli: " << m.lastError() << '\n';
            return Failed;
        }

        if (action == QLatin1String("status")) {
            const QVector<QiMigrator::Migration> all = m.status();
            int pending = 0, changed = 0;
            for (const QiMigrator::Migration &x : all) {
                pending += x.known && !x.applied;
                changed += x.changed;
            }
            if (a.has("json")) {
                QJsonArray list;
                for (const QiMigrator::Migration &x : all)
                    list << QJsonObject{ { "version", x.version }, { "name", x.name }, { "state", stateOf(x).toLower() },
                                         { "appliedAt", x.appliedAt.isValid() ? QJsonValue(x.appliedAt.toString(Qt::ISODate)) : QJsonValue() },
                                         { "durationMs", x.durationMs }, { "reversible", x.reversible }, { "checksum", x.checksum } };
                out << json(QJsonObject{ { "version", m.currentVersion() }, { "target", m.targetVersion() },
                                         { "pending", pending }, { "changed", changed }, { "migrations", list } });
            } else {
                QVector<QStringList> rows;
                for (const QiMigrator::Migration &x : all)
                    rows << QStringList{ QString::number(x.version), x.name, stateOf(x),
                                         x.appliedAt.isValid() ? x.appliedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QString(),
                                         x.durationMs >= 0 ? QStringLiteral("%1 ms").arg(x.durationMs) : QString() };
                out << shown(a.positional.at(2)) << ": version " << m.currentVersion() << " of " << m.targetVersion();
                if (pending) out << ", " << pending << " pending";
                if (changed) out << ", " << changed << " changed since they ran";
                out << "\n\n" << table({ "version", "name", "state", "applied (UTC)", "took" }, rows, 2);
            }
            if (a.has("exit-code") && (pending || changed))
                result = Differs;
        } else if (action == QLatin1String("up") && a.has("dry-run")) {
            const int upTo = a.has("to") ? a.value("to").toInt() : -1;
            int n = 0;
            for (const QiMigrator::Migration &x : m.pending()) {
                if (upTo >= 0 && x.version > upTo)
                    break;
                out << "-- " << x.version << ' ' << x.name << '\n' << (x.sql.isEmpty() ? QStringLiteral("-- (code)\n") : x.sql);
                if (!x.sql.endsWith(QLatin1Char('\n'))) out << '\n';
                out << '\n';
                ++n;
            }
            err << n << (n == 1 ? " migration" : " migrations") << " would run.\n";
        } else if (action == QLatin1String("up")) {
            const int n = a.has("to") ? m.migrateTo(a.value("to").toInt()) : m.migrate();
            if (n < 0) {
                err << "qivot-cli: " << m.lastError() << '\n';
                result = Failed;
            } else {
                out << (n == 0 ? QStringLiteral("Up to date") : QStringLiteral("Applied %1").arg(n))
                    << ": version " << m.currentVersion() << ".\n";
            }
        } else if (action == QLatin1String("down")) {
            const int n = m.rollback(a.value("to").toInt());
            if (n < 0) {
                err << "qivot-cli: " << m.lastError() << '\n';
                result = Failed;
            } else {
                out << "Undid " << n << (n == 1 ? " migration" : " migrations") << ": version " << m.currentVersion() << ".\n";
            }
        } else {    // accept
            const int n = m.changed().size();
            if (!m.acceptChecksums()) {
                err << "qivot-cli: " << m.lastError() << '\n';
                result = Failed;
            } else {
                out << "Accepted " << n << (n == 1 ? " edited migration" : " edited migrations") << ".\n";
            }
        }
        conn.close();
    }
    return result;
}

// --- query ------------------------------------------------------------------

QString cell(const QVariant &v)
{
    if (v.isNull())
        return QString();
    if (v.userType() == QMetaType::QByteArray)
        return QStringLiteral("<%1 bytes>").arg(v.toByteArray().size());
    return v.toString();
}

QString quoted(const QString &s, QChar delimiter)
{
    if (!s.contains(delimiter) && !s.contains(QLatin1Char('"')) && !s.contains(QLatin1Char('\n')) && !s.contains(QLatin1Char('\r')))
        return s;
    return QLatin1Char('"') + QString(s).replace(QLatin1String("\""), QLatin1String("\"\"")) + QLatin1Char('"');
}

int query(const Args &a, QTextStream &out, QTextStream &err)
{
    if (a.positional.size() != 3) { err << "usage: qivot-cli query <db> <sql> [--format table|csv|tsv|json] [-o file]\n"; return Failed; }
    const QString format = a.value("format", a.has("json") ? QStringLiteral("json") : QStringLiteral("table"));
    if (!QStringList{ "table", "csv", "tsv", "json" }.contains(format)) { err << "qivot-cli: --format is table, csv, tsv or json\n"; return Failed; }

    DatabaseSession s; Opened keep; QString error;
    if (!openDb(a.positional.at(1), s, keep, a.value("history", "qivot_migrations"), error)) { err << "qivot-cli: " << error << '\n'; return Failed; }

    QSqlQuery q(QSqlDatabase::database(s.connectionName()));
    q.setForwardOnly(true);
    if (!q.exec(a.positional.at(2))) {
        err << "qivot-cli: " << q.lastError().text().trimmed() << '\n';
        return Failed;
    }
    const QString path = a.value("output");
    if (!path.isEmpty() && (format == QLatin1String("csv") || format == QLatin1String("json"))) {
        const QVariantMap r = DataTransfer::write(q, path, format);    // streams, for big results
        if (!r.value("ok").toBool()) { err << "qivot-cli: " << r.value("error").toString() << '\n'; return Failed; }
        err << "wrote " << r.value("rows").toLongLong() << " rows to " << QDir::toNativeSeparators(path) << '\n';
        return Ok;
    }

    if (!q.isSelect()) {
        out << q.numRowsAffected() << " rows\n";
        return Ok;
    }
    const QSqlRecord rec = q.record();
    QStringList header;
    for (int c = 0; c < rec.count(); ++c)
        header << rec.fieldName(c);

    QString text;
    qint64 count = 0;
    if (format == QLatin1String("json")) {
        QJsonArray rows;
        while (q.next()) {
            QJsonObject o;
            for (int c = 0; c < header.size(); ++c) {
                const QVariant v = q.value(c);
                o.insert(header.at(c), v.isNull() ? QJsonValue() : QJsonValue::fromVariant(v.userType() == QMetaType::QByteArray ? QVariant(cell(v)) : v));
            }
            rows << o;
        }
        text = json(rows);
    } else if (format == QLatin1String("table")) {
        QVector<QStringList> rows;
        while (q.next()) {
            QStringList r;
            for (int c = 0; c < header.size(); ++c) {
                QString v = q.value(c).isNull() ? QStringLiteral("NULL") : cell(q.value(c));
                v.replace(QLatin1Char('\n'), QLatin1Char(' '));
                r << (v.size() > 60 ? v.left(59) + QChar(0x2026) : v);
            }
            rows << r;
        }
        count = rows.size();
        text = table(header, rows) + QStringLiteral("(%1 %2)\n").arg(count).arg(count == 1 ? "row" : "rows");
    } else {
        const QChar d = format == QLatin1String("csv") ? QLatin1Char(',') : QLatin1Char('\t');
        QStringList h;
        for (const QString &c : header) h << quoted(c, d);
        text = h.join(d) + QLatin1Char('\n');
        while (q.next()) {
            QStringList r;
            for (int c = 0; c < header.size(); ++c)
                r << quoted(cell(q.value(c)), d);
            text += r.join(d) + QLatin1Char('\n');
        }
    }
    return writeOut(text, path, out, err) ? Ok : Failed;
}

// --- drivers ----------------------------------------------------------------

int drivers(const Args &, QTextStream &out, QTextStream &)
{
    const DatabaseSession registersDrivers;     // Studio's own drivers (DuckDB)
    QVector<QStringList> rows;
    for (const QString &name : QStringList{ "QSQLITE", "QDUCKDB", "QPSQL", "QMYSQL", "QMARIADB", "QODBC" }) {
        const bool listed = QSqlDatabase::drivers().contains(name);
        const bool loads = listed && DatabaseSession::driverLoads(name);
        rows << QStringList{ name, loads ? "loads" : listed ? "found, but doesn't load" : "not here" };
    }
    out << table({}, rows);
    return Ok;
}

} // namespace

int run(const QStringList &argv, QTextStream &out, QTextStream &err)
{
    Args a;
    QString error;
    if (!parse(argv, a, error)) {
        err << "qivot-cli: " << error << "\n(qivot-cli --help lists the commands)\n";
        return Failed;
    }
    if (a.has("version")) {
        out << "qivot-cli " << QCoreApplication::applicationVersion() << '\n';
        return Ok;
    }
    const QString command = a.positional.value(0);
    if (a.has("help") || command.isEmpty() || command == QLatin1String("help")) {
        out << kUsage;
        return command.isEmpty() && !a.has("help") ? Failed : Ok;
    }

    int code = Failed;
    if (command == QLatin1String("inspect"))      code = inspect(a, out, err);
    else if (command == QLatin1String("models"))  code = models(a, out, err);
    else if (command == QLatin1String("project")) code = project(a, out, err);
    else if (command == QLatin1String("diff"))    code = diff(a, out, err);
    else if (command == QLatin1String("migrate")) code = migrate(a, out, err);
    else if (command == QLatin1String("query"))   code = query(a, out, err);
    else if (command == QLatin1String("drivers")) code = drivers(a, out, err);
    else err << "qivot-cli: no command " << command << "\n(qivot-cli --help lists the commands)\n";
    out.flush();
    err.flush();
    return code;
}

} // namespace QivotCli
