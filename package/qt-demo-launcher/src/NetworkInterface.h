#ifndef NETWORKINTERFACE_H
#define NETWORKINTERFACE_H

#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <QString>

class NetworkInterface : public QObject
{
    Q_OBJECT

public:
    explicit NetworkInterface(int port, QObject *parent = nullptr);
    ~NetworkInterface();

    bool startServer();
    void sendResponse(const QString &response);

signals:
    void commandReceived(const QString &command);

private slots:
    void handleSocketActivity();
    void checkForData();

private:
    int m_serverSocket;
    int m_clientSocket;
    int m_port;
    QSocketNotifier *m_socketNotifier;
    QSocketNotifier *m_clientNotifier;
    QTimer *m_dataTimer;
    QString m_inputBuffer;

    void closeClient();
};

#endif // NETWORKINTERFACE_H
