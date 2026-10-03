#include "listtablemodel.h"
#include "cellformat.h"

void ListTableModel::setColumns(const QStringList &columns)
{
    if (columns == m_columns)
        return;
    beginResetModel();
    m_columns = columns;
    m_rows.clear();
    endResetModel();
    emit columnsChanged();
    emit countChanged();
}

QVariantList ListTableModel::columnInfo() const
{
    QVariantList out;
    for (const QString &c : m_columns) {
        // A column whose values are all numbers is a number column.
        bool numbers = !m_rows.isEmpty();
        for (const QVariant &r : m_rows) {
            const QVariant v = r.toMap().value(c);
            if (!v.isNull() && !CellFormat::isNumber(v)) { numbers = false; break; }
        }
        out << QVariantMap{ { QStringLiteral("name"), c }, { QStringLiteral("type"), numbers ? QStringLiteral("double") : QStringLiteral("QString") } };
    }
    return out;
}

void ListTableModel::reset(const QVariantList &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
    emit countChanged();
    emit columnsChanged();      // the column types may have changed with the rows
}

void ListTableModel::append(const QVariantList &rows)
{
    if (rows.isEmpty())
        return;
    beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size() + rows.size() - 1);
    m_rows += rows;
    endInsertRows();
    emit countChanged();
}

QVariantMap ListTableModel::rowAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).toMap() : QVariantMap();
}

int ListTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int ListTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_columns.size();
}

QVariant ListTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size() || index.column() >= m_columns.size())
        return QVariant();
    const QVariant v = m_rows.at(index.row()).toMap().value(m_columns.at(index.column()));
    switch (role) {
    case Qt::DisplayRole: return CellFormat::display(v);
    case NullRole:        return v.isNull();
    case NumberRole:      return CellFormat::isNumber(v);
    case RawRole:         return v;
    default:              return QVariant();
    }
}

QVariant ListTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();
    if (orientation == Qt::Horizontal)
        return section < m_columns.size() ? m_columns.at(section) : QVariant();
    return section + 1;
}

QHash<int, QByteArray> ListTableModel::roleNames() const
{
    return { { Qt::DisplayRole, "display" }, { NullRole, "isNull" }, { NumberRole, "isNumber" }, { RawRole, "raw" } };
}
