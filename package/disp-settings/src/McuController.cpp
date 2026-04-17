#include "McuController.h"
#include "config.h"
#include <QDebug>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// MCU I2C address
#define MCU_I2C_ADDR        0x66

// Register addresses (16-bit, big-endian)
#define REG_FW_VERSION      0x0000  // 2 bytes BCD: major, minor
#define REG_BUILD_DATE      0x0002  // 6 bytes BCD: year_hi, year_lo, month, day, hour, minute
#define REG_BL_TEMP         0x1002  // 2 bytes signed int16 BE, x10 degC

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

    closeI2c(fd);

    if (success != m_available) {
        m_available = success;
        emit availableChanged();
    }
}
