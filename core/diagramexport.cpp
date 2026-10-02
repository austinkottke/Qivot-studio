#include "diagramexport.h"
#include "datatransfer.h"

#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QLocale>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QRegularExpression>

namespace {

// The light palette the export always uses (it's for documents and printing).
const QColor kBackground(0xFF, 0xFF, 0xFF);
const QColor kCard(0xFF, 0xFF, 0xFF);
const QColor kHeader(0xF2, 0xF2, 0xF5);
const QColor kBorder(0xD8, 0xD8, 0xDE);
const QColor kText(0x1D, 0x1D, 0x1F);
const QColor kSecondary(0x8E, 0x8E, 0x93);
const QColor kKey(0xB0, 0x7D, 0x00);
const QColor kLink(0x89, 0x44, 0xAB);
const QColor kLine(0x9C, 0x9C, 0xA6);
constexpr double kMargin = 32;

struct Column { QString name, type; bool primaryKey, foreignKey; };
struct Card { QString name; QRectF rect; qint64 rows; QVector<Column> columns; };
struct Diagram {
    QVector<Card> cards;
    QStringList paths;
    double headerHeight = 34, rowHeight = 24;
    QRectF bounds;                 // everything, before the margin
};

// A path as M/L/C/Q commands with absolute coordinates.
QPainterPath parsePath(const QString &d)
{
    QPainterPath p;
    const QStringList t = d.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
    int i = 0;
    auto num = [&]() { return i < t.size() ? t.at(i++).toDouble() : 0.0; };
    QChar cmd;
    while (i < t.size()) {
        if (t.at(i).size() == 1 && t.at(i).at(0).isLetter())
            cmd = t.at(i++).at(0).toUpper();
        if (cmd == QLatin1Char('M')) { const double x = num(), y = num(); p.moveTo(x, y); cmd = QLatin1Char('L'); }
        else if (cmd == QLatin1Char('L')) { const double x = num(), y = num(); p.lineTo(x, y); }
        else if (cmd == QLatin1Char('C')) { const double a = num(), b = num(), c = num(), d2 = num(), e = num(), f = num(); p.cubicTo(a, b, c, d2, e, f); }
        else if (cmd == QLatin1Char('Q')) { const double a = num(), b = num(), c = num(), d2 = num(); p.quadTo(a, b, c, d2); }
        else ++i;                  // something unexpected: skip it
    }
    return p;
}

Diagram read(const QVariantMap &s)
{
    Diagram d;
    d.headerHeight = s.value(QStringLiteral("headerHeight"), 34).toDouble();
    d.rowHeight = s.value(QStringLiteral("rowHeight"), 24).toDouble();
    for (const QVariant &v : s.value(QStringLiteral("tables")).toList()) {
        const QVariantMap t = v.toMap();
        Card c;
        c.name = t.value(QStringLiteral("name")).toString();
        c.rect = QRectF(t.value(QStringLiteral("x")).toDouble(), t.value(QStringLiteral("y")).toDouble(),
                        t.value(QStringLiteral("width")).toDouble(), t.value(QStringLiteral("height")).toDouble());
        c.rows = t.value(QStringLiteral("rows"), -1).toLongLong();
        for (const QVariant &cv : t.value(QStringLiteral("columns")).toList()) {
            const QVariantMap col = cv.toMap();
            c.columns << Column{ col.value(QStringLiteral("name")).toString(), col.value(QStringLiteral("type")).toString(),
                                 col.value(QStringLiteral("primaryKey")).toBool(), col.value(QStringLiteral("foreignKey")).toBool() };
        }
        d.bounds = d.bounds.isNull() ? c.rect : d.bounds.united(c.rect);
        d.cards << c;
    }
    for (const QVariant &v : s.value(QStringLiteral("links")).toList()) {
        const QString path = v.toMap().value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            continue;
        d.paths << path;
        const QRectF r = parsePath(path).boundingRect();
        d.bounds = d.bounds.isNull() ? r : d.bounds.united(r);
    }
    return d;
}

QString rowsText(qint64 n)
{
    return n < 0 ? QString() : QLocale().toString(n);
}

void paint(QPainter &p, const Diagram &d)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(QRectF(0, 0, d.bounds.width() + 2 * kMargin, d.bounds.height() + 2 * kMargin), kBackground);
    p.translate(kMargin - d.bounds.left(), kMargin - d.bounds.top());

    QPen line(kLine, 1.4);
    line.setCapStyle(Qt::RoundCap);
    p.setPen(line);
    p.setBrush(Qt::NoBrush);
    for (const QString &path : d.paths)
        p.drawPath(parsePath(path));

    QFont title = QGuiApplication::font();
    title.setPixelSize(14);
    title.setWeight(QFont::DemiBold);
    QFont body = QGuiApplication::font();
    body.setPixelSize(12);
    QFont small = QGuiApplication::font();
    small.setPixelSize(10);
    QFont tag = small;
    tag.setWeight(QFont::Bold);

    for (const Card &c : d.cards) {
        QPainterPath outline;
        outline.addRoundedRect(c.rect, 10, 10);
        p.setPen(Qt::NoPen);
        p.setBrush(kCard);
        p.drawPath(outline);
        // Header: a band with the name and the row count.
        p.save();
        p.setClipPath(outline);
        p.fillRect(QRectF(c.rect.left(), c.rect.top(), c.rect.width(), d.headerHeight), kHeader);
        p.restore();
        p.setPen(QPen(kBorder, 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(outline);
        p.drawLine(QPointF(c.rect.left(), c.rect.top() + d.headerHeight), QPointF(c.rect.right(), c.rect.top() + d.headerHeight));

        const QRectF head(c.rect.left() + 12, c.rect.top(), c.rect.width() - 24, d.headerHeight);
        p.setFont(small);
        p.setPen(kSecondary);
        const QString count = rowsText(c.rows);
        p.drawText(head, Qt::AlignRight | Qt::AlignVCenter, count);
        p.setFont(title);
        p.setPen(kText);
        const double countWidth = QFontMetricsF(small).horizontalAdvance(count) + 10;
        p.drawText(QRectF(head.left(), head.top(), head.width() - countWidth, head.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(title).elidedText(c.name, Qt::ElideRight, head.width() - countWidth));

        for (int i = 0; i < c.columns.size(); ++i) {
            const Column &col = c.columns.at(i);
            const QRectF row(c.rect.left() + 12, c.rect.top() + d.headerHeight + i * d.rowHeight, c.rect.width() - 24, d.rowHeight);
            if (col.primaryKey || col.foreignKey) {
                p.setFont(tag);
                p.setPen(col.primaryKey ? kKey : kLink);
                p.drawText(QRectF(row.left(), row.top(), 22, row.height()), Qt::AlignLeft | Qt::AlignVCenter,
                           col.primaryKey ? QStringLiteral("PK") : QStringLiteral("FK"));
            }
            p.setFont(small);
            p.setPen(kSecondary);
            p.drawText(row, Qt::AlignRight | Qt::AlignVCenter, col.type.toLower());
            const double typeWidth = QFontMetricsF(small).horizontalAdvance(col.type) + 10;
            p.setFont(body);
            p.setPen(kText);
            const QRectF name(row.left() + 26, row.top(), row.width() - 26 - typeWidth, row.height());
            p.drawText(name, Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(body).elidedText(col.name, Qt::ElideRight, name.width()));
        }
    }
}

QString esc(QString s)
{
    return s.replace(QLatin1Char('&'), QLatin1String("&amp;")).replace(QLatin1Char('<'), QLatin1String("&lt;"))
            .replace(QLatin1Char('>'), QLatin1String("&gt;")).replace(QLatin1Char('"'), QLatin1String("&quot;"));
}

QString n(double v) { return QString::number(v, 'f', 1); }

} // namespace

QString DiagramExport::svg(const QVariantMap &snapshot)
{
    const Diagram d = read(snapshot);
    const double w = d.bounds.width() + 2 * kMargin, h = d.bounds.height() + 2 * kMargin;
    QString out;
    out += QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" viewBox=\"0 0 %1 %2\" "
                          "font-family=\"-apple-system, 'Segoe UI', Helvetica, Arial, sans-serif\">\n").arg(n(w), n(h));
    out += QStringLiteral("<rect width=\"100%\" height=\"100%\" fill=\"%1\"/>\n").arg(kBackground.name());
    out += QStringLiteral("<g transform=\"translate(%1 %2)\">\n").arg(n(kMargin - d.bounds.left()), n(kMargin - d.bounds.top()));
    out += QStringLiteral("<g fill=\"none\" stroke=\"%1\" stroke-width=\"1.4\" stroke-linecap=\"round\">\n").arg(kLine.name());
    for (const QString &p : d.paths)
        out += QStringLiteral("  <path d=\"%1\"/>\n").arg(esc(p));
    out += QStringLiteral("</g>\n");
    for (const Card &c : d.cards) {
        const QRectF r = c.rect;
        out += QStringLiteral("<g>\n  <rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" rx=\"10\" fill=\"%5\" stroke=\"%6\"/>\n")
                   .arg(n(r.x()), n(r.y()), n(r.width()), n(r.height()), kCard.name(), kBorder.name());
        // The header band: rounded at the top only.
        out += QStringLiteral("  <path d=\"M %1 %2 V %3 Q %1 %4 %5 %4 H %6 Q %7 %4 %7 %3 V %2 Z\" fill=\"%8\"/>\n")
                   .arg(n(r.left() + 0.5), n(r.top() + d.headerHeight), n(r.top() + 10.5), n(r.top() + 0.5),
                        n(r.left() + 10.5), n(r.right() - 10.5), n(r.right() - 0.5), kHeader.name());
        out += QStringLiteral("  <line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%2\" stroke=\"%4\"/>\n")
                   .arg(n(r.left()), n(r.top() + d.headerHeight), n(r.right()), kBorder.name());
        const double mid = r.top() + d.headerHeight / 2 + 5;
        out += QStringLiteral("  <text x=\"%1\" y=\"%2\" font-size=\"14\" font-weight=\"600\" fill=\"%3\">%4</text>\n")
                   .arg(n(r.left() + 12), n(mid), kText.name(), esc(c.name));
        if (c.rows >= 0)
            out += QStringLiteral("  <text x=\"%1\" y=\"%2\" font-size=\"10\" text-anchor=\"end\" fill=\"%3\">%4</text>\n")
                       .arg(n(r.right() - 12), n(mid - 1), kSecondary.name(), esc(rowsText(c.rows)));
        for (int i = 0; i < c.columns.size(); ++i) {
            const Column &col = c.columns.at(i);
            const double y = r.top() + d.headerHeight + i * d.rowHeight + d.rowHeight / 2 + 4;
            if (col.primaryKey || col.foreignKey)
                out += QStringLiteral("  <text x=\"%1\" y=\"%2\" font-size=\"10\" font-weight=\"700\" fill=\"%3\">%4</text>\n")
                           .arg(n(r.left() + 12), n(y - 0.5), (col.primaryKey ? kKey : kLink).name(),
                                col.primaryKey ? QStringLiteral("PK") : QStringLiteral("FK"));
            out += QStringLiteral("  <text x=\"%1\" y=\"%2\" font-size=\"12\" fill=\"%3\">%4</text>\n")
                       .arg(n(r.left() + 38), n(y), kText.name(), esc(col.name));
            out += QStringLiteral("  <text x=\"%1\" y=\"%2\" font-size=\"10\" text-anchor=\"end\" fill=\"%3\">%4</text>\n")
                       .arg(n(r.right() - 12), n(y - 0.5), kSecondary.name(), esc(col.type.toLower()));
        }
        out += QStringLiteral("</g>\n");
    }
    out += QStringLiteral("</g>\n</svg>\n");
    return out;
}

QVariantMap DiagramExport::save(const QVariantMap &snapshot, const QVariant &fileOrUrl, const QString &format)
{
    const QString path = DataTransfer::localPath(fileOrUrl);
    auto fail = [&](const QString &why) {
        return QVariantMap{ { QStringLiteral("ok"), false }, { QStringLiteral("path"), path }, { QStringLiteral("error"), why } };
    };
    const Diagram d = read(snapshot);
    if (d.cards.isEmpty())
        return fail(tr("There's nothing to export."));
    const QSizeF size(d.bounds.width() + 2 * kMargin, d.bounds.height() + 2 * kMargin);

    if (format == QLatin1String("svg")) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fail(tr("Couldn't write %1: %2").arg(QFileInfo(path).fileName(), f.errorString()));
        f.write(svg(snapshot).toUtf8());
    } else if (format == QLatin1String("pdf")) {
        QPdfWriter pdf(path);
        pdf.setTitle(snapshot.value(QStringLiteral("title")).toString());
        pdf.setCreator(QStringLiteral("Qivot Studio"));
        pdf.setResolution(72);                                    // a unit is a point
        pdf.setPageSize(QPageSize(size, QPageSize::Point));
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter p;
        if (!p.begin(&pdf))
            return fail(tr("Couldn't write %1.").arg(QFileInfo(path).fileName()));
        paint(p, d);
        p.end();
    } else {
        const double scale = 2;                                   // sharp on high-density screens and in print
        QImage image((size * scale).toSize(), QImage::Format_ARGB32_Premultiplied);
        image.fill(kBackground);
        QPainter p(&image);
        p.scale(scale, scale);
        paint(p, d);
        p.end();
        if (!image.save(path, "PNG"))
            return fail(tr("Couldn't write %1.").arg(QFileInfo(path).fileName()));
    }
    return { { QStringLiteral("ok"), true }, { QStringLiteral("path"), path } };
}
