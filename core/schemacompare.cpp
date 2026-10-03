#include "schemacompare.h"
#include "codegen.h"
#include "datatransfer.h"
#include "schemadesign.h"
#include "sqlscript.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

// The tables (not views) of a database, with defaults written as SQL text the
// way the designer compares them, so equal defaults look equal.
QVector<QiTableInfo> comparable(const DatabaseSession *s, const QStringList &ignored)
{
    QVector<QiTableInfo> out;
    for (QiTableInfo t : s->tableInfos()) {
        if (t.kind != QiTableInfo::Table || ignored.contains(t.name, Qt::CaseInsensitive))
            continue;
        for (QiColumnInfo &c : t.columns) {
            const QString d = CodeGen::defaultExpression(c.defaultValue, s->dialect());
            c.defaultValue = d.isEmpty() || c.autoIncrement ? QVariant() : QVariant(d);
        }
        out << t;
    }
    return out;
}

const QiTableInfo *named(const QVector<QiTableInfo> &tables, const QString &name)
{
    for (const QiTableInfo &t : tables)
        if (t.name.compare(name, Qt::CaseInsensitive) == 0)
            return &t;
    return nullptr;
}

} // namespace

SchemaCompare::SchemaCompare(QObject *parent)
    : QObject(parent)
    , m_other(new DatabaseSession(this))
{
    connect(m_other, &DatabaseSession::openChanged, this, &SchemaCompare::compute);
}

void SchemaCompare::setSession(DatabaseSession *session)
{
    if (session == m_session)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    if (m_session)
        connect(m_session, &DatabaseSession::openChanged, this, &SchemaCompare::compute);
    emit sessionChanged();
    compute();
}

void SchemaCompare::setDirection(const QString &direction)
{
    if (direction == m_direction || (direction != QLatin1String("toOther") && direction != QLatin1String("toThis")))
        return;
    m_direction = direction;
    compute();
}

void SchemaCompare::setIgnored(const QStringList &tables)
{
    if (tables == m_ignored)
        return;
    m_ignored = tables;
    compute();
}

bool SchemaCompare::sameDialect() const
{
    return m_session && m_session->isOpen() && m_other->isOpen() && m_session->dialect() == m_other->dialect();
}

void SchemaCompare::compute()
{
    m_changes.clear();
    m_migration.clear();
    m_error.clear();
    if (m_session && m_session->isOpen() && m_other->isOpen()) {
        // The one being changed is the "database"; the other is the "design".
        const bool toOther = m_direction == QLatin1String("toOther");
        const DatabaseSession *from = toOther ? m_session.data() : m_other;
        const DatabaseSession *to = toOther ? m_other : m_session.data();
        const QVector<QiTableInfo> target = comparable(to, m_ignored);
        QVector<DesignTable> design;
        for (const QiTableInfo &t : comparable(from, m_ignored)) {
            DesignTable d;
            d.info = t;
            const QiTableInfo *match = named(target, t.name);
            d.origin = match ? match->name : QString();
            for (const QiColumnInfo &c : t.columns) {
                QString origin;
                if (match)
                    for (const QiColumnInfo &mc : match->columns)
                        if (mc.name.compare(c.name, Qt::CaseInsensitive) == 0)
                            origin = mc.name;
                d.columnOrigins << origin;
            }
            design << d;
        }
        m_changes = Migration::changes(target, design);
        m_migration = Migration::sql(target, design, to->dialect());
    }
    emit changed();
}

bool SchemaCompare::saveMigration(const QVariant &fileOrUrl)
{
    QFile f(DataTransfer::localPath(fileOrUrl));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = tr("Couldn't write %1: %2").arg(QFileInfo(f.fileName()).fileName(), f.errorString());
        emit changed();
        return false;
    }
    f.write(m_migration.toUtf8());
    return true;
}

QVariantMap SchemaCompare::applyToThis()
{
    auto fail = [this](const QString &message, const QString &statement = QString()) {
        m_error = message;
        emit changed();
        return QVariantMap{ { QStringLiteral("ok"), false }, { QStringLiteral("error"), message },
                            { QStringLiteral("failedStatement"), statement } };
    };
    if (!m_session || m_direction != QLatin1String("toThis"))
        return fail(tr("Only this database can be changed from here."));
    if (!m_session->changesAllowed())
        return fail(tr("Allow changes to %1 first.").arg(m_session->displayName()));
    if (m_migration.isEmpty())
        return fail(tr("There's nothing to apply: the two match."));
    QString backup;
    if (m_session->dialect() == QLatin1String("sqlite")) {
        const QFileInfo source(m_session->filePath());
        const QString name = QStringLiteral("%1.backup-%2.%3").arg(source.completeBaseName(),
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")),
            source.suffix().isEmpty() ? QStringLiteral("db") : source.suffix());
        backup = QFileInfo(source.absolutePath()).isWritable() ? source.dir().filePath(name)
                 : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/backups/") + name;
        QDir().mkpath(QFileInfo(backup).absolutePath());
        if (!QFile::copy(source.absoluteFilePath(), backup))
            return fail(tr("Couldn't back up %1 first, so nothing was changed.").arg(source.fileName()));
    }
    const SqlScript::Result r = SqlScript::run(m_session->writeDatabase(), SqlScript::split(m_migration), true);
    if (!r.ok) {
        if (!backup.isEmpty())
            QFile::remove(backup);
        return fail(tr("The migration failed, so nothing was changed: %1").arg(r.error), r.failedStatement);
    }
    m_session->refresh();                   // recomputes the comparison too
    return { { QStringLiteral("ok"), true }, { QStringLiteral("backup"), backup } };
}
