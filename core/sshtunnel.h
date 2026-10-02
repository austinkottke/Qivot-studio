#ifndef SSHTUNNEL_H
#define SSHTUNNEL_H

#include <QObject>
#include <QProcess>
#include <QPointer>

/// A database reached through SSH: the system's `ssh` forwards a local port
/// to the database, as seen from the SSH server, for as long as it runs.
/**
  Signs in with a key: the one given, or whatever ssh-agent and ~/.ssh offer.
  ssh never asks anything (BatchMode), so a key that needs a passphrase has
  to be in the agent. A server seen for the first time is trusted and noted in
  known_hosts; one whose key has changed since is refused.

\code
    SshTunnel t;
    if (t.start({ "bastion.example.com", 22, "me", "", "db.internal", 5432 }))
        db.setHostName("127.0.0.1"), db.setPort(t.localPort());
\endcode
 */
class SshTunnel : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString host;              // the SSH server
        int     port = 22;
        QString user;
        QString keyFile;           // blank: ssh-agent / ~/.ssh
        QString targetHost;        // the database, as the SSH server sees it
        int     targetPort = 0;
    };

    using QObject::QObject;
    ~SshTunnel() override;

    /// Start it and wait (up to `timeoutMs`) until the local port answers.
    /// False, with error(), if ssh couldn't sign in or forward.
    bool start(const Options &options, int timeoutMs = 20000);
    void stop();

    bool    isRunning() const;
    int     localPort() const { return m_localPort; }
    QString error() const { return m_error; }

    /// The ssh program on this computer, or empty.
    static QString sshProgram();
    /// What start() runs: ssh's arguments for `options` and `localPort`.
    static QStringList arguments(const Options &options, int localPort);

private:
    QPointer<QProcess> m_process;
    int     m_localPort = 0;
    QString m_error;
};

#endif // SSHTUNNEL_H
