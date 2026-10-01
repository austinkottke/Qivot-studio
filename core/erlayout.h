#ifndef ERLAYOUT_H
#define ERLAYOUT_H

#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

/// Automatic layout for an ER diagram.
/**
  Tables flow left to right along their foreign keys: a table that references
  nothing sits in the first column, and every other table sits one column to
  the right of the furthest table it references (customer -> orders ->
  order_item). Within a column, tables are ordered to sit level with the
  tables they connect to, which keeps relationship lines short and reduces
  crossings. Tables with no relationships at all are set out in a grid below.

  Pure geometry, no Qt Quick — so it's fast and unit-tested.
 */
namespace ErLayout {

struct Box {
    QString     name;
    double      width = 0;
    double      height = 0;
    QStringList references;     ///< tables this one has foreign keys to
};

struct Options {
    double columnGap = 110;     ///< horizontal space between columns (room for the lines)
    double rowGap    = 40;      ///< vertical space between tables in a column
    double margin    = 40;
    double topMargin = 40;      ///< extra room at the top, e.g. for floating controls
};

/// Top-left position of every box, keyed by name.
QHash<QString, QPointF> layout(const QVector<Box> &boxes, const Options &options = Options());

/// An SVG path for one relationship line, from `a` (leaving its card towards
/// `aDir`: +1 right, -1 left) to `b` (entering from `bDir`).
/**
  When nothing is in the way the line is a smooth curve. When other cards
  are, it is routed around them: out into the gap beside the first card, along
  a clear horizontal channel, and into the gap beside the second, with rounded
  corners. `obstacles` are the other cards; the two being joined are left out.
 */
QString route(QPointF a, int aDir, QPointF b, int bDir, const QVector<QRectF> &obstacles);

}

#endif // ERLAYOUT_H
