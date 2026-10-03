#ifndef LISTTABLEMODEL_H
#define LISTTABLEMODEL_H

#include <QAbstractTableModel>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>

/// Rows given as a list of maps, as a table for ResultGrid: a Redis hash's
/// fields, a sorted set's members… (anything that isn't a query result).
/**
\code
    ListTable { id: t; columns: [ "field", "value" ] }
    t.reset(page.rows); t.append(nextPage.rows)
    ResultGrid { model: t; columns: t.columnInfo }
\endcode
 */
class ListTableModel : public QAbstractTableModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(ListTable)
    Q_PROPERTY(QStringList columns READ columns WRITE setColumns NOTIFY columnsChanged)
    /// `[{ name, type }]` for ResultGrid (numbers line up on the right).
    Q_PROPERTY(QVariantList columnInfo READ columnInfo NOTIFY columnsChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { NullRole = Qt::UserRole + 1, NumberRole, RawRole };
    using QAbstractTableModel::QAbstractTableModel;

    QStringList columns() const { return m_columns; }
    void setColumns(const QStringList &columns);
    QVariantList columnInfo() const;
    int count() const { return m_rows.size(); }

    Q_INVOKABLE void reset(const QVariantList &rows);
    Q_INVOKABLE void append(const QVariantList &rows);
    Q_INVOKABLE QVariantMap rowAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void columnsChanged();
    void countChanged();

private:
    QStringList  m_columns;
    QVariantList m_rows;
};

#endif // LISTTABLEMODEL_H
