#ifndef CELLFORMAT_H
#define CELLFORMAT_H

#include <QChar>
#include <QMetaType>
#include <QString>
#include <QVariant>

/// How a value appears in a one-line grid cell. Shared by every grid in
/// Studio so a NULL, a blob or a multi-line string always looks the same.
namespace CellFormat {

constexpr int MaxChars = 300;      // a cell shows one line; the inspector shows all of it

inline QString display(const QVariant &v)
{
    if (v.isNull())
        return QStringLiteral("NULL");
    if (v.userType() == QMetaType::QByteArray)
        return QStringLiteral("BLOB · %1 bytes").arg(v.toByteArray().size());
    QString s = v.toString();
    s.replace(QLatin1Char('\n'), QStringLiteral(" ↵ "));
    if (s.size() > MaxChars)
        s = s.left(MaxChars) + QChar(0x2026);
    return s;
}

inline bool isNumber(const QVariant &v)
{
    const int t = v.userType();          // typeId() is Qt 6 only
    return t == QMetaType::Int || t == QMetaType::LongLong || t == QMetaType::Double
        || t == QMetaType::UInt || t == QMetaType::ULongLong || t == QMetaType::Float
        || t == QMetaType::Short || t == QMetaType::UShort;
}

}

#endif // CELLFORMAT_H
