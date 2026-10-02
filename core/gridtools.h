#ifndef GRIDTOOLS_H
#define GRIDTOOLS_H

#include <QAbstractItemModel>
#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

/// What the result grids do with a selected block of cells: copy it as text
/// (tab- or comma-separated, the way spreadsheets paste it) and sum it up.
/**
  Works on any model with a "raw" role holding each value as read (QueryModel,
  RowsModel), so a table's rows that aren't on screen yet are fetched too.

\code
    GridTools.text(grid.model, top, left, bottom, right, "tsv", true)
    GridTools.summary(grid.model, top, left, bottom, right)
    // { cells, values, nulls, numbers, sum, avg, min, max, tooMany }
\endcode
 */
class GridTools : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    /// The most rows text() copies at once.
    static constexpr int MaxCopyRows = 100000;
    /// Past this many cells, summary() only counts them.
    static constexpr int MaxSummaryCells = 250000;

    /// Rows `top`..`bottom`, columns `left`..`right` (inclusive) as text:
    /// `format` "tsv" or "csv", with the column names first when `headers`.
    /// NULL is an empty field; a field with the separator, a quote or a line
    /// break is quoted. At most MaxCopyRows rows.
    Q_INVOKABLE QString text(QAbstractItemModel *model, int top, int left, int bottom, int right,
                             const QString &format = QStringLiteral("tsv"), bool headers = false) const;

    /// Counts and, over the numbers among them, sum / avg / min / max:
    /// `{ cells, values, nulls, numbers, sum, avg, min, max, tooMany }`.
    Q_INVOKABLE QVariantMap summary(QAbstractItemModel *model, int top, int left, int bottom, int right) const;
};

#endif // GRIDTOOLS_H
