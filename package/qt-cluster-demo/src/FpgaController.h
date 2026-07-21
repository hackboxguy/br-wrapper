#ifndef FPGACONTROLLER_H
#define FPGACONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <cstdint>

// Auto-detects new (0x1E) and legacy (0x1D) FPGA LD/PC transports. Legacy
// registers are write-only; the last requested state is retained in
// /tmp/fpga-ldpc-state.json for app restarts within the current system boot.
class FpgaController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool localDimmingSupported READ localDimmingSupported NOTIFY localDimmingChanged)
    Q_PROPERTY(bool localDimmingEnabled READ localDimmingEnabled NOTIFY localDimmingChanged)
    Q_PROPERTY(bool pixelCompSupported READ pixelCompSupported NOTIFY pixelCompChanged)
    Q_PROPERTY(bool pixelCompEnabled READ pixelCompEnabled NOTIFY pixelCompChanged)
public:
    explicit FpgaController(QObject *parent = nullptr);
    ~FpgaController();
    void setI2cBus(const QString &bus);
    void setProtocolOverride(const QString &protocol);
    void start();
    bool connected() const { return m_connected; }
    bool localDimmingSupported() const { return m_localDimmingSupported; }
    bool localDimmingEnabled() const { return m_localDimmingEnabled; }
    bool pixelCompSupported() const { return m_pixelCompSupported; }
    bool pixelCompEnabled() const { return m_pixelCompEnabled; }
public slots:
    void setLocalDimming(bool enabled);
    void setPixelCompensation(bool enabled);
    void refresh();
signals:
    void connectedChanged();
    void localDimmingChanged();
    void pixelCompChanged();
private:
    enum class Protocol { None, New, Legacy };
    int openI2c();
    int openI2cAt(uint8_t address);
    void closeI2c(int fd);
    bool ensureProtocol();
    bool probeProtocol(Protocol protocol);
    bool pingCurrentProtocol(int fd);
    void clearProtocol();
    void initializeLegacyState();
    void readToggleSettings(int fd);
    bool readNew(int fd, uint8_t reg, uint8_t *data, int len);
    bool writeNew(int fd, uint8_t reg, const uint8_t *data, int len);
    bool readLegacy(int fd, uint8_t reg, uint8_t *data, int len);
    bool writeLegacy(int fd, uint8_t reg, const uint8_t *data, int len);
    bool loadLegacyState(bool *ld, bool *pc) const;
    void saveLegacyState() const;
    void updateConnected(bool connected);
    QString m_i2cBus;
    QString m_protocolOverride;
    Protocol m_protocol;
    bool m_legacyStateInitialized;
    bool m_connected;
    bool m_localDimmingSupported;
    bool m_localDimmingEnabled;
    bool m_pixelCompSupported;
    bool m_pixelCompEnabled;
    QTimer *m_refreshTimer;
};

#endif // FPGACONTROLLER_H
