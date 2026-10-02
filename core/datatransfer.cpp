#include "datatransfer.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlRecord>
#include <QTextStream>
#include <QUrl>

// ---- Writing ----

QString DataTransfer::localPath(const QVariant &fileOrUrl)
{
    const QUrl url = fileOrUrl.toUrl();
    return url.isLocalFile() ? url.toLocalFile() : fileOrUrl.toString();
}

namespace {

// A value as text: dates the ISO way, bytes as base64.
QString asText(const QVariant &v)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const int type = v.metaType().id();
#else
    const int type = v.userType();
#endif
    switch (type) {
    case QMetaType::QByteArray: return QString::fromLatin1(v.toByteArray().toBase64());
    case QMetaType::QDate:      return v.toDate().toString(Qt::ISODate);
    case QMetaType::QDateTime:  return v.toDateTime().toString(Qt::ISODate);
    default:                    return v.toString();
    }
}

QString csvField(const QVariant &v)
{
    if (v.isNull())
        return QString();
    QString s = asText(v);
    if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"')) || s.contains(QLatin1Char('\n'))
        || s.contains(QLatin1Char('\r')) || s.startsWith(QLatin1Char(' ')) || s.endsWith(QLatin1Char(' '))) {
        s.replace(QLatin1Char('"'), QLatin1String("\"\""));
        s = QLatin1Char('"') + s + QLatin1Char('"');
    }
    return s;
}

QJsonValue jsonValue(const QVariant &v)
{
    if (v.isNull())
        return QJsonValue::Null;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const int type = v.metaType().id();
#else
    const int type = v.userType();
#endif
    switch (type) {
    case QMetaType::Bool:
        return v.toBool();
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:
    case QMetaType::Short: case QMetaType::UShort: case QMetaType::Long: case QMetaType::ULong:
        return v.toLongLong();
    case QMetaType::Double: case QMetaType::Float:
        return v.toDouble();
    default:
        return asText(v);
    }
}

} // namespace

QVariantMap DataTransfer::write(QSqlQuery &query, const QString &path, const QString &format)
{
    QVariantMap out{ { QStringLiteral("ok"), false }, { QStringLiteral("rows"), 0 }, { QStringLiteral("path"), path } };
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out.insert(QStringLiteral("error"), QObject::tr("Couldn't write %1: %2").arg(QFileInfo(path).fileName(), f.errorString()));
        return out;
    }
    QTextStream s(&f);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    s.setCodec("UTF-8");
#endif
    const QSqlRecord record = query.record();
    QStringList names;
    for (int i = 0; i < record.count(); ++i)
        names << record.fieldName(i);
    const bool json = format == QLatin1String("json");
    qint64 rows = 0;
    if (json) {
        s << "[";
    } else {
        QStringList header;
        for (const QString &n : names)
            header << csvField(n);
        s << header.join(QLatin1Char(',')) << "\r\n";
    }
    while (query.next()) {
        if (json) {
            QJsonObject o;
            for (int i = 0; i < names.size(); ++i)
                o.insert(names.at(i), jsonValue(query.value(i)));
            s << (rows ? ",\n  " : "\n  ") << QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
        } else {
            QStringList cells;
            for (int i = 0; i < names.size(); ++i)
                cells << csvField(query.value(i));
            s << cells.join(QLatin1Char(',')) << "\r\n";
        }
        ++rows;
    }
    if (json)
        s << (rows ? "\n]\n" : "]\n");
    s.flush();
    if (query.lastError().isValid() && query.lastError().type() != QSqlError::NoError) {
        out.insert(QStringLiteral("error"), query.lastError().text());
        return out;
    }
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("rows"), rows);
    return out;
}

// ---- Reading ----

QChar DataTransfer::guessDelimiter(const QString &text)
{
    const QString first = text.left(text.indexOf(QLatin1Char('\n')));
    const int commas = first.count(QLatin1Char(',')), semis = first.count(QLatin1Char(';')), tabs = first.count(QLatin1Char('\t'));
    if (tabs > commas && tabs >= semis)
        return QLatin1Char('\t');
    if (semis > commas)
        return QLatin1Char(';');
    return QLatin1Char(',');
}

QVector<QStringList> DataTransfer::parseCsv(const QString &input, QChar delimiter)
{
    QString text = input;
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    QVector<QStringList> records;
    QStringList record;
    QString field;
    bool quoted = false, any = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == QLatin1Char('"') && field.isEmpty()) {
            quoted = true;
            any = true;
        } else if (c == delimiter) {
            record << field;
            field.clear();
            any = true;
        } else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            if (c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n'))
                ++i;
            if (any || !field.isEmpty()) {
                record << field;
                records << record;
            }
            record.clear();
            field.clear();
            any = false;
        } else {
            field += c;
            any = true;
        }
    }
    if (any || !field.isEmpty()) {
        record << field;
        records << record;
    }
    return records;
}

// ---- CsvImport ----

CsvImport::CsvImport(QObject *parent) : QObject(parent) {}

void CsvImport::setSession(DatabaseSession *session)
{
    if (session == m_session)
        return;
    m_session = session;
    emit sessionChanged();
    autoMap();
}

void CsvImport::setTable(const QString &table)
{
    if (table == m_table)
        return;
    m_table = table;
    emit tableChanged();
    autoMap();
}

void CsvImport::setHeaderRow(bool on)
{
    if (on == m_headerRow)
        return;
    m_headerRow = on;
    split();
    autoMap();
    emit loaded();
}

void CsvImport::setEmptyIsNull(bool on)
{
    if (on == m_emptyIsNull)
        return;
    m_emptyIsNull = on;
    emit emptyIsNullChanged();
}

QString CsvImport::delimiterName() const
{
    return m_delimiter == QLatin1Char('\t') ? tr("tabs") : m_delimiter == QLatin1Char(';') ? tr("semicolons") : tr("commas");
}

QStringList CsvImport::tableColumns() const
{
    QStringList out;
    if (m_session && m_session->isOpen())
        for (const QVariant &c : m_session->table(m_table).value(QStringLiteral("columns")).toList())
            out << c.toMap().value(QStringLiteral("name")).toString();
    return out;
}

QVariantList CsvImport::preview() const
{
    QVariantList out;
    for (int i = m_headerRow ? 1 : 0; i < m_records.size() && out.size() < 8; ++i)
        out << QVariant(m_records.at(i));
    return out;
}

int CsvImport::rowCount() const
{
    return qMax(0, int(m_records.size()) - (m_headerRow ? 1 : 0));
}

void CsvImport::setError(const QString &e)
{
    if (e == m_error)
        return;
    m_error = e;
    emit errorChanged();
}

bool CsvImport::load(const QVariant &fileOrUrl)
{
    const QString path = DataTransfer::localPath(fileOrUrl);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setError(tr("Couldn't open %1: %2").arg(QFileInfo(path).fileName(), f.errorString()));
        return false;
    }
    const QString text = QString::fromUtf8(f.readAll());
    m_fileName = QFileInfo(path).fileName();
    m_delimiter = DataTransfer::guessDelimiter(text);
    m_records = DataTransfer::parseCsv(text, m_delimiter);
    m_imported = 0;
    split();
    autoMap();
    setError(m_records.isEmpty() ? tr("%1 is empty.").arg(m_fileName) : QString());
    emit loaded();
    return !m_records.isEmpty();
}

void CsvImport::split()
{
    int width = 0;
    for (const QStringList &r : m_records)
        width = qMax(width, int(r.size()));
    m_fileColumns.clear();
    for (int i = 0; i < width; ++i) {
        const QString named = m_headerRow && !m_records.isEmpty() ? m_records.first().value(i).trimmed() : QString();
        m_fileColumns << (named.isEmpty() ? tr("Column %1").arg(i + 1) : named);
    }
}

void CsvImport::autoMap()
{
    // By name, ignoring case, spaces and underscores: "First Name" goes into first_name.
    auto simple = [](QString s) { return s.toLower().remove(QLatin1Char(' ')).remove(QLatin1Char('_')); };
    const QStringList columns = tableColumns();
    m_mapping.clear();
    for (const QString &f : m_fileColumns) {
        QString match;
        for (const QString &c : columns)
            if (simple(c) == simple(f))
                match = c;
        m_mapping << match;
    }
    emit mappingChanged();
}

void CsvImport::setMapping(int index, const QString &column)
{
    if (index < 0 || index >= m_mapping.size() || m_mapping.at(index) == column)
        return;
    // One file column per table column: take it from any other.
    if (!column.isEmpty())
        for (QString &m : m_mapping)
            if (m == column)
                m.clear();
    m_mapping[index] = column;
    emit mappingChanged();
}

bool CsvImport::run()
{
    if (!m_session || !m_session->changesAllowed()) {
        setError(tr("Allow changes to the database first."));
        return false;
    }
    if (m_session->table(m_table).value(QStringLiteral("kind")).toString() != QLatin1String("table")) {
        setError(tr("Rows can only be imported into a table."));
        return false;
    }
    QList<int> used;
    QStringList names, marks;
    QSqlDatabase db = m_session->writeDatabase();
    for (int i = 0; i < m_mapping.size(); ++i)
        if (!m_mapping.at(i).isEmpty()) {
            used << i;
            names << db.driver()->escapeIdentifier(m_mapping.at(i), QSqlDriver::FieldName);
            marks << QStringLiteral("?");
        }
    if (used.isEmpty()) {
        setError(tr("Choose where at least one of the file's columns goes."));
        return false;
    }
    const QString target = m_session->sqlName(m_table).isEmpty()
                           ? db.driver()->escapeIdentifier(m_table, QSqlDriver::TableName) : m_session->sqlName(m_table);
    if (!db.transaction()) {
        setError(db.lastError().text());
        return false;
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)").arg(target, names.join(QStringLiteral(", ")),
                                                                  marks.join(QStringLiteral(", "))));
    int done = 0;
    for (int r = m_headerRow ? 1 : 0; r < m_records.size(); ++r) {
        const QStringList &record = m_records.at(r);
        for (int i : used) {
            const QString v = record.value(i);
            q.addBindValue(v.isEmpty() && m_emptyIsNull ? QVariant() : QVariant(v));
        }
        if (!q.exec()) {
            const QString why = q.lastError().text();
            q = QSqlQuery();
            db.rollback();
            setError(tr("Nothing was imported: line %1 of %2 failed — %3").arg(r + 1).arg(m_fileName, why));
            return false;
        }
        ++done;
    }
    if (!db.commit()) {
        db.rollback();
        setError(tr("Nothing was imported: %1").arg(db.lastError().text()));
        return false;
    }
    m_imported = done;
    setError(QString());
    emit finished();
    m_session->refresh();
    return true;
}
