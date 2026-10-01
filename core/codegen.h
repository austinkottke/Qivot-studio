#ifndef CODEGEN_H
#define CODEGEN_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <qivot.hpp>

/// Writes Qivot model classes for the tables of an existing database.
/**
  Follows what Qivot actually does, so the output loads the real rows:

  - A field is named exactly like its column: Qivot matches result columns to
    fields by name. A column that can't be a C++ name is left out, with a warning.
  - A table keyed by a single integer `id` becomes QI_DECLARE_MODEL (Qivot's
    built-in id); any other key becomes QI_DECLARE_MODEL_NOID with QiPrimary.
  - A foreign key becomes QiForeignKey<T> only when Qivot can follow it: one
    integer column pointing at another generated model's single integer key
    (its built-in id, or a QiPrimary column such as Chinook's ArtistId).
    Others stay plain fields, with the reference in a comment.
  - The declared SQL type is kept (QI_FIELD_AS) when it says more than the
    C++ type would, e.g. VARCHAR(80) or NUMERIC(10,2).

  Models come out parents first, so every QiForeignKey's target is declared
  before it is used.

\code
    CodeGen gen(QiSchema(db).tables(), QStringLiteral("sqlite"));
    CodeGen::Model m = gen.model(QStringLiteral("book"));
    qDebug().noquote() << m.code;      // class Book : public QiModel { … }
\endcode
 */
class CodeGen {
public:
    struct Warning {
        QString column;                // empty: about the whole table
        QString message;
    };

    struct Field {
        QString name;                  // = the column name
        QString cppType;               // "int", "QString", ...
        QString target;                // the class a QiForeignKey points at; empty if a plain field
        bool    primary = false;       // marked QiPrimary
    };

    struct Model {
        QString            table;
        QString            className;
        QString            code;       // the class plus its QI_DECLARE_MODEL
        QVector<Warning>   warnings;
        QVector<Field>     fields;     // as declared, in column order
        QStringList        dependsOn;  // classes this one's QiForeignKeys point at
        bool               builtinId = false;   // QI_DECLARE_MODEL (vs _NOID)
        bool               isValid() const { return !className.isEmpty(); }
    };

    /// `tables` is every table to generate (views are fine: they come out read-only).
    CodeGen(const QVector<QiTableInfo> &tables, const QString &dialect);

    /// The model for one table (by its listed name).
    Model model(const QString &table) const;

    /// Every model, parents before children.
    QVector<Model> models() const;

    /// A complete models.h: include guard, includes, then models() in order.
    QString header(const QString &guard = QStringLiteral("MODELS_H")) const;

    /// The C++ class name chosen for a table ("order_item" -> "OrderItem").
    QString className(const QString &table) const { return m_classNames.value(table); }

    /// The C++ type a column maps to ("int", "QString", "QDateTime", ...).
    static QString cppType(const QString &sqlType);

    /// A column default as SQL text: casts and SQL Server's parentheses
    /// removed, MySQL's bare strings quoted; "" for none (or a sequence).
    static QString defaultExpression(const QVariant &value, const QString &dialect);

    /// True if `name` can be a field: a C++ identifier that isn't a keyword
    /// and doesn't collide with a QiModel member.
    static bool isFieldName(const QString &name);

    /// Qivot's macros take at most this many fields per model.
    static constexpr int MaxFields = 50;

private:
    const QiTableInfo *find(const QString &table) const;
    bool hasBuiltinId(const QiTableInfo &t) const;
    bool hasIntegerKey(const QiTableInfo &t) const;
    Model build(const QiTableInfo &t) const;

    QVector<QiTableInfo>    m_tables;
    QString                 m_dialect;
    QHash<QString, QString> m_classNames;
};

#endif // CODEGEN_H
