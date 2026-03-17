#ifndef CANREADER_H
#define CANREADER_H

#include <QObject>
#include <QThread>
#include <atomic>

struct can_frame;

class CanReader : public QObject
{
    Q_OBJECT
public:
    explicit CanReader(const QString &interface, QObject *parent = nullptr);
    ~CanReader();

    bool open();
    void start();
    void stop();

signals:
    void speedChanged(int kmh);
    void rpmChanged(int rpm);
    void coolantTempChanged(int celsius);
    void fuelLevelChanged(int percent);
    void batteryVoltageChanged(double volts);
    void telltalesChanged(quint16 bits);
    void canTimeout();

private:
    void pollLoop();
    void sendObd2Request(uint8_t pid);
    bool readFrame(struct can_frame *frame, int timeoutMs);
    void decodeObd2Response(const struct can_frame &frame);
    void decodeTelltales(const struct can_frame &frame);

    QString m_interface;
    int m_socket;
    QThread *m_thread;
    std::atomic<bool> m_running;
};

#endif // CANREADER_H
