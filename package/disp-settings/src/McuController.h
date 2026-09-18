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
 * - 0xFF00: bootloader page -- 'B','L' magic when an A/B-capable image runs
 * - 0xFF20: slot A info -- validity byte + first 32 header bytes (incl. CRC32)
 *
 * Firmware status
 * ---------------
 * versionAlert is true when the running firmware deserves the operator's
 * attention, and the QML colours the version string with it. Two causes:
 *
 *   noBootloader   0xFF00 does not answer with "BL", so this is a pre-A/B
 *                  image that cannot be updated over I2C at all. It needs a
 *                  wire flash, which is worth surfacing prominently.
 *   updateAvailable the slot CRC32 the board reports differs from the CRC32 of
 *                  the image this rootfs ships. Not "newer" -- different; a
 *                  downgrade is a legitimate update and the shipped image is
 *                  authoritative. This mirrors update-iocs.sh --check.
 *
 * When the status cannot be determined the published status is LEFT ALONE --
 * not reset to "ok". An unreadable board must not be painted as a problem it
 * may not have, and it must not clear a problem it does have either.
 *
 * Every reading is debounced: a candidate status must be seen
 * STATUS_CONFIRMATIONS polls in a row before it is published. Measured on the
 * bench, a single poll is not trustworthy -- a 5-minute run flapped between
 * "no bootloader" and "ok" 34 times on boards that had neither problem,
 * because one corrupt read of 0xFF00 looks exactly like pre-A/B firmware.
 * Both IOCs did it, including the one on the direct bus, so this is not only
 * the 984 pass-through. Neither condition can appear or vanish while the board
 * runs, so waiting a few polls costs nothing and removes the flapping.
 */
class McuController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY firmwareVersionChanged)
    Q_PROPERTY(QString buildDateTime READ buildDateTime NOTIFY buildDateTimeChanged)
    Q_PROPERTY(double backlightTemp READ backlightTemp NOTIFY backlightTempChanged)
    Q_PROPERTY(bool backlightTempValid READ backlightTempValid NOTIFY backlightTempValidChanged)
    Q_PROPERTY(bool versionAlert READ versionAlert NOTIFY firmwareStatusChanged)
    Q_PROPERTY(QString versionAlertReason READ versionAlertReason NOTIFY firmwareStatusChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY firmwareStatusChanged)
    Q_PROPERTY(bool noBootloader READ noBootloader NOTIFY firmwareStatusChanged)
    // Empty when the firmware predates the chip-ID registers, so the QML can
    // simply hide the field on a legacy board rather than show a placeholder.
    Q_PROPERTY(QString shortSerial READ shortSerial NOTIFY shortSerialChanged)

public:
    explicit McuController(QObject *parent = nullptr);
    ~McuController();

    void setI2cBus(const QString &bus);
    void setI2cAddress(int address);
    void setReadTemperature(bool enabled);
    void setReferenceImage(const QString &path);
    void start();

    bool available() const { return m_available; }
    QString firmwareVersion() const { return m_firmwareVersion; }
    QString buildDateTime() const { return m_buildDateTime; }
    double backlightTemp() const { return m_backlightTemp; }
    bool backlightTempValid() const { return m_backlightTempValid; }
    bool versionAlert() const { return m_versionAlert; }
    QString versionAlertReason() const { return m_versionAlertReason; }
    bool updateAvailable() const { return m_updateAvailable; }
    bool noBootloader() const { return m_noBootloader; }
    QString shortSerial() const { return m_shortSerial; }

public slots:
    void refresh();

signals:
    void availableChanged();
    void firmwareVersionChanged();
    void buildDateTimeChanged();
    void backlightTempChanged();
    void backlightTempValidChanged();
    void firmwareStatusChanged();
    void shortSerialChanged();

private:
    int openI2c();
    void closeI2c(int fd);
    bool readRegister16(int fd, uint16_t reg, uint8_t *data, int len);
    void parseDeviceInfo(const uint8_t *data);
    void parseTemperature(const uint8_t *data);
    // Tri-state: a read that did not work is not evidence of anything.
    enum Status { StatusUnknown, StatusOk, StatusNoBootloader, StatusUpdateAvailable };
    static const int STATUS_CONFIRMATIONS = 3;

    bool readStable(int fd, uint16_t reg, uint8_t *out, int len);
    Status evaluateStatus(int fd, QString *reason);
    void publishStatus(Status s, const QString &reason);
    void readChipSerial(int fd);
    bool referenceCrc(quint32 *crc);
    void updateFirmwareStatus(int fd);
    void setStatus(bool noBootloader, bool updateAvailable, const QString &reason);

private:
    QString m_i2cBus;
    int m_i2cAddress;

    bool m_available;
    bool m_deviceInfoRead;
    bool m_readTemperature;
    QString m_firmwareVersion;
    QString m_buildDateTime;
    double m_backlightTemp;
    bool m_backlightTempValid;

    QString m_referenceImage;
    bool m_referenceCrcValid;
    quint32 m_referenceCrc;

    Status m_candidateStatus;
    int m_candidateCount;

    QString m_shortSerial;

    bool m_versionAlert;
    bool m_updateAvailable;
    bool m_noBootloader;
    QString m_versionAlertReason;

    QTimer *m_refreshTimer;
};

#endif // MCUCONTROLLER_H
