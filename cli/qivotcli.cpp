#include "qivotcli.h"
#include "codegen.h"
#include "databasesession.h"
#include "datatransfer.h"
#include "projectexport.h"
#include "queryreplay.h"
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
#include <cstdio>
#include <functional>
#include <memory>
#ifdef Q_OS_WIN
#  include <io.h>
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#    define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#  endif
#else
#  include <unistd.h>
#endif

namespace QivotCli {
namespace {

// --- Colour -----------------------------------------------------------------

bool g_colorOut = false, g_colorErr = false;

// ANSI SGR codes: "1" bold, "2" dim, "31" red, "32" green, "33" yellow, "36" cyan …
QString paint(bool on, const char *code, const QString &text)
{
    if (!on || text.isEmpty())
        return text;
    return QStringLiteral("\x1b[%1m%2\x1b[0m").arg(QLatin1String(code), text);
}
QString ink(const char *code, const QString &text) { return paint(g_colorOut, code, text); }

// "qivot-cli: " in front of an error.
QString errorTag() { return paint(g_colorErr, "1;31", QStringLiteral("qivot-cli: ")); }

// A change as the designer words it: additions green, removals red, the rest yellow.
QString changeColour(const QString &change)
{
    const char *code = change.startsWith(QLatin1String("Create")) || change.startsWith(QLatin1String("Add")) ? "32"
                     : change.startsWith(QLatin1String("Drop")) ? "31" : "33";
    return ink(code, change);
}

// SQL with its comments dimmed.
QString sqlColour(const QString &sql)
{
    if (!g_colorOut)
        return sql;
    QStringList lines = sql.split(QLatin1Char('\n'));
    for (QString &l : lines)
        if (l.trimmed().startsWith(QLatin1String("--")))
            l = ink("2", l);
    return lines.join(QLatin1Char('\n'));
}

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
      A complete Qt project around the models: CMake, main.cpp, tests, README.
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
  replay <db> --record app.qrec --dir <dir> [--sandbox] [--json] [--exit-code]
      Replay the queries an app ran (recorded with QIVOT_RECORD=app.qrec) against
      the database before and after its pending migrations: which break, which
      return different rows, which got slower, and how their plans changed.
      Nothing is changed: SQLite runs on copies; on PostgreSQL and SQL Server it all
      happens in a transaction that is rolled back (--sandbox: on a staging copy,
      since schema changes lock tables until then).
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

// The QIVOT banner, shaded top to bottom.
QString banner()
{
    static const char16_t *const rows[] = {
        u" \u2588\u2588\u2588\u2588\u2588\u2588\u2557 \u2588\u2588\u2557\u2588\u2588\u2557   \u2588\u2588\u2557 \u2588\u2588\u2588\u2588\u2588\u2588\u2557 \u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2557",
        u"\u2588\u2588\u2554\u2550\u2550\u2550\u2588\u2588\u2557\u2588\u2588\u2551\u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2554\u2550\u2550\u2550\u2588\u2588\u2557\u255a\u2550\u2550\u2588\u2588\u2554\u2550\u2550\u255d",
        u"\u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2551\u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2551   \u2588\u2588\u2551   \u2588\u2588\u2551",
        u"\u2588\u2588\u2551\u2584\u2584 \u2588\u2588\u2551\u2588\u2588\u2551\u255a\u2588\u2588\u2557 \u2588\u2588\u2554\u255d\u2588\u2588\u2551   \u2588\u2588\u2551   \u2588\u2588\u2551",
        u"\u255a\u2588\u2588\u2588\u2588\u2588\u2588\u2554\u255d\u2588\u2588\u2551 \u255a\u2588\u2588\u2588\u2588\u2554\u255d \u255a\u2588\u2588\u2588\u2588\u2588\u2588\u2554\u255d   \u2588\u2588\u2551",
        u" \u255a\u2550\u2550\u2580\u2580\u2550\u255d \u255a\u2550\u255d  \u255a\u2550\u2550\u2550\u255d   \u255a\u2550\u2550\u2550\u2550\u2550\u255d    \u255a\u2550\u255d",
    };
    static const char *const shades[] = { "1;38;5;51", "1;38;5;45", "1;38;5;39", "1;38;5;33", "1;38;5;63", "1;38;5;99" };
    QString text = QStringLiteral("\n");
    for (int i = 0; i < 6; ++i)
        text += QStringLiteral("  ") + ink(shades[i], QString::fromUtf16(rows[i])) + QLatin1Char('\n');
    text += QStringLiteral("\n  ") + ink("1", QStringLiteral("qivot-cli"))
          + ink("2", QStringLiteral(" %1  \u00b7  Qivot Studio's engine without the window")
                         .arg(QCoreApplication::applicationVersion()))
          + QStringLiteral("\n");
    return text;
}

// The help, coloured: headings, commands, <arguments>, [optional parts], --options.
QString usage()
{
    const QString plain = QString::fromUtf8(kUsage);
    if (!g_colorOut)
        return plain;

    static const QRegularExpression command(
        QStringLiteral("^  (migrate (?:new|status|up|down|accept)|inspect|models|project|diff|replay|query|drivers)\\b"));
    static const QRegularExpression database(QStringLiteral("^  (path/to/file\\.db|sample:<id>|postgres://\\S+|migrations:<dir>)"));
    static const QRegularExpression tokens(
        QStringLiteral("(<[^>]+>)|(\\[[^\\]]+\\](?:\\.\\.\\.)?)|(?<![\\w-])(--?[a-z][a-z-]*)|(\\bqivot-cli\\b)"));
    auto inline_ = [](const QString &text) {
        QString result;
        int at = 0;
        QRegularExpressionMatchIterator it = tokens.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            result += text.mid(at, m.capturedStart() - at);
            const char *code = !m.captured(1).isEmpty() ? "36" : !m.captured(2).isEmpty() ? "2"
                             : !m.captured(3).isEmpty() ? "33" : "1;32";
            result += ink(code, m.captured(0));
            at = m.capturedEnd();
        }
        return result + text.mid(at);
    };

    QStringList lines = plain.split(QLatin1Char('\n'));
    QString text = banner();
    for (int i = 1; i < lines.size(); ++i) {       // the first line is the banner's
        const QString &l = lines.at(i);
        QRegularExpressionMatch m;
        if (l.startsWith(QLatin1String("Usage:"))) {
            text += ink("1", QStringLiteral("Usage:")) + inline_(l.mid(6));
        } else if (l.startsWith(QLatin1String("Exit codes:"))) {
            text += ink("2", l);
        } else if (!l.isEmpty() && !l.startsWith(QLatin1Char(' '))) {
            text += ink("1;35", l);                                        // a heading
        } else if ((m = command.match(l)).hasMatch()) {
            text += QStringLiteral("  ") + ink("1;32", m.captured(1)) + inline_(l.mid(m.capturedEnd()));
        } else if ((m = database.match(l)).hasMatch()) {
            text += QStringLiteral("  ") + ink("36", m.captured(1)) + l.mid(m.capturedEnd());
        } else {
            text += inline_(l);
        }
        if (i + 1 < lines.size())
            text += QLatin1Char('\n');
    }
    return text;
}

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

const QStringList kValueOptions = { "output", "dir", "from", "to", "name", "table", "format", "history", "record", "slower" };
const QStringList kFlags = { "json", "exit-code", "dry-run", "force", "sql", "help", "version", "sandbox" };

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
// paintCell(row, column, text) colours a cell (row -1 is the header); the
// padding stays outside the colour, so the columns still line up.
using CellPainter = std::function<QString(int, int, const QString &)>;

QString table(const QStringList &header, const QVector<QStringList> &rows, int indent = 0,
              const CellPainter &paintCell = CellPainter())
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
    auto line = [&](int row, const QStringList &cells) {
        int last = cells.size() - 1;
        while (last >= 0 && cells.at(last).isEmpty())
            --last;
        QString l(indent, QLatin1Char(' '));
        for (int c = 0; c <= last; ++c) {
            l += paintCell ? paintCell(row, c, cells.at(c)) : cells.at(c);
            if (c < last)
                l += QString(width[c] + 2 - cells.at(c).size(), QLatin1Char(' '));
        }
        text += l + QLatin1Char('\n');
    };
    if (!header.isEmpty())
        line(-1, header);
    for (int r = 0; r < rows.size(); ++r)
        line(r, rows.at(r));
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
                m.setUseUserVersion(false);   // user_version may mean anything here
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
    if (!openDb(a.positional.at(1), s, keep, a.value("history", "qivot_migrations"), error)) { err << errorTag() << error << '\n'; return Failed; }
    const QVector<QiTableInfo> tables = chosen(s, a.all("table"), error);
    if (!error.isEmpty()) { err << errorTag() << error << '\n'; return Failed; }

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

    out << ink("1", s.displayName()) << ink("2", QStringLiteral(" (%1), ").arg(s.dialectName()))
        << ink("2", QStringLiteral("%1 %2").arg(tables.size()).arg(tables.size() == 1 ? "table" : "tables")) << '\n';
    for (const QiTableInfo &t : tables) {
        const qint64 n = rows.value(t.name, -1);
        QString about = kindName(t.kind);
        if (n >= 0)
            about += QStringLiteral(", %1 %2").arg(n).arg(n == 1 ? "row" : "rows");
        out << '\n' << ink("1;36", t.name) << ink("2", QStringLiteral("  (%1)").arg(about)) << '\n';

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
        out << table({}, lines, 2, [](int, int column, const QString &text) {
            if (column == 1)
                return ink("2", text);
            if (column == 2) {
                // primary key in yellow, the reference in magenta
                QString painted;
                for (const QString &part : text.split(QStringLiteral(", "))) {
                    if (!painted.isEmpty()) painted += ink("2", QStringLiteral(", "));
                    painted += part.startsWith(QLatin1String("primary key")) || part == QLatin1String("auto") ? ink("33", part)
                             : part.startsWith(QLatin1String("->")) ? ink("35", part) : ink("2", part);
                }
                return painted;
            }
            return text;
        });
        for (const QiForeignKeyInfo &f : t.foreignKeys)
            if (f.columns.size() > 1)
                out << "  (" << f.columns.join(", ") << ") -> " << f.refTable << '(' << f.refColumns.join(", ") << ")\n";
        for (const QiIndexInfo &i : t.indexes)
            if (!i.implicit)
                out << "  " << ink("2", QStringLiteral("index")) << ' ' << i.name << ink("2", QStringLiteral(" (%1)").arg(i.columns.join(", ")))
                    << (i.unique ? ink("33", QStringLiteral(" unique")) : QString()) << '\n';
    }
    return Ok;
}

// --- models / project -------------------------------------------------------

int models(const Args &a, QTextStream &out, QTextStream &err)
{
    if (a.positional.size() != 2) { err << "usage: qivot-cli models <db> [--table T]... [-o models.h]\n"; return Failed; }
    DatabaseSession s; Opened keep; QString error;
    if (!openDb(a.positional.at(1), s, keep, a.value("history", "qivot_migrations"), error)) { err << errorTag() << error << '\n'; return Failed; }

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
    if (!openDb(a.positional.at(1), s, keep, a.value("history", "qivot_migrations"), error)) { err << errorTag() << error << '\n'; return Failed; }

    ProjectExport pe;
    pe.setSession(&s);
    const QString name = a.value("name", pe.suggestedName());
    const QDir target(dir);
    if (target.exists() && !target.isEmpty() && !a.has("force")) {
        err << errorTag() << dir << " isn't empty (--force writes over it)\n";
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
        err << errorTag() << error << '\n';
        return Failed;
    }
    const QStringList changes = compare.changes();
    const QString sql = compare.migration();

    if (!a.value("output").isEmpty() || a.has("sql")) {
        if (!changes.isEmpty() && !writeOut(sql, a.value("output"), out, err))
            return Failed;
    } else if (changes.isEmpty()) {
        out << ink("1;32", QStringLiteral("No differences: ")) << shown(a.positional.at(2)) << " matches " << shown(a.positional.at(1)) << ".\n";
    } else {
        out << ink("1;33", QStringLiteral("%1 %2").arg(changes.size()).arg(changes.size() == 1 ? "difference" : "differences"))
            << " to make " << ink("1", shown(a.positional.at(2))) << " match " << ink("1", shown(a.positional.at(1))) << ":\n";
        for (const QString &c : changes)
            out << "  " << changeColour(c) << '\n';
        if (!compare.sameDialect())
            out << "(" << s.dialectName() << " and " << compare.other()->dialectName()
                << " write types differently, so most columns differ in type.)\n";
        out << "\n" << sqlColour(sql);
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
        err << errorTag() << error << '\n';
        return Failed;
    }
    const QStringList changes = compare.changes();
    if (changes.isEmpty()) {
        out << ink("1;32", QStringLiteral("No differences: ")) << shown(current) << " already matches " << shown(desired) << ". Nothing written.\n";
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
        out << ink("32", QStringLiteral("wrote ")) << QDir::toNativeSeparators(f.fileName()) << '\n';
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
    out << ink("1", QStringLiteral("%1 %2").arg(changes.size()).arg(changes.size() == 1 ? "change" : "changes")) << ":\n";
    for (const QString &c : changes)
        out << "  " << changeColour(c) << '\n';
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
        err << errorTag() << error << '\n';
        return Failed;
    }
    if (writes && !s.allowChanges(true)) {
        err << errorTag() << s.error() << '\n';
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
        m.setUseUserVersion(false);   // user_version may mean anything here
        m.setTable(a.value("history", "qivot_migrations"));
        if (m.addDirectory(a.value("dir")) < 0) {
            err << errorTag() << m.lastError() << '\n';
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
                out << ink("1", shown(a.positional.at(2))) << ": version " << ink("1", QString::number(m.currentVersion()))
                    << " of " << m.targetVersion();
                if (pending) out << ", " << ink("33", QStringLiteral("%1 pending").arg(pending));
                if (changed) out << ", " << ink("31", QStringLiteral("%1 changed since they ran").arg(changed));
                if (!pending && !changed) out << ink("32", QStringLiteral(", up to date"));
                out << "\n\n" << table({ "version", "name", "state", "applied (UTC)", "took" }, rows, 2,
                                        [](int row, int column, const QString &text) {
                    if (row < 0)
                        return ink("1;2", text);
                    if (column == 2)
                        return ink(text == QLatin1String("applied") ? "32" : text == QLatin1String("pending") ? "33" : "1;31", text);
                    return column >= 3 ? ink("2", text) : text;
                });
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
                err << errorTag() << m.lastError() << '\n';
                result = Failed;
            } else {
                out << ink("1;32", n == 0 ? QStringLiteral("Up to date") : QStringLiteral("Applied %1").arg(n))
                    << ": version " << m.currentVersion() << ".\n";
            }
        } else if (action == QLatin1String("down")) {
            const int n = m.rollback(a.value("to").toInt());
            if (n < 0) {
                err << errorTag() << m.lastError() << '\n';
                result = Failed;
            } else {
                out << ink("1;33", QStringLiteral("Undid ")) << n << (n == 1 ? " migration" : " migrations") << ": version " << m.currentVersion() << ".\n";
            }
        } else {    // accept
            const int n = m.changed().size();
            if (!m.acceptChecksums()) {
                err << errorTag() << m.lastError() << '\n';
                result = Failed;
            } else {
                out << ink("1;32", QStringLiteral("Accepted ")) << n << (n == 1 ? " edited migration" : " edited migrations") << ".\n";
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

// --- replay -----------------------------------------------------------------

QString oneLine(const QString &sql, int width = 96)
{
    QString s = sql.simplified();
    return s.size() > width ? s.left(width - 1) + QChar(0x2026) : s;
}

QString msText(double ms)
{
    return ms < 0 ? QStringLiteral("?") : ms < 10 ? QString::number(ms, 'f', 2) + QStringLiteral(" ms")
                                       : QString::number(ms, 'f', 0) + QStringLiteral(" ms");
}

QJsonObject sideJson(const QueryReplay::Side &s)
{
    QJsonObject o{ { "ok", s.ok }, { "rows", double(s.rows) }, { "ms", s.ms } };
    if (!s.error.isEmpty()) o.insert("error", s.error);
    if (!s.plan.isEmpty()) { o.insert("plan", s.plan); o.insert("scans", s.scans); }
    return o;
}

int replay(const Args &a, QTextStream &out, QTextStream &err)
{
    if (a.positional.size() != 2 || a.value("record").isEmpty() || a.value("dir").isEmpty()) {
        err << "usage: qivot-cli replay <db> --record app.qrec --dir <dir> [--sandbox] [--json] [--exit-code]\n";
        return Failed;
    }
    DatabaseSession s; Opened keep; QString error;
    if (!openDb(a.positional.at(1), s, keep, a.value("history", "qivot_migrations"), error)) { err << errorTag() << error << '\n'; return Failed; }

    QueryReplay::Options o;
    o.dir = a.value("dir");
    o.history = a.value("history", o.history);
    o.sandbox = a.has("sandbox");
    if (a.has("slower")) o.slower = qMax(1.1, a.value("slower").toDouble());
    const QueryReplay::Report r = QueryReplay::run(s, a.value("record"), o);
    if (!r.ok) { err << errorTag() << r.error << '\n'; return Failed; }

    const int bad = r.count("breaks") + r.count("columns") + r.count("different");
    const int code = a.has("exit-code") && (bad || !r.migrationError.isEmpty()) ? Differs : Ok;

    if (a.has("json")) {
        QJsonArray items;
        for (const QueryReplay::Item &i : r.items)
            items << QJsonObject{ { "sql", i.sql }, { "runs", double(i.runs) }, { "verdict", i.verdict },
                                  { "columnsGone", QJsonArray::fromStringList(i.columnsGone) },
                                  { "columnsNew", QJsonArray::fromStringList(i.columnsNew) },
                                  { "planChanged", i.planChanged }, { "before", sideJson(i.before) }, { "after", sideJson(i.after) } };
        QJsonObject o{ { "database", r.database }, { "dialect", r.dialect },
                       { "migrations", QJsonArray::fromStringList(r.migrations) },
                       { "recordedRuns", double(r.recordedRuns) }, { "skipped", r.skipped }, { "queries", items } };
        QJsonObject other;
        for (auto it = r.otherDrivers.constBegin(); it != r.otherDrivers.constEnd(); ++it)
            other.insert(it.key(), it.value());
        o.insert("recordedOnOtherDatabases", other);
        if (!r.migrationError.isEmpty()) o.insert("migrationError", r.migrationError);
        out << json(o);
        return code;
    }

    out << ink("1", QStringLiteral("Replayed %1 %2").arg(r.items.size()).arg(r.items.size() == 1 ? "query" : "queries"))
        << " (" << r.recordedRuns << " runs recorded) on " << ink("1", r.database)
        << ", before and after " << r.migrations.size() << (r.migrations.size() == 1 ? " pending migration:\n" : " pending migrations:\n");
    for (const QString &m : r.migrations)
        out << "  " << ink("36", m) << '\n';
    static const QMap<QString, QString> kinds = { { "QSQLITE", "SQLite" }, { "QPSQL", "PostgreSQL" },
                                                  { "QMYSQL", "MySQL" }, { "QODBC", "SQL Server" }, { "QOCI", "Oracle" } };
    for (auto it = r.otherDrivers.constBegin(); it != r.otherDrivers.constEnd(); ++it)
        out << ink("33", QStringLiteral("\nNote: %1 of these were recorded on %2, so their SQL may not run on %3.\n")
                             .arg(it.value()).arg(kinds.value(it.key(), it.key()), s.dialectName()));
    if (!r.migrationError.isEmpty()) {
        out << '\n' << ink("1;31", QStringLiteral("The migrations themselves fail")) << ", so nothing could be replayed:\n  "
            << r.migrationError << '\n';
        return code;
    }

    struct Group { const char *verdict, *mark, *colour, *title; };
    static const Group groups[] = {
        { "breaks",    "x", "1;31", "break after the migrations" },
        { "columns",   "!", "1;31", "return different columns (a model field would silently lose its value)" },
        { "different", "!", "1;31", "return different rows" },
        { "slower",    "^", "1;33", "got slower" },
        { "failing",   "-", "2",    "fail before and after (already broken, or another database's SQL)" },
        { "fixed",     "+", "32",   "fail now and work after" },
        { "faster",    "v", "32",   "got faster" },
    };
    for (const Group &g : groups) {
        QVector<const QueryReplay::Item *> list;
        for (const QueryReplay::Item &i : r.items)
            if (i.verdict == QLatin1String(g.verdict)) list << &i;
        if (list.isEmpty())
            continue;
        out << '\n' << ink(g.colour, QStringLiteral("%1 %2 %3").arg(QLatin1String(g.mark)).arg(list.size()).arg(QLatin1String(g.title))) << '\n';
        for (const QueryReplay::Item *i : list) {
            out << "  " << oneLine(i->sql) << ink("2", QStringLiteral("  (%1 %2)").arg(i->runs).arg(i->runs == 1 ? "run" : "runs")) << '\n';
            const QString v = QLatin1String(g.verdict);
            if (v == QLatin1String("breaks"))
                out << "    " << ink("31", QStringLiteral("after: ")) << i->after.error << '\n';
            else if (v == QLatin1String("fixed") || v == QLatin1String("failing"))
                out << "    " << ink("2", QStringLiteral("before: ")) << i->before.error << '\n';
            else if (v == QLatin1String("columns")) {
                if (!i->columnsGone.isEmpty()) out << "    " << ink("31", QStringLiteral("gone: ")) << i->columnsGone.join(", ") << '\n';
                if (!i->columnsNew.isEmpty())  out << "    " << ink("33", QStringLiteral("new:  ")) << i->columnsNew.join(", ") << '\n';
            } else if (v == QLatin1String("different"))
                out << "    " << (i->before.rows == i->after.rows
                                    ? QStringLiteral("the same number of rows, with different values")
                                    : QStringLiteral("%1 rows before, %2 after").arg(i->before.rows).arg(i->after.rows)) << '\n';
            else if (v == QLatin1String("slower") || v == QLatin1String("faster"))
                out << "    " << msText(i->before.ms) << " -> " << ink(v == QLatin1String("slower") ? "1;33" : "32", msText(i->after.ms))
                    << QStringLiteral("  (%1x)").arg(i->before.ms > 0 ? QString::number(i->after.ms / i->before.ms, 'f', 1) : QStringLiteral("?")) << '\n';
            if (i->planChanged && (v == QLatin1String("slower") || v == QLatin1String("faster") || v == QLatin1String("different"))) {
                out << "    " << ink("2", QStringLiteral("plan before:")) << '\n';
                for (const QString &l : i->before.plan.split(QLatin1Char('\n'))) out << "      " << ink("2", l) << '\n';
                out << "    " << ink("2", QStringLiteral("plan after:")) << '\n';
                for (const QString &l : i->after.plan.split(QLatin1Char('\n'))) out << "      " << l << '\n';
            }
        }
    }
    const int same = r.count("same");
    int changedPlans = 0;
    for (const QueryReplay::Item &i : r.items)
        changedPlans += i.verdict == QLatin1String("same") && i.planChanged;
    out << '\n' << ink("32", QStringLiteral("= %1 unchanged").arg(same));
    if (changedPlans) out << ink("2", QStringLiteral(" (%1 with a different plan, no slower)").arg(changedPlans));
    if (r.skipped) out << ink("2", QStringLiteral(", %1 other statements skipped (CREATE, PRAGMA, ...)").arg(r.skipped));
    out << '\n';
    if (bad)
        out << ink("1;31", QStringLiteral("\n%1 of the app's queries won't work as they did after these migrations.\n").arg(bad));
    else
        out << ink("1;32", QStringLiteral("\nNothing the app ran breaks or changes its results.\n"));
    return code;
}

int query(const Args &a, QTextStream &out, QTextStream &err)
{
    if (a.positional.size() != 3) { err << "usage: qivot-cli query <db> <sql> [--format table|csv|tsv|json] [-o file]\n"; return Failed; }
    const QString format = a.value("format", a.has("json") ? QStringLiteral("json") : QStringLiteral("table"));
    if (!QStringList{ "table", "csv", "tsv", "json" }.contains(format)) { err << "qivot-cli: --format is table, csv, tsv or json\n"; return Failed; }

    DatabaseSession s; Opened keep; QString error;
    if (!openDb(a.positional.at(1), s, keep, a.value("history", "qivot_migrations"), error)) { err << errorTag() << error << '\n'; return Failed; }

    QSqlQuery q(QSqlDatabase::database(s.connectionName()));
    q.setForwardOnly(true);
    if (!q.exec(a.positional.at(2))) {
        err << errorTag() << q.lastError().text().trimmed() << '\n';
        return Failed;
    }
    const QString path = a.value("output");
    if (!path.isEmpty() && (format == QLatin1String("csv") || format == QLatin1String("json"))) {
        const QVariantMap r = DataTransfer::write(q, path, format);    // streams, for big results
        if (!r.value("ok").toBool()) { err << errorTag() << r.value("error").toString() << '\n'; return Failed; }
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
        const bool toTerminal = path.isEmpty() || path == QLatin1String("-");
        text = table(header, rows, 0, [toTerminal](int row, int, const QString &v) {
                   if (!toTerminal) return v;
                   return row < 0 ? ink("1;36", v) : v == QLatin1String("NULL") ? ink("2", v) : v;
               })
             + (toTerminal ? ink("2", QStringLiteral("(%1 %2)").arg(count).arg(count == 1 ? "row" : "rows"))
                           : QStringLiteral("(%1 %2)").arg(count).arg(count == 1 ? "row" : "rows"))
             + QLatin1Char('\n');
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

void setColors(bool out, bool err)
{
    g_colorOut = out;
    g_colorErr = err;
}

void useTerminal()
{
    const QString force = qEnvironmentVariable("FORCE_COLOR");
    const bool forced = qEnvironmentVariableIsSet("FORCE_COLOR") && force != QLatin1String("0");
    const bool off = !qEnvironmentVariable("NO_COLOR").isEmpty() || qEnvironmentVariable("TERM") == QLatin1String("dumb");
#ifdef Q_OS_WIN
    const bool outTty = _isatty(_fileno(stdout)), errTty = _isatty(_fileno(stderr));
#else
    const bool outTty = isatty(fileno(stdout)), errTty = isatty(fileno(stderr));
#endif
    const bool out = forced || (!off && outTty), err = forced || (!off && errTty);
#ifdef Q_OS_WIN
    if (out || err) {
        SetConsoleOutputCP(CP_UTF8);          // the banner's block characters
        for (const DWORD which : { STD_OUTPUT_HANDLE, STD_ERROR_HANDLE }) {
            const HANDLE h = GetStdHandle(which);
            DWORD mode = 0;
            if (GetConsoleMode(h, &mode))
                SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif
    setColors(out, err);
}

int run(const QStringList &argv, QTextStream &out, QTextStream &err)
{
    Args a;
    QString error;
    if (!parse(argv, a, error)) {
        err << errorTag() << error << "\n(qivot-cli --help lists the commands)\n";
        return Failed;
    }
    if (a.has("version")) {
        out << "qivot-cli " << QCoreApplication::applicationVersion() << '\n';
        return Ok;
    }
    const QString command = a.positional.value(0);
    if (a.has("help") || command.isEmpty() || command == QLatin1String("help")) {
        out << usage();
        return command.isEmpty() && !a.has("help") ? Failed : Ok;
    }

    int code = Failed;
    if (command == QLatin1String("inspect"))      code = inspect(a, out, err);
    else if (command == QLatin1String("models"))  code = models(a, out, err);
    else if (command == QLatin1String("project")) code = project(a, out, err);
    else if (command == QLatin1String("diff"))    code = diff(a, out, err);
    else if (command == QLatin1String("migrate")) code = migrate(a, out, err);
    else if (command == QLatin1String("query"))   code = query(a, out, err);
    else if (command == QLatin1String("replay"))  code = replay(a, out, err);
    else if (command == QLatin1String("drivers")) code = drivers(a, out, err);
    else err << "qivot-cli: no command " << command << "\n(qivot-cli --help lists the commands)\n";
    out.flush();
    err.flush();
    return code;
}

} // namespace QivotCli
