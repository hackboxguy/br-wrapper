#include "McuController.h"
#include "config.h"
#include <QDebug>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <QFile>
#include <cstring>

// MCU I2C address
#define MCU_I2C_ADDR        0x66

// Register addresses (16-bit, big-endian)
#define REG_FW_VERSION      0x0000  // 2 bytes BCD: major, minor
#define REG_BUILD_DATE      0x0002  // 6 bytes BCD: year_hi, year_lo, month, day, hour, minute
#define REG_BL_TEMP         0x1002  // 2 bytes signed int16 BE, x10 degC
#define REG_BL_PAGE         0xFF00  // bootloader page: 'B','L',proto,version,...
#define REG_SLOT_A_INFO     0xFF20  // validity byte + first 32 header bytes

// Firmware header (lib/fwhdr.h, little-endian): magic at 0, image_crc at 28.
// Only the first 32 bytes are exposed on the register page, which is exactly
// enough to reach the CRC -- the field ends at byte 31.
#define FWHDR_OFFSET_IN_IMAGE   0xA00
#define FWHDR_CRC_OFFSET        28
#define SLOT_INFO_LEN           33      // 1 validity byte + 32 header bytes
#define BL_PAGE_LEN             8

// Chip identification, mirrored from the RH850 SCDS block (lib/chipid.h).
// Firmware older than 983HH v01.05 / OTS v01.13 does not serve these and the
// range reads back all 0xFF, which is how a legacy board is recognised.
#define REG_CHIP_ID             0x0008
#define CHIP_ID_LEN             32
#define CHIP_ID_CHUNK           8

// Read sizes
#define DEVICE_INFO_SIZE    8       // version(2) + build date/time(6) in one shot
#define TEMP_SIZE           2

McuController::McuController(QObject *parent)
    : QObject(parent)
    , m_i2cBus(DEFAULT_I2C_BUS)
    , m_i2cAddress(MCU_I2C_ADDR)
    , m_available(false)
    , m_deviceInfoRead(false)
    , m_readTemperature(true)
    , m_backlightTemp(0.0)
    , m_backlightTempValid(false)
    , m_referenceCrcValid(false)
    , m_referenceCrc(0)
    , m_candidateStatus(StatusUnknown)
    , m_candidateCount(0)
    , m_versionAlert(false)
    , m_updateAvailable(false)
    , m_noBootloader(false)
    , m_refreshTimer(new QTimer(this))
{
    connect(m_refreshTimer, &QTimer::timeout, this, &McuController::refresh);
    m_refreshTimer->setInterval(5000);
}

McuController::~McuController()
{
    m_refreshTimer->stop();
}

void McuController::setI2cBus(const QString &bus)
{
    m_i2cBus = bus;
}

void McuController::setI2cAddress(int address)
{
    m_i2cAddress = address;
}

void McuController::setReadTemperature(bool enabled)
{
    m_readTemperature = enabled;
}

void McuController::setReferenceImage(const QString &path)
{
    m_referenceImage = path;
    m_referenceCrcValid = false;
}

void McuController::start()
{
    qDebug() << "McuController: Starting with bus" << m_i2cBus << "address 0x" << Qt::hex << m_i2cAddress;
    refresh();
    m_refreshTimer->start();
}

int McuController::openI2c()
{
    int fd = open(m_i2cBus.toLocal8Bit().constData(), O_RDWR);
    if (fd < 0) {
        return -1;
    }

    if (ioctl(fd, I2C_SLAVE, m_i2cAddress) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

void McuController::closeI2c(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

bool McuController::readRegister16(int fd, uint16_t reg, uint8_t *data, int len)
{
    // 16-bit sub-addressing (EEPROM-style): write 2 address bytes, then read
    uint8_t regAddr[2] = {
        static_cast<uint8_t>((reg >> 8) & 0xFF),
        static_cast<uint8_t>(reg & 0xFF)
    };

    if (write(fd, regAddr, 2) != 2) {
        return false;
    }

    if (read(fd, data, len) != len) {
        return false;
    }

    return true;
}

// Helper to format a BCD byte as two-digit hex string (e.g. 0x26 -> "26")
static QString bcdByte(uint8_t b)
{
    return QString("%1").arg(b, 2, 16, QChar('0')).toUpper();
}

void McuController::parseDeviceInfo(const uint8_t *data)
{
    // data[0] = FW_VERSION_MAJOR (BCD), data[1] = FW_VERSION_MINOR (BCD)
    QString version = QString("%1.%2").arg(bcdByte(data[0]), bcdByte(data[1]));
    if (version != m_firmwareVersion) {
        m_firmwareVersion = version;
        emit firmwareVersionChanged();
    }

    // data[2..7] = BUILD: year_hi, year_lo, month, day, hour, minute (all BCD)
    QString dateTime = QString("%1%2-%3-%4 %5:%6")
        .arg(bcdByte(data[2]), bcdByte(data[3]),
             bcdByte(data[4]), bcdByte(data[5]),
             bcdByte(data[6]), bcdByte(data[7]));
    if (dateTime != m_buildDateTime) {
        m_buildDateTime = dateTime;
        emit buildDateTimeChanged();
    }
}

void McuController::parseTemperature(const uint8_t *data)
{
    // Signed 16-bit big-endian, 0.1 degC resolution
    int16_t raw = static_cast<int16_t>((data[0] << 8) | data[1]);
    double temp = raw / 10.0;

    if (temp != m_backlightTemp) {
        m_backlightTemp = temp;
        emit backlightTempChanged();
    }

    if (!m_backlightTempValid) {
        m_backlightTempValid = true;
        emit backlightTempValidChanged();
    }
}

void McuController::setStatus(bool noBootloader, bool updateAvailable, const QString &reason)
{
    const bool alert = noBootloader || updateAvailable;
    if (noBootloader == m_noBootloader && updateAvailable == m_updateAvailable &&
        alert == m_versionAlert && reason == m_versionAlertReason) {
        return;
    }
    m_noBootloader = noBootloader;
    m_updateAvailable = updateAvailable;
    m_versionAlert = alert;
    m_versionAlertReason = reason;

    // Logged because this is the one piece of state an operator is told about
    // by colour alone; when someone asks "why is that red", the answer should
    // be in the journal rather than reconstructed from the register map.
    qInfo() << "McuController(0x" + QString::number(m_i2cAddress, 16) + "):"
            << "firmware status ->" << (alert ? "ALERT" : "ok")
            << (noBootloader ? "[no-bootloader]" : "")
            << (updateAvailable ? "[update-available]" : "")
            << (reason.isEmpty() ? QString() : reason);

    emit firmwareStatusChanged();
}

// CRC32 of the image this rootfs ships, read straight out of its header.
// Cached: the file does not change while the app runs, and re-reading it on
// every 5 s poll would be pointless I/O.
bool McuController::referenceCrc(quint32 *crc)
{
    if (m_referenceCrcValid) {
        *crc = m_referenceCrc;
        return true;
    }
    if (m_referenceImage.isEmpty()) {
        return false;
    }

    QFile f(m_referenceImage);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }

    // Two layouts carry the same header: a stripped OTA image starts at the
    // slot base, so the header sits at 0xA00; a full bootloader+slot image has
    // it at 0x10A00. Try the OTA layout first -- that is what this points at.
    static const qint64 kOrigins[2] = { 0, 0x10000 };
    for (int i = 0; i < 2; i++) {
        const qint64 off = kOrigins[i] + FWHDR_OFFSET_IN_IMAGE;
        if (!f.seek(off)) {
            continue;
        }
        char hdr[32];
        if (f.read(hdr, sizeof(hdr)) != sizeof(hdr)) {
            continue;
        }
        if (memcmp(hdr, "RH85", 4) != 0) {
            continue;
        }
        const unsigned char *u = reinterpret_cast<const unsigned char *>(hdr);
        m_referenceCrc = static_cast<quint32>(u[FWHDR_CRC_OFFSET]) |
                         (static_cast<quint32>(u[FWHDR_CRC_OFFSET + 1]) << 8) |
                         (static_cast<quint32>(u[FWHDR_CRC_OFFSET + 2]) << 16) |
                         (static_cast<quint32>(u[FWHDR_CRC_OFFSET + 3]) << 24);
        m_referenceCrcValid = true;
        f.close();
        *crc = m_referenceCrc;
        return true;
    }

    f.close();
    return false;
}

// Read a register block twice and require the two to agree.
//
// One read is not evidence. On the 0x66 IOC the path runs through the 984's
// I2C pass-through, which intermittently clears bit 7 of a byte in a bulk
// read; and on both IOCs a burst of back-to-back transactions occasionally
// returns something stale. A corrupt read of 0xFF00 looks exactly like pre-A/B
// firmware, and a corrupt CRC looks exactly like an available update, so a
// disagreeing read is treated as no answer rather than as a finding.
bool McuController::readStable(int fd, uint16_t reg, uint8_t *out, int len)
{
    uint8_t again[SLOT_INFO_LEN > CHIP_ID_LEN ? SLOT_INFO_LEN : CHIP_ID_LEN];
    if (len > static_cast<int>(sizeof(again))) {
        return false;
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        if (!readRegister16(fd, reg, out, len)) {
            return false;
        }
        if (!readRegister16(fd, reg, again, len)) {
            return false;
        }
        if (memcmp(out, again, len) == 0) {
            return true;
        }
    }
    return false;
}

McuController::Status McuController::evaluateStatus(int fd, QString *reason)
{
    // 1. Is an A/B-capable image running at all?
    uint8_t bl[BL_PAGE_LEN];
    if (!readStable(fd, REG_BL_PAGE, bl, BL_PAGE_LEN)) {
        return StatusUnknown;
    }
    if (bl[0] != 'B' || bl[1] != 'L') {
        *reason = QObject::tr("Pre-A/B firmware: no bootloader, so it cannot be "
                              "updated over I2C. A wire flash is required.");
        return StatusNoBootloader;
    }

    // 2. Does the slot it is running match the image this rootfs ships?
    quint32 fileCrc = 0;
    if (!referenceCrc(&fileCrc)) {
        return StatusUnknown;
    }

    uint8_t info[SLOT_INFO_LEN];
    if (!readStable(fd, REG_SLOT_A_INFO, info, SLOT_INFO_LEN)) {
        return StatusUnknown;
    }
    if (info[0] != 0x01 || memcmp(info + 1, "RH85", 4) != 0) {
        return StatusUnknown;
    }

    const uint8_t *hdr = info + 1;              // skip the validity byte
    const quint32 boardCrc =
        static_cast<quint32>(hdr[FWHDR_CRC_OFFSET]) |
        (static_cast<quint32>(hdr[FWHDR_CRC_OFFSET + 1]) << 8) |
        (static_cast<quint32>(hdr[FWHDR_CRC_OFFSET + 2]) << 16) |
        (static_cast<quint32>(hdr[FWHDR_CRC_OFFSET + 3]) << 24);

    if (boardCrc != fileCrc) {
        *reason = QObject::tr("Update available: the board carries a different "
                              "image (0x%1) from the one this system ships (0x%2).")
                      .arg(boardCrc, 8, 16, QChar('0'))
                      .arg(fileCrc, 8, 16, QChar('0'));
        return StatusUpdateAvailable;
    }
    return StatusOk;
}

// Publish only after the same answer has been seen several polls running.
// StatusUnknown publishes nothing and resets the run: it is the absence of
// evidence, so it must neither raise an alert nor clear one.
void McuController::publishStatus(Status s, const QString &reason)
{
    if (s == StatusUnknown) {
        m_candidateStatus = StatusUnknown;
        m_candidateCount = 0;
        return;
    }

    if (s == m_candidateStatus) {
        if (m_candidateCount < STATUS_CONFIRMATIONS) {
            m_candidateCount++;
        }
    } else {
        m_candidateStatus = s;
        m_candidateCount = 1;
    }

    if (m_candidateCount < STATUS_CONFIRMATIONS) {
        return;
    }
    setStatus(s == StatusNoBootloader, s == StatusUpdateAvailable,
              (s == StatusOk) ? QString() : reason);
}

// The chip ID never changes, so this is read once and kept. All-0xFF means the
// firmware predates the chip-ID registers; the serial stays empty and the QML
// hides the field rather than showing a meaningless placeholder.
void McuController::readChipSerial(int fd)
{
    if (!m_shortSerial.isEmpty()) {
        return;
    }

    uint8_t id[CHIP_ID_LEN];
    for (int off = 0; off < CHIP_ID_LEN; off += CHIP_ID_CHUNK) {
        if (!readStable(fd, static_cast<uint16_t>(REG_CHIP_ID + off),
                        id + off, CHIP_ID_CHUNK)) {
            return;                             // try again next poll
        }
    }

    bool blank = true;
    for (int i = 0; i < CHIP_ID_LEN; i++) {
        if (id[i] != 0xFF) { blank = false; break; }
    }
    if (blank) {
        return;                                 // legacy firmware: no serial
    }

    // CRC-64 (reflected ECMA-182) rendered as 13 Crockford base32 characters,
    // grouped 4-4-5. Same short form disptool prints, so a serial read off the
    // screen matches one read from the command line.
    //
    // A CRC rather than a hash on purpose: it detects every burst error up to
    // its own width, so two MCUs from one lot and wafer -- which differ only in
    // their die coordinates, a 32-bit window -- cannot collide. That is the
    // case that actually matters, since boards get built from one reel.
    static const quint64 POLY = Q_UINT64_C(0xC96C5795D7870F42);
    quint64 c = ~Q_UINT64_C(0);
    for (int i = 0; i < CHIP_ID_LEN; i++) {
        c ^= id[i];
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (POLY & (~(c & 1) + 1));
        }
    }
    c = ~c;

    // Crockford base32 omits I, L, O and U so a serial read aloud cannot be
    // transcribed into a different one.
    static const char *A = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    char out[14];
    quint64 v = c;
    for (int i = 12; i >= 0; i--) { out[i] = A[v & 0x1F]; v >>= 5; }
    out[13] = '\0';

    const QString s = QString::fromLatin1(out);
    m_shortSerial = s.mid(0, 4) + "-" + s.mid(4, 4) + "-" + s.mid(8);
    qInfo() << "McuController(0x" + QString::number(m_i2cAddress, 16) + "):"
            << "chip serial" << m_shortSerial;
    emit shortSerialChanged();
}

void McuController::updateFirmwareStatus(int fd)
{
    QString reason;
    publishStatus(evaluateStatus(fd, &reason), reason);
    readChipSerial(fd);
}

void McuController::refresh()
{
    int fd = openI2c();
    if (fd < 0) {
        if (m_available) {
            m_available = false;
            m_deviceInfoRead = false;
            emit availableChanged();
        }
        if (m_backlightTempValid) {
            m_backlightTempValid = false;
            emit backlightTempValidChanged();
        }
        return;
    }

    bool success = true;

    // Read device info: always when temp read is disabled (acts as presence ping),
    // otherwise only on first success since version/build-date don't change at runtime
    if (!m_deviceInfoRead || !m_readTemperature) {
        uint8_t infoData[DEVICE_INFO_SIZE];
        if (readRegister16(fd, REG_FW_VERSION, infoData, DEVICE_INFO_SIZE)) {
            parseDeviceInfo(infoData);
            m_deviceInfoRead = true;
        } else {
            success = false;
        }
    }

    // Read backlight temperature every poll (skip if disabled)
    if (m_readTemperature) {
        uint8_t tempData[TEMP_SIZE];
        if (readRegister16(fd, REG_BL_TEMP, tempData, TEMP_SIZE)) {
            parseTemperature(tempData);
        } else {
            success = false;
        }
    }

    // Firmware status on every poll. A failed poll deliberately leaves the
    // published status alone -- see publishStatus().
    if (success) {
        updateFirmwareStatus(fd);
    }

    closeI2c(fd);

    if (success != m_available) {
        m_available = success;
        emit availableChanged();
    }
}
