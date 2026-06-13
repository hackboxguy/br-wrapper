#include "TemperatureController.h"
#include "config.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrent>

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
    , m_sensor1Watcher(new QFutureWatcher<SensorReading>(this))
    , m_sensor2Watcher(new QFutureWatcher<SensorReading>(this))
    , m_readInProgress(false)
{
    // Refresh temperatures every 2 seconds (async, non-blocking)
    connect(m_refreshTimer, &QTimer::timeout, this, &TemperatureController::refresh);
    m_refreshTimer->setInterval(2000);

    // Scan for new sensors every 30 seconds
    connect(m_scanTimer, &QTimer::timeout, this, &TemperatureController::scanSensors);
    m_scanTimer->setInterval(30000);

    // Connect future watchers to result handlers
    connect(m_sensor1Watcher, &QFutureWatcher<SensorReading>::finished,
            this, &TemperatureController::onSensor1ReadFinished);
    connect(m_sensor2Watcher, &QFutureWatcher<SensorReading>::finished,
            this, &TemperatureController::onSensor2ReadFinished);
}

TemperatureController::~TemperatureController()
{
    m_refreshTimer->stop();
    m_scanTimer->stop();

    // Wait for any pending reads to complete
    if (m_sensor1Watcher->isRunning()) {
        m_sensor1Watcher->waitForFinished();
    }
    if (m_sensor2Watcher->isRunning()) {
        m_sensor2Watcher->waitForFinished();
    }
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

// Static function that runs in thread pool - does blocking I/O
SensorReading TemperatureController::readTemperatureAsync(const QString &basePath, const QString &sensorId)
{
    SensorReading result;
    result.sensorId = sensorId;
    result.temperature = 0.0;
    result.healthy = false;
    result.valid = false;

    if (sensorId.isEmpty()) {
        return result;
    }

    QString slavePath = basePath + "/" + sensorId + "/w1_slave";
    QFile file(slavePath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "TemperatureController: Failed to open" << slavePath;
        return result;
    }

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    result.valid = true;  // We successfully read the file

    // Check for valid CRC (first line ends with "YES")
    if (!content.contains("YES")) {
        qWarning() << "TemperatureController: CRC check failed for" << sensorId;
        return result;
    }

    // Extract temperature from second line (format: "t=xxxxx" where xxxxx is millidegrees)
    QRegularExpression regex("t=(-?\\d+)");
    QRegularExpressionMatch match = regex.match(content);

    if (match.hasMatch()) {
        int millidegrees = match.captured(1).toInt();
        result.temperature = millidegrees / 1000.0;

        // 85.0°C is the power-on default - often indicates communication issue
        if (qAbs(result.temperature - 85.0) < 0.1) {
            qWarning() << "TemperatureController: Sensor" << sensorId << "returned power-on default (85C)";
            return result;
        }

        result.healthy = true;
        return result;
    }

    qWarning() << "TemperatureController: Failed to parse temperature for" << sensorId;
    return result;
}

void TemperatureController::refresh()
{
    // Don't start new reads if previous ones are still running
    if (m_readInProgress) {
        return;
    }

    m_readInProgress = true;

    // Start async reads for available sensors
    if (m_sensor1Available && !m_sensor1Id.isEmpty()) {
        QFuture<SensorReading> future = QtConcurrent::run(
            readTemperatureAsync, m_basePath, m_sensor1Id);
        m_sensor1Watcher->setFuture(future);
    } else {
        // No sensor 1, check if we need to update health
        if (m_sensor1Healthy) {
            m_sensor1Healthy = false;
            emit sensor1HealthyChanged();
        }
    }

    if (m_sensor2Available && !m_sensor2Id.isEmpty()) {
        QFuture<SensorReading> future = QtConcurrent::run(
            readTemperatureAsync, m_basePath, m_sensor2Id);
        m_sensor2Watcher->setFuture(future);
    } else {
        // No sensor 2, check if we need to update health
        if (m_sensor2Healthy) {
            m_sensor2Healthy = false;
            emit sensor2HealthyChanged();
        }
    }

    // If no sensors available, clear the in-progress flag
    if (!m_sensor1Available && !m_sensor2Available) {
        m_readInProgress = false;
    }
}

void TemperatureController::onSensor1ReadFinished()
{
    SensorReading result = m_sensor1Watcher->result();

    if (result.healthy != m_sensor1Healthy) {
        m_sensor1Healthy = result.healthy;
        emit sensor1HealthyChanged();
    }

    if (result.healthy && qAbs(result.temperature - m_sensor1Temp) > 0.05) {
        m_sensor1Temp = result.temperature;
        emit sensor1TempChanged();
    }

    // Check if all reads are complete
    if (!m_sensor2Watcher->isRunning()) {
        m_readInProgress = false;
    }
}

void TemperatureController::onSensor2ReadFinished()
{
    SensorReading result = m_sensor2Watcher->result();

    if (result.healthy != m_sensor2Healthy) {
        m_sensor2Healthy = result.healthy;
        emit sensor2HealthyChanged();
    }

    if (result.healthy && qAbs(result.temperature - m_sensor2Temp) > 0.05) {
        m_sensor2Temp = result.temperature;
        emit sensor2TempChanged();
    }

    // Check if all reads are complete
    if (!m_sensor1Watcher->isRunning()) {
        m_readInProgress = false;
    }
}
