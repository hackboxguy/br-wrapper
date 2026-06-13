#include "FpgaController.h"
#include <QDebug>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define DEFAULT_I2C_BUS     "/dev/i2c-1"
#define FPGA_I2C_ADDR       0x1D

// Register addresses
#define REG_LOCAL_DIMMING   0x2C    // 1 byte: 0x00=enabled (default), 0x01=disabled
#define REG_PIXEL_COMP      0x2D    // 1 byte: 0x00=enabled (default), 0x01=disabled

FpgaController::FpgaController(QObject *parent)
    : QObject(parent)
    , m_i2cBus(DEFAULT_I2C_BUS)
    , m_i2cAddress(FPGA_I2C_ADDR)
    , m_connected(false)
    , m_localDimmingSupported(false)
    , m_localDimmingEnabled(false)
    , m_pixelCompSupported(false)
    , m_pixelCompEnabled(false)
    , m_refreshTimer(new QTimer(this))
{
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
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, m_i2cAddress) < 0) {
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
    // FPGA protocol: write 4-byte register address [0x00, 0x00, 0x00, REG], then read
    uint8_t regAddr[4] = {0x00, 0x00, 0x00, reg};
    if (write(fd, regAddr, 4) != 4) {
        return false;
    }
    return read(fd, data, len) == len;
}

bool FpgaController::writeRegister(int fd, uint8_t reg, uint8_t value)
{
    // FPGA protocol: write [0x00, 0x00, 0x00, REG, VALUE]
    uint8_t buf[5] = {0x00, 0x00, 0x00, reg, value};
    return write(fd, buf, 5) == 5;
}

void FpgaController::readToggleSettings(int fd)
{
    // A valid response is exactly 0x00 (enabled) or 0x01 (disabled); anything else
    // means the register is not supported/wired on this bitstream.
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

    readToggleSettings(fd);
    closeI2c(fd);

    // Connected if either control responded with a valid value
    bool connected = m_localDimmingSupported || m_pixelCompSupported;
    if (connected != m_connected) {
        m_connected = connected;
        emit connectedChanged();
    }
}

void FpgaController::setLocalDimming(bool enabled)
{
    qDebug() << "FpgaController: Setting local dimming to" << enabled;

    int fd = openI2c();
    if (fd < 0) {
        return;
    }

    // Inverted semantics: 0x00 = enabled, 0x01 = disabled
    uint8_t value = enabled ? 0x00 : 0x01;
    if (writeRegister(fd, REG_LOCAL_DIMMING, value)) {
        uint8_t readBack;
        if (readRegister(fd, REG_LOCAL_DIMMING, &readBack, 1) &&
            (readBack == 0x00 || readBack == 0x01)) {
            m_localDimmingSupported = true;
            m_localDimmingEnabled = (readBack == 0x00);
        } else {
            m_localDimmingEnabled = enabled;
        }
        emit localDimmingChanged();
    }

    closeI2c(fd);
}

void FpgaController::setPixelCompensation(bool enabled)
{
    qDebug() << "FpgaController: Setting pixel compensation to" << enabled;

    int fd = openI2c();
    if (fd < 0) {
        return;
    }

    // Inverted semantics: 0x00 = enabled, 0x01 = disabled
    uint8_t value = enabled ? 0x00 : 0x01;
    if (writeRegister(fd, REG_PIXEL_COMP, value)) {
        uint8_t readBack;
        if (readRegister(fd, REG_PIXEL_COMP, &readBack, 1) &&
            (readBack == 0x00 || readBack == 0x01)) {
            m_pixelCompSupported = true;
            m_pixelCompEnabled = (readBack == 0x00);
        } else {
            m_pixelCompEnabled = enabled;
        }
        emit pixelCompChanged();
    }

    closeI2c(fd);
}
