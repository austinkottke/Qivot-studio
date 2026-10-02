#include <QtTest>
#include <QImage>
#include <QTemporaryDir>

#include "diagramexport.h"

/// The diagram as PNG, SVG and PDF, from a snapshot of what's on screen.
class TestDiagramExport : public QObject
{
    Q_OBJECT

    static QVariantMap snapshot()
    {
        const QVariantList bookColumns = {
            QVariantMap{ { "name", "id" }, { "type", "INTEGER" }, { "primaryKey", true }, { "foreignKey", false } },
            QVariantMap{ { "name", "author_id" }, { "type", "INTEGER" }, { "primaryKey", false }, { "foreignKey", true } },
            QVariantMap{ { "name", "title & <subtitle>" }, { "type", "TEXT" }, { "primaryKey", false }, { "foreignKey", false } } };
        const QVariantList authorColumns = {
            QVariantMap{ { "name", "id" }, { "type", "INTEGER" }, { "primaryKey", true }, { "foreignKey", false } } };
        return {
            { "tables", QVariantList{
                QVariantMap{ { "name", "book" }, { "x", 300 }, { "y", 40 }, { "width", 220 }, { "height", 106 }, { "rows", 1200 }, { "columns", bookColumns } },
                QVariantMap{ { "name", "author" }, { "x", 0 }, { "y", 0 }, { "width", 200 }, { "height", 58 }, { "rows", 150 }, { "columns", authorColumns } } } },
            { "links", QVariantList{ QVariantMap{ { "path", "M 300 98 C 250 98 250 46 200 46 M 288 98 L 300 92 Q 290 70 200 46" } } } },
            { "headerHeight", 34 }, { "rowHeight", 24 }, { "title", "shop" } };
    }

private slots:
    void svg()
    {
        DiagramExport e;
        const QString s = e.svg(snapshot());
        QVERIFY(s.startsWith("<svg"));
        QVERIFY(s.contains(">book</text>"));
        QVERIFY(s.contains(">1,200</text>") || s.contains(">1200</text>"));
        QVERIFY(s.contains("title &amp; &lt;subtitle&gt;"));             // escaped
        QVERIFY(s.contains("<path d=\"M 300 98 C 250 98 250 46 200 46"));
        // Everything inside the margin: width covers 0..520 plus 2 x 32.
        QVERIFY(s.contains("width=\"584.0\""));
    }

    void files()
    {
        QTemporaryDir dir;
        DiagramExport e;
        for (const char *format : { "png", "svg", "pdf" }) {
            const QString path = dir.filePath(QString("d.") + format);
            const QVariantMap r = e.save(snapshot(), QUrl::fromLocalFile(path), format);
            QVERIFY2(r.value("ok").toBool(), qPrintable(r.value("error").toString()));
            QVERIFY(QFileInfo(path).size() > 500);
        }
        const QImage png(dir.filePath("d.png"));
        QCOMPARE(png.size(), QSize(1168, 2 * (146 + 64)));              // twice the size, for sharpness
        QFile pdf(dir.filePath("d.pdf"));
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.read(5) == "%PDF-");
        QVERIFY(!e.save(QVariantMap(), dir.filePath("empty.png"), "png").value("ok").toBool());
    }
};

QTEST_MAIN(TestDiagramExport)
#include "tst_diagramexport.moc"
