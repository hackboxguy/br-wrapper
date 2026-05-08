#include "DualDisplayAbsoluteController.h"

#include <QAbstractSocket>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonValue>
#include <QProcess>
#include <QSaveFile>
#include <QTcpSocket>
#include <QtMath>
#include <algorithm>
#include <unistd.h>

namespace {
const int kPrimaryPort = 9000;
const int kSecondaryPort = 9001;
const int kSocketTimeoutMs = 1000;
const int kServiceTimeoutMs = 10000;
const int kBrightnessThrottleMs = 100;
const char *kSecondaryService = "als-dimmer-pwm.service";
const char *kStateFilePath = "/tmp/disp-settings-dual-display-mode.json";

bool parseResponseLine(int port, const QString &command, const QByteArray &line,
                       QJsonObject *data, QString *error)
{
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

bool readNitsValue(const QJsonObject &data, double *nits)
{
    QJsonValue value = data.value("nits");
    if (!value.isDouble()) {
        return false;
    }

    double parsed = value.toDouble();
    if (!qIsFinite(parsed) || parsed < 0.0) {
        return false;
    }

    *nits = parsed;
    return true;
}
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
    , m_asyncTimeout(new QTimer(this))
    , m_asyncSocket(nullptr)
    , m_sendInFlight(false)
    , m_exitFlushed(false)
    , m_asyncPort(0)
    , m_inFlightNits(0.0)
{
    m_brightnessThrottle->setInterval(kBrightnessThrottleMs);
    m_brightnessThrottle->setSingleShot(true);
    connect(m_brightnessThrottle, &QTimer::timeout,
            this, &DualDisplayAbsoluteController::processBrightnessUpdate);

    m_asyncTimeout->setSingleShot(true);
    connect(m_asyncTimeout, &QTimer::timeout,
            this, &DualDisplayAbsoluteController::onAsyncSocketTimeout);

    QTimer::singleShot(0, this, &DualDisplayAbsoluteController::adoptSavedState);
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
    closeAsyncSocket();
    m_sendInFlight = false;

    if (m_active && !m_exitFlushed) {
        QString error;
        if (!sendAbsoluteBrightnessToBoth(m_pendingNits, &error)) {
            reportError(error);
            return;
        }
        m_brightnessUpdatePending = false;
        m_exitFlushed = true;
    }
}

bool DualDisplayAbsoluteController::enableMode(const QString &primaryMode, int primaryBrightness)
{
    if (m_active) {
        return true;
    }

    setBusy(true);
    setStatusText("Starting dual absolute mode...");
    m_sendInFlight = false;

    m_restorePrimaryMode = primaryMode.isEmpty() ? "manual" : primaryMode;
    m_restorePrimaryBrightness = qBound(0, primaryBrightness, 100);
    m_hasRestoreState = true;

    QString error;
    if (!runServiceAction("start", &error)) {
        reportError(error);
        clearStateFile();
        m_hasRestoreState = false;
        setBusy(false);
        return false;
    }

    if (!setMode(kPrimaryPort, "manual", &error) ||
        !setMode(kSecondaryPort, "manual", &error)) {
        reportError(error);
        runServiceAction("stop", nullptr);
        restorePrimary(nullptr);
        clearStateFile();
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
        clearStateFile();
        m_hasRestoreState = false;
        setBusy(false);
        return false;
    }

    double sharedMax = std::min(primaryMax, secondaryMax);
    if (!qIsFinite(sharedMax) || sharedMax <= 0.0) {
        reportError(QString("Invalid shared max brightness: %1").arg(sharedMax));
        runServiceAction("stop", nullptr);
        restorePrimary(nullptr);
        clearStateFile();
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
        clearStateFile();
        m_hasRestoreState = false;
        setBusy(false);
        return false;
    }

    setCurrentNits(initialNits);
    m_pendingNits = initialNits;
    m_brightnessUpdatePending = false;
    setEnabledState(true);
    setStatusText(QString("Dual absolute mode: 0-%1 nits").arg(qRound(sharedMax)));
    saveStateFile();
    m_exitFlushed = false;
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
    closeAsyncSocket();
    m_sendInFlight = false;
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
    clearStateFile();
    setStatusText("Dual absolute mode off");
    m_exitFlushed = true;
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

    return parseResponseLine(port, command, line, data, error);
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

bool DualDisplayAbsoluteController::readStatus(int port, QJsonObject *data, QString *error)
{
    return callJson(port, "get_status", QJsonObject(), data, error);
}

bool DualDisplayAbsoluteController::readAbsoluteBrightness(int port, double *nits, QString *error)
{
    QJsonObject data;
    if (!callJson(port, "get_absolute_brightness", QJsonObject(), &data, error)) {
        return false;
    }

    if (!readNitsValue(data, nits)) {
        if (error) {
            *error = QString("als-dimmer port %1 missing absolute nits").arg(port);
        }
        return false;
    }

    return true;
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

bool DualDisplayAbsoluteController::loadStateFile(QString *restoreMode, int *restoreBrightness) const
{
    QFile file(kStateFilePath);
    if (!file.exists()) {
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to open state file"
                   << kStateFilePath << file.errorString();
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "DualDisplayAbsoluteController: Invalid state file:"
                   << parseError.errorString();
        return false;
    }

    QJsonObject object = document.object();
    *restoreMode = object.value("restore_primary_mode").toString("manual");
    *restoreBrightness = qBound(0, object.value("restore_primary_brightness").toInt(50), 100);
    return true;
}

bool DualDisplayAbsoluteController::saveStateFile() const
{
    QJsonObject object;
    object["version"] = 1;
    object["restore_primary_mode"] = m_restorePrimaryMode;
    object["restore_primary_brightness"] = m_restorePrimaryBrightness;

    QSaveFile file(kStateFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to write state file"
                   << kStateFilePath << file.errorString();
        return false;
    }

    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.write("\n");
    if (!file.commit()) {
        qWarning() << "DualDisplayAbsoluteController: Failed to commit state file"
                   << kStateFilePath << file.errorString();
        return false;
    }

    return true;
}

void DualDisplayAbsoluteController::clearStateFile() const
{
    if (!QFile::exists(kStateFilePath)) {
        return;
    }
    if (!QFile::remove(kStateFilePath)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to remove state file"
                   << kStateFilePath;
    }
}

void DualDisplayAbsoluteController::adoptSavedState()
{
    if (m_active || m_busy) {
        return;
    }

    QString restoreMode;
    int restoreBrightness = 50;
    if (!loadStateFile(&restoreMode, &restoreBrightness)) {
        return;
    }

    setBusy(true);
    setStatusText("Checking dual display mode...");

    QString error;
    QJsonObject primaryStatus;
    QJsonObject secondaryStatus;
    if (!readStatus(kPrimaryPort, &primaryStatus, &error) ||
        !readStatus(kSecondaryPort, &secondaryStatus, &error)) {
        qWarning() << "DualDisplayAbsoluteController: Saved dual mode not active:" << error;
        setStatusText("Dual absolute mode off");
        setBusy(false);
        return;
    }

    QString primaryMode = primaryStatus.value("mode").toString();
    QString secondaryMode = secondaryStatus.value("mode").toString();
    if (primaryMode != "manual" || secondaryMode != "manual") {
        qWarning() << "DualDisplayAbsoluteController: Saved dual mode is stale:"
                   << "primary" << primaryMode << "secondary" << secondaryMode;
        clearStateFile();
        setStatusText("Dual absolute mode off");
        setBusy(false);
        return;
    }

    double primaryMax = 0.0;
    double secondaryMax = 0.0;
    if (!readMaxBrightness(kPrimaryPort, &primaryMax, &error) ||
        !readMaxBrightness(kSecondaryPort, &secondaryMax, &error)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to read saved dual range:" << error;
        setStatusText("Dual absolute mode off");
        setBusy(false);
        return;
    }

    double currentNits = 0.0;
    if (!readNitsValue(primaryStatus, &currentNits) &&
        !readAbsoluteBrightness(kPrimaryPort, &currentNits, &error) &&
        !readNitsValue(secondaryStatus, &currentNits) &&
        !readAbsoluteBrightness(kSecondaryPort, &currentNits, &error)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to read saved dual brightness:" << error;
        setStatusText("Dual absolute mode off");
        setBusy(false);
        return;
    }

    double sharedMax = std::min(primaryMax, secondaryMax);
    if (!qIsFinite(sharedMax) || sharedMax <= 0.0) {
        qWarning() << "DualDisplayAbsoluteController: Invalid saved dual range" << sharedMax;
        setStatusText("Dual absolute mode off");
        setBusy(false);
        return;
    }

    m_restorePrimaryMode = restoreMode.isEmpty() ? "manual" : restoreMode;
    m_restorePrimaryBrightness = qBound(0, restoreBrightness, 100);
    m_hasRestoreState = true;

    setMaxNits(sharedMax);
    double rounded = roundedNits(currentNits);
    setCurrentNits(rounded);
    m_pendingNits = rounded;
    m_brightnessUpdatePending = false;
    setEnabledState(true);
    setStatusText(QString("Dual absolute mode: 0-%1 nits").arg(qRound(sharedMax)));
    m_exitFlushed = false;
    setBusy(false);
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
    m_exitFlushed = false;

    if (!m_brightnessThrottle->isActive()) {
        m_brightnessThrottle->start();
    }
}

void DualDisplayAbsoluteController::processBrightnessUpdate()
{
    if (!m_active || !m_brightnessUpdatePending || m_sendInFlight) {
        return;
    }

    double target = m_pendingNits;
    m_brightnessUpdatePending = false;
    startAsyncBrightnessSend(target);
}

void DualDisplayAbsoluteController::startAsyncBrightnessSend(double nits)
{
    if (m_sendInFlight) {
        m_brightnessUpdatePending = true;
        m_pendingNits = nits;
        return;
    }

    m_sendInFlight = true;
    m_inFlightNits = nits;
    startAsyncBrightnessSendToPort(kPrimaryPort);
}

void DualDisplayAbsoluteController::startAsyncBrightnessSendToPort(int port)
{
    closeAsyncSocket();

    m_asyncPort = port;
    m_asyncReadBuffer.clear();
    m_asyncSocket = new QTcpSocket(this);
    connect(m_asyncSocket, &QTcpSocket::connected,
            this, &DualDisplayAbsoluteController::onAsyncSocketConnected);
    connect(m_asyncSocket, &QTcpSocket::readyRead,
            this, &DualDisplayAbsoluteController::onAsyncSocketReadyRead);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_asyncSocket, &QTcpSocket::errorOccurred,
            this, &DualDisplayAbsoluteController::onAsyncSocketError);
#else
    connect(m_asyncSocket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &DualDisplayAbsoluteController::onAsyncSocketError);
#endif

    m_asyncTimeout->start(kSocketTimeoutMs);
    m_asyncSocket->connectToHost("127.0.0.1", port);
}

void DualDisplayAbsoluteController::onAsyncSocketConnected()
{
    if (!m_asyncSocket) {
        return;
    }

    QJsonObject request;
    request["version"] = "1.0";
    request["command"] = "set_absolute_brightness";

    QJsonObject params;
    params["nits"] = m_inFlightNits;
    request["params"] = params;

    QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (m_asyncSocket->write(payload) < 0) {
        failAsyncBrightnessSend(QString("als-dimmer port %1 write failed: %2")
                                    .arg(m_asyncPort)
                                    .arg(m_asyncSocket->errorString()));
        return;
    }
    m_asyncSocket->flush();
}

void DualDisplayAbsoluteController::onAsyncSocketReadyRead()
{
    if (!m_asyncSocket) {
        return;
    }

    m_asyncReadBuffer.append(m_asyncSocket->readAll());
    int newline = m_asyncReadBuffer.indexOf('\n');
    if (newline < 0) {
        return;
    }

    QByteArray line = m_asyncReadBuffer.left(newline);
    QString error;
    if (!parseResponseLine(m_asyncPort, "set_absolute_brightness", line, nullptr, &error)) {
        failAsyncBrightnessSend(error);
        return;
    }

    int completedPort = m_asyncPort;
    closeAsyncSocket();
    if (completedPort == kPrimaryPort) {
        startAsyncBrightnessSendToPort(kSecondaryPort);
        return;
    }

    finishAsyncBrightnessSend();
}

void DualDisplayAbsoluteController::onAsyncSocketError()
{
    if (!m_asyncSocket) {
        return;
    }

    failAsyncBrightnessSend(QString("als-dimmer port %1 socket error: %2")
                                .arg(m_asyncPort)
                                .arg(m_asyncSocket->errorString()));
}

void DualDisplayAbsoluteController::onAsyncSocketTimeout()
{
    failAsyncBrightnessSend(QString("als-dimmer port %1 did not respond to set_absolute_brightness")
                                .arg(m_asyncPort));
}

void DualDisplayAbsoluteController::finishAsyncBrightnessSend()
{
    m_sendInFlight = false;
    if (m_active && m_brightnessUpdatePending) {
        m_brightnessThrottle->start();
    }
}

void DualDisplayAbsoluteController::failAsyncBrightnessSend(const QString &error)
{
    closeAsyncSocket();
    m_sendInFlight = false;
    reportError(error);
    if (m_active && m_brightnessUpdatePending) {
        m_brightnessThrottle->start();
    }
}

void DualDisplayAbsoluteController::closeAsyncSocket()
{
    m_asyncTimeout->stop();
    m_asyncReadBuffer.clear();
    if (!m_asyncSocket) {
        return;
    }

    m_asyncSocket->disconnect(this);
    m_asyncSocket->abort();
    m_asyncSocket->deleteLater();
    m_asyncSocket = nullptr;
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
