#include "connectionhistory.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <algorithm>

static const char *kKey = "connections/entries";

ConnectionHistory::ConnectionHistory(QObject *parent)
    : QObject(parent)
{
    load();
}

void ConnectionHistory::load()
{
    // Kept as JSON text, which reads back the same from a plist, an INI file or the registry.
    m_entries = QJsonDocument::fromJson(QSettings().value(QLatin1String(kKey)).toString().toUtf8())
                    .array().toVariantList();
}

void ConnectionHistory::store()
{
    QSettings().setValue(QLatin1String(kKey),
                         QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(m_entries)).toJson(QJsonDocument::Compact)));
    emit changed();
}

QString ConnectionHistory::idOf(const QVariantMap &s)
{
    const QString type = s.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("sqlite") || type == QLatin1String("duckdb"))
        return QStringLiteral("file:") + QFileInfo(s.value(QStringLiteral("path")).toString()).absoluteFilePath();
    const QVariantMap ssh = s.value(QStringLiteral("ssh")).toMap();
    QString id = QStringLiteral("%1:%2@%3:%4/%5")
                     .arg(type, s.value(QStringLiteral("user")).toString(), s.value(QStringLiteral("host")).toString().toLower())
                     .arg(s.value(QStringLiteral("port")).toInt())
                     .arg(s.value(QStringLiteral("database")).toString());
    if (!ssh.value(QStringLiteral("host")).toString().isEmpty())
        id += QStringLiteral(" via ") + ssh.value(QStringLiteral("user")).toString() + QLatin1Char('@')
              + ssh.value(QStringLiteral("host")).toString().toLower();
    return id;
}

QString ConnectionHistory::remember(const QVariantMap &settingsIn)
{
    QVariantMap settings = settingsIn;
    settings.remove(QStringLiteral("password"));
    if (settings.value(QStringLiteral("type")).toString().isEmpty())
        return QString();
    const QString id = idOf(settings);
    QVariantMap e;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).toMap().value(QStringLiteral("id")).toString() == id) {
            e = m_entries.takeAt(i).toMap();
            break;
        }
    }
    e.insert(QStringLiteral("id"), id);
    e.insert(QStringLiteral("settings"), settings);
    e.insert(QStringLiteral("at"), QDateTime::currentDateTime().toString(Qt::ISODate));
    m_entries.prepend(e);
    // Recent ones beyond the limit go; saved ones stay.
    int recent = 0;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).toMap().value(QStringLiteral("saved")).toBool())
            continue;
        if (++recent > MaxRecent)
            m_entries.removeAt(i--);
    }
    store();
    return id;
}

void ConnectionHistory::setSaved(const QString &id, bool saved, const QString &label)
{
    for (QVariant &v : m_entries) {
        QVariantMap e = v.toMap();
        if (e.value(QStringLiteral("id")).toString() != id)
            continue;
        e.insert(QStringLiteral("saved"), saved);
        if (!label.trimmed().isEmpty())
            e.insert(QStringLiteral("label"), label.trimmed());
        v = e;
        store();
        return;
    }
}

void ConnectionHistory::rename(const QString &id, const QString &label)
{
    for (QVariant &v : m_entries) {
        QVariantMap e = v.toMap();
        if (e.value(QStringLiteral("id")).toString() != id)
            continue;
        e.insert(QStringLiteral("label"), label.trimmed());
        v = e;
        store();
        return;
    }
}

void ConnectionHistory::forget(const QString &id)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).toMap().value(QStringLiteral("id")).toString() == id) {
            m_entries.removeAt(i);
            store();
            return;
        }
    }
}

QVariantMap ConnectionHistory::entry(const QString &id) const
{
    for (const QVariant &v : m_entries)
        if (v.toMap().value(QStringLiteral("id")).toString() == id)
            return decorate(v.toMap());
    return {};
}

// What the lists show: a title, where it is, and whether a file is still there.
QVariantMap ConnectionHistory::decorate(const QVariantMap &stored) const
{
    QVariantMap e = stored;
    const QVariantMap s = e.value(QStringLiteral("settings")).toMap();
    const QString type = s.value(QStringLiteral("type")).toString();
    const QString label = e.value(QStringLiteral("label")).toString();
    QString title, detail;
    if (type == QLatin1String("sqlite") || type == QLatin1String("duckdb")) {
        const QFileInfo fi(s.value(QStringLiteral("path")).toString());
        title = fi.fileName();
        detail = fi.absolutePath();
        e.insert(QStringLiteral("kind"), QStringLiteral("file"));
        e.insert(QStringLiteral("missing"), !fi.exists());
    } else {
        const QString kind = type == QLatin1String("redis") ? QStringLiteral("Redis")
                           : type == QLatin1String("postgres") ? QStringLiteral("PostgreSQL")
                           : type == QLatin1String("mysql")    ? QStringLiteral("MySQL")
                           : type == QLatin1String("sqlserver") ? QStringLiteral("SQL Server") : type;
        title = type == QLatin1String("redis")
                    ? s.value(QStringLiteral("host")).toString() + QLatin1Char(':') + QString::number(s.value(QStringLiteral("port")).toInt())
                          + (s.value(QStringLiteral("database")).toInt() ? QStringLiteral(" · db ") + s.value(QStringLiteral("database")).toString() : QString())
                    : s.value(QStringLiteral("database")).toString();
        detail = QStringLiteral("%1 · %2@%3:%4").arg(kind, s.value(QStringLiteral("user")).toString(),
                                                      s.value(QStringLiteral("host")).toString())
                     .arg(s.value(QStringLiteral("port")).toInt());
        const QString sshHost = s.value(QStringLiteral("ssh")).toMap().value(QStringLiteral("host")).toString();
        if (!sshHost.isEmpty())
            detail += QStringLiteral(" via ") + sshHost;
        e.insert(QStringLiteral("kind"), QStringLiteral("server"));
        e.insert(QStringLiteral("missing"), false);
    }
    e.insert(QStringLiteral("title"), label.isEmpty() ? title : label);
    e.insert(QStringLiteral("detail"), label.isEmpty() ? detail : title + QStringLiteral(" · ") + detail);
    e.insert(QStringLiteral("saved"), e.value(QStringLiteral("saved")).toBool());
    return e;
}

QVariantList ConnectionHistory::entries() const
{
    QVariantList saved, recent;
    for (const QVariant &v : m_entries) {
        const QVariantMap e = decorate(v.toMap());
        (e.value(QStringLiteral("saved")).toBool() ? saved : recent) << e;
    }
    std::sort(saved.begin(), saved.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("title")).toString()
                   .compare(b.toMap().value(QStringLiteral("title")).toString(), Qt::CaseInsensitive) < 0;
    });
    return saved + recent;                  // recent ones are kept latest first
}
