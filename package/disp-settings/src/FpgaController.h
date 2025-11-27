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
    Q_PROPERTY(QString boardType READ boardType NOTIFY boardTypeChanged)
    Q_PROPERTY(QString displaySize READ displaySize NOTIFY displaySizeChanged)
    Q_PROPERTY(QString displayResolution READ displayResolution NOTIFY displayResolutionChanged)
    Q_PROPERTY(bool privacyMode READ privacyMode NOTIFY privacyModeChanged)
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
    QString boardType() const { return m_boardType; }
    QString displaySize() const { return m_displaySize; }
    QString displayResolution() const { return m_displayResolution; }
    bool privacyMode() const { return m_privacyMode; }
    bool connected() const { return m_connected; }

public slots:
    void setPrivacyMode(bool enabled);
    void refresh();

signals:
    void firmwareVersionChanged();
    void firmwareIdChanged();
    void buildDateChanged();
    void boardTypeChanged();
    void displaySizeChanged();
    void displayResolutionChanged();
    void privacyModeChanged();
    void connectedChanged();
    void errorOccurred(const QString &message);

private:
    int openI2c();
    void closeI2c(int fd);
    bool readRegister(int fd, uint8_t reg, uint8_t *data, int len);
    bool writeRegister(int fd, uint8_t reg, uint8_t value);
    void parseFirmwareInfo(const uint8_t *data);
    void parseBoardInfo(const uint8_t *data);

private:
    QString m_i2cBus;
    int m_i2cAddress;

    QString m_firmwareVersion;
    QString m_firmwareId;
    QString m_buildDate;
    QString m_boardType;
    QString m_displaySize;
    QString m_displayResolution;
    bool m_privacyMode;
    bool m_connected;

    QTimer *m_refreshTimer;
};

#endif // FPGACONTROLLER_H
