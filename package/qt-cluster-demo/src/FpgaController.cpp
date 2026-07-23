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
const char * const kBus = "/dev/i2c-1";
const char * const kStateFile = "/tmp/fpga-ldpc-state.json";
const uint8_t kLegacyAddr = 0x1D, kNewAddr = 0x1E;
const uint8_t kVersion = 0x00, kLegacyLd = 0x29, kNewLd = 0x2C;
const uint8_t kNewPc = 0x2D, kLegacyPc = 0x47;
bool bcd(uint8_t value) { return ((value >> 4) & 0x0F) <= 9 && (value & 0x0F) <= 9; }
bool plausibleVersion(const uint8_t value[4]) {
    if (value[0] == 0x48) return true;
    if (!bcd(value[0]) || !bcd(value[1])) return false;
    const int month = ((value[0] >> 4) & 0x0F) * 10 + (value[0] & 0x0F);
    const int day = ((value[1] >> 4) & 0x0F) * 10 + (value[1] & 0x0F);
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}
}

FpgaController::FpgaController(QObject *parent)
    : QObject(parent), m_i2cBus(kBus), m_protocolOverride("auto"), m_protocol(Protocol::None),
      m_legacyStateInitialized(false), m_connected(false), m_localDimmingSupported(false),
      m_localDimmingEnabled(false), m_pixelCompSupported(false), m_pixelCompEnabled(false),
      m_refreshTimer(new QTimer(this))
{
    connect(m_refreshTimer, &QTimer::timeout, this, &FpgaController::refresh);
    m_refreshTimer->setInterval(5000);
}

FpgaController::~FpgaController() { m_refreshTimer->stop(); }
void FpgaController::setI2cBus(const QString &bus) { m_i2cBus = bus; clearProtocol(); }
void FpgaController::setProtocolOverride(const QString &protocol) {
    const QString value = protocol.trimmed().toLower();
    m_protocolOverride = (value == "auto" || value == "new" || value == "legacy") ? value : "auto";
    if (m_protocolOverride != value) qWarning() << "FpgaController: invalid protocol override" << protocol;
    clearProtocol();
}
void FpgaController::start() {
    qDebug() << "FpgaController: starting with bus" << m_i2cBus << "protocol override" << m_protocolOverride;
    refresh(); m_refreshTimer->start();
}
int FpgaController::openI2cAt(uint8_t address) {
    const int fd = open(m_i2cBus.toLocal8Bit().constData(), O_RDWR);
    if (fd < 0) return -1;
    if (ioctl(fd, I2C_SLAVE, address) < 0) { close(fd); return -1; }
    return fd;
}
int FpgaController::openI2c() {
    return m_protocol == Protocol::New ? openI2cAt(kNewAddr) :
           m_protocol == Protocol::Legacy ? openI2cAt(kLegacyAddr) : -1;
}
void FpgaController::closeI2c(int fd) { if (fd >= 0) close(fd); }
bool FpgaController::readNew(int fd, uint8_t reg, uint8_t *data, int len) {
    return write(fd, &reg, 1) == 1 && read(fd, data, len) == len;
}
bool FpgaController::writeNew(int fd, uint8_t reg, const uint8_t *data, int len) {
    if (len < 1 || len > 2) return false;
    uint8_t packet[4] = {0x00, reg, 0x00, 0x00};
    for (int i = 0; i < len; ++i) packet[2 + i] = data[i];
    return write(fd, packet, len + 2) == len + 2;
}
bool FpgaController::readLegacy(int fd, uint8_t reg, uint8_t *data, int len) {
    const uint8_t address[4] = {0, 0, 0, reg};
    return write(fd, address, 4) == 4 && read(fd, data, len) == len;
}
bool FpgaController::writeLegacy(int fd, uint8_t reg, const uint8_t *data, int len) {
    if (len < 1 || len > 2) return false;
    uint8_t packet[6] = {0, 0, 0, reg, 0, 0};
    for (int i = 0; i < len; ++i) packet[4 + i] = data[i];
    return write(fd, packet, len + 4) == len + 4;
}
bool FpgaController::probeProtocol(Protocol protocol) {
    const int fd = openI2cAt(protocol == Protocol::New ? kNewAddr : kLegacyAddr);
    if (fd < 0) return false;
    uint8_t version[4];
    const bool ok = protocol == Protocol::New ? readNew(fd, kVersion, version, 4)
                                                : readLegacy(fd, kVersion, version, 4);
    closeI2c(fd);
    return ok && plausibleVersion(version);
}
bool FpgaController::ensureProtocol() {
    if (m_protocol != Protocol::None) return true;
    if (m_protocolOverride != "legacy" && probeProtocol(Protocol::New)) m_protocol = Protocol::New;
    else if (m_protocolOverride != "new" && probeProtocol(Protocol::Legacy)) m_protocol = Protocol::Legacy;
    else return false;
    m_legacyStateInitialized = false;
    qDebug() << "FpgaController: selected" << (m_protocol == Protocol::New ? "new FPGA protocol (0x1E)" : "legacy FPGA protocol (0x1D)");
    return true;
}
bool FpgaController::pingCurrentProtocol(int fd) {
    uint8_t version[4];
    const bool ok = m_protocol == Protocol::New ? readNew(fd, kVersion, version, 4)
                                                  : readLegacy(fd, kVersion, version, 4);
    return ok && plausibleVersion(version);
}
void FpgaController::clearProtocol() {
    m_protocol = Protocol::None; m_legacyStateInitialized = false;
    const bool ldChanged = m_localDimmingSupported, pcChanged = m_pixelCompSupported;
    m_localDimmingSupported = false; m_pixelCompSupported = false;
    if (ldChanged) emit localDimmingChanged();
    if (pcChanged) emit pixelCompChanged();
}
bool FpgaController::loadLegacyState(bool *ld, bool *pc) const {
    QFile file(kStateFile); if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject state = doc.object();
    if (state.value("protocol").toString() != "legacy") return false;
    *ld = state.value("local_dimming").toBool(true);
    *pc = state.value("pixel_compensation").toBool(true);
    return true;
}
void FpgaController::saveLegacyState() const {
    QJsonObject state; state["version"] = 1; state["protocol"] = "legacy";
    state["local_dimming"] = m_localDimmingEnabled; state["pixel_compensation"] = m_pixelCompEnabled;
    QSaveFile file(kStateFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(state).toJson(QJsonDocument::Compact)) < 0 || !file.commit())
        qWarning() << "FpgaController: failed to save legacy LD/PC state";
}
void FpgaController::initializeLegacyState() {
    const bool firstObservation = !m_legacyStateInitialized;
    bool ld = true, pc = true; const bool restored = loadLegacyState(&ld, &pc); m_legacyStateInitialized = true;
    const bool ldChanged = !m_localDimmingSupported || m_localDimmingEnabled != ld;
    const bool pcChanged = !m_pixelCompSupported || m_pixelCompEnabled != pc;
    m_localDimmingSupported = true; m_localDimmingEnabled = ld; m_pixelCompSupported = true; m_pixelCompEnabled = pc;
    if (ldChanged) emit localDimmingChanged(); if (pcChanged) emit pixelCompChanged();
    if (firstObservation || ldChanged || pcChanged)
        qDebug() << "FpgaController: legacy LD/PC state" << (restored ? "synchronized from /tmp" : "assumed on after FPGA power-on");
}
void FpgaController::readToggleSettings(int fd) {
    if (m_protocol == Protocol::Legacy) { initializeLegacyState(); return; }
    uint8_t value; bool supported = false, enabled = m_localDimmingEnabled;
    if (readNew(fd, kNewLd, &value, 1) && (value == 0 || value == 1)) { supported = true; enabled = value == 0; }
    if (supported != m_localDimmingSupported || enabled != m_localDimmingEnabled) { m_localDimmingSupported = supported; m_localDimmingEnabled = enabled; emit localDimmingChanged(); }
    supported = false; enabled = m_pixelCompEnabled;
    if (readNew(fd, kNewPc, &value, 1) && (value == 0 || value == 1)) { supported = true; enabled = value == 0; }
    if (supported != m_pixelCompSupported || enabled != m_pixelCompEnabled) { m_pixelCompSupported = supported; m_pixelCompEnabled = enabled; emit pixelCompChanged(); }
}
void FpgaController::updateConnected(bool connected) { if (connected != m_connected) { m_connected = connected; emit connectedChanged(); } }
void FpgaController::refresh() {
    if (!ensureProtocol()) { updateConnected(false); return; }
    const int fd = openI2c();
    if (fd < 0 || !pingCurrentProtocol(fd)) { closeI2c(fd); clearProtocol(); updateConnected(false); return; }
    readToggleSettings(fd); closeI2c(fd); updateConnected(true);
}
void FpgaController::setLocalDimming(bool enabled) {
    if (!ensureProtocol()) return; if (m_protocol == Protocol::Legacy) initializeLegacyState();
    const int fd = openI2c(); if (fd < 0) return;
    const uint8_t value = enabled ? 0 : 1;
    const bool ok = m_protocol == Protocol::New ? writeNew(fd, kNewLd, &value, 1) : writeLegacy(fd, kLegacyLd, &value, 1);
    if (ok) {
        if (m_protocol == Protocol::New) {
            uint8_t readBack;
            if (readNew(fd, kNewLd, &readBack, 1) && (readBack == 0 || readBack == 1)) {
                m_localDimmingSupported = true;
                m_localDimmingEnabled = readBack == 0;
            } else {
                m_localDimmingEnabled = enabled;
            }
            emit localDimmingChanged();
        } else {
            m_localDimmingEnabled = enabled;
            emit localDimmingChanged();
        }
        saveLegacyState();
        updateConnected(true);
    }
    closeI2c(fd);
}
void FpgaController::setPixelCompensation(bool enabled) {
    if (!ensureProtocol()) return; if (m_protocol == Protocol::Legacy) initializeLegacyState();
    const int fd = openI2c(); if (fd < 0) return;
    const uint8_t newValue = enabled ? 0 : 1, legacyValue[2] = {0, static_cast<uint8_t>(enabled ? 0x70 : 0)};
    const bool ok = m_protocol == Protocol::New ? writeNew(fd, kNewPc, &newValue, 1) : writeLegacy(fd, kLegacyPc, legacyValue, 2);
    if (ok) {
        if (m_protocol == Protocol::New) {
            uint8_t readBack;
            if (readNew(fd, kNewPc, &readBack, 1) && (readBack == 0 || readBack == 1)) {
                m_pixelCompSupported = true;
                m_pixelCompEnabled = readBack == 0;
            } else {
                m_pixelCompEnabled = enabled;
            }
            emit pixelCompChanged();
        } else {
            m_pixelCompEnabled = enabled;
            emit pixelCompChanged();
        }
        saveLegacyState();
        updateConnected(true);
    }
    closeI2c(fd);
}
