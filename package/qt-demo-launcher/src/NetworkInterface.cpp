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
    , m_socketNotifier(nullptr)
    , m_clientNotifier(nullptr)
    , m_dataTimer(nullptr)
{
    m_dataTimer = new QTimer(this);
    connect(m_dataTimer, &QTimer::timeout, this, &NetworkInterface::checkForData);
    m_dataTimer->setInterval(100); // Check for data every 100ms
}

NetworkInterface::~NetworkInterface()
{
    closeClient();
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
    if (listen(m_serverSocket, 1) < 0) {
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
    if (m_clientSocket >= 0) {
        QString message = response + "\n";
        QByteArray data = message.toUtf8();
        ssize_t sent = write(m_clientSocket, data.constData(), data.size());
        if (sent < 0) {
            qWarning() << "Failed to send response:" << strerror(errno);
        } else {
            qDebug() << "Sent response:" << response;
        }
        // Close client connection after sending response
        closeClient();
    }
}

void NetworkInterface::handleSocketActivity()
{
    if (m_clientSocket >= 0) {
        // Already have a client, reject new connections
        int newSocket = accept(m_serverSocket, nullptr, nullptr);
        if (newSocket >= 0) {
            qDebug() << "Rejecting connection - client already connected";
            close(newSocket);
        }
        return;
    }

    // Accept new connection
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    m_clientSocket = accept(m_serverSocket, (struct sockaddr*)&clientAddr, &clientLen);

    if (m_clientSocket < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            qWarning() << "Failed to accept connection:" << strerror(errno);
        }
        return;
    }

    // Set client socket non-blocking
    int flags = fcntl(m_clientSocket, F_GETFL, 0);
    fcntl(m_clientSocket, F_SETFL, flags | O_NONBLOCK);

    qDebug() << "Client connected";

    // Start checking for data
    m_dataTimer->start();
}

void NetworkInterface::checkForData()
{
    if (m_clientSocket < 0) {
        m_dataTimer->stop();
        return;
    }

    char buffer[1024];
    ssize_t bytesRead = read(m_clientSocket, buffer, sizeof(buffer) - 1);

    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        m_inputBuffer += QString::fromUtf8(buffer);

        // Process complete lines
        while (m_inputBuffer.contains('\n')) {
            int newlinePos = m_inputBuffer.indexOf('\n');
            QString command = m_inputBuffer.left(newlinePos).trimmed();
            m_inputBuffer = m_inputBuffer.mid(newlinePos + 1);

            if (!command.isEmpty()) {
                emit commandReceived(command);
            }
        }
    } else if (bytesRead == 0) {
        // Client disconnected
        qDebug() << "Client disconnected";
        closeClient();
    } else if (errno != EWOULDBLOCK && errno != EAGAIN) {
        qWarning() << "Read error:" << strerror(errno);
        closeClient();
    }
}

void NetworkInterface::closeClient()
{
    if (m_clientSocket >= 0) {
        close(m_clientSocket);
        m_clientSocket = -1;
        m_inputBuffer.clear();
        m_dataTimer->stop();
    }
}
