#ifndef MCUCONTROLLER_H
#define MCUCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QString>

/**
 * McuController - I2C communication with RH850 MCU (16-bit sub-addressing)
 *
 * Register map (EEPROM-style, 16-bit addresses):
 * - 0x0000: Firmware version (2 bytes, BCD: major.minor)
 * - 0x0002: Build date/time (6 bytes, BCD: year_hi,year_lo,month,day,hour,minute)
 * - 0x1002: Backlight temperature (2 bytes, signed int16 BE, x10 degC)
 */
class McuController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY firmwareVersionChanged)
    Q_PROPERTY(QString buildDateTime READ buildDateTime NOTIFY buildDateTimeChanged)
    Q_PROPERTY(double backlightTemp READ backlightTemp NOTIFY backlightTempChanged)
    Q_PROPERTY(bool backlightTempValid READ backlightTempValid NOTIFY backlightTempValidChanged)

public:
    explicit McuController(QObject *parent = nullptr);
    ~McuController();

    void setI2cBus(const QString &bus);
    void start();

    bool available() const { return m_available; }
    QString firmwareVersion() const { return m_firmwareVersion; }
    QString buildDateTime() const { return m_buildDateTime; }
    double backlightTemp() const { return m_backlightTemp; }
    bool backlightTempValid() const { return m_backlightTempValid; }

public slots:
    void refresh();

signals:
    void availableChanged();
    void firmwareVersionChanged();
    void buildDateTimeChanged();
    void backlightTempChanged();
    void backlightTempValidChanged();

private:
    int openI2c();
    void closeI2c(int fd);
    bool readRegister16(int fd, uint16_t reg, uint8_t *data, int len);
    void parseDeviceInfo(const uint8_t *data);
    void parseTemperature(const uint8_t *data);

private:
    QString m_i2cBus;
    int m_i2cAddress;

    bool m_available;
    bool m_deviceInfoRead;
    QString m_firmwareVersion;
    QString m_buildDateTime;
    double m_backlightTemp;
    bool m_backlightTempValid;

    QTimer *m_refreshTimer;
};

#endif // MCUCONTROLLER_H
