#include "erlayout.h"

#include <QLineF>
#include <QSet>
#include <limits>
#include <algorithm>
#include <functional>

QHash<QString, QPointF> ErLayout::layout(const QVector<Box> &boxes, const Options &o)
{
    QHash<QString, QPointF> out;
    QHash<QString, int> indexOf;
    for (int i = 0; i < boxes.size(); ++i)
        indexOf.insert(boxes.at(i).name, i);

    // Edges both ways, ignoring self-references and tables we don't have.
    QVector<QVector<int>> parents(boxes.size()), children(boxes.size());
    for (int i = 0; i < boxes.size(); ++i) {
        for (const QString &ref : boxes.at(i).references) {
            const int p = indexOf.value(ref, -1);
            if (p < 0 || p == i || parents[i].contains(p))
                continue;
            parents[i] << p;
            children[p] << i;
        }
    }

    // Column = length of the longest chain of references below a table.
    // A cycle (a -> b -> a) is cut where it's found, so it can't recurse forever.
    QVector<int> column(boxes.size(), -1);
    QVector<char> visiting(boxes.size(), 0);
    std::function<int(int)> depth = [&](int i) -> int {
        if (column[i] >= 0)
            return column[i];
        if (visiting[i])
            return 0;
        visiting[i] = 1;
        int d = 0;
        for (int p : parents[i])
            d = std::max(d, depth(p) + 1);
        visiting[i] = 0;
        return column[i] = d;
    };

    QVector<int> connected, isolated;
    for (int i = 0; i < boxes.size(); ++i)
        (parents[i].isEmpty() && children[i].isEmpty() ? isolated : connected) << i;
    for (int i : connected)
        depth(i);

    int columns = 0;
    for (int i : connected)
        columns = std::max(columns, column[i] + 1);
    QVector<QVector<int>> byColumn(columns);
    for (int i : connected)
        byColumn[column[i]] << i;
    for (QVector<int> &col : byColumn)            // deterministic start: alphabetical
        std::sort(col.begin(), col.end(), [&](int a, int b) {
            return boxes.at(a).name.compare(boxes.at(b).name, Qt::CaseInsensitive) < 0;
        });

    // Barycenter ordering: sweep right then left, placing each table at the
    // average position of its neighbours in the column just visited.
    QVector<double> rank(boxes.size(), 0);
    auto renumber = [&](const QVector<int> &col) {
        for (int k = 0; k < col.size(); ++k)
            rank[col[k]] = k;
    };
    for (const QVector<int> &col : byColumn)
        renumber(col);
    auto reorder = [&](QVector<int> &col, bool useParents) {
        QHash<int, double> key;
        for (int k = 0; k < col.size(); ++k) {
            const QVector<int> &nb = useParents ? parents[col[k]] : children[col[k]];
            double sum = 0;
            int n = 0;
            for (int j : nb) { sum += rank[j]; ++n; }
            key.insert(col[k], n ? sum / n : rank[col[k]]);
        }
        std::stable_sort(col.begin(), col.end(), [&](int a, int b) { return key[a] < key[b]; });
        renumber(col);
    };
    for (int pass = 0; pass < 4; ++pass) {
        for (int c = 1; c < columns; ++c)
            reorder(byColumn[c], true);
        for (int c = columns - 2; c >= 0; --c)
            reorder(byColumn[c], false);
    }

    // Coordinates: columns side by side, each column centred on the tallest.
    QVector<double> colWidth(columns, 0), colHeight(columns, 0);
    for (int c = 0; c < columns; ++c) {
        for (int i : byColumn[c]) {
            colWidth[c] = std::max(colWidth[c], boxes.at(i).width);
            colHeight[c] += boxes.at(i).height + o.rowGap;
        }
        colHeight[c] = std::max(0.0, colHeight[c] - o.rowGap);
    }
    const double tallest = columns ? *std::max_element(colHeight.begin(), colHeight.end()) : 0;
    double x = o.margin, graphRight = o.margin;
    for (int c = 0; c < columns; ++c) {
        double y = o.topMargin + (tallest - colHeight[c]) / 2;
        for (int i : byColumn[c]) {
            out.insert(boxes.at(i).name, QPointF(x, y));
            y += boxes.at(i).height + o.rowGap;
        }
        graphRight = x + colWidth[c];
        x += colWidth[c] + o.columnGap;
    }

    // Unrelated tables: a grid below, no wider than the graph (or 4 across).
    std::sort(isolated.begin(), isolated.end(), [&](int a, int b) {
        return boxes.at(a).name.compare(boxes.at(b).name, Qt::CaseInsensitive) < 0;
    });
    const double gridWidth = std::max(graphRight - o.margin, 4 * 260.0);
    double gx = o.margin, gy = o.topMargin + tallest + (connected.isEmpty() ? 0 : o.columnGap), rowHeight = 0;
    for (int i : isolated) {
        const Box &b = boxes.at(i);
        if (gx > o.margin && gx + b.width > o.margin + gridWidth) {
            gx = o.margin;
            gy += rowHeight + o.rowGap;
            rowHeight = 0;
        }
        out.insert(b.name, QPointF(gx, gy));
        gx += b.width + o.rowGap;
        rowHeight = std::max(rowHeight, b.height);
    }
    return out;
}

// ---------------------------------------------------------------------------
//  Routing
// ---------------------------------------------------------------------------

namespace {
constexpr double kStub   = 26;   // how far a line runs straight out of a card
constexpr double kClear  = 14;   // space kept around other cards
constexpr double kRadius = 10;   // rounded corners

bool hitsHorizontal(double y, double x1, double x2, const QVector<QRectF> &obstacles)
{
    const double lo = std::min(x1, x2), hi = std::max(x1, x2);
    for (const QRectF &r : obstacles)
        if (y > r.top() - kClear && y < r.bottom() + kClear && hi > r.left() - kClear && lo < r.right() + kClear)
            return true;
    return false;
}

bool hitsVertical(double x, double y1, double y2, const QVector<QRectF> &obstacles)
{
    const double lo = std::min(y1, y2), hi = std::max(y1, y2);
    for (const QRectF &r : obstacles)
        if (x > r.left() - kClear && x < r.right() + kClear && hi > r.top() - kClear && lo < r.bottom() + kClear)
            return true;
    return false;
}

QString num(double v) { return QString::number(v, 'f', 1); }

// A polyline as an SVG path with each corner rounded.
QString roundedPath(const QVector<QPointF> &pts)
{
    QString d = QStringLiteral("M %1 %2").arg(num(pts[0].x()), num(pts[0].y()));
    for (int i = 1; i < pts.size() - 1; ++i) {
        const QPointF prev = pts[i - 1], at = pts[i], next = pts[i + 1];
        const double inLen = QLineF(prev, at).length(), outLen = QLineF(at, next).length();
        const double r = std::min({ kRadius, inLen / 2, outLen / 2 });
        if (r < 0.5) {
            d += QStringLiteral(" L %1 %2").arg(num(at.x()), num(at.y()));
            continue;
        }
        const QPointF enter = at + (prev - at) * (r / inLen);
        const QPointF leave = at + (next - at) * (r / outLen);
        d += QStringLiteral(" L %1 %2 Q %3 %4 %5 %6")
                 .arg(num(enter.x()), num(enter.y()), num(at.x()), num(at.y()), num(leave.x()), num(leave.y()));
    }
    d += QStringLiteral(" L %1 %2").arg(num(pts.last().x()), num(pts.last().y()));
    return d;
}
} // namespace

QString ErLayout::route(QPointF a, int aDir, QPointF b, int bDir, const QVector<QRectF> &obstacles)
{
    const double x1 = a.x() + aDir * kStub;
    const double x2 = b.x() + bDir * kStub;

    // Nothing between the two cards: a smooth curve, as before.
    const QRectF band(QPointF(std::min(x1, x2), std::min(a.y(), b.y())),
                      QPointF(std::max(x1, x2), std::max(a.y(), b.y())));
    bool clear = true;
    for (const QRectF &r : obstacles)
        if (band.adjusted(-kClear, -kClear, kClear, kClear).intersects(r))
            clear = false;
    if (clear) {
        const double bend = std::max(50.0, std::abs(b.x() - a.x()) / 2);
        return QStringLiteral("M %1 %2 C %3 %4 %5 %6 %7 %8")
            .arg(num(a.x()), num(a.y()), num(a.x() + aDir * bend), num(a.y()),
                 num(b.x() + bDir * bend), num(b.y()), num(b.x()), num(b.y()));
    }

    // Otherwise find a horizontal channel that crosses between the cards
    // without touching any, as close as possible to the two ends.
    QVector<double> candidates = { a.y(), b.y(), (a.y() + b.y()) / 2 };
    for (const QRectF &r : obstacles) {
        candidates << r.top() - kClear - 6 << r.bottom() + kClear + 6;
    }
    double best = 0, bestCost = std::numeric_limits<double>::max();
    for (double y : candidates) {
        double cost = std::abs(y - a.y()) + std::abs(y - b.y());
        if (hitsHorizontal(y, x1, x2, obstacles))
            continue;
        // The vertical runs sit in the gaps beside each card; penalise any
        // that would still clip something rather than rejecting them outright.
        if (hitsVertical(x1, a.y(), y, obstacles)) cost += 4000;
        if (hitsVertical(x2, y, b.y(), obstacles)) cost += 4000;
        if (cost < bestCost) { bestCost = cost; best = y; }
    }
    if (bestCost == std::numeric_limits<double>::max())
        best = (a.y() + b.y()) / 2;            // nowhere clear at all: go straight through

    return roundedPath({ a, QPointF(x1, a.y()), QPointF(x1, best), QPointF(x2, best), QPointF(x2, b.y()), b });
}
