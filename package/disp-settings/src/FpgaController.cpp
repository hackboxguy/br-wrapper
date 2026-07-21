#include "FpgaController.h"
#include "config.h"
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// Register addresses (from disptool)
#define REG_VERSION         0x00    // 4 bytes: month, day, binary, version (BCD)
#define REG_FPGA_ID         0x10    // 4 bytes: reserved, resolution, board_type|size_hi, size_lo
#define REG_BUILD_TIME      0x14    // 4 bytes: year, hour, minute, second (BCD) — optional
#define REG_LOCAL_DIMMING   0x2C    // 1 byte: 0x00=enabled (default), 0x01=disabled — optional
#define REG_PIXEL_COMP      0x2D    // 1 byte: 0x00=enabled (default), 0x01=disabled — optional
#define REG_PRIVACY_MODE    0x34    // 1 byte: 0=off, 1=on
#define REG_LEGACY_LOCAL_DIMMING 0x29
#define REG_LEGACY_PIXEL_COMP    0x47
#define FPGA_NEW_I2C_ADDR  0x1E

namespace {
const char * const LEGACY_STATE_FILE = "/tmp/fpga-ldpc-state.json";
bool isPlausibleVersion(const uint8_t version[4]) {
    if (version[0] == 0x48) return true;
    if (((version[0] >> 4) & 0x0F) > 9 || (version[0] & 0x0F) > 9 ||
        ((version[1] >> 4) & 0x0F) > 9 || (version[1] & 0x0F) > 9) return false;
    const int month = ((version[0] >> 4) & 0x0F) * 10 + (version[0] & 0x0F);
    const int day = ((version[1] >> 4) & 0x0F) * 10 + (version[1] & 0x0F);
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}
}

// Data sizes
#define VERSION_SIZE        4
#define FPGA_ID_SIZE        4
#define BUILD_TIME_SIZE     4

FpgaController::FpgaController(QObject *parent)
    : QObject(parent)
    , m_i2cBus(DEFAULT_I2C_BUS)
    , m_i2cAddress(FPGA_I2C_ADDR)
    , m_protocolOverride("auto")
    , m_protocol(Protocol::None)
    , m_legacyStateInitialized(false)
    , m_buildTimeValid(false)
    , m_privacyMode(false)
    , m_localDimmingSupported(false)
    , m_localDimmingEnabled(false)
    , m_pixelCompSupported(false)
    , m_pixelCompEnabled(false)
    , m_connected(false)
    , m_refreshTimer(new QTimer(this))
{
    // Refresh FPGA info periodically (every 5 seconds)
    connect(m_refreshTimer, &QTimer::timeout, this, &FpgaController::refresh);
    m_refreshTimer->setInterval(5000);
}

FpgaController::~FpgaController()
{
    m_refreshTimer->stop();
}

void FpgaController::setI2cBus(const QString &bus)
{
    m_i2cBus = bus;
    clearProtocol();
}

void FpgaController::setI2cAddress(int address)
{
    m_i2cAddress = address;
    clearProtocol();
}

void FpgaController::setProtocolOverride(const QString &protocol)
{
    const QString value = protocol.trimmed().toLower();
    m_protocolOverride = (value == "auto" || value == "new" || value == "legacy") ? value : "auto";
    if (m_protocolOverride != value)
        qWarning() << "FpgaController: invalid protocol override" << protocol;
    clearProtocol();
}

void FpgaController::start()
{
    qDebug() << "FpgaController: Starting with bus" << m_i2cBus
             << "protocol override" << m_protocolOverride;
    refresh();
    m_refreshTimer->start();
}

int FpgaController::openI2cAt(uint8_t address)
{
    int fd = open(m_i2cBus.toLocal8Bit().constData(), O_RDWR);
    if (fd < 0) {
        qWarning() << "FpgaController: Failed to open I2C bus" << m_i2cBus;
        return -1;
    }

    if (ioctl(fd, I2C_SLAVE, address) < 0) {
        qWarning() << "FpgaController: Failed to set I2C address";
        close(fd);
        return -1;
    }

    return fd;
}

int FpgaController::openI2c()
{
    if (m_protocol == Protocol::New)
        return openI2cAt(FPGA_NEW_I2C_ADDR);
    if (m_protocol == Protocol::Legacy)
        return openI2cAt(static_cast<uint8_t>(m_i2cAddress));
    return -1;
}

void FpgaController::closeI2c(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

bool FpgaController::readRegister(int fd, uint8_t reg, uint8_t *data, int len)
{
    return m_protocol == Protocol::New ? readRegisterNew(fd, reg, data, len)
                                       : readRegisterLegacy(fd, reg, data, len);
}

bool FpgaController::readRegisterNew(int fd, uint8_t reg, uint8_t *data, int len)
{
    return write(fd, &reg, 1) == 1 && read(fd, data, len) == len;
}

bool FpgaController::readRegisterLegacy(int fd, uint8_t reg, uint8_t *data, int len)
{
    uint8_t regAddr[4] = {0x00, 0x00, 0x00, reg};
    if (write(fd, regAddr, 4) != 4) {
        qWarning() << "FpgaController: Failed to write register address";
        return false;
    }

    // Read data
    if (read(fd, data, len) != len) {
        qWarning() << "FpgaController: Failed to read register data";
        return false;
    }

    return true;
}

bool FpgaController::writeRegister(int fd, uint8_t reg, uint8_t value)
{
    return m_protocol == Protocol::New ? writeRegisterNew(fd, reg, &value, 1)
                                       : writeRegisterLegacy(fd, reg, &value, 1);
}

bool FpgaController::writeRegisterNew(int fd, uint8_t reg, const uint8_t *data, int len)
{
    if (len < 1 || len > 2) return false;
    uint8_t buf[4] = {0x00, reg, 0x00, 0x00};
    for (int i = 0; i < len; ++i) buf[2 + i] = data[i];
    return write(fd, buf, len + 2) == len + 2;
}

bool FpgaController::writeRegisterLegacy(int fd, uint8_t reg, const uint8_t *data, int len)
{
    if (len < 1 || len > 2) return false;
    uint8_t packet[6] = {0x00, 0x00, 0x00, reg, 0x00, 0x00};
    for (int i = 0; i < len; ++i) packet[4 + i] = data[i];
    if (write(fd, packet, len + 4) != len + 4) {
        qWarning() << "FpgaController: Failed to write register";
        return false;
    }
    return true;
}

bool FpgaController::probeProtocol(Protocol protocol)
{
    const int fd = openI2cAt(protocol == Protocol::New ? FPGA_NEW_I2C_ADDR
                                                       : static_cast<uint8_t>(m_i2cAddress));
    if (fd < 0) return false;
    uint8_t version[4];
    const bool ok = protocol == Protocol::New ? readRegisterNew(fd, REG_VERSION, version, 4)
                                               : readRegisterLegacy(fd, REG_VERSION, version, 4);
    closeI2c(fd);
    return ok && isPlausibleVersion(version);
}

bool FpgaController::ensureProtocol()
{
    if (m_protocol != Protocol::None) return true;
    if (m_protocolOverride != "legacy" && probeProtocol(Protocol::New))
        m_protocol = Protocol::New;
    else if (m_protocolOverride != "new" && probeProtocol(Protocol::Legacy))
        m_protocol = Protocol::Legacy;
    else
        return false;
    m_legacyStateInitialized = false;
    qDebug() << "FpgaController: selected"
             << (m_protocol == Protocol::New ? "new FPGA protocol (0x1E)"
                                               : "legacy FPGA protocol (0x1D)");
    return true;
}

bool FpgaController::pingCurrentProtocol(int fd)
{
    uint8_t version[4];
    const bool ok = m_protocol == Protocol::New ? readRegisterNew(fd, REG_VERSION, version, 4)
                                                  : readRegisterLegacy(fd, REG_VERSION, version, 4);
    return ok && isPlausibleVersion(version);
}

void FpgaController::clearProtocol()
{
    m_protocol = Protocol::None;
    m_legacyStateInitialized = false;
    const bool ldChanged = m_localDimmingSupported;
    const bool pcChanged = m_pixelCompSupported;
    m_localDimmingSupported = false;
    m_pixelCompSupported = false;
    if (ldChanged) emit localDimmingChanged();
    if (pcChanged) emit pixelCompChanged();
}

bool FpgaController::loadLegacyState(bool *localDimming, bool *pixelCompensation) const
{
    QFile file(LEGACY_STATE_FILE);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const QJsonObject state = document.object();
    if (state.value("protocol").toString() != "legacy") return false;
    *localDimming = state.value("local_dimming").toBool(true);
    *pixelCompensation = state.value("pixel_compensation").toBool(true);
    return true;
}

void FpgaController::saveLegacyState() const
{
    QJsonObject state;
    state["version"] = 1;
    state["protocol"] = "legacy";
    state["local_dimming"] = m_localDimmingEnabled;
    state["pixel_compensation"] = m_pixelCompEnabled;
    QSaveFile file(LEGACY_STATE_FILE);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(state).toJson(QJsonDocument::Compact)) < 0 || !file.commit())
        qWarning() << "FpgaController: failed to save legacy LD/PC state";
}

void FpgaController::initializeLegacyState()
{
    if (m_legacyStateInitialized) return;
    bool ld = true, pc = true;
    const bool restored = loadLegacyState(&ld, &pc);
    m_legacyStateInitialized = true;
    const bool ldChanged = !m_localDimmingSupported || m_localDimmingEnabled != ld;
    const bool pcChanged = !m_pixelCompSupported || m_pixelCompEnabled != pc;
    m_localDimmingSupported = true;
    m_localDimmingEnabled = ld;
    m_pixelCompSupported = true;
    m_pixelCompEnabled = pc;
    if (ldChanged) emit localDimmingChanged();
    if (pcChanged) emit pixelCompChanged();
    qDebug() << "FpgaController: legacy LD/PC state"
             << (restored ? "restored from /tmp" : "assumed on after FPGA power-on");
}

// Helper to convert BCD byte to decimal
static uint8_t bcdToDecimal(uint8_t bcd) {
    return ((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F);
}

// Helper to validate a BCD byte (both nibbles must be 0-9)
static bool isValidBcd(uint8_t value) {
    return (((value >> 4) & 0x0F) <= 9) && ((value & 0x0F) <= 9);
}

// Helper to get month name from BCD value
static QString getMonthName(uint8_t bcd_month) {
    uint8_t month = bcdToDecimal(bcd_month);
    static const char* months[] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    if (month >= 1 && month <= 12) {
        return QString(months[month]);
    }
    return QString("Invalid");
}

void FpgaController::parseFirmwareInfo(const uint8_t *data)
{
    // Version register format (BCD mode):
    // byte0 = BCD month (0x11 = November)
    // byte1 = BCD day (0x14 = 14)
    // byte2 = binary type
    // byte3 = BCD version (0x01 = v01)

    // Parse version (BCD)
    uint8_t versionNum = bcdToDecimal(data[3]);
    QString version = QString("v%1").arg(versionNum, 2, 10, QChar('0'));
    if (version != m_firmwareVersion) {
        m_firmwareVersion = version;
        emit firmwareVersionChanged();
    }

    // Binary/firmware ID
    uint8_t binaryType = bcdToDecimal(data[2]);
    QString firmwareId = QString("%1").arg(binaryType, 2, 10, QChar('0'));
    if (firmwareId != m_firmwareId) {
        m_firmwareId = firmwareId;
        emit firmwareIdChanged();
    }

    // Build date from BCD month and day
    QString monthName = getMonthName(data[0]);
    uint8_t day = bcdToDecimal(data[1]);
    QString buildDate = QString("%1 %2").arg(monthName).arg(day);
    if (buildDate != m_buildDate) {
        m_buildDate = buildDate;
        emit buildDateChanged();
    }
}

void FpgaController::parseBoardInfo(const uint8_t *data)
{
    // FPGA ID register format:
    // byte0 = reserved
    // byte1 = resolution code (0=1920x1080, 1=1920x720, 2=2560x1440)
    // byte2 high nibble = board type
    // byte2 low nibble + byte3 = display size BCD (0x146 = 14.6")

    // Display resolution
    QString displayResolution;
    switch (data[1]) {
        case 0: displayResolution = "1920x1080"; break;
        case 1: displayResolution = "1920x720"; break;
        case 2: displayResolution = "2560x1440"; break;
        default: displayResolution = QString("Unknown (%1)").arg(data[1]);
    }
    if (displayResolution != m_displayResolution) {
        m_displayResolution = displayResolution;
        emit displayResolutionChanged();
    }

    // Board type (high nibble of byte 2)
    uint8_t boardTypeCode = (data[2] >> 4) & 0x0F;
    QString boardType;
    switch (boardTypeCode) {
        case 0: boardType = "xilinx-spartan7"; break;
        case 1: boardType = "xilinx-artix7"; break;
        case 2: boardType = "xilinx-au15p"; break;
        case 3: boardType = "lattice-ecp5"; break;
        case 4: boardType = "lattice-lae3u25f"; break;
        default: boardType = QString("Unknown (%1)").arg(boardTypeCode);
    }
    if (boardType != m_boardType) {
        m_boardType = boardType;
        emit boardTypeChanged();
    }

    // Display size BCD (low nibble of byte2 + byte3)
    // 0x146 = 14.6"
    uint16_t sizeRaw = ((data[2] & 0x0F) << 8) | data[3];
    uint8_t tens = (sizeRaw >> 8) & 0x0F;
    uint8_t ones = (sizeRaw >> 4) & 0x0F;
    uint8_t tenths = sizeRaw & 0x0F;
    QString displaySize = QString("%1%2.%3\"").arg(tens).arg(ones).arg(tenths);
    if (displaySize != m_displaySize) {
        m_displaySize = displaySize;
        emit displaySizeChanged();
    }
}

void FpgaController::parseBuildTime(const uint8_t *data)
{
    // Build time register format (BCD): byte0=year, byte1=hour, byte2=min, byte3=sec
    // Only present on FPGA builds that implement register 0x14. Validate the BCD
    // and ranges; unimplemented registers typically read 0xFF and fail validation.
    bool valid = isValidBcd(data[0]) && isValidBcd(data[1]) &&
                 isValidBcd(data[2]) && isValidBcd(data[3]);

    uint8_t hour = bcdToDecimal(data[1]);
    uint8_t minute = bcdToDecimal(data[2]);
    uint8_t second = bcdToDecimal(data[3]);
    if (valid) {
        valid = (hour <= 23) && (minute <= 59) && (second <= 59);
    }

    if (!valid) {
        if (m_buildTimeValid) {
            m_buildTimeValid = false;
            emit buildDateTimeChanged();
        }
        return;
    }

    // Combine with existing "Month Day" (from register 0x00) into
    // "Month Day Year HH:MM:SS", e.g. "November 14 2026 13:45:30"
    int year = 2000 + bcdToDecimal(data[0]);
    QString dateTime = QString("%1 %2 %3:%4:%5")
        .arg(m_buildDate)
        .arg(year)
        .arg(hour,   2, 10, QChar('0'))
        .arg(minute, 2, 10, QChar('0'))
        .arg(second, 2, 10, QChar('0'));

    if (dateTime != m_buildDateTime || !m_buildTimeValid) {
        m_buildDateTime = dateTime;
        m_buildTimeValid = true;
        emit buildDateTimeChanged();
    }
}

void FpgaController::refresh()
{
    if (!ensureProtocol()) {
        if (m_connected) {
            m_connected = false;
            emit connectedChanged();
        }
        return;
    }

    int fd = openI2c();
    if (fd < 0 || !pingCurrentProtocol(fd)) {
        closeI2c(fd);
        clearProtocol();
        if (m_connected) {
            m_connected = false;
            emit connectedChanged();
        }
        return;
    }

    bool success = true;
    uint8_t versionData[VERSION_SIZE];
    uint8_t fpgaIdData[FPGA_ID_SIZE];
    uint8_t privacyData;

    // Read version info (register 0x00, 4 bytes)
    if (readRegister(fd, REG_VERSION, versionData, VERSION_SIZE)) {
        parseFirmwareInfo(versionData);
    } else {
        success = false;
    }

    // Read FPGA ID (register 0x10, 4 bytes)
    if (readRegister(fd, REG_FPGA_ID, fpgaIdData, FPGA_ID_SIZE)) {
        parseBoardInfo(fpgaIdData);
    } else {
        success = false;
    }

    // Read optional build time (register 0x14, 4 bytes: year, hour, min, sec BCD)
    // Not all FPGA builds implement this; parseBuildTime validates and hides if absent
    uint8_t buildTimeData[BUILD_TIME_SIZE];
    if (readRegister(fd, REG_BUILD_TIME, buildTimeData, BUILD_TIME_SIZE)) {
        parseBuildTime(buildTimeData);
    } else if (m_buildTimeValid) {
        m_buildTimeValid = false;
        emit buildDateTimeChanged();
    }

    // Read privacy mode (register 0x34, 1 byte)
    if (readRegister(fd, REG_PRIVACY_MODE, &privacyData, 1)) {
        bool privacy = (privacyData != 0);
        if (privacy != m_privacyMode) {
            m_privacyMode = privacy;
            emit privacyModeChanged();
        }
    } else {
        success = false;
    }

    // Read optional local-dimming (0x2C) and pixel-compensation (0x2D) toggles
    readToggleSettings(fd);

    closeI2c(fd);

    if (success != m_connected) {
        m_connected = success;
        emit connectedChanged();
    }
}

void FpgaController::setPrivacyMode(bool enabled)
{
    qDebug() << "FpgaController: Setting privacy mode to" << enabled;

    if (!ensureProtocol()) {
        emit errorOccurred("No compatible FPGA interface found");
        return;
    }

    int fd = openI2c();
    if (fd < 0) {
        emit errorOccurred("Failed to open I2C bus");
        return;
    }

    uint8_t value = enabled ? 0x01 : 0x00;
    if (writeRegister(fd, REG_PRIVACY_MODE, value)) {
        m_privacyMode = enabled;
        emit privacyModeChanged();
        qDebug() << "FpgaController: Privacy mode set successfully";
    } else {
        emit errorOccurred("Failed to set privacy mode");
    }

    closeI2c(fd);
}

void FpgaController::readToggleSettings(int fd)
{
    if (m_protocol == Protocol::Legacy) {
        initializeLegacyState();
        return;
    }

    // Local dimming (0x2C) and pixel compensation (0x2D) are write-only registers
    // that echo back the last-written value (block-RAM, powers up at 0). A valid
    // response is exactly 0x00 (enabled) or 0x01 (disabled); anything else means
    // the register is not supported/wired on this bitstream.
    uint8_t val;

    bool ldSupported = false;
    bool ldEnabled = m_localDimmingEnabled;
    if (readRegister(fd, REG_LOCAL_DIMMING, &val, 1) && (val == 0x00 || val == 0x01)) {
        ldSupported = true;
        ldEnabled = (val == 0x00);  // 0x00 = enabled, 0x01 = disabled
    }
    if (ldSupported != m_localDimmingSupported || ldEnabled != m_localDimmingEnabled) {
        m_localDimmingSupported = ldSupported;
        m_localDimmingEnabled = ldEnabled;
        emit localDimmingChanged();
    }

    bool pcSupported = false;
    bool pcEnabled = m_pixelCompEnabled;
    if (readRegister(fd, REG_PIXEL_COMP, &val, 1) && (val == 0x00 || val == 0x01)) {
        pcSupported = true;
        pcEnabled = (val == 0x00);  // 0x00 = enabled, 0x01 = disabled
    }
    if (pcSupported != m_pixelCompSupported || pcEnabled != m_pixelCompEnabled) {
        m_pixelCompSupported = pcSupported;
        m_pixelCompEnabled = pcEnabled;
        emit pixelCompChanged();
    }
}

void FpgaController::setLocalDimming(bool enabled)
{
    qDebug() << "FpgaController: Setting local dimming to" << enabled;

    if (!ensureProtocol()) {
        emit errorOccurred("No compatible FPGA LD/PC interface found");
        return;
    }
    if (m_protocol == Protocol::Legacy) initializeLegacyState();
    int fd = openI2c();
    if (fd < 0) {
        emit errorOccurred("Failed to open I2C bus");
        return;
    }

    // Inverted semantics: 0x00 = enabled, 0x01 = disabled
    uint8_t value = enabled ? 0x00 : 0x01;
    const bool writeOk = m_protocol == Protocol::New
        ? writeRegisterNew(fd, REG_LOCAL_DIMMING, &value, 1)
        : writeRegisterLegacy(fd, REG_LEGACY_LOCAL_DIMMING, &value, 1);
    if (writeOk) {
        // Read back to confirm the write took effect
        uint8_t readBack;
        if (m_protocol == Protocol::New && readRegister(fd, REG_LOCAL_DIMMING, &readBack, 1) &&
            (readBack == 0x00 || readBack == 0x01)) {
            m_localDimmingSupported = true;
            m_localDimmingEnabled = (readBack == 0x00);
        } else {
            m_localDimmingEnabled = enabled;
        }
        if (m_protocol == Protocol::Legacy) saveLegacyState();
        emit localDimmingChanged();
    } else {
        emit errorOccurred("Failed to set local dimming");
    }

    closeI2c(fd);
}

void FpgaController::setPixelCompensation(bool enabled)
{
    qDebug() << "FpgaController: Setting pixel compensation to" << enabled;

    if (!ensureProtocol()) {
        emit errorOccurred("No compatible FPGA LD/PC interface found");
        return;
    }
    if (m_protocol == Protocol::Legacy) initializeLegacyState();
    int fd = openI2c();
    if (fd < 0) {
        emit errorOccurred("Failed to open I2C bus");
        return;
    }

    // Inverted semantics: 0x00 = enabled, 0x01 = disabled
    uint8_t value = enabled ? 0x00 : 0x01;
    const uint8_t legacyValue[2] = {0x00, static_cast<uint8_t>(enabled ? 0x70 : 0x00)};
    const bool writeOk = m_protocol == Protocol::New
        ? writeRegisterNew(fd, REG_PIXEL_COMP, &value, 1)
        : writeRegisterLegacy(fd, REG_LEGACY_PIXEL_COMP, legacyValue, 2);
    if (writeOk) {
        // Read back to confirm the write took effect
        uint8_t readBack;
        if (m_protocol == Protocol::New && readRegister(fd, REG_PIXEL_COMP, &readBack, 1) &&
            (readBack == 0x00 || readBack == 0x01)) {
            m_pixelCompSupported = true;
            m_pixelCompEnabled = (readBack == 0x00);
        } else {
            m_pixelCompEnabled = enabled;
        }
        if (m_protocol == Protocol::Legacy) saveLegacyState();
        emit pixelCompChanged();
    } else {
        emit errorOccurred("Failed to set pixel compensation");
    }

    closeI2c(fd);
}
