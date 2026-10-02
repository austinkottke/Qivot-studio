#include "gridtools.h"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <limits>

namespace {

int roleNamed(const QAbstractItemModel *model, const QByteArray &name, int fallback)
{
    const QHash<int, QByteArray> roles = model->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == name)
            return it.key();
    return fallback;
}

// The block, clipped to the model; false when nothing of it is in there.
bool clip(const QAbstractItemModel *model, int &top, int &left, int &bottom, int &right)
{
    if (!model)
        return false;
    if (top > bottom) std::swap(top, bottom);
    if (left > right) std::swap(left, right);
    top = qMax(0, top);
    left = qMax(0, left);
    bottom = qMin(bottom, model->rowCount() - 1);
    right = qMin(right, model->columnCount() - 1);
    return top <= bottom && left <= right;
}

QString plain(const QVariant &v)
{
    if (v.isNull())
        return QString();
    switch (v.userType()) {
    case QMetaType::QDateTime: return v.toDateTime().toString(Qt::ISODate);
    case QMetaType::QDate:     return v.toDate().toString(Qt::ISODate);
    case QMetaType::QTime:     return v.toTime().toString(Qt::ISODate);
    case QMetaType::QByteArray: {
        const QByteArray b = v.toByteArray();
        return QString::fromUtf8(b);
    }
    default:
        return v.toString();
    }
}

QString field(const QString &s, QChar separator)
{
    if (!s.contains(separator) && !s.contains(QLatin1Char('"')) && !s.contains(QLatin1Char('\n'))
        && !s.contains(QLatin1Char('\r')))
        return s;
    QString q = s;
    q.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

// A value as a number, if it is one (a number, or text that reads as one).
bool number(const QVariant &v, double *out)
{
    switch (v.userType()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:
    case QMetaType::Double: case QMetaType::Float: case QMetaType::Long: case QMetaType::ULong:
    case QMetaType::Short: case QMetaType::UShort:
        *out = v.toDouble();
        return true;
    case QMetaType::QString: {
        // Drivers hand NUMERIC/DECIMAL back as text.
        bool ok = false;
        *out = v.toString().trimmed().toDouble(&ok);
        return ok;
    }
    default:
        return false;
    }
}

} // namespace

QString GridTools::text(QAbstractItemModel *model, int top, int left, int bottom, int right,
                        const QString &format, bool headers) const
{
    if (!clip(model, top, left, bottom, right))
        return QString();
    bottom = qMin(bottom, top + MaxCopyRows - 1);
    const QChar sep = format == QLatin1String("csv") ? QLatin1Char(',') : QLatin1Char('\t');
    const int raw = roleNamed(model, "raw", Qt::DisplayRole);
    QString out;
    if (headers) {
        QStringList names;
        for (int c = left; c <= right; ++c)
            names << field(model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString(), sep);
        out += names.join(sep) + QLatin1Char('\n');
    }
    for (int r = top; r <= bottom; ++r) {
        QStringList cells;
        for (int c = left; c <= right; ++c)
            cells << field(plain(model->data(model->index(r, c), raw)), sep);
        out += cells.join(sep) + QLatin1Char('\n');
    }
    return out;
}

QVariantMap GridTools::summary(QAbstractItemModel *model, int top, int left, int bottom, int right) const
{
    QVariantMap out{ { QStringLiteral("cells"), 0 } };
    if (!clip(model, top, left, bottom, right))
        return out;
    const qint64 cells = qint64(bottom - top + 1) * (right - left + 1);
    out.insert(QStringLiteral("cells"), cells);
    if (cells > MaxSummaryCells) {
        out.insert(QStringLiteral("tooMany"), true);
        return out;
    }
    const int raw = roleNamed(model, "raw", Qt::DisplayRole);
    qint64 nulls = 0, numbers = 0;
    double sum = 0, lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
    for (int r = top; r <= bottom; ++r) {
        for (int c = left; c <= right; ++c) {
            const QVariant v = model->data(model->index(r, c), raw);
            if (v.isNull()) {
                ++nulls;
                continue;
            }
            double d;
            if (number(v, &d)) {
                ++numbers;
                sum += d;
                lo = qMin(lo, d);
                hi = qMax(hi, d);
            }
        }
    }
    out.insert(QStringLiteral("values"), cells - nulls);
    out.insert(QStringLiteral("nulls"), nulls);
    out.insert(QStringLiteral("numbers"), numbers);
    if (numbers > 0) {
        out.insert(QStringLiteral("sum"), sum);
        out.insert(QStringLiteral("avg"), sum / numbers);
        out.insert(QStringLiteral("min"), lo);
        out.insert(QStringLiteral("max"), hi);
    }
    return out;
}
