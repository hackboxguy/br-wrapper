#include "DualDisplayAbsoluteController.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonValue>
#include <QProcess>
#include <QTcpSocket>
#include <QtMath>
#include <algorithm>
#include <unistd.h>

namespace {
const int kPrimaryPort = 9000;
const int kSecondaryPort = 9001;
const int kSocketTimeoutMs = 1000;
const int kServiceTimeoutMs = 10000;
const int kBrightnessThrottleMs = 200;
const char *kSecondaryService = "als-dimmer-pwm.service";
}

DualDisplayAbsoluteController::DualDisplayAbsoluteController(QObject *parent)
    : QObject(parent)
    , m_active(false)
    , m_busy(false)
    , m_maxNits(1000.0)
    , m_currentNits(0.0)
    , m_pendingNits(0.0)
    , m_nitsStep(5.0)
    , m_brightnessUpdatePending(false)
    , m_statusText("Dual absolute mode off")
    , m_restorePrimaryBrightness(50)
    , m_hasRestoreState(false)
    , m_brightnessThrottle(new QTimer(this))
{
    m_brightnessThrottle->setInterval(kBrightnessThrottleMs);
    m_brightnessThrottle->setSingleShot(true);
    connect(m_brightnessThrottle, &QTimer::timeout,
            this, &DualDisplayAbsoluteController::processBrightnessUpdate);
}

DualDisplayAbsoluteController::~DualDisplayAbsoluteController()
{
    cleanup();
}

void DualDisplayAbsoluteController::setEnabled(bool enabled, const QString &primaryMode, int primaryBrightness)
{
    if (enabled == m_active && !m_busy) {
        return;
    }

    if (enabled) {
        enableMode(primaryMode, primaryBrightness);
    } else {
        disableMode();
    }
}

void DualDisplayAbsoluteController::cleanup()
{
    m_brightnessThrottle->stop();

    if (m_active && m_brightnessUpdatePending) {
        QString error;
        if (!sendAbsoluteBrightnessToBoth(m_pendingNits, &error)) {
            reportError(error);
            return;
        }
        m_brightnessUpdatePending = false;
    }
}

bool DualDisplayAbsoluteController::enableMode(const QString &primaryMode, int primaryBrightness)
{
    if (m_active) {
        return true;
    }

    setBusy(true);
    setStatusText("Starting dual absolute mode...");

    m_restorePrimaryMode = primaryMode.isEmpty() ? "manual" : primaryMode;
    m_restorePrimaryBrightness = qBound(0, primaryBrightness, 100);
    m_hasRestoreState = true;

    QString error;
    if (!runServiceAction("start", &error)) {
        reportError(error);
        setBusy(false);
        return false;
    }

    if (!setMode(kPrimaryPort, "manual", &error) ||
        !setMode(kSecondaryPort, "manual", &error)) {
        reportError(error);
        runServiceAction("stop", nullptr);
        restorePrimary(nullptr);
        m_hasRestoreState = false;
        setBusy(false);
        return false;
    }

    double primaryMax = 0.0;
    double secondaryMax = 0.0;
    if (!readMaxBrightness(kPrimaryPort, &primaryMax, &error) ||
        !readMaxBrightness(kSecondaryPort, &secondaryMax, &error)) {
        reportError(error);
        runServiceAction("stop", nullptr);
        restorePrimary(nullptr);
        m_hasRestoreState = false;
        setBusy(false);
        return false;
    }

    double sharedMax = std::min(primaryMax, secondaryMax);
    if (!qIsFinite(sharedMax) || sharedMax <= 0.0) {
        reportError(QString("Invalid shared max brightness: %1").arg(sharedMax));
        runServiceAction("stop", nullptr);
        restorePrimary(nullptr);
        m_hasRestoreState = false;
        setBusy(false);
        return false;
    }

    setMaxNits(sharedMax);
    double initialNits = roundedNits(sharedMax / 2.0);
    if (!sendAbsoluteBrightnessToBoth(initialNits, &error)) {
        reportError(error);
        runServiceAction("stop", nullptr);
        restorePrimary(nullptr);
        m_hasRestoreState = false;
        setBusy(false);
        return false;
    }

    setCurrentNits(initialNits);
    m_pendingNits = initialNits;
    m_brightnessUpdatePending = false;
    setEnabledState(true);
    setStatusText(QString("Dual absolute mode: 0-%1 nits").arg(qRound(sharedMax)));
    setBusy(false);
    return true;
}

void DualDisplayAbsoluteController::disableMode()
{
    if (!m_active && !m_busy) {
        return;
    }

    setBusy(true);
    m_brightnessThrottle->stop();
    m_brightnessUpdatePending = false;

    QString error;
    if (!restorePrimary(&error)) {
        reportError(error);
    }
    if (!runServiceAction("stop", &error)) {
        reportError(error);
    }

    m_hasRestoreState = false;
    setEnabledState(false);
    setStatusText("Dual absolute mode off");
    setBusy(false);
}

bool DualDisplayAbsoluteController::restorePrimary(QString *error)
{
    if (!m_hasRestoreState) {
        return true;
    }

    QString restoreMode = m_restorePrimaryMode.toLower();
    if (restoreMode == "auto") {
        return setMode(kPrimaryPort, "auto", error);
    }

    if (restoreMode == "manual_temporary") {
        if (!setMode(kPrimaryPort, "auto", error)) {
            return false;
        }
        return setRelativeBrightness(kPrimaryPort, m_restorePrimaryBrightness, error);
    }

    if (!setMode(kPrimaryPort, "manual", error)) {
        return false;
    }
    return setRelativeBrightness(kPrimaryPort, m_restorePrimaryBrightness, error);
}

bool DualDisplayAbsoluteController::runServiceAction(const QString &action, QString *error)
{
    QString program;
    QStringList args;
    if (geteuid() == 0) {
        program = "systemctl";
        args << action << kSecondaryService;
    } else {
        program = "sudo";
        args << "-n" << "systemctl" << action << kSecondaryService;
    }

    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(1000)) {
        if (error) {
            *error = QString("Failed to start %1: %2").arg(program, process.errorString());
        }
        return false;
    }

    if (!process.waitForFinished(kServiceTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        if (error) {
            *error = QString("%1 %2 timed out").arg(program, args.join(' '));
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (detail.isEmpty()) {
            detail = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        }
        if (error) {
            *error = QString("%1 %2 failed: %3").arg(program, args.join(' '), detail);
        }
        return false;
    }

    return true;
}

bool DualDisplayAbsoluteController::callJson(int port, const QString &command,
                                             const QJsonObject &params,
                                             QJsonObject *data, QString *error)
{
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", port);
    if (!socket.waitForConnected(kSocketTimeoutMs)) {
        if (error) {
            *error = QString("als-dimmer port %1 connect failed: %2")
                .arg(port)
                .arg(socket.errorString());
        }
        return false;
    }

    QJsonObject request;
    request["version"] = "1.0";
    request["command"] = command;
    if (!params.isEmpty()) {
        request["params"] = params;
    }

    QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    payload.append('\n');
    socket.write(payload);
    if (!socket.waitForBytesWritten(kSocketTimeoutMs)) {
        if (error) {
            *error = QString("als-dimmer port %1 write failed: %2")
                .arg(port)
                .arg(socket.errorString());
        }
        return false;
    }

    QByteArray response;
    QElapsedTimer timer;
    timer.start();
    while (!response.contains('\n') && timer.elapsed() < kSocketTimeoutMs) {
        int remaining = kSocketTimeoutMs - static_cast<int>(timer.elapsed());
        if (!socket.waitForReadyRead(qMax(1, remaining))) {
            break;
        }
        response.append(socket.readAll());
    }

    if (response.isEmpty()) {
        if (error) {
            *error = QString("als-dimmer port %1 did not respond to %2")
                .arg(port)
                .arg(command);
        }
        return false;
    }

    int newline = response.indexOf('\n');
    QByteArray line = newline >= 0 ? response.left(newline) : response;

    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QString("als-dimmer port %1 returned invalid JSON: %2")
                .arg(port)
                .arg(parseError.errorString());
        }
        return false;
    }

    QJsonObject responseObject = document.object();
    if (responseObject.value("status").toString() != "success") {
        if (error) {
            QString message = responseObject.value("message").toString("command failed");
            *error = QString("als-dimmer port %1 %2 failed: %3")
                .arg(port)
                .arg(command)
                .arg(message);
        }
        return false;
    }

    if (data) {
        *data = responseObject.value("data").toObject();
    }
    return true;
}

bool DualDisplayAbsoluteController::setMode(int port, const QString &mode, QString *error)
{
    QJsonObject params;
    params["mode"] = mode;
    return callJson(port, "set_mode", params, nullptr, error);
}

bool DualDisplayAbsoluteController::setRelativeBrightness(int port, int brightness, QString *error)
{
    QJsonObject params;
    params["brightness"] = qBound(0, brightness, 100);
    return callJson(port, "set_brightness", params, nullptr, error);
}

bool DualDisplayAbsoluteController::readMaxBrightness(int port, double *maxNits, QString *error)
{
    QJsonObject data;
    if (!callJson(port, "get_calibration_info", QJsonObject(), &data, error)) {
        return false;
    }

    if (data.contains("calibrated") && !data.value("calibrated").toBool(false)) {
        if (error) {
            *error = QString("als-dimmer port %1 has no brightness calibration").arg(port);
        }
        return false;
    }

    QJsonValue value = data.value("max_nits");
    if (!value.isDouble() || value.toDouble() <= 0.0) {
        if (error) {
            *error = QString("als-dimmer port %1 missing max_nits").arg(port);
        }
        return false;
    }

    *maxNits = value.toDouble();
    return true;
}

bool DualDisplayAbsoluteController::sendAbsoluteBrightness(int port, double nits, QString *error)
{
    QJsonObject params;
    params["nits"] = nits;
    return callJson(port, "set_absolute_brightness", params, nullptr, error);
}

bool DualDisplayAbsoluteController::sendAbsoluteBrightnessToBoth(double nits, QString *error)
{
    return sendAbsoluteBrightness(kPrimaryPort, nits, error) &&
           sendAbsoluteBrightness(kSecondaryPort, nits, error);
}

void DualDisplayAbsoluteController::setAbsoluteBrightness(double nits)
{
    if (!m_active || m_busy) {
        return;
    }

    double rounded = roundedNits(nits);
    setCurrentNits(rounded);
    m_pendingNits = rounded;
    m_brightnessUpdatePending = true;

    if (!m_brightnessThrottle->isActive()) {
        m_brightnessThrottle->start();
    }
}

void DualDisplayAbsoluteController::processBrightnessUpdate()
{
    if (!m_active || !m_brightnessUpdatePending) {
        return;
    }

    QString error;
    double target = m_pendingNits;
    if (!sendAbsoluteBrightnessToBoth(target, &error)) {
        reportError(error);
        return;
    }

    m_brightnessUpdatePending = false;
    m_brightnessThrottle->start();
}

double DualDisplayAbsoluteController::roundedNits(double nits) const
{
    double clamped = qBound(0.0, nits, m_maxNits);
    return qRound(clamped / m_nitsStep) * m_nitsStep;
}

void DualDisplayAbsoluteController::setEnabledState(bool enabled)
{
    if (m_active == enabled) {
        return;
    }

    m_active = enabled;
    emit activeChanged();
}

void DualDisplayAbsoluteController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void DualDisplayAbsoluteController::setMaxNits(double maxNits)
{
    if (qAbs(m_maxNits - maxNits) <= 0.05) {
        return;
    }

    m_maxNits = maxNits;
    emit rangeChanged();
}

void DualDisplayAbsoluteController::setCurrentNits(double nits)
{
    if (qAbs(m_currentNits - nits) <= 0.05) {
        return;
    }

    m_currentNits = nits;
    emit currentNitsChanged();
}

void DualDisplayAbsoluteController::setStatusText(const QString &text)
{
    if (m_statusText == text) {
        return;
    }

    m_statusText = text;
    emit statusTextChanged();
}

void DualDisplayAbsoluteController::reportError(const QString &error)
{
    if (error.isEmpty()) {
        return;
    }

    qWarning() << "DualDisplayAbsoluteController:" << error;
    setStatusText(error);
    emit errorOccurred(error);
}
