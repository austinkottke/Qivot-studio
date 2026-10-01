#ifndef DIAGRAMDATA_H
#define DIAGRAMDATA_H

#include <QHash>
#include <QVariantMap>
#include <QVector>
#include <qivot.hpp>

/// What the ER diagram draws, laid out (see ErLayout), for any set of tables:
/// the open database's, or a design's.
namespace DiagramData {

/// `{ tables: [{ name, rows, x, y, width, height, columns: [{ name, type,
///    primaryKey, foreignKey }] }], links: [{ from, fromColumns, to, toColumns,
///    onDelete }], width, height, headerHeight, rowHeight }`. Views are left
/// out. `rowCounts` is shown on each card; a table missing from it shows none.
QVariantMap build(const QVector<QiTableInfo> &infos, const QHash<QString, qint64> &rowCounts);

}

#endif // DIAGRAMDATA_H
