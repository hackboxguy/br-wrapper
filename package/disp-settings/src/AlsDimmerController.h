#ifndef ALSDIMMERCONTROLLER_H
#define ALSDIMMERCONTROLLER_H

#include <QObject>
#include <QLocalSocket>
#include <QTimer>
#include <QJsonObject>

class AlsDimmerController : public QObject
{
    Q_OBJECT

    // Properties exposed to QML
    Q_PROPERTY(int brightness READ brightness NOTIFY brightnessChanged)
    Q_PROPERTY(double luxValue READ luxValue NOTIFY luxValueChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(QString zone READ zone NOTIFY zoneChanged)
    Q_PROPERTY(bool adaptiveEnabled READ adaptiveEnabled NOTIFY modeChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

public:
    explicit AlsDimmerController(QObject *parent = nullptr);
    ~AlsDimmerController();

    // Property getters
    int brightness() const { return m_brightness; }
    double luxValue() const { return m_luxValue; }
    QString mode() const { return m_mode; }
    QString zone() const { return m_zone; }
    bool adaptiveEnabled() const { return m_mode == "auto"; }
    bool connected() const { return m_connected; }

    // Initialize connection
    void setSocketPath(const QString &path);
    void start();

public slots:
    // QML-callable methods
    Q_INVOKABLE void setBrightness(int value);
    Q_INVOKABLE void setAdaptiveMode(bool enabled);
    Q_INVOKABLE void adjustBrightness(int delta);
    Q_INVOKABLE void reconnect();

signals:
    void brightnessChanged();
    void luxValueChanged();
    void modeChanged();
    void zoneChanged();
    void connectedChanged();
    void errorOccurred(const QString &error);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QLocalSocket::LocalSocketError error);
    void pollStatus();
    void processBrightnessUpdate();

private:
    void connectToService();
    void sendCommand(const QJsonObject &command);
    void parseResponse(const QByteArray &data);
    void handleStatusResponse(const QJsonObject &data);
    void handleConfigResponse(const QJsonObject &data);

    QLocalSocket *m_socket;
    QTimer *m_pollTimer;
    QTimer *m_brightnessThrottle;
    QString m_socketPath;

    // State
    int m_brightness;
    int m_pendingBrightness;
    double m_luxValue;
    QString m_mode;
    QString m_zone;
    bool m_connected;
    bool m_brightnessUpdatePending;

    QByteArray m_readBuffer;
};

#endif // ALSDIMMERCONTROLLER_H
