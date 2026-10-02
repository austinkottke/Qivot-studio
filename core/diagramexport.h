#ifndef DIAGRAMEXPORT_H
#define DIAGRAMEXPORT_H

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

/// Saving the ER diagram as a picture: PNG, SVG or PDF.
/**
  QML hands over what's on screen — every card where it is now (after any
  dragging) and every link's path as drawn — and this draws it again, in a
  light style that prints well:

\code
    DiagramExport { id: exporter }
    exporter.save(diagramView.snapshot(), fileUrl, "pdf")   // { ok, path, error }
\endcode

  The snapshot: `{ tables: [{ name, x, y, width, height, rows, columns:
  [{ name, type, primaryKey, foreignKey }] }], links: [{ path }],
  headerHeight, rowHeight, title }`. Paths use M, L, C and Q, as
  ErLayout::route writes them.
 */
class DiagramExport : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    using QObject::QObject;

    /// Write the diagram to `fileOrUrl` as "png" (at twice the size, for
    /// sharpness), "svg" or "pdf". `{ ok, path, error }`.
    Q_INVOKABLE QVariantMap save(const QVariantMap &snapshot, const QVariant &fileOrUrl, const QString &format);

    /// The SVG text of the diagram (what save() writes for "svg").
    Q_INVOKABLE QString svg(const QVariantMap &snapshot);
};

#endif // DIAGRAMEXPORT_H
