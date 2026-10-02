#include "querylibrary.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSettings>
#include <algorithm>

QueryLibrary::QueryLibrary(QObject *parent) : QObject(parent) {}

QString QueryLibrary::key(const char *what) const
{
    // The scope can be a path or host/database: hashed, it's a safe settings key.
    const QByteArray h = QCryptographicHash::hash(m_scope.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return QStringLiteral("queries/%1/%2").arg(QString::fromLatin1(h), QLatin1String(what));
}

void QueryLibrary::setScope(const QString &scope)
{
    if (scope == m_scope)
        return;
    m_scope = scope;
    load();
    emit changed();
}

void QueryLibrary::load()
{
    m_history.clear();
    m_saved.clear();
    if (m_scope.isEmpty())
        return;
    QSettings s;
    // Kept as text, readable in the registry or a plist; toString() also reads
    // what older versions stored as bytes.
    m_history = QJsonDocument::fromJson(s.value(key("history")).toString().toUtf8()).array().toVariantList();
    m_saved = QJsonDocument::fromJson(s.value(key("saved")).toString().toUtf8()).array().toVariantList();
}

void QueryLibrary::store()
{
    if (m_scope.isEmpty())
        return;
    QSettings s;
    s.setValue(key("history"), QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(m_history)).toJson(QJsonDocument::Compact)));
    s.setValue(key("saved"), QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(m_saved)).toJson(QJsonDocument::Compact)));
    s.setValue(key("scope"), m_scope);         // so a person reading the settings can tell what's whose
}

void QueryLibrary::record(const QString &sqlIn, int rows, int ms, bool ok, const QString &error)
{
    const QString sql = sqlIn.trimmed();
    if (sql.isEmpty())
        return;
    for (int i = 0; i < m_history.size(); ++i)
        if (m_history.at(i).toMap().value(QStringLiteral("sql")).toString() == sql)
            m_history.removeAt(i--);
    m_history.prepend(QVariantMap{ { QStringLiteral("sql"), sql },
                                   { QStringLiteral("at"), QDateTime::currentDateTime().toString(Qt::ISODate) },
                                   { QStringLiteral("rows"), rows }, { QStringLiteral("ms"), ms },
                                   { QStringLiteral("ok"), ok }, { QStringLiteral("error"), error } });
    while (m_history.size() > MaxHistory)
        m_history.removeLast();
    store();
    emit changed();
}

void QueryLibrary::save(const QString &nameIn, const QString &sql)
{
    const QString name = nameIn.trimmed();
    if (name.isEmpty() || sql.trimmed().isEmpty())
        return;
    remove(name);
    m_saved << QVariantMap{ { QStringLiteral("name"), name }, { QStringLiteral("sql"), sql.trimmed() },
                            { QStringLiteral("at"), QDateTime::currentDateTime().toString(Qt::ISODate) } };
    std::sort(m_saved.begin(), m_saved.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().compare(b.toMap().value(QStringLiteral("name")).toString(),
                                                                           Qt::CaseInsensitive) < 0;
    });
    store();
    emit changed();
}

bool QueryLibrary::hasName(const QString &name) const
{
    for (const QVariant &v : m_saved)
        if (v.toMap().value(QStringLiteral("name")).toString().compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

bool QueryLibrary::rename(const QString &from, const QString &to)
{
    if (to.trimmed().isEmpty() || (hasName(to) && to.trimmed().compare(from, Qt::CaseInsensitive) != 0))
        return false;
    for (const QVariant &v : std::as_const(m_saved)) {
        const QVariantMap q = v.toMap();
        if (q.value(QStringLiteral("name")).toString() == from) {
            save(to, q.value(QStringLiteral("sql")).toString());
            if (from.compare(to.trimmed(), Qt::CaseInsensitive) != 0)
                remove(from);
            return true;
        }
    }
    return false;
}

void QueryLibrary::remove(const QString &name)
{
    for (int i = 0; i < m_saved.size(); ++i)
        if (m_saved.at(i).toMap().value(QStringLiteral("name")).toString().compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            m_saved.removeAt(i--);
    store();
    emit changed();
}

void QueryLibrary::clearHistory()
{
    m_history.clear();
    store();
    emit changed();
}

QString QueryLibrary::suggestName(const QString &sql) const
{
    // The tables after FROM and JOIN: "book by author", "orders and customer".
    static const QRegularExpression from(QStringLiteral("\\b(?:FROM|JOIN)\\s+([A-Za-z_][\\w.]*)"),
                                         QRegularExpression::CaseInsensitiveOption);
    QStringList tables;
    auto it = from.globalMatch(sql);
    while (it.hasNext()) {
        const QString t = it.next().captured(1).section(QLatin1Char('.'), -1);
        if (!tables.contains(t))
            tables << t;
    }
    QString name = tables.isEmpty() ? tr("Query") : tables.size() == 1 ? tables.first()
                 : tables.mid(0, tables.size() - 1).join(QStringLiteral(", ")) + tr(" and ") + tables.last();
    QString candidate = name;
    for (int n = 2; hasName(candidate); ++n)
        candidate = QStringLiteral("%1 %2").arg(name).arg(n);
    return candidate;
}
