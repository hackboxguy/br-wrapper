#ifndef PMICCONTROLLER_H
#define PMICCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QString>

/**
 * PmicController - Reads RTQ6749 PMIC status via MCU (0x66) I2C bridge
 *
 * Uses the IOC MCU's I2C bridge (registers 0x0310-0x032F) to relay reads
 * to the RTQ6749 PMIC at internal I2C1 slave address 0x6B.
 *
 * Exposes high-level status: faults, channel enables, and protection flags.
 */
class PmicController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)
    Q_PROPERTY(QString faultSummary READ faultSummary NOTIFY statusChanged)

    // Channel enables (from register 0x16)
    Q_PROPERTY(bool chPavdd READ chPavdd NOTIFY statusChanged)
    Q_PROPERTY(bool chNavdd READ chNavdd NOTIFY statusChanged)
    Q_PROPERTY(bool chVgh   READ chVgh   NOTIFY statusChanged)
    Q_PROPERTY(bool chVgl   READ chVgl   NOTIFY statusChanged)
    Q_PROPERTY(bool chVcom  READ chVcom  NOTIFY statusChanged)
    Q_PROPERTY(bool chReset READ chReset NOTIFY statusChanged)

    // Protection enables (from register 0x14)
    Q_PROPERTY(bool protOtp READ protOtp NOTIFY statusChanged)
    Q_PROPERTY(bool protUvp READ protUvp NOTIFY statusChanged)
    Q_PROPERTY(bool protScp READ protScp NOTIFY statusChanged)

    // Per-channel fault flags (from register 0x1D, lower 4 bits)
    // Note: only PAVDD/NAVDD/VGH/VGL are monitored — VCOM/RESET have no fault bits
    Q_PROPERTY(bool faultPavdd READ faultPavdd NOTIFY statusChanged)
    Q_PROPERTY(bool faultNavdd READ faultNavdd NOTIFY statusChanged)
    Q_PROPERTY(bool faultVgh   READ faultVgh   NOTIFY statusChanged)
    Q_PROPERTY(bool faultVgl   READ faultVgl   NOTIFY statusChanged)

public:
    explicit PmicController(QObject *parent = nullptr);
    ~PmicController();

    void setI2cBus(const QString &bus);
    void setMcuAddress(int address);
    void start();

    bool available() const { return m_available; }
    bool statusOk() const { return m_statusOk; }
    QString faultSummary() const { return m_faultSummary; }

    bool chPavdd() const { return m_chPavdd; }
    bool chNavdd() const { return m_chNavdd; }
    bool chVgh() const { return m_chVgh; }
    bool chVgl() const { return m_chVgl; }
    bool chVcom() const { return m_chVcom; }
    bool chReset() const { return m_chReset; }

    bool protOtp() const { return m_protOtp; }
    bool protUvp() const { return m_protUvp; }
    bool protScp() const { return m_protScp; }

    bool faultPavdd() const { return m_faultPavdd; }
    bool faultNavdd() const { return m_faultNavdd; }
    bool faultVgh() const { return m_faultVgh; }
    bool faultVgl() const { return m_faultVgl; }

public slots:
    void refresh();

signals:
    void availableChanged();
    void statusChanged();

private:
    int openI2c();
    void closeI2c(int fd);
    bool writeMcuReg16(int fd, uint16_t reg, const uint8_t *data, int len);
    bool readMcuReg16(int fd, uint16_t reg, uint8_t *data, int len);
    bool bridgeRead(int fd, uint8_t slave, uint8_t reg, uint8_t len, uint8_t *out);
    void parseStatus(uint8_t chanReg, uint8_t protReg, uint8_t faultReg);

private:
    QString m_i2cBus;
    int m_mcuAddress;

    bool m_available;
    bool m_statusOk;
    QString m_faultSummary;

    bool m_chPavdd, m_chNavdd, m_chVgh, m_chVgl, m_chVcom, m_chReset;
    bool m_protOtp, m_protUvp, m_protScp;
    bool m_faultPavdd, m_faultNavdd, m_faultVgh, m_faultVgl;

    QTimer *m_refreshTimer;
};

#endif // PMICCONTROLLER_H
