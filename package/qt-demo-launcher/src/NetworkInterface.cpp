#include "NetworkInterface.h"
#include "NetworkInterface.moc"
#include <QDebug>
#include <QTimer>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

NetworkInterface::NetworkInterface(int port, QObject *parent)
    : QObject(parent)
    , m_serverSocket(-1)
    , m_clientSocket(-1)
    , m_port(port)
    , m_clientQueued(false)
    , m_awaitingResponse(false)
    , m_inCommand(false)
    , m_socketNotifier(nullptr)
    , m_clientNotifier(nullptr)
    , m_idleTimer(new QTimer(this))
{
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(kIdleTimeoutMs);
    connect(m_idleTimer, &QTimer::timeout, this, &NetworkInterface::idleTimeout);
}

NetworkInterface::~NetworkInterface()
{
    if (m_clientSocket >= 0) {
        close(m_clientSocket);
    }
    if (m_serverSocket >= 0) {
        close(m_serverSocket);
    }
}

bool NetworkInterface::startServer()
{
    // Close-on-exec: the apps the launcher starts (and their children) must not inherit the
    // listening socket, or a launcher restart cannot bind its port while one of them runs
    m_serverSocket = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (m_serverSocket < 0) {
        qWarning() << "Failed to create socket:" << strerror(errno);
        return false;
    }

    int opt = 1;
    if (setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        qWarning() << "Failed to set socket options:" << strerror(errno);
        close(m_serverSocket);
        m_serverSocket = -1;
        return false;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(m_port);

    if (bind(m_serverSocket, (struct sockaddr*)&address, sizeof(address)) < 0) {
        qWarning() << "Failed to bind socket to port" << m_port << ":" << strerror(errno);
        close(m_serverSocket);
        m_serverSocket = -1;
        return false;
    }

    // Clients that arrive while one is served wait here (see the header)
    if (listen(m_serverSocket, kListenBacklog) < 0) {
        qWarning() << "Failed to listen on socket:" << strerror(errno);
        close(m_serverSocket);
        m_serverSocket = -1;
        return false;
    }

    int flags = fcntl(m_serverSocket, F_GETFL, 0);
    fcntl(m_serverSocket, F_SETFL, flags | O_NONBLOCK);

    m_socketNotifier = new QSocketNotifier(m_serverSocket, QSocketNotifier::Read, this);
    connect(m_socketNotifier, &QSocketNotifier::activated, this, &NetworkInterface::handleSocketActivity);

    qDebug() << "TCP server listening on port" << m_port;
    return true;
}

void NetworkInterface::sendResponse(const QString &response)
{
    // One reply per command: a second one (some handlers answer twice) would
    // otherwise reach nobody, or, with clients queued, the wrong one
    if (m_clientSocket < 0 || !m_awaitingResponse) {
        return;
    }
    m_awaitingResponse = false;
    QByteArray data = (response + "\n").toUtf8();
    // MSG_NOSIGNAL: a client that has gone gives EPIPE, not a SIGPIPE that ends the launcher
    ssize_t sent = send(m_clientSocket, data.constData(), data.size(), MSG_NOSIGNAL);
    if (sent < 0) {
        qWarning() << "Failed to send response:" << strerror(errno);
    } else {
        qDebug() << "Sent response:" << response;
    }
    // One command per connection
    closeClient();
}

// A connection is waiting while nobody is served: take it
void NetworkInterface::handleSocketActivity()
{
    if (m_clientSocket >= 0) {
        // Not expected (the notifier is off while a client is served): leave it queued
        m_socketNotifier->setEnabled(false);
        return;
    }
    acceptClient(false);
}

// The client before has been served: take the next queued connection, if any
void NetworkInterface::acceptNext()
{
    if (m_clientSocket >= 0 || m_inCommand || m_serverSocket < 0) {
        return;
    }
    if (!acceptClient(true)) {
        // Nobody waiting: back to listening
        m_socketNotifier->setEnabled(true);
    }
}

bool NetworkInterface::acceptClient(bool queued)
{
    int fd = accept4(m_serverSocket, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (fd < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            qWarning() << "Failed to accept connection:" << strerror(errno);
        }
        return false;
    }

    // Serve this one alone; the next ones wait in the listen queue
    m_socketNotifier->setEnabled(false);
    m_clientSocket = fd;
    m_clientQueued = queued;
    m_awaitingResponse = false;
    m_inputBuffer.clear();
    m_clientNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_clientNotifier, &QSocketNotifier::activated, this, &NetworkInterface::readClient);
    m_idleTimer->start();
    qDebug() << "Client connected" << (queued ? "(was queued)" : "");
    return true;
}

void NetworkInterface::readClient()
{
    if (m_clientSocket < 0) {
        return;
    }

    // Everything there is; a hang-up shows as read() == 0 after the data
    bool hungUp = false;
    char buffer[1024];
    for (;;) {
        ssize_t n = read(m_clientSocket, buffer, sizeof(buffer));
        if (n > 0) {
            m_inputBuffer.append(buffer, int(n));
            if (m_inputBuffer.size() > kMaxCommandBytes) {
                qWarning() << "Command too long, closing the connection";
                closeClient();
                return;
            }
            continue;
        }
        if (n == 0) {
            hungUp = true;
        } else if (errno == EINTR) {
            continue;
        } else if (errno != EWOULDBLOCK && errno != EAGAIN) {
            qWarning() << "Read error:" << strerror(errno);
            closeClient();
            return;
        }
        break;
    }

    int newline;
    while (m_clientSocket >= 0 && (newline = m_inputBuffer.indexOf('\n')) >= 0) {
        QString command = QString::fromUtf8(m_inputBuffer.left(newline)).trimmed();
        m_inputBuffer.remove(0, newline + 1);
        if (command.isEmpty()) {
            continue;
        }
        if (hungUp && m_clientQueued) {
            // It waited behind another client and gave up meanwhile; running its
            // command now could undo what its retry has done since (a late
            // stop-app ending the app the retry started)
            qDebug() << "Dropping command of a client that gave up while queued:" << command;
            closeClient();
            return;
        }
        m_awaitingResponse = true;
        m_inCommand = true;
        emit commandReceived(command);
        m_inCommand = false;
    }

    if (m_clientSocket >= 0 && hungUp) {
        qDebug() << "Client disconnected";
        closeClient();
    } else if (m_clientSocket < 0) {
        // Answered inside the handler: now take the next one
        QTimer::singleShot(0, this, &NetworkInterface::acceptNext);
    }
}

void NetworkInterface::idleTimeout()
{
    if (m_clientSocket >= 0) {
        qDebug() << "Client sent no command in" << kIdleTimeoutMs << "ms, closing it";
        closeClient();
    }
}

void NetworkInterface::closeClient()
{
    if (m_clientSocket < 0) {
        return;
    }
    if (m_clientNotifier) {
        m_clientNotifier->setEnabled(false);
        m_clientNotifier->deleteLater();   // we may be inside its activated() slot
        m_clientNotifier = nullptr;
    }
    close(m_clientSocket);
    m_clientSocket = -1;
    m_awaitingResponse = false;
    m_inputBuffer.clear();
    m_idleTimer->stop();
    // The next client from the event loop, never from inside a command's
    // handler (its second reply must not reach the next client)
    if (!m_inCommand) {
        QTimer::singleShot(0, this, &NetworkInterface::acceptNext);
    }
}
