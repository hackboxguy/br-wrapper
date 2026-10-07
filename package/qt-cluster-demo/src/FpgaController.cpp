#include "FpgaController.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
const char * const kBus = "/dev/i2c-1";
const char * const kDataStateDir = "/data/cluster";
const char * const kStateName = "fpga-ldpc-state.json";
const char * const kTmpStateFile = "/tmp/fpga-ldpc-state.json";
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
// The 0x1E slave keeps its page/register pointer between transactions and other
// processes (update-fpga.sh's flash scan, the launcher's update badge) move it, so
// a pointer write and a separate read can return another page's bytes. Set
// [page 0, reg] and read in ONE repeated-start transfer.
bool readNewAtomic(int fd, uint8_t reg, uint8_t *data, int len) {
    uint8_t pointer[2] = {0x00, reg};
    struct i2c_msg msgs[2] = {{kNewAddr, 0, 2, pointer},
                              {kNewAddr, I2C_M_RD, static_cast<__u16>(len), data}};
    struct i2c_rdwr_ioctl_data transfer = {msgs, 2};
    return ioctl(fd, I2C_RDWR, &transfer) == 2;
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
    return readNewAtomic(fd, reg, data, len);
}
bool FpgaController::writeNew(int fd, uint8_t reg, const uint8_t *data, int len) {
    if (len < 1 || len > 2) return false;
    uint8_t packet[4] = {0x00, reg, 0x00, 0x00};
    for (int i = 0; i < len; ++i) packet[2 + i] = data[i];
    return write(fd, packet, len + 2) == len + 2;
}
// The legacy slave too, in ONE repeated-start transfer: als-dimmer and the
// serializer driver's bus check share this bus, and a pointer write followed
// by a separate read can return what another master's pointer selected
bool FpgaController::readLegacy(int fd, uint8_t reg, uint8_t *data, int len) {
    uint8_t address[4] = {0, 0, 0, reg};
    struct i2c_msg msgs[2] = {{kLegacyAddr, 0, 4, address},
                              {kLegacyAddr, I2C_M_RD, static_cast<__u16>(len), data}};
    struct i2c_rdwr_ioctl_data transfer = {msgs, 2};
    return ioctl(fd, I2C_RDWR, &transfer) == 2;
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
    bool newFound = false;
    for (int attempt = 0; m_protocolOverride != "legacy" && attempt < 3 && !newFound; ++attempt) {
        if (attempt) usleep(20000);
        newFound = probeProtocol(Protocol::New);
    }
    if (newFound) m_protocol = Protocol::New;
    else if (m_protocolOverride == "new") return false;
    else {
        // An FPGA with the 0x1E slave never gets legacy writes: on those bitstreams
        // legacy LD register 0x29 is the OTA work-mode bit, and setting it locks every
        // register write until a power cycle. Odd data here is a transient: retry later.
        if (m_protocolOverride != "legacy") {
            const int fd = openI2cAt(kNewAddr);
            uint8_t byte;
            const bool answers = fd >= 0 && readNewAtomic(fd, kVersion, &byte, 1);
            closeI2c(fd);
            if (answers) { qWarning() << "FpgaController: 0x1E answers but its version is not plausible; retrying later"; return false; }
        }
        if (!probeProtocol(Protocol::Legacy)) return false;
        m_protocol = Protocol::Legacy;
    }
    m_legacyStateInitialized = false;
    qDebug() << "FpgaController: selected" << (m_protocol == Protocol::New ? "new FPGA protocol (0x1E)" : "legacy FPGA protocol (0x1D)");
    applySavedState();
    return true;
}
// The user's last LD/PC choice, back into an FPGA that has just been found
// (app start, or the FPGA back after it stopped answering - e.g. a display
// power cycle, which resets it to its default). Writes only through the
// selected protocol, so a 0x1E FPGA never gets legacy writes.
void FpgaController::applySavedState() {
    bool ld = true, pc = true;
    if (!loadLegacyState(&ld, &pc)) return;
    const int fd = openI2c(); if (fd < 0) return;
    bool ok;
    if (m_protocol == Protocol::New) {
        const uint8_t ldValue = ld ? 0 : 1, pcValue = pc ? 0 : 1;
        ok = writeNew(fd, kNewLd, &ldValue, 1) & writeNew(fd, kNewPc, &pcValue, 1);
    } else {
        const uint8_t ldValue = ld ? 0 : 1, pcValue[2] = {0, static_cast<uint8_t>(pc ? 0x70 : 0)};
        ok = writeLegacy(fd, kLegacyLd, &ldValue, 1) & writeLegacy(fd, kLegacyPc, pcValue, 2);
    }
    closeI2c(fd);
    qDebug() << "FpgaController: saved state" << (ok ? "applied" : "NOT applied") << "- local dimming" << ld << "pixel compensation" << pc;
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
QString FpgaController::stateFilePath() {
    const QFileInfo dir(QString::fromLatin1(kDataStateDir));
    return dir.isDir() && dir.isWritable() ? dir.filePath() + QLatin1Char('/') + QLatin1String(kStateName)
                                           : QString::fromLatin1(kTmpStateFile);
}
// The saved choice, whichever FPGA protocol it was set on (a display swapped
// for one with the other FPGA keeps the user's choice)
bool FpgaController::loadLegacyState(bool *ld, bool *pc) const {
    QFile file(stateFilePath()); if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject state = doc.object();
    *ld = state.value("local_dimming").toBool(true);
    *pc = state.value("pixel_compensation").toBool(true);
    return true;
}
void FpgaController::saveLegacyState() const {
    QJsonObject state; state["version"] = 1;
    state["protocol"] = m_protocol == Protocol::New ? "new" : "legacy";
    state["local_dimming"] = m_localDimmingEnabled; state["pixel_compensation"] = m_pixelCompEnabled;
    QSaveFile file(stateFilePath());
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
        qDebug() << "FpgaController: legacy LD/PC state" << (restored ? "synchronized from " + stateFilePath() : QString("assumed on after FPGA power-on"));
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
