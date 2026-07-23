#include "FpgaController.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

const char * const DEFAULT_I2C_BUS = "/dev/i2c-1";
const char * const LEGACY_STATE_FILE = "/tmp/fpga-ldpc-state.json";
const uint8_t FPGA_LEGACY_I2C_ADDR = 0x1D;
const uint8_t FPGA_NEW_I2C_ADDR = 0x1E;
const uint8_t REG_VERSION = 0x00;
const uint8_t REG_LEGACY_LOCAL_DIMMING = 0x29;
const uint8_t REG_NEW_LOCAL_DIMMING = 0x2C;
const uint8_t REG_NEW_PIXEL_COMP = 0x2D;
const uint8_t REG_LEGACY_PIXEL_COMP = 0x47;

bool isValidBcd(uint8_t value)
{
    return ((value >> 4) & 0x0F) <= 9 && (value & 0x0F) <= 9;
}

bool isPlausibleVersion(const uint8_t version[4])
{
    // Legacy hex-format bitstreams identify themselves with ASCII 'H'.
    if (version[0] == 0x48)
        return true;

    if (!isValidBcd(version[0]) || !isValidBcd(version[1]))
        return false;

    const int month = ((version[0] >> 4) & 0x0F) * 10 + (version[0] & 0x0F);
    const int day = ((version[1] >> 4) & 0x0F) * 10 + (version[1] & 0x0F);
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

} // namespace

FpgaController::FpgaController(QObject *parent)
    : QObject(parent)
    , m_i2cBus(DEFAULT_I2C_BUS)
    , m_protocolOverride("auto")
    , m_protocol(Protocol::None)
    , m_legacyStateInitialized(false)
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
    clearProtocol();
}

void FpgaController::setProtocolOverride(const QString &protocol)
{
    const QString normalized = protocol.trimmed().toLower();
    if (normalized == "auto" || normalized == "new" || normalized == "legacy")
        m_protocolOverride = normalized;
    else {
        qWarning() << "FpgaController: invalid protocol override" << protocol
                   << "(using auto)";
        m_protocolOverride = "auto";
    }
    clearProtocol();
}

void FpgaController::start()
{
    qDebug() << "FpgaController: starting with bus" << m_i2cBus
             << "protocol override" << m_protocolOverride;
    refresh();
    m_refreshTimer->start();
}

int FpgaController::openI2cAt(uint8_t address)
{
    const int fd = open(m_i2cBus.toLocal8Bit().constData(), O_RDWR);
    if (fd < 0)
        return -1;
    if (ioctl(fd, I2C_SLAVE, address) < 0) {
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
        return openI2cAt(FPGA_LEGACY_I2C_ADDR);
    return -1;
}

void FpgaController::closeI2c(int fd)
{
    if (fd >= 0)
        close(fd);
}

bool FpgaController::readRegisterNew(int fd, uint8_t reg, uint8_t *data, int len)
{
    return write(fd, &reg, 1) == 1 && read(fd, data, len) == len;
}

bool FpgaController::writeRegisterNew(int fd, uint8_t reg, const uint8_t *data, int len)
{
    uint8_t packet[2 + 2];
    if (len < 1 || len > 2)
        return false;
    packet[0] = 0x00; // page 0
    packet[1] = reg;
    for (int i = 0; i < len; ++i)
        packet[2 + i] = data[i];
    return write(fd, packet, len + 2) == len + 2;
}

bool FpgaController::readRegisterLegacy(int fd, uint8_t reg, uint8_t *data, int len)
{
    const uint8_t address[4] = {0x00, 0x00, 0x00, reg};
    return write(fd, address, sizeof(address)) == static_cast<ssize_t>(sizeof(address)) &&
           read(fd, data, len) == len;
}

bool FpgaController::writeRegisterLegacy(int fd, uint8_t reg, const uint8_t *data, int len)
{
    uint8_t packet[4 + 2];
    if (len < 1 || len > 2)
        return false;
    packet[0] = 0x00;
    packet[1] = 0x00;
    packet[2] = 0x00;
    packet[3] = reg;
    for (int i = 0; i < len; ++i)
        packet[4 + i] = data[i];
    return write(fd, packet, len + 4) == len + 4;
}

bool FpgaController::probeProtocol(Protocol protocol)
{
    const int fd = openI2cAt(protocol == Protocol::New ? FPGA_NEW_I2C_ADDR : FPGA_LEGACY_I2C_ADDR);
    if (fd < 0)
        return false;

    uint8_t version[4];
    const bool readOk = protocol == Protocol::New
        ? readRegisterNew(fd, REG_VERSION, version, sizeof(version))
        : readRegisterLegacy(fd, REG_VERSION, version, sizeof(version));
    closeI2c(fd);
    return readOk && isPlausibleVersion(version);
}

bool FpgaController::ensureProtocol()
{
    if (m_protocol != Protocol::None)
        return true;

    const bool allowNew = m_protocolOverride != "legacy";
    const bool allowLegacy = m_protocolOverride != "new";
    if (allowNew && probeProtocol(Protocol::New))
        m_protocol = Protocol::New;
    else if (allowLegacy && probeProtocol(Protocol::Legacy))
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
    const bool readOk = m_protocol == Protocol::New
        ? readRegisterNew(fd, REG_VERSION, version, sizeof(version))
        : readRegisterLegacy(fd, REG_VERSION, version, sizeof(version));
    return readOk && isPlausibleVersion(version);
}

void FpgaController::clearProtocol()
{
    m_protocol = Protocol::None;
    m_legacyStateInitialized = false;
    const bool ldChanged = m_localDimmingSupported;
    const bool pcChanged = m_pixelCompSupported;
    m_localDimmingSupported = false;
    m_pixelCompSupported = false;
    if (ldChanged)
        emit localDimmingChanged();
    if (pcChanged)
        emit pixelCompChanged();
}

bool FpgaController::loadLegacyState(bool *localDimming, bool *pixelCompensation) const
{
    QFile file(LEGACY_STATE_FILE);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject state = document.object();
    if (state.value("protocol").toString() != "legacy" ||
        !state.contains("local_dimming") || !state.contains("pixel_compensation"))
        return false;

    *localDimming = state.value("local_dimming").toBool(true);
    *pixelCompensation = state.value("pixel_compensation").toBool(true);
    return true;
}

void FpgaController::saveLegacyState() const
{
    QJsonObject state;
    state.insert("version", 1);
    state.insert("protocol", "legacy");
    state.insert("local_dimming", m_localDimmingEnabled);
    state.insert("pixel_compensation", m_pixelCompEnabled);

    QSaveFile file(LEGACY_STATE_FILE);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(state).toJson(QJsonDocument::Compact)) < 0 ||
        !file.commit()) {
        qWarning() << "FpgaController: failed to save legacy LD/PC state";
    }
}

void FpgaController::initializeLegacyState()
{
    const bool firstObservation = !m_legacyStateInitialized;
    bool localDimming = true;
    bool pixelCompensation = true;
    const bool restored = loadLegacyState(&localDimming, &pixelCompensation);
    m_legacyStateInitialized = true;

    const bool ldChanged = !m_localDimmingSupported || m_localDimmingEnabled != localDimming;
    const bool pcChanged = !m_pixelCompSupported || m_pixelCompEnabled != pixelCompensation;
    m_localDimmingSupported = true;
    m_localDimmingEnabled = localDimming;
    m_pixelCompSupported = true;
    m_pixelCompEnabled = pixelCompensation;
    if (ldChanged)
        emit localDimmingChanged();
    if (pcChanged)
        emit pixelCompChanged();

    if (firstObservation || ldChanged || pcChanged) {
        qDebug() << "FpgaController: legacy LD/PC state"
                 << (restored ? "synchronized from /tmp" : "assumed on after FPGA power-on");
    }
}

void FpgaController::readToggleSettings(int fd)
{
    if (m_protocol == Protocol::Legacy) {
        initializeLegacyState();
        return;
    }

    uint8_t value;
    bool ldSupported = false;
    bool ldEnabled = m_localDimmingEnabled;
    if (readRegisterNew(fd, REG_NEW_LOCAL_DIMMING, &value, 1) && (value == 0x00 || value == 0x01)) {
        ldSupported = true;
        ldEnabled = value == 0x00;
    }
    if (ldSupported != m_localDimmingSupported || ldEnabled != m_localDimmingEnabled) {
        m_localDimmingSupported = ldSupported;
        m_localDimmingEnabled = ldEnabled;
        emit localDimmingChanged();
    }

    bool pcSupported = false;
    bool pcEnabled = m_pixelCompEnabled;
    if (readRegisterNew(fd, REG_NEW_PIXEL_COMP, &value, 1) && (value == 0x00 || value == 0x01)) {
        pcSupported = true;
        pcEnabled = value == 0x00;
    }
    if (pcSupported != m_pixelCompSupported || pcEnabled != m_pixelCompEnabled) {
        m_pixelCompSupported = pcSupported;
        m_pixelCompEnabled = pcEnabled;
        emit pixelCompChanged();
    }
}

bool FpgaController::writeLocalDimming(int fd, bool enabled)
{
    const uint8_t value = enabled ? 0x00 : 0x01;
    return m_protocol == Protocol::New
        ? writeRegisterNew(fd, REG_NEW_LOCAL_DIMMING, &value, 1)
        : writeRegisterLegacy(fd, REG_LEGACY_LOCAL_DIMMING, &value, 1);
}

bool FpgaController::writePixelCompensation(int fd, bool enabled)
{
    if (m_protocol == Protocol::New) {
        const uint8_t value = enabled ? 0x00 : 0x01;
        return writeRegisterNew(fd, REG_NEW_PIXEL_COMP, &value, 1);
    }

    const uint8_t value[2] = {0x00, static_cast<uint8_t>(enabled ? 0x70 : 0x00)};
    return writeRegisterLegacy(fd, REG_LEGACY_PIXEL_COMP, value, sizeof(value));
}

void FpgaController::updateConnected(bool connected)
{
    if (connected != m_connected) {
        m_connected = connected;
        emit connectedChanged();
    }
}

void FpgaController::refresh()
{
    if (!ensureProtocol()) {
        updateConnected(false);
        return;
    }

    const int fd = openI2c();
    if (fd < 0 || !pingCurrentProtocol(fd)) {
        closeI2c(fd);
        clearProtocol();
        updateConnected(false);
        return;
    }

    readToggleSettings(fd);
    closeI2c(fd);
    updateConnected(true);
}

void FpgaController::setLocalDimming(bool enabled)
{
    if (!ensureProtocol())
        return;
    if (m_protocol == Protocol::Legacy)
        initializeLegacyState();
    const int fd = openI2c();
    if (fd < 0)
        return;

    if (writeLocalDimming(fd, enabled)) {
        if (m_protocol == Protocol::New) {
            uint8_t value;
            if (readRegisterNew(fd, REG_NEW_LOCAL_DIMMING, &value, 1) &&
                (value == 0x00 || value == 0x01)) {
                m_localDimmingSupported = true;
                m_localDimmingEnabled = value == 0x00;
            } else {
                // Preserve the existing optimistic update behaviour when a
                // new-FPGA bitstream accepts the write but has no readback.
                m_localDimmingEnabled = enabled;
            }
            emit localDimmingChanged();
        } else {
            m_localDimmingSupported = true;
            m_localDimmingEnabled = enabled;
            emit localDimmingChanged();
        }
        // New FPGA hardware remains the readback authority.  This runtime
        // record also lets write-only legacy FPGA users synchronize with the
        // Stream Deck and other local UI clients during the same boot.
        saveLegacyState();
        updateConnected(true);
    }
    closeI2c(fd);
}

void FpgaController::setPixelCompensation(bool enabled)
{
    if (!ensureProtocol())
        return;
    if (m_protocol == Protocol::Legacy)
        initializeLegacyState();
    const int fd = openI2c();
    if (fd < 0)
        return;

    if (writePixelCompensation(fd, enabled)) {
        if (m_protocol == Protocol::New) {
            uint8_t value;
            if (readRegisterNew(fd, REG_NEW_PIXEL_COMP, &value, 1) &&
                (value == 0x00 || value == 0x01)) {
                m_pixelCompSupported = true;
                m_pixelCompEnabled = value == 0x00;
            } else {
                // Preserve the existing optimistic update behaviour when a
                // new-FPGA bitstream accepts the write but has no readback.
                m_pixelCompEnabled = enabled;
            }
            emit pixelCompChanged();
        } else {
            m_pixelCompSupported = true;
            m_pixelCompEnabled = enabled;
            emit pixelCompChanged();
        }
        saveLegacyState();
        updateConnected(true);
    }
    closeI2c(fd);
}
