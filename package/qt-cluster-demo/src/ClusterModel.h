#ifndef CLUSTERMODEL_H
#define CLUSTERMODEL_H

#include <QObject>

class ClusterModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(int rpm READ rpm NOTIFY rpmChanged)
    Q_PROPERTY(int coolantTemp READ coolantTemp NOTIFY coolantTempChanged)
    Q_PROPERTY(int fuelLevel READ fuelLevel NOTIFY fuelLevelChanged)
    Q_PROPERTY(double batteryVoltage READ batteryVoltage NOTIFY batteryVoltageChanged)
    Q_PROPERTY(quint16 telltales READ telltales NOTIFY telltalesChanged)
    Q_PROPERTY(bool canConnected READ canConnected NOTIFY canConnectedChanged)

public:
    explicit ClusterModel(QObject *parent = nullptr);

    int speed() const { return m_speed; }
    int rpm() const { return m_rpm; }
    int coolantTemp() const { return m_coolantTemp; }
    int fuelLevel() const { return m_fuelLevel; }
    double batteryVoltage() const { return m_batteryVoltage; }
    quint16 telltales() const { return m_telltales; }
    bool canConnected() const { return m_canConnected; }

public slots:
    void setSpeed(int v);
    void setRpm(int v);
    void setCoolantTemp(int v);
    void setFuelLevel(int v);
    void setBatteryVoltage(double v);
    void setTelltales(quint16 v);
    void setCanConnected(bool v);

signals:
    void speedChanged(int v);
    void rpmChanged(int v);
    void coolantTempChanged(int v);
    void fuelLevelChanged(int v);
    void batteryVoltageChanged(double v);
    void telltalesChanged(quint16 v);
    void canConnectedChanged(bool v);

private:
    int m_speed = 0;
    int m_rpm = 0;
    int m_coolantTemp = 20;
    int m_fuelLevel = 0;
    double m_batteryVoltage = 0.0;
    quint16 m_telltales = 0;
    bool m_canConnected = false;
};

#endif // CLUSTERMODEL_H
