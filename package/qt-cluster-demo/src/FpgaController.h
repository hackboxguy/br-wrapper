#ifndef FPGACONTROLLER_H
#define FPGACONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QString>

/**
 * FpgaController - minimal FPGA I2C access for local-dimming / pixel-compensation
 *
 * The display FPGA (slave 0x1D) exposes two write-only-with-readback controls:
 * - 0x2C: Local-Dimming Enable     (0x00 = enabled [default], 0x01 = disabled)
 * - 0x2D: Pixel-Compensation Enable (0x00 = enabled [default], 0x01 = disabled)
 *
 * A control is considered "supported" only if the register reads back exactly
 * 0x00 or 0x01; any other value (e.g. 0xFF) means the bitstream does not wire it.
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
    int openI2c();
    void closeI2c(int fd);
    bool readRegister(int fd, uint8_t reg, uint8_t *data, int len);
    bool writeRegister(int fd, uint8_t reg, uint8_t value);
    void readToggleSettings(int fd);

private:
    QString m_i2cBus;
    int m_i2cAddress;

    bool m_connected;
    bool m_localDimmingSupported;
    bool m_localDimmingEnabled;
    bool m_pixelCompSupported;
    bool m_pixelCompEnabled;

    QTimer *m_refreshTimer;
};

#endif // FPGACONTROLLER_H
