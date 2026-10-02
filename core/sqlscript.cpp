#include "sqlscript.h"

#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

QStringList SqlScript::split(const QString &script)
{
    QStringList out;
    QString current;
    auto flush = [&] {
        const QString s = current.trimmed();
        if (!s.isEmpty())
            out << s;
        current.clear();
    };
    const int n = script.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = script.at(i);
        const QChar next = i + 1 < n ? script.at(i + 1) : QChar();
        // Comments: -- to the end of the line, /* ... */.
        if (c == QLatin1Char('-') && next == QLatin1Char('-')) {
            while (i < n && script.at(i) != QLatin1Char('\n'))
                ++i;
            current += QLatin1Char('\n');
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            i += 2;
            while (i + 1 < n && !(script.at(i) == QLatin1Char('*') && script.at(i + 1) == QLatin1Char('/')))
                ++i;
            ++i;
            current += QLatin1Char(' ');
            continue;
        }
        // Quoted text: copied as it is, doubled quotes included.
        if (c == QLatin1Char('\'') || c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('[')) {
            const QChar close = c == QLatin1Char('[') ? QLatin1Char(']') : c;
            current += c;
            for (++i; i < n; ++i) {
                current += script.at(i);
                if (script.at(i) == close) {
                    if (i + 1 < n && script.at(i + 1) == close && close != QLatin1Char(']')) {   // '' inside ''
                        current += script.at(++i);
                        continue;
                    }
                    break;
                }
            }
            continue;
        }
        if (c == QLatin1Char(';')) {
            flush();
            continue;
        }
        // GO on a line of its own (SQL Server).
        if ((c == QLatin1Char('G') || c == QLatin1Char('g')) && (i == 0 || script.at(i - 1) == QLatin1Char('\n'))) {
            int j = i + 2;
            if (j <= n && script.mid(i, 2).compare(QLatin1String("GO"), Qt::CaseInsensitive) == 0) {
                while (j < n && (script.at(j) == QLatin1Char(' ') || script.at(j) == QLatin1Char('\t') || script.at(j) == QLatin1Char('\r')))
                    ++j;
                if (j >= n || script.at(j) == QLatin1Char('\n')) {
                    flush();
                    i = j;
                    continue;
                }
            }
        }
        current += c;
    }
    flush();
    return out;
}

SqlScript::Result SqlScript::run(QSqlDatabase db, const QStringList &statements, bool transaction)
{
    static const QRegularExpression txControl(
        QStringLiteral("^(BEGIN|BEGIN TRANSACTION|START TRANSACTION|COMMIT|COMMIT TRANSACTION|END)$"),
        QRegularExpression::CaseInsensitiveOption);
    Result r;
    QSqlQuery q(db);
    // SQLite ignores PRAGMA foreign_keys inside a transaction: run those first.
    QStringList pragmas, body;
    for (const QString &s : statements) {
        if (transaction && txControl.match(s.simplified()).hasMatch())
            continue;
        (transaction && s.startsWith(QLatin1String("PRAGMA foreign_keys"), Qt::CaseInsensitive) ? pragmas : body) << s;
    }
    const bool sqlite = db.driverName() == QLatin1String("QSQLITE");
    if (transaction && sqlite && !pragmas.isEmpty())
        q.exec(pragmas.first());                         // the "off" one, before
    if (transaction && !db.transaction()) {
        r.ok = false;
        r.error = db.lastError().text();
        return r;
    }
    for (const QString &s : body) {
        if (!q.exec(s)) {
            r.ok = false;
            r.failedStatement = s;
            r.error = q.lastError().text();
            break;
        }
        ++r.done;
    }
    if (transaction) {
        if (r.ok && !db.commit()) {
            r.ok = false;
            r.error = db.lastError().text();
        }
        if (!r.ok)
            db.rollback();
    }
    if (transaction && sqlite && pragmas.size() > 1)
        q.exec(pragmas.last());                          // and "on", after
    return r;
}
