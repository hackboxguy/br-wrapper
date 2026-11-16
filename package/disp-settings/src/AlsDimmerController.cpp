#include "AlsDimmerController.h"
#include "config.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>

AlsDimmerController::AlsDimmerController(QObject *parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
    , m_pollTimer(new QTimer(this))
    , m_brightnessThrottle(new QTimer(this))
    , m_privacyModeThrottle(new QTimer(this))
    , m_socketPath(DEFAULT_ALS_DIMMER_SOCKET)
    , m_brightness(50)
    , m_pendingBrightness(50)
    , m_luxValue(0.0)
    , m_absoluteBrightnessNits(0.0)
    , m_absoluteBrightnessValid(false)
    , m_absoluteBrightnessCalibrated(false)
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

    m_privacyModeThrottle->setInterval(100);
    m_privacyModeThrottle->setSingleShot(true);
    connect(m_privacyModeThrottle, &QTimer::timeout, this, &AlsDimmerController::sendPendingPrivacyMode);
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
        // An als-dimmer without dynamic local dimming: stop asking (get_ld_status is the only
        // command this controller sends that an older daemon does not know).
        if (message == "Unknown command type" && m_ldStatusSupported) {
            m_ldStatusSupported = false;
            if (m_ldAvailable) { m_ldAvailable = false; emit ldStatusChanged(); }
            qDebug() << "AlsDimmerController: als-dimmer has no get_ld_status; LD status off";
            return;
        }
        qWarning() << "AlsDimmerController: Command failed:" << message;
        emit errorOccurred(message);
        return;
    }

    QJsonObject responseData = response["data"].toObject();

    // Dynamic local dimming status (get_ld_status and the set_ld_dynamic reply)
    if (responseData.contains("ld")) {
        handleLdStatus(responseData);
        return;
    }

    // Privacy film: the set_privacy_mode / get_privacy_mode reply
    if (responseData.contains("privacy_mode") && !responseData.contains("lux")) {
        handlePrivacyMode(responseData["privacy_mode"].toObject());
        return;
    }

    // Privacy light: the set_privacy_level / get_privacy_status reply (get_status carries it too,
    // handled with the rest of the status below)
    if (responseData.contains("privacy") && !responseData.contains("lux")) {
        handlePrivacy(responseData["privacy"].toObject());
        return;
    }

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
    // Update brightness
    if (data.contains("brightness")) {
        int newBrightness = data["brightness"].toInt();
        if (newBrightness != m_brightness) {
            m_brightness = newBrightness;
            emit brightnessChanged();
        }
    }

    // Update lux value - always update if present in response
    if (data.contains("lux")) {
        double newLux = data["lux"].toDouble();
        if (qAbs(newLux - m_luxValue) > 0.1) {
            m_luxValue = newLux;
            emit luxValueChanged();
            qDebug() << "AlsDimmerController: Lux updated to" << newLux;
        }
    } else {
        qDebug() << "AlsDimmerController: Status response missing 'lux' field";
    }

    bool absoluteChanged = false;

    if (data.contains("calibrated")) {
        bool newCalibrated = data["calibrated"].toBool(false);
        if (newCalibrated != m_absoluteBrightnessCalibrated) {
            m_absoluteBrightnessCalibrated = newCalibrated;
            absoluteChanged = true;
        }
    } else if (m_absoluteBrightnessCalibrated) {
        m_absoluteBrightnessCalibrated = false;
        absoluteChanged = true;
    }

    QJsonValue nitsValue = data.value("nits");
    if (nitsValue.isDouble()) {
        double newNits = nitsValue.toDouble();
        if (!m_absoluteBrightnessValid || qAbs(newNits - m_absoluteBrightnessNits) > 0.05) {
            m_absoluteBrightnessNits = newNits;
            absoluteChanged = true;
        }
        if (!m_absoluteBrightnessValid) {
            m_absoluteBrightnessValid = true;
            absoluteChanged = true;
        }
    } else if (m_absoluteBrightnessValid) {
        m_absoluteBrightnessNits = 0.0;
        m_absoluteBrightnessValid = false;
        absoluteChanged = true;
    }

    if (absoluteChanged) {
        emit absoluteBrightnessChanged();
    }

    // Update mode
    if (data.contains("mode")) {
        QString newMode = data["mode"].toString();
        if (newMode != m_mode) {
            m_mode = newMode;
            emit modeChanged();
        }
    }

    // Privacy light; an als-dimmer that does not report it has none to offer
    handlePrivacy(data["privacy"].toObject());
    handlePrivacyMode(data["privacy_mode"].toObject());

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

    // LD status once a second (it reads three FPGA registers)
    if (m_ldStatusSupported && (m_pollCount++ % 2) == 0) {
        QJsonObject ld;
        ld["version"] = "1.0";
        ld["command"] = "get_ld_status";
        sendCommand(ld);
    }
}

void AlsDimmerController::setLdDynamic(bool enabled)
{
    if (!m_connected) return;
    QJsonObject cmd;
    cmd["version"] = "1.0";
    cmd["command"] = "set_ld_dynamic";
    QJsonObject params;
    params["enabled"] = enabled;
    cmd["params"] = params;
    sendCommand(cmd);
}

void AlsDimmerController::setPrivacyLevel(int level, bool persist)
{
    if (!m_connected || !m_privacyAvailable) return;
    QJsonObject cmd;
    cmd["version"] = "1.0";
    cmd["command"] = "set_privacy_level";
    QJsonObject params;
    params["level"] = qBound(0, level, 100);
    params["persist"] = persist;
    cmd["params"] = params;
    sendCommand(cmd);
}

void AlsDimmerController::setPrivacyMode(bool enabled)
{
    if (!m_connected || !m_privacyModeAvailable) return;
    // Show the tap at once and keep it while the write is under way; status replies correct it
    // after the hold if the FPGA did not take it
    m_privacyModeHold.start();
    if (m_privacyModeOn != enabled) {
        m_privacyModeOn = enabled;
        emit privacyModeChanged();
    }
    m_privacyModePending = enabled ? 1 : 0;
    if (!m_privacyModeThrottle->isActive()) {
        sendPendingPrivacyMode();
    }
}

void AlsDimmerController::sendPendingPrivacyMode()
{
    if (m_privacyModePending < 0 || !m_connected) return;
    const int value = m_privacyModePending;
    m_privacyModePending = -1;
    QJsonObject cmd;
    cmd["version"] = "1.0";
    cmd["command"] = "set_privacy_mode";
    QJsonObject params;
    params["enabled"] = value == 1;
    cmd["params"] = params;
    sendCommand(cmd);
    m_privacyModeThrottle->start();
}

void AlsDimmerController::handlePrivacyMode(const QJsonObject &mode)
{
    // An als-dimmer without "privacy_mode" offers no switch
    const bool available = mode["available"].toBool(false);
    const bool reachable = mode["reachable"].toBool(false);
    // The FPGA's read-back; the saved choice until the first read. Held after a tap.
    const QJsonValue fpgaOn = mode["fpga_on"];
    const bool held = m_privacyModeHold.isValid() && m_privacyModeHold.elapsed() < 1500;
    const bool on = held ? m_privacyModeOn
                         : (fpgaOn.isBool() ? fpgaOn.toBool() : mode["enabled"].toBool(false));
    if (available != m_privacyModeAvailable || on != m_privacyModeOn ||
        reachable != m_privacyModeReachable) {
        m_privacyModeAvailable = available;
        m_privacyModeOn = on;
        m_privacyModeReachable = reachable;
        emit privacyModeChanged();
    }
}

void AlsDimmerController::handlePrivacy(const QJsonObject &privacy)
{
    const bool available = privacy["available"].toBool(false);
    const int level = privacy["level"].toInt(0);
    const bool running = privacy["running"].toBool(false);
    const bool reachable = privacy["reachable"].toBool(false);
    if (available != m_privacyAvailable || level != m_privacyLevel ||
        running != m_privacyRunning || reachable != m_privacyReachable) {
        m_privacyAvailable = available;
        m_privacyLevel = level;
        m_privacyRunning = running;
        m_privacyReachable = reachable;
        emit privacyChanged();
    }
}

void AlsDimmerController::handleLdStatus(const QJsonObject &data)
{
    const QJsonObject ld = data["ld"].toObject();
    const QJsonObject tgt = ld["target"].toObject();
    m_ldAvailable = ld["available"].toBool(false);
    m_ldDynamicEnabled = ld["dynamic_enabled"].toBool(false);
    m_ldProfile = ld["profile"].toString();
    m_ldSettled = ld["settled"].toBool(true);
    m_ldAlpha = tgt["ld_alpha"].toInt(16);
    m_ldBoost = tgt.contains("bl_boost") ? tgt["bl_boost"].toInt(64) / 64.0 : 1.0;
    m_ldPcGain = tgt.contains("pc_tune") ? (tgt["pc_tune"].toInt(64) & 0x7F) / 64.0 : 1.0;
    m_ldBoostSuppressed = ld["boost_suppressed"].toBool(false);
    m_ldMultiplierPercent = ld["fpga_multiplier_percent"].toDouble(0.0);
    m_ldAplPercent = ld["fpga_apl_percent"].toDouble(0.0);
    if (data.contains("thermal_limit")) {
        const QJsonObject tl = data["thermal_limit"].toObject();
        m_thermalLimitEnabled = tl["enabled"].toBool(false);
        m_thermalLimiting = tl["limiting"].toBool(false);
        m_thermalLimitPercent = tl["brightness_limit_percent"].toInt(100);
        m_thermalStartC = tl["start_c"].toDouble(0.0);
        m_backlightTempValid = tl["backlight_temp_c"].isDouble();
        m_backlightTempC = m_backlightTempValid ? tl["backlight_temp_c"].toDouble() : 0.0;
    }
    emit ldStatusChanged();
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

    // Request immediate status update for instant UI feedback
    pollStatus();

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

    // Request immediate status update for instant UI feedback
    pollStatus();
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

    // Request immediate status update for instant UI feedback
    pollStatus();
}
