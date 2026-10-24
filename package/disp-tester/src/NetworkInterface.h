#ifndef NETWORKINTERFACE_H
#define NETWORKINTERFACE_H

#include <QObject>
#include <QMap>
#include <QSocketNotifier>

class NetworkInterface : public QObject
{
    Q_OBJECT

public:
    explicit NetworkInterface(int port, QObject *parent = nullptr);
    ~NetworkInterface();

    bool startServer();
    // reply to the client whose command is being handled (commandReceived is emitted per command)
    void sendResponse(const QString &response);

signals:
    void commandReceived(const QString &command);

private slots:
    void handleSocketActivity();
    void handleClientData(int fd);

private:
    struct Client {
        QSocketNotifier *notifier;
        QByteArray input;
    };

    int m_serverSocket;
    int m_port;
    QSocketNotifier *m_socketNotifier;
    QMap<int, Client> m_clients;
    int m_currentClient;   // fd the current command came from, -1 outside a command

    void closeClient(int fd);
};

#endif // NETWORKINTERFACE_H
