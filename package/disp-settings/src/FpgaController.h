#ifndef FPGACONTROLLER_H
#define FPGACONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QString>

/**
 * FpgaController - I2C communication with FPGA
 *
 * Register map:
 * - 0x00: Firmware version (8 bytes)
 * - 0x10: Board info (8 bytes)
 * - 0x34: Privacy mode (1 byte, 0x00=off, 0x01=on)
 */
class FpgaController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY firmwareVersionChanged)
    Q_PROPERTY(QString firmwareId READ firmwareId NOTIFY firmwareIdChanged)
    Q_PROPERTY(QString buildDate READ buildDate NOTIFY buildDateChanged)
    Q_PROPERTY(QString buildDateTime READ buildDateTime NOTIFY buildDateTimeChanged)
    Q_PROPERTY(bool buildTimeValid READ buildTimeValid NOTIFY buildDateTimeChanged)
    Q_PROPERTY(QString boardType READ boardType NOTIFY boardTypeChanged)
    Q_PROPERTY(QString displaySize READ displaySize NOTIFY displaySizeChanged)
    Q_PROPERTY(QString displayResolution READ displayResolution NOTIFY displayResolutionChanged)
    Q_PROPERTY(bool privacyMode READ privacyMode NOTIFY privacyModeChanged)
    Q_PROPERTY(bool localDimmingSupported READ localDimmingSupported NOTIFY localDimmingChanged)
    Q_PROPERTY(bool localDimmingEnabled READ localDimmingEnabled NOTIFY localDimmingChanged)
    Q_PROPERTY(bool pixelCompSupported READ pixelCompSupported NOTIFY pixelCompChanged)
    Q_PROPERTY(bool pixelCompEnabled READ pixelCompEnabled NOTIFY pixelCompChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

public:
    explicit FpgaController(QObject *parent = nullptr);
    ~FpgaController();

    void setI2cBus(const QString &bus);
    void setI2cAddress(int address);
    void start();

    QString firmwareVersion() const { return m_firmwareVersion; }
    QString firmwareId() const { return m_firmwareId; }
    QString buildDate() const { return m_buildDate; }
    QString buildDateTime() const { return m_buildDateTime; }
    bool buildTimeValid() const { return m_buildTimeValid; }
    QString boardType() const { return m_boardType; }
    QString displaySize() const { return m_displaySize; }
    QString displayResolution() const { return m_displayResolution; }
    bool privacyMode() const { return m_privacyMode; }
    bool localDimmingSupported() const { return m_localDimmingSupported; }
    bool localDimmingEnabled() const { return m_localDimmingEnabled; }
    bool pixelCompSupported() const { return m_pixelCompSupported; }
    bool pixelCompEnabled() const { return m_pixelCompEnabled; }
    bool connected() const { return m_connected; }

public slots:
    void setPrivacyMode(bool enabled);
    void setLocalDimming(bool enabled);
    void setPixelCompensation(bool enabled);
    void refresh();

signals:
    void firmwareVersionChanged();
    void firmwareIdChanged();
    void buildDateChanged();
    void buildDateTimeChanged();
    void boardTypeChanged();
    void displaySizeChanged();
    void displayResolutionChanged();
    void privacyModeChanged();
    void localDimmingChanged();
    void pixelCompChanged();
    void connectedChanged();
    void errorOccurred(const QString &message);

private:
    int openI2c();
    void closeI2c(int fd);
    bool readRegister(int fd, uint8_t reg, uint8_t *data, int len);
    bool writeRegister(int fd, uint8_t reg, uint8_t value);
    void parseFirmwareInfo(const uint8_t *data);
    void parseBoardInfo(const uint8_t *data);
    void parseBuildTime(const uint8_t *data);
    void readToggleSettings(int fd);

private:
    QString m_i2cBus;
    int m_i2cAddress;

    QString m_firmwareVersion;
    QString m_firmwareId;
    QString m_buildDate;
    QString m_buildDateTime;
    bool m_buildTimeValid;
    QString m_boardType;
    QString m_displaySize;
    QString m_displayResolution;
    bool m_privacyMode;
    bool m_localDimmingSupported;
    bool m_localDimmingEnabled;
    bool m_pixelCompSupported;
    bool m_pixelCompEnabled;
    bool m_connected;

    QTimer *m_refreshTimer;
};

#endif // FPGACONTROLLER_H
