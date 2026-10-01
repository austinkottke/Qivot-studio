#ifndef DIAGRAMGEOMETRY_H
#define DIAGRAMGEOMETRY_H

#include <QObject>
#include <QQmlEngine>
#include <QRectF>
#include <QVariantList>

#include "erlayout.h"

/// ER diagram geometry for QML: line routing around the other cards.
class DiagramGeometry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    /// SVG path from (ax, ay) to (bx, by), avoiding every rect in `cards`
    /// except the two at indexes `fromCard` and `toCard` (the ends of the line).
    Q_INVOKABLE QString route(qreal ax, qreal ay, int aDir, qreal bx, qreal by, int bDir,
                              const QVariantList &cards, int fromCard, int toCard) const
    {
        QVector<QRectF> obstacles;
        obstacles.reserve(cards.size());
        for (int i = 0; i < cards.size(); ++i)
            if (i != fromCard && i != toCard)
                obstacles << cards.at(i).toRectF();
        return ErLayout::route(QPointF(ax, ay), aDir, QPointF(bx, by), bDir, obstacles);
    }
};

#endif // DIAGRAMGEOMETRY_H
