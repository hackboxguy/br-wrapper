#ifndef DEMOSIMULATOR_H
#define DEMOSIMULATOR_H

#include <QObject>
#include <QTimer>

class DemoSimulator : public QObject
{
    Q_OBJECT
public:
    explicit DemoSimulator(QObject *parent = nullptr);

    void start();
    void stop();

signals:
    void speedChanged(int kmh);
    void rpmChanged(int rpm);
    void coolantTempChanged(int celsius);
    void fuelLevelChanged(int percent);
    void batteryVoltageChanged(double volts);
    void telltalesChanged(quint16 bits);

private:
    struct Phase {
        int durationMs;
        int speedStart, speedEnd;
        int rpmStart, rpmEnd;
        int tempStart, tempEnd;
        quint16 telltaleSet;    // bits to turn ON at phase start
        quint16 telltaleClr;    // bits to turn OFF at phase start
    };

    void tick();
    int lerp(int a, int b, double t) const;

    QTimer m_timer;
    int m_phase;
    int m_phaseElapsed;
    int m_fuel;
    quint16 m_telltales;

    static const Phase s_phases[];
    static const int s_phaseCount;
};

#endif // DEMOSIMULATOR_H
