#ifndef TEMPERATURECONTROLLER_H
#define TEMPERATURECONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <QStringList>

/**
 * TemperatureController - DS18B20 1-wire temperature sensor reader
 *
 * Reads temperature from /sys/bus/w1/devices/28-XXXX/w1_slave
 */
class TemperatureController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double sensor1Temp READ sensor1Temp NOTIFY sensor1TempChanged)
    Q_PROPERTY(double sensor2Temp READ sensor2Temp NOTIFY sensor2TempChanged)
    Q_PROPERTY(QString sensor1Id READ sensor1Id NOTIFY sensor1IdChanged)
    Q_PROPERTY(QString sensor2Id READ sensor2Id NOTIFY sensor2IdChanged)
    Q_PROPERTY(bool sensor1Available READ sensor1Available NOTIFY sensor1AvailableChanged)
    Q_PROPERTY(bool sensor2Available READ sensor2Available NOTIFY sensor2AvailableChanged)
    Q_PROPERTY(bool sensor1Healthy READ sensor1Healthy NOTIFY sensor1HealthyChanged)
    Q_PROPERTY(bool sensor2Healthy READ sensor2Healthy NOTIFY sensor2HealthyChanged)

public:
    explicit TemperatureController(QObject *parent = nullptr);
    ~TemperatureController();

    void setBasePath(const QString &path);
    void start();

    double sensor1Temp() const { return m_sensor1Temp; }
    double sensor2Temp() const { return m_sensor2Temp; }
    QString sensor1Id() const { return m_sensor1Id; }
    QString sensor2Id() const { return m_sensor2Id; }
    bool sensor1Available() const { return m_sensor1Available; }
    bool sensor2Available() const { return m_sensor2Available; }
    bool sensor1Healthy() const { return m_sensor1Healthy; }
    bool sensor2Healthy() const { return m_sensor2Healthy; }

public slots:
    void refresh();
    void scanSensors();

signals:
    void sensor1TempChanged();
    void sensor2TempChanged();
    void sensor1IdChanged();
    void sensor2IdChanged();
    void sensor1AvailableChanged();
    void sensor2AvailableChanged();
    void sensor1HealthyChanged();
    void sensor2HealthyChanged();
    void errorOccurred(const QString &message);

private:
    bool readTemperature(const QString &sensorId, double &temp);
    QStringList findSensors();

private:
    QString m_basePath;
    QTimer *m_refreshTimer;
    QTimer *m_scanTimer;

    QString m_sensor1Id;
    QString m_sensor2Id;
    double m_sensor1Temp;
    double m_sensor2Temp;
    bool m_sensor1Available;
    bool m_sensor2Available;
    bool m_sensor1Healthy;
    bool m_sensor2Healthy;
};

#endif // TEMPERATURECONTROLLER_H
