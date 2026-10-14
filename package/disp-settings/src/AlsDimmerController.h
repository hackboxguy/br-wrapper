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
    Q_PROPERTY(double absoluteBrightnessNits READ absoluteBrightnessNits NOTIFY absoluteBrightnessChanged)
    Q_PROPERTY(bool absoluteBrightnessValid READ absoluteBrightnessValid NOTIFY absoluteBrightnessChanged)
    Q_PROPERTY(bool absoluteBrightnessCalibrated READ absoluteBrightnessCalibrated NOTIFY absoluteBrightnessChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(QString zone READ zone NOTIFY zoneChanged)
    Q_PROPERTY(bool adaptiveEnabled READ adaptiveEnabled NOTIFY modeChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

    // Dynamic local dimming + thermal limiter (als-dimmer get_ld_status / set_ld_dynamic;
    // pixelpipe FPGA displays with local_dimming_profiles). ldAvailable is false on displays
    // or als-dimmer builds without it, and the QML hides the controls then.
    Q_PROPERTY(bool ldAvailable READ ldAvailable NOTIFY ldStatusChanged)
    Q_PROPERTY(bool ldDynamicEnabled READ ldDynamicEnabled NOTIFY ldStatusChanged)
    Q_PROPERTY(QString ldProfile READ ldProfile NOTIFY ldStatusChanged)
    Q_PROPERTY(bool ldSettled READ ldSettled NOTIFY ldStatusChanged)
    Q_PROPERTY(int ldAlpha READ ldAlpha NOTIFY ldStatusChanged)
    Q_PROPERTY(double ldBoost READ ldBoost NOTIFY ldStatusChanged)          // x (1.0 = off)
    Q_PROPERTY(double ldPcGain READ ldPcGain NOTIFY ldStatusChanged)        // x (1.0 = default)
    Q_PROPERTY(bool ldBoostSuppressed READ ldBoostSuppressed NOTIFY ldStatusChanged)
    Q_PROPERTY(double ldMultiplierPercent READ ldMultiplierPercent NOTIFY ldStatusChanged)
    Q_PROPERTY(double ldAplPercent READ ldAplPercent NOTIFY ldStatusChanged)
    Q_PROPERTY(bool thermalLimitEnabled READ thermalLimitEnabled NOTIFY ldStatusChanged)
    Q_PROPERTY(bool thermalLimiting READ thermalLimiting NOTIFY ldStatusChanged)
    Q_PROPERTY(int thermalLimitPercent READ thermalLimitPercent NOTIFY ldStatusChanged)
    Q_PROPERTY(double thermalStartC READ thermalStartC NOTIFY ldStatusChanged)
    Q_PROPERTY(bool backlightTempValid READ backlightTempValid NOTIFY ldStatusChanged)
    Q_PROPERTY(double backlightTempC READ backlightTempC NOTIFY ldStatusChanged)

public:
    explicit AlsDimmerController(QObject *parent = nullptr);
    ~AlsDimmerController();

    // Property getters
    int brightness() const { return m_brightness; }
    double luxValue() const { return m_luxValue; }
    double absoluteBrightnessNits() const { return m_absoluteBrightnessNits; }
    bool absoluteBrightnessValid() const { return m_absoluteBrightnessValid; }
    bool absoluteBrightnessCalibrated() const { return m_absoluteBrightnessCalibrated; }
    QString mode() const { return m_mode; }
    QString zone() const { return m_zone; }
    bool adaptiveEnabled() const { return m_mode == "auto"; }
    bool connected() const { return m_connected; }
    bool ldAvailable() const { return m_ldAvailable; }
    bool ldDynamicEnabled() const { return m_ldDynamicEnabled; }
    QString ldProfile() const { return m_ldProfile; }
    bool ldSettled() const { return m_ldSettled; }
    int ldAlpha() const { return m_ldAlpha; }
    double ldBoost() const { return m_ldBoost; }
    double ldPcGain() const { return m_ldPcGain; }
    bool ldBoostSuppressed() const { return m_ldBoostSuppressed; }
    double ldMultiplierPercent() const { return m_ldMultiplierPercent; }
    double ldAplPercent() const { return m_ldAplPercent; }
    bool thermalLimitEnabled() const { return m_thermalLimitEnabled; }
    bool thermalLimiting() const { return m_thermalLimiting; }
    int thermalLimitPercent() const { return m_thermalLimitPercent; }
    double thermalStartC() const { return m_thermalStartC; }
    bool backlightTempValid() const { return m_backlightTempValid; }
    double backlightTempC() const { return m_backlightTempC; }

    // Initialize connection
    void setSocketPath(const QString &path);
    void start();

public slots:
    // QML-callable methods
    Q_INVOKABLE void setBrightness(int value);
    Q_INVOKABLE void setAdaptiveMode(bool enabled);
    Q_INVOKABLE void adjustBrightness(int delta);
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void setLdDynamic(bool enabled);

signals:
    void brightnessChanged();
    void luxValueChanged();
    void absoluteBrightnessChanged();
    void modeChanged();
    void zoneChanged();
    void connectedChanged();
    void ldStatusChanged();
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
    void handleLdStatus(const QJsonObject &data);

    QLocalSocket *m_socket;
    QTimer *m_pollTimer;
    QTimer *m_brightnessThrottle;
    QString m_socketPath;

    // State
    int m_brightness;
    int m_pendingBrightness;
    double m_luxValue;
    double m_absoluteBrightnessNits;
    bool m_absoluteBrightnessValid;
    bool m_absoluteBrightnessCalibrated;
    QString m_mode;
    QString m_zone;
    bool m_connected;
    bool m_brightnessUpdatePending;

    // Dynamic local dimming status (get_ld_status, polled every second poll)
    bool m_ldStatusSupported = true;   // false once als-dimmer answered "Unknown command type"
    int m_pollCount = 0;
    bool m_ldAvailable = false;
    bool m_ldDynamicEnabled = false;
    QString m_ldProfile;
    bool m_ldSettled = false;
    int m_ldAlpha = 0;
    double m_ldBoost = 1.0;
    double m_ldPcGain = 1.0;
    bool m_ldBoostSuppressed = false;
    double m_ldMultiplierPercent = 0.0;
    double m_ldAplPercent = 0.0;
    bool m_thermalLimitEnabled = false;
    bool m_thermalLimiting = false;
    int m_thermalLimitPercent = 100;
    double m_thermalStartC = 0.0;
    bool m_backlightTempValid = false;
    double m_backlightTempC = 0.0;

    QByteArray m_readBuffer;
};

#endif // ALSDIMMERCONTROLLER_H
