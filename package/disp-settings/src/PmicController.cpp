#include "PmicController.h"
#include "config.h"
#include <QDebug>
#include <QStringList>
#include <QThread>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// Default MCU address for bridge (IOC)
#define DEFAULT_MCU_ADDR        0x66

// PMIC internal slave address
#define PMIC_I2C_ADDR           0x6B

// PMIC registers we care about
#define PMIC_REG_PROT           0x14  // Protections
#define PMIC_REG_CHAN           0x16  // Channel enables
#define PMIC_REG_FAULT          0x1D  // Faults

// MCU bridge registers (16-bit sub-addressing on MCU)
#define MCU_REG_BRIDGE_SLAVE    0x0310
#define MCU_REG_BRIDGE_STATUS   0x0314
#define MCU_REG_BRIDGE_DATA     0x0320

// Bridge protocol constants
#define BRIDGE_CMD_READ         0x01
#define BRIDGE_STATUS_DONE      0x02
#define BRIDGE_STATUS_ERROR     0xFF
#define BRIDGE_MAX_POLL         20

PmicController::PmicController(QObject *parent)
    : QObject(parent)
    , m_i2cBus(DEFAULT_I2C_BUS)
    , m_mcuAddress(DEFAULT_MCU_ADDR)
    , m_available(false)
    , m_statusOk(false)
    , m_chPavdd(false), m_chNavdd(false), m_chVgh(false)
    , m_chVgl(false), m_chVcom(false), m_chReset(false)
    , m_protOtp(false), m_protUvp(false), m_protScp(false)
    , m_faultPavdd(false), m_faultNavdd(false), m_faultVgh(false), m_faultVgl(false)
    , m_refreshTimer(new QTimer(this))
{
    connect(m_refreshTimer, &QTimer::timeout, this, &PmicController::refresh);
    m_refreshTimer->setInterval(2000);
}

PmicController::~PmicController()
{
    m_refreshTimer->stop();
}

void PmicController::setI2cBus(const QString &bus)
{
    m_i2cBus = bus;
}

void PmicController::setMcuAddress(int address)
{
    m_mcuAddress = address;
}

void PmicController::start()
{
    qDebug() << "PmicController: Starting via MCU 0x" << Qt::hex << m_mcuAddress
             << " bridge to PMIC 0x" << PMIC_I2C_ADDR;
    refresh();
    m_refreshTimer->start();
}

int PmicController::openI2c()
{
    int fd = open(m_i2cBus.toLocal8Bit().constData(), O_RDWR);
    if (fd < 0) {
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, m_mcuAddress) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void PmicController::closeI2c(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

bool PmicController::writeMcuReg16(int fd, uint16_t reg, const uint8_t *data, int len)
{
    // Write 16-bit register address + data in one transaction
    uint8_t buf[2 + 16];
    if (len > 16) return false;
    buf[0] = (reg >> 8) & 0xFF;
    buf[1] = reg & 0xFF;
    for (int i = 0; i < len; ++i) buf[2 + i] = data[i];
    return write(fd, buf, 2 + len) == (2 + len);
}

bool PmicController::readMcuReg16(int fd, uint16_t reg, uint8_t *data, int len)
{
    uint8_t regAddr[2] = {
        static_cast<uint8_t>((reg >> 8) & 0xFF),
        static_cast<uint8_t>(reg & 0xFF)
    };
    if (write(fd, regAddr, 2) != 2) return false;
    return read(fd, data, len) == len;
}

bool PmicController::bridgeRead(int fd, uint8_t slave, uint8_t reg, uint8_t len, uint8_t *out)
{
    if (len < 1 || len > 16) return false;

    // Step 1: burst-write [slave, reg, len, cmd=read] to 0x0310
    uint8_t setup[4] = { slave, reg, len, BRIDGE_CMD_READ };
    if (!writeMcuReg16(fd, MCU_REG_BRIDGE_SLAVE, setup, 4)) {
        return false;
    }

    // Step 2: poll status (10ms initial, 20 attempts at 10ms each)
    QThread::msleep(10);
    for (int i = 0; i < BRIDGE_MAX_POLL; ++i) {
        uint8_t status = 0;
        if (readMcuReg16(fd, MCU_REG_BRIDGE_STATUS, &status, 1)) {
            if (status == BRIDGE_STATUS_DONE) {
                // Step 3: read result from 0x0320
                return readMcuReg16(fd, MCU_REG_BRIDGE_DATA, out, len);
            }
            if (status == BRIDGE_STATUS_ERROR) {
                return false;  // NACK from PMIC
            }
        }
        QThread::msleep(10);
    }
    return false;  // timeout
}

void PmicController::parseStatus(uint8_t chanReg, uint8_t protReg, uint8_t faultReg)
{
    // Channels (0x16): bit0=PAVDD, bit1=VGL, bit2=VGH, bit3=NAVDD, bit4=VCOM, bit5=RESET
    m_chPavdd = (chanReg >> 0) & 1;
    m_chVgl   = (chanReg >> 1) & 1;
    m_chVgh   = (chanReg >> 2) & 1;
    m_chNavdd = (chanReg >> 3) & 1;
    m_chVcom  = (chanReg >> 4) & 1;
    m_chReset = (chanReg >> 5) & 1;

    // Protections (0x14): bit0=SCP, bit1=UVP, bit2=OTP
    m_protScp = (protReg >> 0) & 1;
    m_protUvp = (protReg >> 1) & 1;
    m_protOtp = (protReg >> 2) & 1;

    // Faults (0x1D, lower 4 bits): bit0=NAVDD, bit1=VGH, bit2=VGL, bit3=PAVDD
    m_faultNavdd = (faultReg >> 0) & 1;
    m_faultVgh   = (faultReg >> 1) & 1;
    m_faultVgl   = (faultReg >> 2) & 1;
    m_faultPavdd = (faultReg >> 3) & 1;
    bool anyFault = (faultReg & 0x0F) != 0;

    bool newStatusOk = !anyFault;
    if (newStatusOk != m_statusOk) m_statusOk = newStatusOk;

    QString summary;
    if (!anyFault) {
        summary = "OK";
    } else {
        QStringList faults;
        if (m_faultPavdd) faults << "PAVDD";
        if (m_faultNavdd) faults << "NAVDD";
        if (m_faultVgh)   faults << "VGH";
        if (m_faultVgl)   faults << "VGL";
        summary = "FAULT: " + faults.join(", ");
    }
    if (summary != m_faultSummary) m_faultSummary = summary;
}

void PmicController::refresh()
{
    int fd = openI2c();
    if (fd < 0) {
        if (m_available) {
            m_available = false;
            emit availableChanged();
            emit statusChanged();
        }
        return;
    }

    // Read 10 bytes from PMIC reg 0x14 covers 0x14-0x1D (Prot, Chan, Fault)
    uint8_t block[10];
    bool ok = bridgeRead(fd, PMIC_I2C_ADDR, PMIC_REG_PROT, 10, block);
    closeI2c(fd);

    if (!ok) {
        if (m_available) {
            m_available = false;
            emit availableChanged();
            emit statusChanged();
        }
        return;
    }

    // block[0]=0x14(Prot), block[2]=0x16(Chan), block[9]=0x1D(Fault)
    parseStatus(block[2], block[0], block[9]);

    if (!m_available) {
        m_available = true;
        emit availableChanged();
    }
    emit statusChanged();
}
