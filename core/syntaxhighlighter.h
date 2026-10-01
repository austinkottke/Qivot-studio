#ifndef SYNTAXHIGHLIGHTER_H
#define SYNTAXHIGHLIGHTER_H

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>

/// Syntax colouring for a QML TextEdit / TextArea: SQL, C++, CMake or Markdown.
/**
\code
    TextArea { id: editor }
    SyntaxHighlighter { document: editor.textDocument; language: "cpp"; dark: Theme.dark }
\endcode
 */
class SyntaxHighlighter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)

public:
    explicit SyntaxHighlighter(QObject *parent = nullptr);

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
    /// "sql" (the default), "cpp", "cmake" or "markdown".
    QString language() const { return m_language; }
    void setLanguage(const QString &language);
    bool dark() const { return m_dark; }
    void setDark(bool dark);

signals:
    void documentChanged();
    void languageChanged();
    void darkChanged();

private:
    class Engine;
    QPointer<QQuickTextDocument> m_document;
    Engine *m_engine = nullptr;
    QString m_language = QStringLiteral("sql");
    bool m_dark = false;
};

#endif // SYNTAXHIGHLIGHTER_H
