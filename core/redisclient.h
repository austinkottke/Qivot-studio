#ifndef REDISCLIENT_H
#define REDISCLIENT_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariant>
#include <QVector>
#include <memory>

class QTcpSocket;

/// A small Redis client: RESP over TCP (or TLS), one command or a pipeline at
/// a time, waiting for the answer. Used from the thread that opened it.
/**
\code
    RedisClient r;
    if (r.open("localhost", 6379, false, false, 5000, &why)) {
        r.command({ "AUTH", "secret" });
        const RedisClient::Reply n = r.command({ "DBSIZE" });   // n.integer
    }
\endcode
 */
class RedisClient {
public:
    /// A reply, as RESP2 or RESP3 gives it.
    struct Reply {
        enum Type { Nil, Status, Error, Integer, Bulk, Array, Double, Boolean, Map, Set, BigNumber, Verbatim, Push };
        Type        type = Nil;
        QByteArray  str;            // Status, Error, Bulk, Verbatim, BigNumber
        qint64      integer = 0;    // Integer, Boolean
        double      number = 0;     // Double
        QVector<Reply> items;       // Array, Set, Push; Map as key, value, key, value…

        bool isError() const { return type == Error; }
        bool isNil() const { return type == Nil; }
        QString text() const { return QString::fromUtf8(str); }
        /// Plain Qt values: strings, numbers, lists (a Map as a list of pairs).
        QVariant toVariant() const;
    };

    RedisClient();
    ~RedisClient();
    RedisClient(const RedisClient &) = delete;
    RedisClient &operator=(const RedisClient &) = delete;

    /// Connect (TLS when `tls`; `verify` checks the server's certificate).
    bool open(const QString &host, int port, bool tls, bool verify, int timeoutMs, QString *error);
    void close();
    bool isOpen() const;

    /// Send one command and wait for its reply (an Error reply if it timed out
    /// or the connection went).
    Reply command(const QList<QByteArray> &args, int timeoutMs = 10000);
    /// Send several at once and wait for all their replies, in order.
    QVector<Reply> pipeline(const QList<QList<QByteArray>> &commands, int timeoutMs = 10000);

    /// The wire form of a command: an array of bulk strings.
    static QByteArray encode(const QList<QByteArray> &args);
    /// Read one reply from `data` at `pos`: the bytes it took, 0 if more are
    /// needed, -1 if it isn't RESP.
    static int parse(const QByteArray &data, int pos, Reply *out);

    /// Split a line typed in a console into arguments: spaces separate, "…"
    /// and '…' quote (with \" \n \t \\ and \xHH inside double quotes).
    static QList<QByteArray> split(const QString &line, QString *error = nullptr);
    /// A reply as redis-cli shows it: "value", (integer) 5, (nil), 1) …
    static QString format(const Reply &reply, int indent = 0);

private:
    static Reply errorReply(const QString &message);
    bool readReplies(int count, QVector<Reply> *out, int timeoutMs);

    std::unique_ptr<QTcpSocket> m_socket;
    QByteArray m_buffer;
};

#endif // REDISCLIENT_H
