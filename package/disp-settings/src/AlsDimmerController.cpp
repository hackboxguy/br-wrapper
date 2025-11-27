#include "AlsDimmerController.h"
#include "config.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

AlsDimmerController::AlsDimmerController(QObject *parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
    , m_pollTimer(new QTimer(this))
    , m_brightnessThrottle(new QTimer(this))
    , m_socketPath(DEFAULT_ALS_DIMMER_SOCKET)
    , m_brightness(50)
    , m_pendingBrightness(50)
    , m_luxValue(0.0)
    , m_mode("unknown")
    , m_zone("unknown")
    , m_connected(false)
    , m_brightnessUpdatePending(false)
{
    // Socket connections
    connect(m_socket, &QLocalSocket::connected, this, &AlsDimmerController::onConnected);
    connect(m_socket, &QLocalSocket::disconnected, this, &AlsDimmerController::onDisconnected);
    connect(m_socket, &QLocalSocket::readyRead, this, &AlsDimmerController::onReadyRead);
    connect(m_socket, QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::error),
            this, &AlsDimmerController::onError);

    // Poll timer for status updates
    connect(m_pollTimer, &QTimer::timeout, this, &AlsDimmerController::pollStatus);
    m_pollTimer->setInterval(DEFAULT_ALS_POLL_INTERVAL_MS);

    // Throttle timer for brightness updates (100ms = max 10 updates/sec)
    m_brightnessThrottle->setInterval(100);
    m_brightnessThrottle->setSingleShot(true);
    connect(m_brightnessThrottle, &QTimer::timeout, this, &AlsDimmerController::processBrightnessUpdate);
}

AlsDimmerController::~AlsDimmerController()
{
    m_pollTimer->stop();
    m_brightnessThrottle->stop();
    if (m_socket->state() == QLocalSocket::ConnectedState) {
        m_socket->disconnectFromServer();
    }
}

void AlsDimmerController::setSocketPath(const QString &path)
{
    m_socketPath = path;
}

void AlsDimmerController::start()
{
    connectToService();
}

void AlsDimmerController::connectToService()
{
    if (m_socket->state() == QLocalSocket::ConnectedState) {
        return;
    }

    qDebug() << "AlsDimmerController: Connecting to" << m_socketPath;
    m_socket->connectToServer(m_socketPath);
}

void AlsDimmerController::reconnect()
{
    qDebug() << "AlsDimmerController: Reconnecting...";
    m_socket->disconnectFromServer();
    QTimer::singleShot(500, this, &AlsDimmerController::connectToService);
}

void AlsDimmerController::onConnected()
{
    qDebug() << "AlsDimmerController: Connected to als-dimmer service";
    m_connected = true;
    emit connectedChanged();

    // Start polling
    pollStatus();
    m_pollTimer->start();
}

void AlsDimmerController::onDisconnected()
{
    qDebug() << "AlsDimmerController: Disconnected from als-dimmer service";
    m_connected = false;
    m_pollTimer->stop();
    emit connectedChanged();

    // Try to reconnect after 2 seconds
    QTimer::singleShot(2000, this, &AlsDimmerController::connectToService);
}

void AlsDimmerController::onError(QLocalSocket::LocalSocketError error)
{
    QString errorStr;
    switch (error) {
        case QLocalSocket::ConnectionRefusedError:
            errorStr = "Connection refused";
            break;
        case QLocalSocket::PeerClosedError:
            errorStr = "Connection closed by server";
            break;
        case QLocalSocket::ServerNotFoundError:
            errorStr = "Server not found";
            break;
        default:
            errorStr = m_socket->errorString();
    }

    qWarning() << "AlsDimmerController: Socket error:" << errorStr;
    emit errorOccurred(errorStr);

    // Don't set connected to false here - onDisconnected will handle it
}

void AlsDimmerController::onReadyRead()
{
    m_readBuffer.append(m_socket->readAll());

    // Process complete JSON messages (newline-delimited)
    while (true) {
        int newlinePos = m_readBuffer.indexOf('\n');
        if (newlinePos < 0) {
            // Also try to parse without newline (single response)
            if (!m_readBuffer.isEmpty()) {
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(m_readBuffer, &parseError);
                if (parseError.error == QJsonParseError::NoError) {
                    parseResponse(m_readBuffer);
                    m_readBuffer.clear();
                }
            }
            break;
        }

        QByteArray message = m_readBuffer.left(newlinePos);
        m_readBuffer.remove(0, newlinePos + 1);

        if (!message.isEmpty()) {
            parseResponse(message);
        }
    }
}

void AlsDimmerController::parseResponse(const QByteArray &data)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "AlsDimmerController: JSON parse error:" << parseError.errorString();
        return;
    }

    QJsonObject response = doc.object();
    QString status = response["status"].toString();

    if (status != "success") {
        QString message = response["message"].toString();
        qWarning() << "AlsDimmerController: Command failed:" << message;
        emit errorOccurred(message);
        return;
    }

    QJsonObject responseData = response["data"].toObject();

    // Check what kind of response this is
    if (responseData.contains("lux")) {
        // This is a status response
        handleStatusResponse(responseData);
    } else if (responseData.contains("brightness") && !responseData.contains("lux")) {
        // This is a brightness set response - just acknowledge
        qDebug() << "AlsDimmerController: Brightness set acknowledged";
    } else if (responseData.contains("mode") && !responseData.contains("lux")) {
        // This is a mode set response
        qDebug() << "AlsDimmerController: Mode set acknowledged";
    }
}

void AlsDimmerController::handleStatusResponse(const QJsonObject &data)
{
    bool changed = false;

    // Update brightness
    if (data.contains("brightness")) {
        int newBrightness = data["brightness"].toInt();
        if (newBrightness != m_brightness) {
            m_brightness = newBrightness;
            changed = true;
            emit brightnessChanged();
        }
    }

    // Update lux value
    if (data.contains("lux")) {
        double newLux = data["lux"].toDouble();
        if (qAbs(newLux - m_luxValue) > 0.1) {
            m_luxValue = newLux;
            emit luxValueChanged();
        }
    }

    // Update mode
    if (data.contains("mode")) {
        QString newMode = data["mode"].toString();
        if (newMode != m_mode) {
            m_mode = newMode;
            emit modeChanged();
        }
    }

    // Update zone
    if (data.contains("zone")) {
        QString newZone = data["zone"].toString();
        if (newZone != m_zone) {
            m_zone = newZone;
            emit zoneChanged();
        }
    }
}

void AlsDimmerController::handleConfigResponse(const QJsonObject &data)
{
    // Handle get_config response if needed
    Q_UNUSED(data);
}

void AlsDimmerController::pollStatus()
{
    if (!m_connected) {
        return;
    }

    QJsonObject cmd;
    cmd["version"] = "1.0";
    cmd["command"] = "get_status";

    sendCommand(cmd);
}

void AlsDimmerController::sendCommand(const QJsonObject &command)
{
    if (m_socket->state() != QLocalSocket::ConnectedState) {
        qWarning() << "AlsDimmerController: Not connected, cannot send command";
        return;
    }

    QJsonDocument doc(command);
    QByteArray data = doc.toJson(QJsonDocument::Compact) + "\n";

    qDebug() << "AlsDimmerController: Sending:" << data.trimmed();
    m_socket->write(data);
    m_socket->flush();
}

void AlsDimmerController::setBrightness(int value)
{
    // Clamp value
    value = qBound(0, value, 100);

    // Store pending value
    m_pendingBrightness = value;
    m_brightnessUpdatePending = true;

    // Start or restart throttle timer
    if (!m_brightnessThrottle->isActive()) {
        // Send immediately for first update, then throttle
        processBrightnessUpdate();
    }
    // If timer is already running, it will pick up the latest value when it fires
}

void AlsDimmerController::processBrightnessUpdate()
{
    if (!m_brightnessUpdatePending || !m_connected) {
        return;
    }

    QJsonObject cmd;
    cmd["version"] = "1.0";
    cmd["command"] = "set_brightness";

    QJsonObject params;
    params["brightness"] = m_pendingBrightness;
    cmd["params"] = params;

    sendCommand(cmd);

    m_brightnessUpdatePending = false;

    // If there might be more updates coming, restart the throttle timer
    m_brightnessThrottle->start();
}

void AlsDimmerController::setAdaptiveMode(bool enabled)
{
    if (!m_connected) {
        qWarning() << "AlsDimmerController: Not connected, cannot set mode";
        return;
    }

    QJsonObject cmd;
    cmd["version"] = "1.0";
    cmd["command"] = "set_mode";

    QJsonObject params;
    params["mode"] = enabled ? "auto" : "manual";
    cmd["params"] = params;

    sendCommand(cmd);
}

void AlsDimmerController::adjustBrightness(int delta)
{
    if (!m_connected) {
        qWarning() << "AlsDimmerController: Not connected, cannot adjust brightness";
        return;
    }

    QJsonObject cmd;
    cmd["version"] = "1.0";
    cmd["command"] = "adjust_brightness";

    QJsonObject params;
    params["delta"] = delta;
    cmd["params"] = params;

    sendCommand(cmd);
}
