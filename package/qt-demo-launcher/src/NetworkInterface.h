#ifndef NETWORKINTERFACE_H
#define NETWORKINTERFACE_H

#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <QString>

// The launcher's command port: one command per connection, one reply line.
//
// Clients are served one at a time, in order. A connection that arrives
// while another is served waits in the kernel's listen queue (it used to be
// closed unread, "Connection reset by peer", its command lost) and is served
// when the one before it is answered.
//
// A queued client that hung up before its turn has given up (launcher-client
// timed out, and may already have retried): its command is dropped, not run
// late. A client that hangs up right after sending while nothing is queued
// ("echo stop-app | nc ...") still has its command run, as before.
class NetworkInterface : public QObject
{
    Q_OBJECT

public:
    explicit NetworkInterface(int port, QObject *parent = nullptr);
    ~NetworkInterface();

    bool startServer();
    // The reply to the command being handled; the connection is then closed.
    // Only the first reply to a command is sent, later ones are ignored.
    void sendResponse(const QString &response);

    // A connection that sends no complete command within this time is closed
    // (it would hold up the clients queued behind it).
    static const int kIdleTimeoutMs = 5000;
    static const int kListenBacklog = 64;   // when full, new clients retry ~1 s later
    static const int kMaxCommandBytes = 4096;

signals:
    void commandReceived(const QString &command);

private slots:
    void handleSocketActivity();
    void readClient();
    void acceptNext();
    void idleTimeout();

private:
    int m_serverSocket;
    int m_clientSocket;
    int m_port;
    bool m_clientQueued;      // accepted after waiting behind another client
    bool m_awaitingResponse;  // a command was emitted and not answered yet
    bool m_inCommand;         // inside the commandReceived handler
    QSocketNotifier *m_socketNotifier;
    QSocketNotifier *m_clientNotifier;
    QTimer *m_idleTimer;
    QByteArray m_inputBuffer;

    bool acceptClient(bool queued);
    void closeClient();
};

#endif // NETWORKINTERFACE_H
