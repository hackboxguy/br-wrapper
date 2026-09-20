#ifndef MCUCONTROLLER_H
#define MCUCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <QVector>

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
 *   updateAvailable the slot CRC32 the board reports matches NEITHER slot image
 *                  this rootfs ships for that board. Not "newer" -- different;
 *                  a downgrade is a legitimate update and the shipped image is
 *                  authoritative. This mirrors update-iocs.sh --check.
 *
 * Which image is "the one this rootfs ships" is decided by the board_code in
 * the running image's own header, not by a fixed path. Three things forced
 * that, all seen on the bench on 2026-09-20:
 *
 *   - The display IOC is a different application per board variant (OTS,
 *     REMOTE_DISP, SPARTAN7/S4). One hardcoded default could only ever be
 *     right on one rig, and painted the other two red while they were running
 *     exactly the right firmware.
 *   - app_code does NOT identify a variant: OTS and REMOTE_DISP are both
 *     display_manager and share 0xcdcbc722. board_code is the discriminator.
 *   - A board legitimately runs slot B, whose CRC differs from the slot A
 *     image. Both slot images of a build carry the same board_code and
 *     app_code and differ only in image_crc, so BOTH are accepted.
 *
 * When no shipped image carries the running board_code the status is Unknown,
 * not "update available": a board this rootfs ships nothing for is not a
 * board with a problem.
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
    // "A", "B", or empty when the board does not report it (pre-A/B firmware,
    // or a read that did not land). The QML hides the field when it is empty.
    Q_PROPERTY(QString activeSlot READ activeSlot NOTIFY activeSlotChanged)

public:
    explicit McuController(QObject *parent = nullptr);
    ~McuController();

    void setI2cBus(const QString &bus);
    void setI2cAddress(int address);
    void setReadTemperature(bool enabled);
    // Explicit single image: that file AND its A/B sibling are accepted.
    void setReferenceImage(const QString &path);
    // Directory of shipped images: the one matching the board's board_code is
    // selected automatically, which needs no per-rig configuration.
    void setReferenceDir(const QString &dir);
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
    QString activeSlot() const { return m_activeSlot; }

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
    void activeSlotChanged();

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
    void readActiveSlot(int fd);

    // One shipped slot image, reduced to what identifies it.
    struct RefImage {
        quint32 boardCode;
        quint32 appCode;
        quint32 crc;
    };
    void loadReferences();
    bool appendReference(const QString &path);
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
    QString m_referenceDir;
    bool m_referencesLoaded;
    QVector<RefImage> m_references;

    Status m_candidateStatus;
    int m_candidateCount;

    QString m_shortSerial;
    QString m_activeSlot;

    bool m_versionAlert;
    bool m_updateAvailable;
    bool m_noBootloader;
    QString m_versionAlertReason;

    QTimer *m_refreshTimer;
};

#endif // MCUCONTROLLER_H
