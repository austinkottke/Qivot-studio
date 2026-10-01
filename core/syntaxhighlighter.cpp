#include "syntaxhighlighter.h"

#include <QRegularExpression>
#include <QTextDocument>

// The colouring itself, in Xcode's palette. SQL: keywords, functions, strings,
// numbers, comments. C++: adds types, macros and preprocessor lines.
class SyntaxHighlighter::Engine : public QSyntaxHighlighter {
public:
    enum Language { Sql, Cpp, CMake, Markdown };

    explicit Engine(QTextDocument *doc) : QSyntaxHighlighter(doc) { setStyle(false, Sql); }

    static Language languageFor(const QString &name)
    {
        return name == QLatin1String("cpp")      ? Cpp
             : name == QLatin1String("cmake")    ? CMake
             : name == QLatin1String("markdown") ? Markdown : Sql;
    }

    void setStyle(bool dark, Language language)
    {
        m_language = language;
        auto fmt = [](const char *color, bool bold = false, bool italic = false) {
            QTextCharFormat f;
            f.setForeground(QColor(QLatin1String(color)));
            if (bold) f.setFontWeight(QFont::DemiBold);
            f.setFontItalic(italic);
            return f;
        };
        m_keyword  = fmt(dark ? "#FF7AB2" : "#AD3DA4", true);
        m_function = fmt(dark ? "#67B7A4" : "#3E8087");
        m_string   = fmt(dark ? "#FC6A5D" : "#C41A16");
        m_number   = fmt(dark ? "#D0BF69" : "#1C00CF");
        m_comment  = fmt(dark ? "#7F8C98" : "#707F8C", false, true);
        m_type     = fmt(dark ? "#5DD8FF" : "#0B4F79");
        m_macro    = fmt(dark ? "#FD8F3F" : "#643820");
        m_preproc  = fmt(dark ? "#FD8F3F" : "#643820");
        rehighlight();
    }

protected:
    void highlightBlock(const QString &text) override
    {
        auto apply = [&](const QRegularExpression &re, const QTextCharFormat &f) {
            for (auto it = re.globalMatch(text); it.hasNext();) {
                const auto m = it.next();
                setFormat(m.capturedStart(), m.capturedLength(), f);
            }
        };
        static const QRegularExpression functions(QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()"));
        static const QRegularExpression numbers(QStringLiteral("\\b\\d+(\\.\\d+)?\\b"));

        if (m_language == Markdown) {
            static const QRegularExpression heading(QStringLiteral("^#{1,6} .*$"));
            static const QRegularExpression code(QStringLiteral("`[^`]*`"));
            static const QRegularExpression fence(QStringLiteral("^```.*$"));
            apply(heading, m_keyword);
            apply(code, m_string);
            apply(fence, m_comment);
            return;
        }
        if (m_language == CMake) {
            static const QRegularExpression variables(QStringLiteral("\\$\\{[^}]*\\}|\\b[A-Z][A-Z0-9_]{2,}\\b"));
            static const QRegularExpression strings(QStringLiteral("\"[^\"]*\"?"));
            static const QRegularExpression comment(QStringLiteral("#[^\\n]*"));
            apply(functions, m_keyword);
            apply(variables, m_type);
            apply(strings, m_string);
            apply(comment, m_comment);
            return;
        }
        apply(functions, m_function);

        if (m_language == Cpp) {
            static const QRegularExpression keywords(QStringLiteral(
                "\\b(class|struct|public|private|protected|const|static|inline|virtual|override|return|"
                "if|else|for|while|auto|void|bool|int|double|float|char|long|short|unsigned|template|"
                "typename|namespace|using|new|delete|this|true|false|nullptr|enum|explicit|constexpr)\\b"));
            static const QRegularExpression types(QStringLiteral("\\b(Q[A-Z][A-Za-z0-9]*|Qi[A-Z][A-Za-z0-9]*|qint64|qreal)\\b"));
            static const QRegularExpression macros(QStringLiteral("\\b(QI|Q)_[A-Z0-9_]+\\b"));
            static const QRegularExpression strings(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"?"));
            static const QRegularExpression preproc(QStringLiteral("^\\s*#\\s*[a-z]+.*$"));
            static const QRegularExpression lineComment(QStringLiteral("//[^\\n]*"));
            apply(types, m_type);
            apply(keywords, m_keyword);
            apply(macros, m_macro);
            apply(numbers, m_number);
            apply(preproc, m_preproc);
            apply(strings, m_string);
            apply(lineComment, m_comment);
        } else {
            static const QRegularExpression keywords(QStringLiteral(
                "\\b(SELECT|FROM|WHERE|AND|OR|NOT|IN|IS|NULL|AS|ON|JOIN|INNER|LEFT|RIGHT|FULL|OUTER|CROSS|"
                "GROUP|BY|ORDER|HAVING|LIMIT|OFFSET|FETCH|NEXT|ROWS|ONLY|TOP|DISTINCT|UNION|ALL|EXCEPT|"
                "INTERSECT|CASE|WHEN|THEN|ELSE|END|LIKE|ILIKE|BETWEEN|EXISTS|ASC|DESC|WITH|RECURSIVE|"
                "INSERT|INTO|VALUES|UPDATE|SET|DELETE|CREATE|TABLE|VIEW|INDEX|DROP|ALTER|TRUE|FALSE|"
                "EXPLAIN|ANALYZE|OVER|PARTITION|WINDOW|USING|NATURAL|CAST)\\b"),
                QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression strings(QStringLiteral("'(?:[^']|'')*'?"));
            static const QRegularExpression lineComment(QStringLiteral("--[^\\n]*"));
            apply(keywords, m_keyword);
            apply(numbers, m_number);
            apply(strings, m_string);
            apply(lineComment, m_comment);
        }

        // /* block comments */, which can span lines (both languages).
        int start = previousBlockState() == 1 ? 0 : int(text.indexOf(QLatin1String("/*")));
        setCurrentBlockState(0);
        while (start >= 0) {
            const int end = int(text.indexOf(QLatin1String("*/"), start + (previousBlockState() == 1 && start == 0 ? 0 : 2)));
            if (end < 0) {
                setCurrentBlockState(1);
                setFormat(start, text.size() - start, m_comment);
                break;
            }
            setFormat(start, end - start + 2, m_comment);
            start = int(text.indexOf(QLatin1String("/*"), end + 2));
        }
    }

private:
    Language m_language = Sql;
    QTextCharFormat m_keyword, m_function, m_string, m_number, m_comment, m_type, m_macro, m_preproc;
};

SyntaxHighlighter::SyntaxHighlighter(QObject *parent)
    : QObject(parent)
{
}

void SyntaxHighlighter::setDocument(QQuickTextDocument *document)
{
    if (document == m_document)
        return;
    m_document = document;
    delete m_engine;
    m_engine = nullptr;
    if (m_document && m_document->textDocument()) {
        m_engine = new Engine(m_document->textDocument());
        m_engine->setStyle(m_dark, Engine::languageFor(m_language));
    }
    emit documentChanged();
}

void SyntaxHighlighter::setLanguage(const QString &language)
{
    if (language == m_language)
        return;
    m_language = language;
    if (m_engine)
        m_engine->setStyle(m_dark, Engine::languageFor(m_language));
    emit languageChanged();
}

void SyntaxHighlighter::setDark(bool dark)
{
    if (dark == m_dark)
        return;
    m_dark = dark;
    if (m_engine)
        m_engine->setStyle(m_dark, Engine::languageFor(m_language));
    emit darkChanged();
}
