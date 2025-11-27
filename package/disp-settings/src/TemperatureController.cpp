#include "TemperatureController.h"
#include "config.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

TemperatureController::TemperatureController(QObject *parent)
    : QObject(parent)
    , m_basePath(DEFAULT_W1_PATH)
    , m_refreshTimer(new QTimer(this))
    , m_scanTimer(new QTimer(this))
    , m_sensor1Temp(0.0)
    , m_sensor2Temp(0.0)
    , m_sensor1Available(false)
    , m_sensor2Available(false)
    , m_sensor1Healthy(false)
    , m_sensor2Healthy(false)
{
    // Refresh temperatures every 400ms
    connect(m_refreshTimer, &QTimer::timeout, this, &TemperatureController::refresh);
    m_refreshTimer->setInterval(400);

    // Scan for new sensors every 30 seconds
    connect(m_scanTimer, &QTimer::timeout, this, &TemperatureController::scanSensors);
    m_scanTimer->setInterval(30000);
}

TemperatureController::~TemperatureController()
{
    m_refreshTimer->stop();
    m_scanTimer->stop();
}

void TemperatureController::setBasePath(const QString &path)
{
    m_basePath = path;
}

void TemperatureController::start()
{
    qDebug() << "TemperatureController: Starting with base path" << m_basePath;
    scanSensors();
    m_refreshTimer->start();
    m_scanTimer->start();
}

QStringList TemperatureController::findSensors()
{
    QStringList sensors;
    QDir dir(m_basePath);

    if (!dir.exists()) {
        qDebug() << "TemperatureController: 1-wire directory does not exist:" << m_basePath;
        return sensors;
    }

    // Look for DS18B20 devices (IDs start with "28-")
    QStringList entries = dir.entryList(QStringList() << "28-*", QDir::Dirs);

    for (const QString &entry : entries) {
        QString slavePath = m_basePath + "/" + entry + "/w1_slave";
        if (QFile::exists(slavePath)) {
            sensors.append(entry);
            qDebug() << "TemperatureController: Found sensor" << entry;
        }
    }

    return sensors;
}

void TemperatureController::scanSensors()
{
    QStringList sensors = findSensors();

    // Assign sensors (up to 2)
    if (sensors.size() >= 1) {
        if (m_sensor1Id != sensors[0]) {
            m_sensor1Id = sensors[0];
            emit sensor1IdChanged();
        }
        if (!m_sensor1Available) {
            m_sensor1Available = true;
            emit sensor1AvailableChanged();
        }
    } else {
        if (m_sensor1Available) {
            m_sensor1Available = false;
            m_sensor1Id.clear();
            emit sensor1AvailableChanged();
            emit sensor1IdChanged();
        }
    }

    if (sensors.size() >= 2) {
        if (m_sensor2Id != sensors[1]) {
            m_sensor2Id = sensors[1];
            emit sensor2IdChanged();
        }
        if (!m_sensor2Available) {
            m_sensor2Available = true;
            emit sensor2AvailableChanged();
        }
    } else {
        if (m_sensor2Available) {
            m_sensor2Available = false;
            m_sensor2Id.clear();
            emit sensor2AvailableChanged();
            emit sensor2IdChanged();
        }
    }

    // Do an immediate temperature read after scan
    refresh();
}

bool TemperatureController::readTemperature(const QString &sensorId, double &temp)
{
    temp = 0.0;

    if (sensorId.isEmpty()) {
        return false;
    }

    QString slavePath = m_basePath + "/" + sensorId + "/w1_slave";
    QFile file(slavePath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "TemperatureController: Failed to open" << slavePath;
        return false;
    }

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    // Check for valid CRC (first line ends with "YES")
    if (!content.contains("YES")) {
        qWarning() << "TemperatureController: CRC check failed for" << sensorId;
        return false;
    }

    // Extract temperature from second line (format: "t=xxxxx" where xxxxx is millidegrees)
    QRegularExpression regex("t=(-?\\d+)");
    QRegularExpressionMatch match = regex.match(content);

    if (match.hasMatch()) {
        int millidegrees = match.captured(1).toInt();
        temp = millidegrees / 1000.0;

        // 85.0°C is the power-on default - often indicates communication issue
        if (qAbs(temp - 85.0) < 0.1) {
            qWarning() << "TemperatureController: Sensor" << sensorId << "returned power-on default (85°C)";
            return false;
        }

        return true;
    }

    qWarning() << "TemperatureController: Failed to parse temperature for" << sensorId;
    return false;
}

void TemperatureController::refresh()
{
    // Sensor 1
    if (m_sensor1Available && !m_sensor1Id.isEmpty()) {
        double temp;
        bool healthy = readTemperature(m_sensor1Id, temp);

        if (healthy != m_sensor1Healthy) {
            m_sensor1Healthy = healthy;
            emit sensor1HealthyChanged();
        }

        if (healthy && qAbs(temp - m_sensor1Temp) > 0.05) {
            m_sensor1Temp = temp;
            emit sensor1TempChanged();
        }
    } else {
        if (m_sensor1Healthy) {
            m_sensor1Healthy = false;
            emit sensor1HealthyChanged();
        }
    }

    // Sensor 2
    if (m_sensor2Available && !m_sensor2Id.isEmpty()) {
        double temp;
        bool healthy = readTemperature(m_sensor2Id, temp);

        if (healthy != m_sensor2Healthy) {
            m_sensor2Healthy = healthy;
            emit sensor2HealthyChanged();
        }

        if (healthy && qAbs(temp - m_sensor2Temp) > 0.05) {
            m_sensor2Temp = temp;
            emit sensor2TempChanged();
        }
    } else {
        if (m_sensor2Healthy) {
            m_sensor2Healthy = false;
            emit sensor2HealthyChanged();
        }
    }
}
