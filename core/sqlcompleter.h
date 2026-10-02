#ifndef SQLCOMPLETER_H
#define SQLCOMPLETER_H

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QVariantMap>

#include "databasesession.h"

/// What could come next as SQL is typed: tables after FROM/JOIN, a table's
/// columns after `alias.`, otherwise the columns of the tables the statement
/// uses, then tables, keywords and functions.
/**
\code
    SqlCompleter { id: completer; session: db }
    completer.complete(editor.text, editor.cursorPosition, false)
    // { start, prefix, items: [{ text, kind: "column"|"table"|"view"|"keyword"|"function", detail }] }
\endcode
 */
class SqlCompleter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(DatabaseSession *session READ session WRITE setSession NOTIFY sessionChanged)

public:
    using QObject::QObject;

    DatabaseSession *session() const { return m_session; }
    void setSession(DatabaseSession *session);

    static constexpr int MaxItems = 40;

    /// Suggestions for the word at `cursor` in `text`. Without `force`, nothing
    /// is offered until there's a word started (or a `.`, or after FROM/JOIN).
    Q_INVOKABLE QVariantMap complete(const QString &text, int cursor, bool force) const;

signals:
    void sessionChanged();

private:
    QPointer<DatabaseSession> m_session;
};

#endif // SQLCOMPLETER_H
