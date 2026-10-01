#include "codegen.h"

#include <QRegularExpression>
#include <QSet>

namespace {

const QRegularExpression &identifier()
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return re;
}

// C++ keywords, plus QiModel's own members: a field can't be named like either.
const QSet<QString> &reservedFieldNames()
{
    static const QSet<QString> names = [] {
        QSet<QString> s;
        for (const char *w : { "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
                               "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
                               "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
                               "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
                               "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit",
                               "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline",
                               "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
                               "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
                               "register", "reinterpret_cast", "requires", "return", "short", "signed",
                               "sizeof", "static", "static_assert", "static_cast", "struct", "switch",
                               "template", "this", "thread_local", "throw", "true", "try", "typedef",
                               "typeid", "typename", "union", "unsigned", "using", "virtual", "void",
                               "volatile", "wchar_t", "while", "xor", "xor_eq",
                               // Qt macros that would expand
                               "signals", "slots", "emit", "foreach", "forever",
                               // QiModel / QiAbstractModel
                               "afterLoad", "afterRemove", "afterSave", "beforeRemove", "className", "clean",
                               "col", "connection", "fields", "initialData", "lastError", "load", "metaInfo",
                               "objects", "remove", "save", "setConnection", "setError", "tableName",
                               "TableName", "upsert" })
            s.insert(QString::fromLatin1(w));
        return s;
    }();
    return names;
}

// Words Qivot would write unquoted into SQL, where they'd be a syntax error as a table name.
const QSet<QString> &sqlReserved()
{
    static const QSet<QString> words = [] {
        QSet<QString> s;
        for (const char *w : { "all", "and", "as", "by", "case", "check", "column", "constraint", "create",
                               "default", "delete", "desc", "distinct", "drop", "else", "end", "from",
                               "group", "having", "in", "index", "insert", "into", "is", "join", "key",
                               "limit", "not", "null", "offset", "on", "or", "order", "primary",
                               "references", "select", "set", "table", "then", "to", "union", "unique",
                               "update", "user", "values", "when", "where", "with" })
            s.insert(QString::fromLatin1(w));
        return s;
    }();
    return words;
}

QString cString(const QString &text)
{
    QString s = text;
    s.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    s.replace(QLatin1Char('"'), QLatin1String("\\\""));
    s.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    return QLatin1Char('"') + s + QLatin1Char('"');
}

// The type without its length/precision and sign words: "int(11) unsigned" -> "int".
QString baseType(const QString &sqlType)
{
    QString t = sqlType.toLower().trimmed();
    const int paren = t.indexOf(QLatin1Char('('));
    if (paren >= 0) {
        const int close = t.indexOf(QLatin1Char(')'), paren);
        t = t.left(paren) + (close >= 0 ? t.mid(close + 1) : QString());
    }
    for (const char *word : { " unsigned", " signed", " zerofill" })
        t.remove(QLatin1String(word));
    return t.simplified();
}

bool isInteger(const QString &cpp) { return cpp == QLatin1String("int") || cpp == QLatin1String("qint64"); }

// The SQL types Qivot itself would write for a C++ type; anything else is kept with QI_FIELD_AS.
bool isPlainType(const QString &sqlType)
{
    static const QSet<QString> plain = { QStringLiteral("integer"), QStringLiteral("int"), QStringLiteral("bigint"),
                                         QStringLiteral("double"), QStringLiteral("double precision"),
                                         QStringLiteral("text"), QStringLiteral("blob"), QStringLiteral("bytea"),
                                         QStringLiteral("date"), QStringLiteral("datetime"),
                                         QStringLiteral("timestamp"), QStringLiteral("time"),
                                         QStringLiteral("boolean"), QStringLiteral("bool") };
    return plain.contains(sqlType.toLower().trimmed());
}

// A column default as Qivot wants it: raw SQL text for QiDefault(), or empty to leave it out.
QString sqlDefault(const QVariant &value, const QString &dialect)
{
    if (value.isNull())
        return QString();
    QString d = value.toString().trimmed();
    if (d.isEmpty() || d.contains(QLatin1String("nextval("), Qt::CaseInsensitive))
        return QString();                                   // a sequence: the key fills itself
    // SQL Server wraps defaults in parentheses: ((0)), ('x'), (getdate()).
    while (d.startsWith(QLatin1Char('(')) && d.endsWith(QLatin1Char(')'))
           && d.count(QLatin1Char('(')) == d.count(QLatin1Char(')'))
           && d.indexOf(QLatin1Char(')')) == d.size() - 1)
        d = d.mid(1, d.size() - 2).trimmed();
    // PostgreSQL adds casts: 'pending'::character varying, 0::numeric.
    static const QRegularExpression cast(QStringLiteral("::[A-Za-z_][A-Za-z0-9_ ]*(\\[\\])?$"));
    while (cast.match(d).hasMatch() && !d.endsWith(QLatin1String("()")))
        d.remove(cast);
    // MySQL reports a string default without its quotes.
    if (dialect == QLatin1String("mysql") && !d.startsWith(QLatin1Char('\''))) {
        static const QRegularExpression number(QStringLiteral("^-?\\d+(\\.\\d+)?$"));
        const bool expression = number.match(d).hasMatch() || d.contains(QLatin1Char('('))
                             || d.startsWith(QLatin1String("CURRENT_"), Qt::CaseInsensitive)
                             || d.compare(QLatin1String("NULL"), Qt::CaseInsensitive) == 0;
        if (!expression)
            d = QLatin1Char('\'') + QString(d).replace(QLatin1Char('\''), QLatin1String("''")) + QLatin1Char('\'');
    }
    if (d.compare(QLatin1String("NULL"), Qt::CaseInsensitive) == 0)
        return QString();
    return d;
}

QString pascalCase(const QString &name)
{
    QString out;
    bool upper = true;
    for (const QChar c : name) {
        if (!c.isLetterOrNumber() || c.unicode() > 127) {
            upper = true;
            continue;
        }
        out += upper ? c.toUpper() : c;
        upper = false;
    }
    if (out.isEmpty() || out.at(0).isDigit())
        out.prepend(QLatin1String("Table"));
    return out;
}

QString onDeleteTemplateArg(const QString &action)
{
    const QString a = action.toUpper();
    if (a == QLatin1String("CASCADE"))     return QStringLiteral("QiFkCascade");
    if (a == QLatin1String("SET NULL"))    return QStringLiteral("QiFkSetNull");
    if (a == QLatin1String("RESTRICT"))    return QStringLiteral("QiFkRestrict");
    if (a == QLatin1String("SET DEFAULT")) return QStringLiteral("QiFkSetDefault");
    return QString();
}

} // namespace

CodeGen::CodeGen(const QVector<QiTableInfo> &tables, const QString &dialect)
    : m_dialect(dialect)
{
    for (const QiTableInfo &t : tables)
        if (t.kind == QiTableInfo::Table || t.kind == QiTableInfo::View)
            m_tables << t;

    // Class names: unique, and never the same as one of the class's own fields
    // (a member can't share its class's name).
    QSet<QString> used;
    for (const QiTableInfo &t : m_tables) {
        QString base = pascalCase(t.name);
        for (const QiColumnInfo &c : t.columns)
            if (c.name == base)
                base += QLatin1String("Model");
        QString name = base;
        for (int n = 2; used.contains(name); ++n)
            name = base + QString::number(n);
        used.insert(name);
        m_classNames.insert(t.name, name);
    }
}

QString CodeGen::cppType(const QString &sqlType)
{
    const QString full = sqlType.toLower().simplified();
    const QString t = baseType(sqlType);
    if (full.startsWith(QLatin1String("tinyint(1)")) || t == QLatin1String("bit")
        || t == QLatin1String("bool") || t == QLatin1String("boolean"))
        return QStringLiteral("bool");
    static const QSet<QString> big = { QStringLiteral("bigint"), QStringLiteral("int8"), QStringLiteral("bigserial"),
                                       QStringLiteral("serial8"), QStringLiteral("unsigned big int"),
                                       QStringLiteral("hugeint"), QStringLiteral("ubigint") };
    if (big.contains(t))
        return QStringLiteral("qint64");
    static const QSet<QString> ints = { QStringLiteral("int"), QStringLiteral("integer"), QStringLiteral("smallint"),
                                        QStringLiteral("tinyint"), QStringLiteral("mediumint"), QStringLiteral("int2"),
                                        QStringLiteral("int4"), QStringLiteral("serial"), QStringLiteral("serial4"),
                                        QStringLiteral("smallserial"), QStringLiteral("serial2"), QStringLiteral("year"),
                                        QStringLiteral("usmallint"), QStringLiteral("utinyint"), QStringLiteral("uinteger") };
    if (ints.contains(t))
        return QStringLiteral("int");
    static const QSet<QString> reals = { QStringLiteral("real"), QStringLiteral("float"), QStringLiteral("double"),
                                         QStringLiteral("double precision"), QStringLiteral("numeric"),
                                         QStringLiteral("decimal"), QStringLiteral("money"), QStringLiteral("smallmoney"),
                                         QStringLiteral("float4"), QStringLiteral("float8"), QStringLiteral("number"),
                                         QStringLiteral("dec"), QStringLiteral("fixed") };
    if (reals.contains(t))
        return QStringLiteral("double");
    if (t == QLatin1String("date"))
        return QStringLiteral("QDate");
    if (t.startsWith(QLatin1String("timestamp")) || t.startsWith(QLatin1String("datetime"))
        || t == QLatin1String("smalldatetime"))
        return QStringLiteral("QDateTime");
    if (t == QLatin1String("time") || t.startsWith(QLatin1String("time with")) || t == QLatin1String("timetz"))
        return QStringLiteral("QTime");
    static const QSet<QString> bytes = { QStringLiteral("blob"), QStringLiteral("bytea"), QStringLiteral("binary"),
                                         QStringLiteral("varbinary"), QStringLiteral("image"), QStringLiteral("longblob"),
                                         QStringLiteral("mediumblob"), QStringLiteral("tinyblob") };
    if (bytes.contains(t))
        return QStringLiteral("QByteArray");
    return QStringLiteral("QString");
}

QString CodeGen::defaultExpression(const QVariant &value, const QString &dialect)
{
    return sqlDefault(value, dialect);
}

bool CodeGen::isFieldName(const QString &name)
{
    return identifier().match(name).hasMatch() && !reservedFieldNames().contains(name)
        && !name.startsWith(QLatin1String("__"))
        && !(name.size() > 1 && name.at(0) == QLatin1Char('_') && name.at(1).isUpper());
}

const QiTableInfo *CodeGen::find(const QString &table) const
{
    for (const QiTableInfo &t : m_tables)
        if (t.name == table)
            return &t;
    return nullptr;
}

bool CodeGen::hasBuiltinId(const QiTableInfo &t) const
{
    if (t.kind != QiTableInfo::Table || t.primaryKey != QStringList{ QStringLiteral("id") })
        return false;
    const QiColumnInfo *id = t.column(QStringLiteral("id"));
    return id && isInteger(cppType(id->type));
}

// One integer key column: what a QiForeignKey (a QiField<int>) can point at.
bool CodeGen::hasIntegerKey(const QiTableInfo &t) const
{
    if (t.kind != QiTableInfo::Table || t.primaryKey.size() != 1)
        return false;
    const QiColumnInfo *key = t.column(t.primaryKey.first());
    return key && isInteger(cppType(key->type));
}

CodeGen::Model CodeGen::model(const QString &table) const
{
    for (const Model &m : models())
        if (m.table == table)
            return m;
    return Model();
}

QVector<CodeGen::Model> CodeGen::models() const
{
    QVector<Model> built;
    for (const QiTableInfo &t : m_tables)
        built << build(t);

    // Parents first. A cycle of QiForeignKeys can't be declared in any order,
    // so the link that closes one is turned back into a plain integer field.
    QVector<Model> ordered;
    QSet<QString>  emitted;
    QVector<int>   pending;
    for (int i = 0; i < built.size(); ++i)
        pending << i;
    while (!pending.isEmpty()) {
        int pick = -1;
        for (int k = 0; k < pending.size() && pick < 0; ++k) {
            const Model &m = built[pending[k]];
            bool ready = true;
            for (const QString &dep : m.dependsOn)
                ready = ready && emitted.contains(dep);
            if (ready)
                pick = k;
        }
        if (pick < 0) {
            pick = 0;                                      // in a cycle: break it here
            Model &m = built[pending[0]];
            for (const QString &dep : m.dependsOn) {
                if (emitted.contains(dep))
                    continue;
                // Rewrite "QiForeignKey<Dep...> col;" as a plain field.
                const QRegularExpression fk(QStringLiteral("QiForeignKey<%1(, \\w+)?>(\\s+)(\\w+);")
                                                .arg(QRegularExpression::escape(dep)));
                QRegularExpressionMatch match = fk.match(m.code);
                while (match.hasMatch()) {
                    const QString column = match.captured(3);
                    QString plain = QStringLiteral("QiField<int>");
                    plain = plain.leftJustified(match.capturedLength(0) - match.capturedLength(2)
                                                - match.capturedLength(3) - 1);
                    m.code.replace(match.capturedStart(0), match.capturedLength(0),
                                   plain + match.captured(2) + column + QStringLiteral(";"));
                    for (Field &f : m.fields)
                        if (f.name == column)
                            f.target.clear();
                    m.warnings << Warning{ column, QStringLiteral("Stays a plain integer: its table and %1 refer to "
                                                                  "each other, and Qivot needs a foreign key's "
                                                                  "target declared first.").arg(dep) };
                    match = fk.match(m.code);
                }
            }
            m.dependsOn.clear();
        }
        const int index = pending.takeAt(pick);
        emitted.insert(built[index].className);
        ordered << built[index];
    }
    return ordered;
}

CodeGen::Model CodeGen::build(const QiTableInfo &t) const
{
    Model m;
    m.table = t.name;
    m.className = m_classNames.value(t.name);
    const bool builtinId = hasBuiltinId(t);
    const bool isView = t.kind == QiTableInfo::View;
    const QString bareName = t.schema.isEmpty() || !t.name.contains(QLatin1Char('.'))
                           ? t.name : t.name.mid(t.name.indexOf(QLatin1Char('.')) + 1);

    // ---- Table-level notes ----
    if (isView)
        m.warnings << Warning{ QString(), QStringLiteral("A view: rows can be read, but save() and remove() won't work.") };
    else if (t.primaryKey.isEmpty())
        m.warnings << Warning{ QString(), QStringLiteral("No primary key: rows can be read, but save() can't update one.") };
    if (!identifier().match(bareName).hasMatch() || sqlReserved().contains(bareName.toLower()))
        m.warnings << Warning{ QString(), QStringLiteral("\"%1\" needs quoting in SQL, and Qivot writes table names "
                                                         "unquoted.").arg(bareName) };
    else if (m_dialect == QLatin1String("postgres") && bareName != bareName.toLower())
        m.warnings << Warning{ QString(), QStringLiteral("Mixed-case name: Qivot writes it unquoted, so PostgreSQL "
                                                         "would look for \"%1\".").arg(bareName.toLower()) };

    // ---- Fields ----
    struct Line { QString type, name, declare, comment; Field field; };
    QVector<Line> fields;
    for (const QiColumnInfo &c : t.columns) {
        if (builtinId && c.name == QLatin1String("id"))
            continue;                                      // QiModel's own id
        if (!isFieldName(c.name)) {
            m.warnings << Warning{ c.name, identifier().match(c.name).hasMatch()
                ? QStringLiteral("\"%1\" is a C++ keyword or a QiModel member, so it can't be a field; "
                                 "it is left out.").arg(c.name)
                : QStringLiteral("\"%1\" isn't a C++ name, and Qivot matches fields to columns by name; "
                                 "it is left out.").arg(c.name) };
            continue;
        }
        if (m_dialect == QLatin1String("postgres") && c.name != c.name.toLower())
            m.warnings << Warning{ c.name, QStringLiteral("Mixed-case column: Qivot writes it unquoted, so "
                                                          "PostgreSQL would look for \"%1\".").arg(c.name.toLower()) };

        const QString cpp = cppType(c.type);
        Line f;
        f.name = c.name;
        f.field.name = c.name;
        f.field.cppType = cpp;
        f.field.primary = !builtinId && t.primaryKey.contains(c.name);
        f.type = QStringLiteral("QiField<%1>").arg(cpp);

        // A foreign key Qivot can follow: one integer column -> another model's integer key.
        for (const QiForeignKeyInfo &fk : t.foreignKeys) {
            if (fk.columns != QStringList{ c.name })
                continue;
            const QiTableInfo *target = find(fk.refTable);
            const QString ref = QStringLiteral("-> %1(%2)").arg(fk.refTable, fk.refColumns.join(QStringLiteral(", ")));
            if (target && target != &t && !isView && hasIntegerKey(*target) && isInteger(cpp)
                && fk.refColumns == target->primaryKey) {
                const QString targetClass = m_classNames.value(target->name);
                const QString action = onDeleteTemplateArg(fk.onDelete);
                f.type = action.isEmpty() ? QStringLiteral("QiForeignKey<%1>").arg(targetClass)
                                          : QStringLiteral("QiForeignKey<%1, %2>").arg(targetClass, action);
                f.comment = ref;
                f.field.target = targetClass;
                if (!m.dependsOn.contains(targetClass))
                    m.dependsOn << targetClass;
            } else {
                f.comment = ref;
            }
            break;
        }

        // Clauses.
        QStringList clauses;
        const bool inKey = !builtinId && t.primaryKey.contains(c.name);
        if (inKey)
            clauses << QStringLiteral("QiPrimary");
        if (!c.nullable || inKey)
            clauses << QStringLiteral("QiNotNull");
        if (!inKey)
            for (const QiIndexInfo &index : t.indexes)
                if (index.unique && index.columns == QStringList{ c.name }) {
                    clauses << QStringLiteral("QiUnique");
                    break;
                }
        const QString def = sqlDefault(c.defaultValue, m_dialect);
        if (!def.isEmpty() && !c.autoIncrement)
            clauses << QStringLiteral("QiDefault(%1)").arg(cString(def));

        const bool keepType = !isPlainType(c.type) && !f.type.startsWith(QLatin1String("QiForeignKey"));
        if (keepType)
            f.declare = QStringLiteral("QI_FIELD_AS(%1, %2%3)")
                            .arg(c.name, cString(c.type),
                                 clauses.isEmpty() ? QString() : QStringLiteral(", ") + clauses.join(QStringLiteral(" | ")));
        else
            f.declare = QStringLiteral("QI_FIELD(%1%2)")
                            .arg(c.name, clauses.isEmpty() ? QString() : QStringLiteral(", ") + clauses.join(QStringLiteral(" | ")));
        fields << f;
    }

    if (fields.size() > MaxFields) {
        m.warnings << Warning{ QString(), QStringLiteral("%1 columns: Qivot's macros take %2, so the rest are left out.")
                                              .arg(fields.size()).arg(MaxFields) };
        fields.resize(MaxFields);
    }
    m.builtinId = builtinId;
    for (const Line &f : fields)
        m.fields << f.field;

    // ---- Code ----
    int typeWidth = 0;
    for (const Line &f : fields)
        typeWidth = qMax(typeWidth, int(f.type.size()));
    int lineWidth = 0;
    for (const Line &f : fields)
        lineWidth = qMax(lineWidth, typeWidth + 1 + int(f.name.size()) + 1);

    QString code;
    code += QStringLiteral("/// %1 \"%2\"\n").arg(isView ? QStringLiteral("The view") : QStringLiteral("The table"), t.name);
    code += QStringLiteral("class %1 : public QiModel {\n    QI_MODEL\npublic:\n").arg(m.className);
    for (const Line &f : fields) {
        QString line = QStringLiteral("    ") + f.type.leftJustified(typeWidth) + QLatin1Char(' ') + f.name + QLatin1Char(';');
        if (!f.comment.isEmpty())
            line = line.leftJustified(4 + lineWidth) + QStringLiteral("   // ") + f.comment;
        code += line + QLatin1Char('\n');
    }
    code += QStringLiteral("};\n");
    if (builtinId && fields.isEmpty()) {
        // Nothing but the key (a lookup table): Qivot's macros need at least one
        // field, so name QiModel's own id as it. (Saving needs Qivot with
        // insertDefaults(), which writes INSERT ... DEFAULT VALUES.)
        code += QStringLiteral("QI_DECLARE_MODEL_NOID(%1, %2, QI_FIELD(id));\n").arg(m.className, cString(t.name));
        m.code = code;
        return m;
    }
    code += QStringLiteral("%1(%2, %3")
                .arg(builtinId ? QStringLiteral("QI_DECLARE_MODEL") : QStringLiteral("QI_DECLARE_MODEL_NOID"),
                     m.className, cString(t.name));
    for (const Line &f : fields)
        code += QStringLiteral(",\n    ") + f.declare;
    code += QStringLiteral(");\n");
    m.code = code;
    return m;
}

QString CodeGen::header(const QString &guard) const
{
    const QVector<Model> all = models();
    QString out;
    out += QStringLiteral("// Qivot models, generated by Qivot Studio.\n");
    out += QStringLiteral("#ifndef %1\n#define %1\n\n").arg(guard);
    out += QStringLiteral("#include <QByteArray>\n#include <QDate>\n#include <QDateTime>\n"
                          "#include <QString>\n#include <QTime>\n#include <qivot.hpp>\n");
    for (const Model &m : all) {
        out += QLatin1Char('\n');
        for (const Warning &w : m.warnings)
            out += QStringLiteral("// Note%1: %2\n")
                       .arg(w.column.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(w.column), w.message);
        out += m.code;
    }
    out += QStringLiteral("\n#endif // %1\n").arg(guard);
    return out;
}
