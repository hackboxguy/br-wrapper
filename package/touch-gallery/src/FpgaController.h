#ifndef FPGACONTROLLER_H
#define FPGACONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <cstdint>

/**
 * Runtime-selecting local-dimming / pixel-compensation controller.
 *
 * auto mode probes the read-only VERSION register, preferring the new FPGA
 * slave (0x1E) and its page/register transport. If that is absent it probes
 * the legacy slave (0x1D) and its four-byte register prefix. Legacy LD/PC
 * controls are write-only, so their last requested values are retained in
 * /tmp/fpga-ldpc-state.json while the system remains booted.
 */
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

    bool readRegisterNew(int fd, uint8_t reg, uint8_t *data, int len);
    bool writeRegisterNew(int fd, uint8_t reg, const uint8_t *data, int len);
    bool readRegisterLegacy(int fd, uint8_t reg, uint8_t *data, int len);
    bool writeRegisterLegacy(int fd, uint8_t reg, const uint8_t *data, int len);
    bool writeLocalDimming(int fd, bool enabled);
    bool writePixelCompensation(int fd, bool enabled);

    bool loadLegacyState(bool *localDimming, bool *pixelCompensation) const;
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
