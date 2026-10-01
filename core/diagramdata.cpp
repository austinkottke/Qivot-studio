#include "diagramdata.h"

#include "erlayout.h"

#include <QSet>
#include <algorithm>


// Card geometry shared with the QML that draws the cards.
namespace {
constexpr double kHeaderHeight = 40;
constexpr double kRowHeight    = 24;
constexpr double kCardPadding  = 6;
constexpr double kMinWidth     = 190;
constexpr double kMaxWidth     = 340;
}

QVariantMap DiagramData::build(const QVector<QiTableInfo> &infos, const QHash<QString, qint64> &rowCounts)
{
    QVector<ErLayout::Box> boxes;
    for (const QiTableInfo &t : infos) {
        if (t.kind != QiTableInfo::Table)
            continue;
        // Wide enough for the title and the longest "name   type" row (approximate glyph widths).
        int longest = 0;
        for (const QiColumnInfo &c : t.columns)
            longest = std::max<int>(longest, c.name.size() + c.type.size());
        const double width = qBound(kMinWidth, std::max(t.name.size() * 9.0 + 90, longest * 7.4 + 70), kMaxWidth);
        ErLayout::Box b;
        b.name = t.name;
        b.width = width;
        b.height = kHeaderHeight + t.columns.size() * kRowHeight + kCardPadding;
        for (const QiForeignKeyInfo &fk : t.foreignKeys)
            b.references << fk.refTable;
        boxes << b;
    }
    ErLayout::Options options;
    options.topMargin = 80;          // keeps the floating Find / summary controls off the cards
    const QHash<QString, QPointF> at = ErLayout::layout(boxes, options);

    QVariantList tables, links;
    double right = 0, bottom = 0;
    for (const ErLayout::Box &b : boxes) {
        const QiTableInfo *t = nullptr;
        for (const QiTableInfo &candidate : infos)
            if (candidate.name == b.name)
                t = &candidate;
        QSet<QString> fkColumns;
        for (const QiForeignKeyInfo &fk : t->foreignKeys) {
            for (const QString &c : fk.columns)
                fkColumns << c;
            links << QVariantMap{
                { QStringLiteral("from"),        t->name },
                { QStringLiteral("fromColumns"), fk.columns },
                { QStringLiteral("to"),          fk.refTable },
                { QStringLiteral("toColumns"),   fk.refColumns },
                { QStringLiteral("onDelete"),    fk.onDelete },
            };
        }
        QVariantList columns;
        for (const QiColumnInfo &c : t->columns)
            columns << QVariantMap{
                { QStringLiteral("name"),       c.name },
                { QStringLiteral("type"),       c.type },
                { QStringLiteral("primaryKey"), c.primaryKey },
                { QStringLiteral("foreignKey"), fkColumns.contains(c.name) },
            };
        const QPointF p = at.value(b.name);
        tables << QVariantMap{
            { QStringLiteral("name"),    b.name },
            { QStringLiteral("rows"),    rowCounts.value(b.name, -1) },
            { QStringLiteral("x"),       p.x() },
            { QStringLiteral("y"),       p.y() },
            { QStringLiteral("width"),   b.width },
            { QStringLiteral("height"),  b.height },
            { QStringLiteral("columns"), columns },
        };
        right  = std::max(right, p.x() + b.width);
        bottom = std::max(bottom, p.y() + b.height);
    }
    return QVariantMap{
        { QStringLiteral("tables"),       tables },
        { QStringLiteral("links"),        links },
        { QStringLiteral("width"),        right + 40 },
        { QStringLiteral("height"),       bottom + 40 },
        { QStringLiteral("headerHeight"), kHeaderHeight },
        { QStringLiteral("rowHeight"),    kRowHeight },
    };
}
