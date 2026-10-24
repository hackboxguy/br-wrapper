#include "NetworkInterface.h"
#include <QDebug>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

NetworkInterface::NetworkInterface(int port, QObject *parent)
    : QObject(parent)
    , m_serverSocket(-1)
    , m_port(port)
    , m_socketNotifier(nullptr)
    , m_currentClient(-1)
{
}

NetworkInterface::~NetworkInterface()
{
    for (int fd : m_clients.keys()) {
        closeClient(fd);
    }
    if (m_serverSocket >= 0) {
        close(m_serverSocket);
    }
}

bool NetworkInterface::startServer()
{
    // Create socket
    m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverSocket < 0) {
        qWarning() << "Failed to create socket:" << strerror(errno);
        return false;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        qWarning() << "Failed to set socket options:" << strerror(errno);
        close(m_serverSocket);
        m_serverSocket = -1;
        return false;
    }

    // Bind socket
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

    // Listen for connections
    if (listen(m_serverSocket, 8) < 0) {
        qWarning() << "Failed to listen on socket:" << strerror(errno);
        close(m_serverSocket);
        m_serverSocket = -1;
        return false;
    }

    // Set non-blocking
    int flags = fcntl(m_serverSocket, F_GETFL, 0);
    fcntl(m_serverSocket, F_SETFL, flags | O_NONBLOCK);

    // Setup socket notifier for incoming connections
    m_socketNotifier = new QSocketNotifier(m_serverSocket, QSocketNotifier::Read, this);
    connect(m_socketNotifier, &QSocketNotifier::activated, this, &NetworkInterface::handleSocketActivity);

    qDebug() << "TCP server listening on port" << m_port;
    return true;
}

void NetworkInterface::sendResponse(const QString &response)
{
    int fd = m_currentClient;
    if (fd < 0 || !m_clients.contains(fd)) {
        return;
    }
    QByteArray data = (response + "\n").toUtf8();
    ssize_t sent = write(fd, data.constData(), data.size());
    if (sent < 0) {
        qWarning() << "Failed to send response:" << strerror(errno);
        closeClient(fd);
    } else {
        qDebug() << "Sent response:" << response;
    }
}

/*
 * Every client is served: launcher-client opens a new connection per command, and the old
 * one-client server (disconnects noticed by a 100 ms poll) closed a connection that arrived
 * before the previous client's close was seen - the client got "Connection reset by peer".
 */
void NetworkInterface::handleSocketActivity()
{
    while (true) {
        int fd = accept(m_serverSocket, nullptr, nullptr);
        if (fd < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                qWarning() << "Failed to accept connection:" << strerror(errno);
            }
            return;
        }
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        QSocketNotifier *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, &NetworkInterface::handleClientData);
        m_clients.insert(fd, Client{notifier, QByteArray()});
        qDebug() << "Client connected" << fd << "(" << m_clients.size() << "open)";
    }
}

void NetworkInterface::handleClientData(int fd)
{
    if (!m_clients.contains(fd)) {
        return;
    }
    char buffer[1024];
    while (true) {
        ssize_t bytesRead = read(fd, buffer, sizeof(buffer));
        if (bytesRead > 0) {
            m_clients[fd].input.append(buffer, int(bytesRead));
            continue;
        }
        if (bytesRead == 0 || (errno != EWOULDBLOCK && errno != EAGAIN)) {
            if (bytesRead < 0) {
                qWarning() << "Read error:" << strerror(errno);
            }
            // a client may send its command and close at once: handle what arrived first
            QByteArray rest = m_clients[fd].input.trimmed();
            m_clients[fd].input.clear();
            if (!rest.isEmpty()) {
                m_currentClient = fd;
                emit commandReceived(QString::fromUtf8(rest));
                m_currentClient = -1;
            }
            qDebug() << "Client disconnected" << fd;
            closeClient(fd);
            return;
        }
        break;   // nothing more to read now
    }
    // process complete lines (the handler may close this client)
    while (m_clients.contains(fd)) {
        QByteArray &input = m_clients[fd].input;
        int newlinePos = input.indexOf('\n');
        if (newlinePos < 0) {
            break;
        }
        QString command = QString::fromUtf8(input.left(newlinePos)).trimmed();
        input.remove(0, newlinePos + 1);
        if (!command.isEmpty()) {
            m_currentClient = fd;
            emit commandReceived(command);
            m_currentClient = -1;
        }
    }
}

void NetworkInterface::closeClient(int fd)
{
    auto it = m_clients.find(fd);
    if (it == m_clients.end()) {
        return;
    }
    it->notifier->setEnabled(false);
    it->notifier->deleteLater();
    m_clients.erase(it);
    close(fd);
}
