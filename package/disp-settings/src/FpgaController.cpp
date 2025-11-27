#include "FpgaController.h"
#include "config.h"
#include <QDebug>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// Register addresses (from disptool)
#define REG_VERSION         0x00    // 4 bytes: month, day, binary, version (BCD)
#define REG_FPGA_ID         0x10    // 4 bytes: reserved, resolution, board_type|size_hi, size_lo
#define REG_PRIVACY_MODE    0x34    // 1 byte: 0=off, 1=on

// Data sizes
#define VERSION_SIZE        4
#define FPGA_ID_SIZE        4

FpgaController::FpgaController(QObject *parent)
    : QObject(parent)
    , m_i2cBus(DEFAULT_I2C_BUS)
    , m_i2cAddress(FPGA_I2C_ADDR)
    , m_privacyMode(false)
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
}

void FpgaController::setI2cAddress(int address)
{
    m_i2cAddress = address;
}

void FpgaController::start()
{
    qDebug() << "FpgaController: Starting with bus" << m_i2cBus << "address 0x" << Qt::hex << m_i2cAddress;
    refresh();
    m_refreshTimer->start();
}

int FpgaController::openI2c()
{
    int fd = open(m_i2cBus.toLocal8Bit().constData(), O_RDWR);
    if (fd < 0) {
        qWarning() << "FpgaController: Failed to open I2C bus" << m_i2cBus;
        return -1;
    }

    if (ioctl(fd, I2C_SLAVE, m_i2cAddress) < 0) {
        qWarning() << "FpgaController: Failed to set I2C address";
        close(fd);
        return -1;
    }

    return fd;
}

void FpgaController::closeI2c(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

bool FpgaController::readRegister(int fd, uint8_t reg, uint8_t *data, int len)
{
    // FPGA protocol: Write 4-byte register address [0x00, 0x00, 0x00, REGISTER]
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
    // FPGA protocol: Write [0x00, 0x00, 0x00, REGISTER, VALUE]
    uint8_t buf[5] = {0x00, 0x00, 0x00, reg, value};
    if (write(fd, buf, 5) != 5) {
        qWarning() << "FpgaController: Failed to write register";
        return false;
    }
    return true;
}

// Helper to convert BCD byte to decimal
static uint8_t bcdToDecimal(uint8_t bcd) {
    return ((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F);
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
    // byte1 = resolution code (0=1920x720, 1=1920x1080, 2=2560x1440)
    // byte2 high nibble = board type
    // byte2 low nibble + byte3 = display size BCD (0x146 = 14.6")

    // Display resolution
    QString displayResolution;
    switch (data[1]) {
        case 0: displayResolution = "1920x720"; break;
        case 1: displayResolution = "1920x1080"; break;
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

void FpgaController::refresh()
{
    int fd = openI2c();
    if (fd < 0) {
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

    closeI2c(fd);

    if (success != m_connected) {
        m_connected = success;
        emit connectedChanged();
    }
}

void FpgaController::setPrivacyMode(bool enabled)
{
    qDebug() << "FpgaController: Setting privacy mode to" << enabled;

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
