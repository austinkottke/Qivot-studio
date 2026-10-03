#include "redisclient.h"

#include <QElapsedTimer>
#include <QSslSocket>
#include <QTcpSocket>

RedisClient::RedisClient() = default;

RedisClient::~RedisClient()
{
    close();
}

RedisClient::Reply RedisClient::errorReply(const QString &message)
{
    Reply r;
    r.type = Reply::Error;
    r.str = message.toUtf8();
    return r;
}

bool RedisClient::open(const QString &host, int port, bool tls, bool verify, int timeoutMs, QString *error)
{
    close();
    if (tls) {
        if (!QSslSocket::supportsSsl()) {
            if (error)
                *error = QStringLiteral("TLS isn't available in this build of Qt.");
            return false;
        }
        auto *ssl = new QSslSocket;
        if (!verify)
            ssl->setPeerVerifyMode(QSslSocket::VerifyNone);
        m_socket.reset(ssl);
        ssl->connectToHostEncrypted(host, static_cast<quint16>(port));
        if (!ssl->waitForEncrypted(timeoutMs)) {
            if (error)
                *error = ssl->errorString();
            close();
            return false;
        }
    } else {
        m_socket = std::make_unique<QTcpSocket>();
        m_socket->connectToHost(host, static_cast<quint16>(port));
        if (!m_socket->waitForConnected(timeoutMs)) {
            if (error)
                *error = m_socket->errorString();
            close();
            return false;
        }
    }
    return true;
}

void RedisClient::close()
{
    if (m_socket) {
        m_socket->abort();
        m_socket.reset();
    }
    m_buffer.clear();
}

bool RedisClient::isOpen() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

QByteArray RedisClient::encode(const QList<QByteArray> &args)
{
    QByteArray out = '*' + QByteArray::number(args.size()) + "\r\n";
    for (const QByteArray &a : args)
        out += '$' + QByteArray::number(a.size()) + "\r\n" + a + "\r\n";
    return out;
}

int RedisClient::parse(const QByteArray &data, int pos, Reply *out)
{
    if (pos >= data.size())
        return 0;
    const int eol = data.indexOf("\r\n", pos);
    if (eol < 0)
        return 0;
    const char kind = data.at(pos);
    const QByteArray line = data.mid(pos + 1, eol - pos - 1);
    const int afterLine = eol + 2;
    auto aggregate = [&](Reply::Type type, qint64 count) -> int {
        out->type = type;
        out->items.clear();
        if (count < 0) {                      // a nil array
            out->type = Reply::Nil;
            return afterLine - pos;
        }
        int at = afterLine;
        for (qint64 i = 0; i < count; ++i) {
            Reply item;
            const int used = parse(data, at, &item);
            if (used <= 0)
                return used;
            out->items << item;
            at += used;
        }
        return at - pos;
    };
    switch (kind) {
    case '+': out->type = Reply::Status;  out->str = line; return afterLine - pos;
    case '-': out->type = Reply::Error;   out->str = line; return afterLine - pos;
    case ':': out->type = Reply::Integer; out->integer = line.toLongLong(); return afterLine - pos;
    case ',': out->type = Reply::Double;  out->number = line.toDouble(); out->str = line; return afterLine - pos;
    case '#': out->type = Reply::Boolean; out->integer = line == "t" ? 1 : 0; return afterLine - pos;
    case '(': out->type = Reply::BigNumber; out->str = line; return afterLine - pos;
    case '_': out->type = Reply::Nil; return afterLine - pos;
    case '$': case '=': case '!': {
        const qint64 n = line.toLongLong();
        if (n < 0) { out->type = Reply::Nil; return afterLine - pos; }
        if (data.size() < afterLine + n + 2)
            return 0;
        out->type = kind == '$' ? Reply::Bulk : kind == '=' ? Reply::Verbatim : Reply::Error;
        out->str = data.mid(afterLine, int(n));
        if (kind == '=' && out->str.size() >= 4 && out->str.at(3) == ':')
            out->str.remove(0, 4);            // "txt:" – the format, not the text
        return afterLine + int(n) + 2 - pos;
    }
    case '*': return aggregate(Reply::Array, line.toLongLong());
    case '~': return aggregate(Reply::Set, line.toLongLong());
    case '>': return aggregate(Reply::Push, line.toLongLong());
    case '%': return aggregate(Reply::Map, line.toLongLong() * 2);
    case '|': {                                // attributes: skip them, then the reply itself
        Reply attrs;
        const int used = aggregate(Reply::Map, line.toLongLong() * 2);
        if (used <= 0)
            return used;
        Q_UNUSED(attrs);
        const int rest = parse(data, pos + used, out);
        return rest <= 0 ? rest : used + rest;
    }
    default:
        return -1;
    }
}

bool RedisClient::readReplies(int count, QVector<Reply> *out, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (out->size() < count) {
        Reply r;
        const int used = parse(m_buffer, 0, &r);
        if (used < 0)
            return false;
        if (used > 0) {
            m_buffer.remove(0, used);
            *out << r;
            continue;
        }
        const qint64 left = timeoutMs - timer.elapsed();
        if (left <= 0 || !m_socket || !m_socket->waitForReadyRead(int(left)))
            return false;
        m_buffer += m_socket->readAll();
    }
    return true;
}

RedisClient::Reply RedisClient::command(const QList<QByteArray> &args, int timeoutMs)
{
    const QVector<Reply> r = pipeline({ args }, timeoutMs);
    return r.isEmpty() ? errorReply(QStringLiteral("No reply")) : r.first();
}

QVector<RedisClient::Reply> RedisClient::pipeline(const QList<QList<QByteArray>> &commands, int timeoutMs)
{
    QVector<Reply> out;
    if (!isOpen()) {
        for (int i = 0; i < commands.size(); ++i)
            out << errorReply(QStringLiteral("Not connected"));
        return out;
    }
    QByteArray wire;
    for (const QList<QByteArray> &c : commands)
        wire += encode(c);
    m_socket->write(wire);
    m_socket->flush();
    if (!readReplies(commands.size(), &out, timeoutMs)) {
        // A late reply would answer the wrong question: start again.
        const QString why = m_socket && m_socket->state() == QAbstractSocket::ConnectedState
                                ? QStringLiteral("No answer in %1 s").arg(timeoutMs / 1000)
                                : QStringLiteral("The connection was closed");
        close();
        while (out.size() < commands.size())
            out << errorReply(why);
    }
    return out;
}

QVariant RedisClient::Reply::toVariant() const
{
    switch (type) {
    case Nil:       return QVariant();
    case Integer:   return integer;
    case Boolean:   return integer != 0;
    case Double:    return number;
    case Array: case Set: case Push: case Map: {
        QVariantList list;
        for (const Reply &r : items)
            list << r.toVariant();
        return list;
    }
    default:        return QString::fromUtf8(str);
    }
}

QList<QByteArray> RedisClient::split(const QString &line, QString *error)
{
    QList<QByteArray> args;
    const QByteArray s = line.toUtf8();
    int i = 0;
    auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    while (i < s.size()) {
        while (i < s.size() && (s.at(i) == ' ' || s.at(i) == '\t'))
            ++i;
        if (i >= s.size())
            break;
        QByteArray arg;
        const char quote = (s.at(i) == '"' || s.at(i) == '\'') ? s.at(i) : 0;
        if (quote) {
            ++i;
            bool closed = false;
            while (i < s.size()) {
                const char c = s.at(i);
                if (c == quote) { closed = true; ++i; break; }
                if (c == '\\' && i + 1 < s.size()) {
                    const char n = s.at(i + 1);
                    if (quote == '\'') {
                        arg += n == '\'' ? '\'' : '\\';
                        if (n == '\'') i += 2; else ++i;
                        continue;
                    }
                    if (n == 'x' && i + 3 < s.size() && hex(s.at(i + 2)) >= 0 && hex(s.at(i + 3)) >= 0) {
                        arg += char(hex(s.at(i + 2)) * 16 + hex(s.at(i + 3)));
                        i += 4;
                        continue;
                    }
                    arg += n == 'n' ? '\n' : n == 'r' ? '\r' : n == 't' ? '\t' : n == 'b' ? '\b' : n == 'a' ? '\a' : n;
                    i += 2;
                    continue;
                }
                arg += c;
                ++i;
            }
            if (!closed) {
                if (error)
                    *error = QStringLiteral("A quote isn't closed.");
                return {};
            }
        } else {
            while (i < s.size() && s.at(i) != ' ' && s.at(i) != '\t')
                arg += s.at(i++);
        }
        args << arg;
    }
    return args;
}

QString RedisClient::format(const Reply &r, int indent)
{
    auto quoted = [](const QByteArray &b) {
        QString out = QStringLiteral("\"");
        for (unsigned char c : b) {
            if (c == '"' || c == '\\') { out += QLatin1Char('\\'); out += QLatin1Char(char(c)); }
            else if (c == '\n') out += QStringLiteral("\\n");
            else if (c == '\r') out += QStringLiteral("\\r");
            else if (c == '\t') out += QStringLiteral("\\t");
            else if (c < 0x20 || c == 0x7f) out += QStringLiteral("\\x%1").arg(c, 2, 16, QLatin1Char('0'));
            else out += QLatin1Char(char(c));
        }
        // Valid UTF-8 shows as text; the bytes above were taken one at a time.
        const QString utf8 = QString::fromUtf8(b);
        if (!utf8.contains(QChar::ReplacementCharacter) && !b.contains('\0')) {
            QString t = utf8;
            t.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""))
             .replace(QLatin1Char('\n'), QStringLiteral("\\n")).replace(QLatin1Char('\r'), QStringLiteral("\\r"))
             .replace(QLatin1Char('\t'), QStringLiteral("\\t"));
            return QLatin1Char('"') + t + QLatin1Char('"');
        }
        return out + QLatin1Char('"');
    };
    switch (r.type) {
    case Reply::Nil:       return QStringLiteral("(nil)");
    case Reply::Status:    return r.text();
    case Reply::Error:     return QStringLiteral("(error) ") + r.text();
    case Reply::Integer:   return QStringLiteral("(integer) %1").arg(r.integer);
    case Reply::Boolean:   return r.integer ? QStringLiteral("(true)") : QStringLiteral("(false)");
    case Reply::Double:    return QStringLiteral("(double) ") + r.text();
    case Reply::BigNumber: return QStringLiteral("(big number) ") + r.text();
    case Reply::Bulk: case Reply::Verbatim: return quoted(r.str);
    case Reply::Array: case Reply::Set: case Reply::Push: case Reply::Map: {
        if (r.items.isEmpty())
            return r.type == Reply::Map ? QStringLiteral("(empty hash)") : QStringLiteral("(empty array)");
        QStringList lines;
        const bool map = r.type == Reply::Map;
        const int n = map ? r.items.size() / 2 : r.items.size();
        const int width = QString::number(n).size();
        const QString pad(indent, QLatin1Char(' '));
        for (int i = 0; i < n; ++i) {
            const QString number = QStringLiteral("%1) ").arg(i + 1, width);
            const int inner = indent + number.size();
            QString item = map ? format(r.items.at(2 * i), inner) + QStringLiteral(" => ") + format(r.items.at(2 * i + 1), inner)
                               : format(r.items.at(i), inner);
            lines << (i == 0 ? QString() : pad) + number + item;
        }
        return lines.join(QLatin1Char('\n'));
    }
    }
    return QString();
}
