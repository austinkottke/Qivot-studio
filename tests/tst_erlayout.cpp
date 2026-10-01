#include <QtTest>
#include <QRectF>
#include <QTemporaryDir>

#include "databasesession.h"
#include "erlayout.h"
#include "sampledatabase.h"

/// The ER diagram layout: direction, no overlaps, unrelated tables, cycles.
class TestErLayout : public QObject
{
    Q_OBJECT

private:
    static ErLayout::Box box(const QString &name, QStringList refs = {}, double h = 100)
    {
        ErLayout::Box b;
        b.name = name; b.width = 200; b.height = h; b.references = refs;
        return b;
    }
    static bool overlaps(const QVector<ErLayout::Box> &boxes, const QHash<QString, QPointF> &at)
    {
        for (int i = 0; i < boxes.size(); ++i)
            for (int j = i + 1; j < boxes.size(); ++j) {
                const QRectF a(at[boxes[i].name], QSizeF(boxes[i].width, boxes[i].height));
                const QRectF b(at[boxes[j].name], QSizeF(boxes[j].width, boxes[j].height));
                if (a.intersects(b))
                    return true;
            }
        return false;
    }

    // The points a routed path passes through: M / L points and the end of
    // each rounded corner (Q x1 y1 x y), in order.
    static QVector<QPointF> corners(const QString &path)
    {
        QVector<QPointF> out;
        const QStringList tok = path.split(' ', Qt::SkipEmptyParts);
        for (int i = 0; i + 2 < tok.size(); ++i) {
            if (tok[i] == "M" || tok[i] == "L")
                out << QPointF(tok[i + 1].toDouble(), tok[i + 2].toDouble());
            else if (tok[i] == "Q" && i + 4 < tok.size())
                out << QPointF(tok[i + 3].toDouble(), tok[i + 4].toDouble());
        }
        return out;
    }
    static bool crosses(const QVector<QPointF> &pts, const QRectF &r)
    {
        for (int i = 0; i + 1 < pts.size(); ++i) {
            const QLineF seg(pts[i], pts[i + 1]);
            for (int k = 0; k <= 50; ++k)
                if (r.contains(seg.pointAt(k / 50.0)))
                    return true;
        }
        return false;
    }

private slots:
    void flowsLeftToRight()
    {
        // order_item -> orders -> customer: referenced tables sit to the left.
        const QVector<ErLayout::Box> boxes = {
            box("order_item", { "orders", "book" }), box("orders", { "customer" }),
            box("customer"), box("book", { "author" }), box("author") };
        const auto at = ErLayout::layout(boxes);
        QCOMPARE(at.size(), 5);
        QVERIFY(at["customer"].x() < at["orders"].x());
        QVERIFY(at["orders"].x() < at["order_item"].x());
        QVERIFY(at["author"].x() < at["book"].x());
        QCOMPARE(at["customer"].x(), at["author"].x());          // both reference nothing
        QVERIFY(!overlaps(boxes, at));
    }

    void columnIsTheLongestChain()
    {
        // c references a (column 0) and b (column 1): it must go right of b.
        const QVector<ErLayout::Box> boxes = { box("a"), box("b", { "a" }), box("c", { "a", "b" }) };
        const auto at = ErLayout::layout(boxes);
        QVERIFY(at["c"].x() > at["b"].x());
    }

    void unrelatedTablesGoBelow()
    {
        const QVector<ErLayout::Box> boxes = {
            box("parent", {}, 300), box("child", { "parent" }), box("settings"), box("audit_log") };
        const auto at = ErLayout::layout(boxes);
        QVERIFY(at["settings"].y() > at["parent"].y() + 300);
        QVERIFY(at["audit_log"].y() > at["parent"].y() + 300);
        QVERIFY(at["audit_log"].x() < at["settings"].x());       // alphabetical, left to right
        QVERIFY(!overlaps(boxes, at));
    }

    void cyclesAndSelfReferencesTerminate()
    {
        // employee.manager_id -> employee, and a -> b -> a.
        const QVector<ErLayout::Box> boxes = {
            box("employee", { "employee", "department" }), box("department"),
            box("a", { "b" }), box("b", { "a" }), box("ghost", { "missing_table" }) };
        const auto at = ErLayout::layout(boxes);
        QCOMPARE(at.size(), 5);
        QVERIFY(at["department"].x() < at["employee"].x());
        QVERIFY(!overlaps(boxes, at));
    }

    void tallColumnsDontOverlap()
    {
        QVector<ErLayout::Box> boxes = { box("hub", {}, 80) };
        for (int i = 0; i < 12; ++i)
            boxes << box(QString("spoke%1").arg(i), { "hub" }, 60 + i * 15);
        const auto at = ErLayout::layout(boxes);
        QVERIFY(!overlaps(boxes, at));
        for (int i = 0; i < 12; ++i)
            QVERIFY(at[QString("spoke%1").arg(i)].x() > at["hub"].x());
    }

    void routesClearPathsAsCurves()
    {
        // Nothing in between: one smooth cubic curve.
        const QString d = ErLayout::route(QPointF(200, 100), +1, QPointF(400, 160), -1,
                                          { QRectF(600, 0, 200, 300) });
        QVERIFY(d.startsWith("M 200.0 100.0"));
        QVERIFY(d.contains(" C "));
        QVERIFY(d.endsWith("400.0 160.0"));
    }

    void routesAroundCardsInTheWay()
    {
        // A card sits right between the two ends.
        const QRectF wall(300, 40, 160, 220);
        const QPointF a(200, 150), b(560, 150);
        const QString d = ErLayout::route(a, +1, b, -1, { wall });
        QVERIFY2(!d.contains(" C "), qPrintable(d));
        const QVector<QPointF> pts = corners(d);
        QVERIFY(pts.size() >= 4);
        QCOMPARE(pts.first(), a);
        QCOMPARE(pts.last(), b);
        QVERIFY2(!crosses(pts, wall), qPrintable(d));
        QVERIFY(d.contains(" Q "));                          // rounded corners
    }

    void routesThroughTheGapBetweenCards()
    {
        // Two cards stacked in the middle column with a gap between them:
        // the line should take the gap rather than go all the way round.
        const QRectF upper(300, 0, 160, 140), lower(300, 200, 160, 200);
        const QString d = ErLayout::route(QPointF(200, 120), +1, QPointF(560, 260), -1, { upper, lower });
        const QVector<QPointF> pts = corners(d);
        QVERIFY(!crosses(pts, upper) && !crosses(pts, lower));
        double channel = -1;
        for (int i = 0; i + 1 < pts.size(); ++i)
            if (qFuzzyCompare(pts[i].y(), pts[i + 1].y()) && pts[i + 1].x() - pts[i].x() > 100)
                channel = pts[i].y();
        QVERIFY2(channel > 140 && channel < 200, qPrintable(d));
    }

    void sampleDiagram()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("bookshop.db");
        QVERIFY(SampleDatabase::create(path));
        DatabaseSession db;
        QVERIFY(db.open(path));
        const QVariantMap d = db.diagram();

        const QVariantList tables = d.value("tables").toList();
        QCOMPARE(tables.size(), 7);                          // tables only, no views
        QStringList names;
        QHash<QString, QVariantMap> byName;
        for (const QVariant &v : tables) {
            const QVariantMap t = v.toMap();
            names << t.value("name").toString();
            byName.insert(t.value("name").toString(), t);
            QVERIFY(t.value("width").toDouble() >= 190);
            QCOMPARE(t.value("height").toDouble(),
                     d.value("headerHeight").toDouble()
                         + t.value("columns").toList().size() * d.value("rowHeight").toDouble() + 6);
        }
        QVERIFY(!names.contains("book_sales"));

        // 2 (book) + 1 (orders) + 2 (order_item) + 2 (review) = 7 relationships
        QCOMPARE(d.value("links").toList().size(), 7);
        QVERIFY(byName["author"].value("x").toDouble() < byName["book"].value("x").toDouble());
        QVERIFY(byName["book"].value("x").toDouble() < byName["order_item"].value("x").toDouble());
        QVERIFY(d.value("width").toDouble() > 0 && d.value("height").toDouble() > 0);

        // Column flags drive the PK / FK markers.
        bool sawFk = false;
        for (const QVariant &c : byName["book"].value("columns").toList())
            if (c.toMap().value("name") == "author_id")
                sawFk = c.toMap().value("foreignKey").toBool();
        QVERIFY(sawFk);
    }
};

QTEST_GUILESS_MAIN(TestErLayout)
#include "tst_erlayout.moc"
