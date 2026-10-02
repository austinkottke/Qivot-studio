#include "sshtunnel.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

SshTunnel::~SshTunnel()
{
    stop();
}

QString SshTunnel::sshProgram()
{
    QString ssh = QStandardPaths::findExecutable(QStringLiteral("ssh"));
#ifdef Q_OS_WIN
    if (ssh.isEmpty())        // Windows' own OpenSSH, where PATH doesn't list it
        ssh = QStandardPaths::findExecutable(QStringLiteral("ssh"), { QStringLiteral("C:/Windows/System32/OpenSSH") });
#endif
    return ssh;
}

QStringList SshTunnel::arguments(const Options &o, int localPort)
{
    QStringList args{
        QStringLiteral("-N"), QStringLiteral("-T"),
        QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),              // never ask: no terminal to ask in
        QStringLiteral("-o"), QStringLiteral("ExitOnForwardFailure=yes"),
        QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=accept-new"),
        QStringLiteral("-o"), QStringLiteral("ConnectTimeout=15"),
        QStringLiteral("-o"), QStringLiteral("ServerAliveInterval=30"),
        QStringLiteral("-L"), QStringLiteral("127.0.0.1:%1:%2:%3").arg(localPort).arg(o.targetHost).arg(o.targetPort),
        QStringLiteral("-p"), QString::number(o.port > 0 ? o.port : 22),
    };
    if (!o.keyFile.trimmed().isEmpty())
        args << QStringLiteral("-i") << o.keyFile.trimmed() << QStringLiteral("-o") << QStringLiteral("IdentitiesOnly=yes");
    args << (o.user.trimmed().isEmpty() ? o.host.trimmed() : o.user.trimmed() + QLatin1Char('@') + o.host.trimmed());
    return args;
}

bool SshTunnel::start(const Options &o, int timeoutMs)
{
    stop();
    m_error.clear();
    const QString ssh = sshProgram();
    if (ssh.isEmpty()) {
        m_error = tr("ssh isn't installed (or isn't on the PATH), so Studio can't open an SSH tunnel.");
        return false;
    }
    if (o.host.trimmed().isEmpty() || o.targetHost.trimmed().isEmpty() || o.targetPort <= 0) {
        m_error = tr("The SSH tunnel needs the SSH server, and the database's host and port.");
        return false;
    }
    // A free local port: ask for any, and give it back for ssh to take.
    {
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0)) {
            m_error = tr("No free local port for the tunnel: %1").arg(probe.errorString());
            return false;
        }
        m_localPort = probe.serverPort();
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->start(ssh, arguments(o, m_localPort));
    if (!m_process->waitForStarted(5000)) {
        m_error = tr("Couldn't start ssh: %1").arg(m_process->errorString());
        stop();
        return false;
    }

    // Ready once the forwarded port answers; failed if ssh gives up first.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (m_process->state() == QProcess::NotRunning || m_process->waitForFinished(150)) {
            QString why = QString::fromLocal8Bit(m_process->readAll()).trimmed();
            if (why.isEmpty())
                why = tr("ssh exited (code %1)").arg(m_process->exitCode());
            m_error = why;
            stop();
            return false;
        }
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(m_localPort));
        if (socket.waitForConnected(250)) {
            socket.abort();
            return true;
        }
        QCoreApplication::processEvents();
    }
    m_error = tr("The SSH server didn't answer in %1 s.").arg(timeoutMs / 1000);
    stop();
    return false;
}

void SshTunnel::stop()
{
    if (!m_process)
        return;
    QProcess *p = m_process;
    m_process = nullptr;
    if (p->state() != QProcess::NotRunning) {
        p->terminate();
        if (!p->waitForFinished(2000)) {
            p->kill();
            p->waitForFinished(1000);
        }
    }
    delete p;
}

bool SshTunnel::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}
