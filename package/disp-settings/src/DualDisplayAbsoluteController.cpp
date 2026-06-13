#include "DualDisplayAbsoluteController.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonValue>
#include <QProcess>
#include <QSaveFile>
#include <QStringList>
#include <QTcpSocket>
#include <QtConcurrent>
#include <QtMath>
#include <algorithm>
#include <unistd.h>

namespace {
const int kPrimaryPort = 9000;
const int kSecondaryPort = 9001;
const int kSocketTimeoutMs = 1000;
const int kServiceTimeoutMs = 10000;
const int kBrightnessThrottleMs = 100;
const int kHardwarePollMs = 3000;
const int kStateSaveDelayMs = 750;
const char *kSecondaryService = "als-dimmer-pwm.service";
const char *kStateDirPath = "/var/lib/disp-settings";
const char *kStateFilePath = "/var/lib/disp-settings/dual-display-mode.json";
const char *kSessionStateFilePath = "/tmp/disp-settings-dual-display-mode.json";
const char *kUsbDevicesPath = "/sys/bus/usb/devices";
const char *kI2cTinyUsbVendor = "0403";
const char *kI2cTinyUsbProduct = "c631";

struct DualDisplayStateFile {
    QString restoreMode;
    int restoreBrightness = 50;
    double lastNits = 0.0;
    bool enabled = false;
    qint64 modifiedMsecs = 0;
};

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

bool readDualDisplayStateFile(const QString &path, DualDisplayStateFile *state)
{
    QFile file(path);
    if (!file.exists()) {
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to open state file"
                   << path << file.errorString();
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "DualDisplayAbsoluteController: Invalid state file"
                   << path << parseError.errorString();
        return false;
    }

    QJsonObject object = document.object();
    state->enabled = object.value("enabled").toBool(true);
    state->restoreMode = object.value("restore_primary_mode").toString("manual");
    state->restoreBrightness =
        qBound(0, object.value("restore_primary_brightness").toInt(50), 100);

    QJsonValue lastNitsValue = object.value("last_nits");
    if (!lastNitsValue.isDouble() || !qIsFinite(lastNitsValue.toDouble())) {
        qWarning() << "DualDisplayAbsoluteController: State file missing last_nits"
                   << path;
        return false;
    }

    state->lastNits = lastNitsValue.toDouble();
    state->modifiedMsecs = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    return true;
}

bool writeDualDisplayStateFile(const QString &path, const QJsonObject &object)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to write state file"
                   << path << file.errorString();
        return false;
    }

    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.write("\n");
    if (!file.commit()) {
        qWarning() << "DualDisplayAbsoluteController: Failed to commit state file"
                   << path << file.errorString();
        return false;
    }

    return true;
}

bool blockingRunServiceAction(const QString &action, QString *error)
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

bool blockingCallJson(int port, const QString &command,
                      const QJsonObject &params, QJsonObject *data, QString *error)
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

bool blockingSetMode(int port, const QString &mode, QString *error)
{
    QJsonObject params;
    params["mode"] = mode;
    return blockingCallJson(port, "set_mode", params, nullptr, error);
}

bool blockingSetRelativeBrightness(int port, int brightness, QString *error)
{
    QJsonObject params;
    params["brightness"] = qBound(0, brightness, 100);
    return blockingCallJson(port, "set_brightness", params, nullptr, error);
}

bool blockingRestorePrimary(const QString &mode, int brightness, bool hasRestoreState,
                            QString *error)
{
    if (!hasRestoreState) {
        return true;
    }

    QString restoreMode = mode.toLower();
    if (restoreMode == "auto") {
        return blockingSetMode(kPrimaryPort, "auto", error);
    }

    if (restoreMode == "manual_temporary") {
        if (!blockingSetMode(kPrimaryPort, "auto", error)) {
            return false;
        }
        return blockingSetRelativeBrightness(kPrimaryPort, brightness, error);
    }

    if (!blockingSetMode(kPrimaryPort, "manual", error)) {
        return false;
    }
    return blockingSetRelativeBrightness(kPrimaryPort, brightness, error);
}

bool blockingReadMaxBrightness(int port, double *maxNits, QString *error)
{
    QJsonObject data;
    if (!blockingCallJson(port, "get_calibration_info", QJsonObject(), &data, error)) {
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

bool blockingSendAbsoluteBrightness(int port, double nits, QString *error)
{
    QJsonObject params;
    params["nits"] = nits;
    return blockingCallJson(port, "set_absolute_brightness", params, nullptr, error);
}

bool blockingSendAbsoluteBrightnessToBoth(double nits, QString *error)
{
    return blockingSendAbsoluteBrightness(kPrimaryPort, nits, error) &&
           blockingSendAbsoluteBrightness(kSecondaryPort, nits, error);
}

double roundedNitsForRange(double nits, double minNits, double maxNits, double step)
{
    double lowerBound = qMin(minNits, maxNits);
    double clamped = qBound(lowerBound, nits, maxNits);
    double rounded = qRound(clamped / step) * step;
    return qBound(lowerBound, rounded, maxNits);
}

struct ModeOperationResult
{
    bool enable = false;
    bool success = false;
    QString error;
    double sharedMax = 0.0;
    double nits = 0.0;
    QString restoreMode;
    int restoreBrightness = 50;
    bool hasRestoreState = false;
};

ModeOperationResult runEnableOperation(QString primaryMode, int primaryBrightness,
                                       double minNits, double nitsStep)
{
    ModeOperationResult result;
    result.enable = true;
    result.restoreMode = primaryMode.isEmpty() ? "manual" : primaryMode;
    result.restoreBrightness = qBound(0, primaryBrightness, 100);
    result.hasRestoreState = true;

    QString error;
    if (!blockingRunServiceAction("start", &error)) {
        result.error = error;
        return result;
    }

    if (!blockingSetMode(kPrimaryPort, "manual", &error) ||
        !blockingSetMode(kSecondaryPort, "manual", &error)) {
        blockingRunServiceAction("stop", nullptr);
        blockingRestorePrimary(result.restoreMode, result.restoreBrightness,
                               result.hasRestoreState, nullptr);
        result.error = error;
        return result;
    }

    double primaryMax = 0.0;
    double secondaryMax = 0.0;
    if (!blockingReadMaxBrightness(kPrimaryPort, &primaryMax, &error) ||
        !blockingReadMaxBrightness(kSecondaryPort, &secondaryMax, &error)) {
        blockingRunServiceAction("stop", nullptr);
        blockingRestorePrimary(result.restoreMode, result.restoreBrightness,
                               result.hasRestoreState, nullptr);
        result.error = error;
        return result;
    }

    double sharedMax = std::min(primaryMax, secondaryMax);
    if (!qIsFinite(sharedMax) || sharedMax <= 0.0) {
        blockingRunServiceAction("stop", nullptr);
        blockingRestorePrimary(result.restoreMode, result.restoreBrightness,
                               result.hasRestoreState, nullptr);
        result.error = QString("Invalid shared max brightness: %1").arg(sharedMax);
        return result;
    }

    double initialNits = roundedNitsForRange(sharedMax / 2.0, minNits, sharedMax, nitsStep);
    if (!blockingSendAbsoluteBrightnessToBoth(initialNits, &error)) {
        blockingRunServiceAction("stop", nullptr);
        blockingRestorePrimary(result.restoreMode, result.restoreBrightness,
                               result.hasRestoreState, nullptr);
        result.error = error;
        return result;
    }

    result.success = true;
    result.sharedMax = sharedMax;
    result.nits = initialNits;
    return result;
}

ModeOperationResult runDisableOperation(QString restoreMode, int restoreBrightness,
                                        bool hasRestoreState)
{
    ModeOperationResult result;
    result.enable = false;
    result.success = true;
    result.restoreMode = restoreMode;
    result.restoreBrightness = restoreBrightness;
    result.hasRestoreState = hasRestoreState;

    QStringList errors;
    QString error;
    if (!blockingRestorePrimary(restoreMode, restoreBrightness, hasRestoreState, &error)) {
        errors << error;
    }
    if (!blockingRunServiceAction("stop", &error)) {
        errors << error;
    }

    result.error = errors.join("; ");
    return result;
}
}

DualDisplayAbsoluteController::DualDisplayAbsoluteController(QObject *parent)
    : QObject(parent)
    , m_active(false)
    , m_targetActive(false)
    , m_hardwareAvailable(false)
    , m_busy(false)
    , m_loadingSavedState(false)
    , m_maxNits(1000.0)
    , m_currentNits(0.0)
    , m_pendingNits(0.0)
    , m_minNits(20.0)
    , m_nitsStep(5.0)
    , m_brightnessUpdatePending(false)
    , m_statusText("Dual absolute mode off")
    , m_restorePrimaryBrightness(50)
    , m_hasRestoreState(false)
    , m_brightnessThrottle(new QTimer(this))
    , m_hardwarePollTimer(new QTimer(this))
    , m_stateSaveTimer(new QTimer(this))
    , m_asyncTimeout(new QTimer(this))
    , m_asyncSocket(nullptr)
    , m_sendInFlight(false)
    , m_exitFlushed(false)
    , m_asyncPort(0)
    , m_inFlightNits(0.0)
{
    QString restoreMode;
    int restoreBrightness = 50;
    double savedNits = 0.0;
    bool stateEnabled = false;
    m_loadingSavedState =
        loadStateFile(&restoreMode, &restoreBrightness, &savedNits, &stateEnabled) &&
        stateEnabled;

    m_brightnessThrottle->setInterval(kBrightnessThrottleMs);
    m_brightnessThrottle->setSingleShot(true);
    connect(m_brightnessThrottle, &QTimer::timeout,
            this, &DualDisplayAbsoluteController::processBrightnessUpdate);

    m_hardwarePollTimer->setInterval(kHardwarePollMs);
    connect(m_hardwarePollTimer, &QTimer::timeout,
            this, &DualDisplayAbsoluteController::refreshHardwareAvailability);
    refreshHardwareAvailability();
    m_hardwarePollTimer->start();

    m_stateSaveTimer->setInterval(kStateSaveDelayMs);
    m_stateSaveTimer->setSingleShot(true);
    connect(m_stateSaveTimer, &QTimer::timeout,
            this, &DualDisplayAbsoluteController::flushStateSave);

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
    if (m_busy) {
        return;
    }

    if (enabled && !m_hardwareAvailable) {
        reportError("Dual display PWM controller not detected");
        setTargetActive(false);
        return;
    }

    if (enabled == m_active) {
        setTargetActive(m_active);
        return;
    }

    setTargetActive(enabled);
    setBusy(true);

    if (enabled) {
        setStatusText("Starting dual absolute mode...");
    } else {
        setStatusText("Stopping dual absolute mode...");
        m_brightnessThrottle->stop();
        m_stateSaveTimer->stop();
        closeAsyncSocket();
        m_sendInFlight = false;
        m_brightnessUpdatePending = false;
    }

    QFutureWatcher<ModeOperationResult> *watcher =
        new QFutureWatcher<ModeOperationResult>(this);
    connect(watcher, &QFutureWatcher<ModeOperationResult>::finished, this,
            [this, watcher]() {
                ModeOperationResult result = watcher->result();
                watcher->deleteLater();

                if (result.enable) {
                    if (!result.success) {
                        reportError(result.error);
                        clearStateFile();
                        m_hasRestoreState = false;
                        setTargetActive(m_active);
                        setBusy(false);
                        return;
                    }

                    m_restorePrimaryMode = result.restoreMode;
                    m_restorePrimaryBrightness = result.restoreBrightness;
                    m_hasRestoreState = result.hasRestoreState;
                    setMaxNits(result.sharedMax);
                    m_pendingNits = result.nits;
                    m_brightnessUpdatePending = false;
                    setEnabledState(true);
                    setTargetActive(true);
                    setCurrentNits(result.nits);
                    setStatusText(QString("Dual absolute mode: 0-%1 nits")
                                      .arg(qRound(result.sharedMax)));
                    saveStateFile();
                    m_exitFlushed = false;
                    setBusy(false);
                    return;
                }

                if (!result.error.isEmpty()) {
                    reportError(result.error);
                }
                m_hasRestoreState = false;
                setEnabledState(false);
                setTargetActive(false);
                clearStateFile();
                setStatusText("Dual absolute mode off");
                m_exitFlushed = true;
                setBusy(false);
            });

    if (enabled) {
        watcher->setFuture(QtConcurrent::run(runEnableOperation,
                                             primaryMode,
                                             primaryBrightness,
                                             m_minNits,
                                             m_nitsStep));
    } else {
        watcher->setFuture(QtConcurrent::run(runDisableOperation,
                                             m_restorePrimaryMode,
                                             m_restorePrimaryBrightness,
                                             m_hasRestoreState));
    }
}

void DualDisplayAbsoluteController::cleanup()
{
    m_brightnessThrottle->stop();
    m_stateSaveTimer->stop();
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
        saveStateFile();
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

    m_pendingNits = initialNits;
    m_brightnessUpdatePending = false;
    setEnabledState(true);
    setTargetActive(true);
    setCurrentNits(initialNits);
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
    m_stateSaveTimer->stop();
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
    setTargetActive(false);
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

bool DualDisplayAbsoluteController::loadStateFile(QString *restoreMode, int *restoreBrightness,
                                                  double *lastNits, bool *enabled) const
{
    DualDisplayStateFile persistentState;
    DualDisplayStateFile sessionState;
    bool hasPersistentState = readDualDisplayStateFile(kStateFilePath, &persistentState);
    bool hasSessionState = readDualDisplayStateFile(kSessionStateFilePath, &sessionState);

    if (!hasPersistentState && !hasSessionState) {
        return false;
    }

    const DualDisplayStateFile *selectedState = &persistentState;
    if (hasSessionState &&
        (!hasPersistentState ||
         sessionState.modifiedMsecs > persistentState.modifiedMsecs)) {
        selectedState = &sessionState;
        qWarning() << "DualDisplayAbsoluteController: Using session dual-display state"
                   << kSessionStateFilePath;
    }

    *enabled = selectedState->enabled;
    *restoreMode = selectedState->restoreMode;
    *restoreBrightness = selectedState->restoreBrightness;
    *lastNits = selectedState->lastNits;
    return true;
}

bool DualDisplayAbsoluteController::ensureStateDirectory() const
{
    QDir dir;
    if (dir.exists(kStateDirPath)) {
        return true;
    }
    if (!dir.mkpath(kStateDirPath)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to create state directory"
                   << kStateDirPath;
        return false;
    }
    return true;
}

bool DualDisplayAbsoluteController::saveStateFile() const
{
    if (!m_active) {
        return false;
    }

    QJsonObject object;
    object["version"] = 1;
    object["enabled"] = true;
    object["last_nits"] = m_pendingNits;
    object["min_nits"] = m_minNits;
    object["restore_primary_mode"] = m_restorePrimaryMode;
    object["restore_primary_brightness"] = m_restorePrimaryBrightness;

    if (ensureStateDirectory() && writeDualDisplayStateFile(kStateFilePath, object)) {
        if (QFile::exists(kSessionStateFilePath) && !QFile::remove(kSessionStateFilePath)) {
            qWarning() << "DualDisplayAbsoluteController: Failed to remove session state file"
                       << kSessionStateFilePath;
        }
        return true;
    }

    if (writeDualDisplayStateFile(kSessionStateFilePath, object)) {
        qWarning() << "DualDisplayAbsoluteController: Persistent state unavailable;"
                   << "saved session fallback" << kSessionStateFilePath;
        return true;
    }

    return false;
}

void DualDisplayAbsoluteController::scheduleStateSave()
{
    if (m_active) {
        m_stateSaveTimer->start();
    }
}

void DualDisplayAbsoluteController::flushStateSave()
{
    saveStateFile();
}

void DualDisplayAbsoluteController::clearStateFile() const
{
    bool persistentRemoveFailed = false;
    if (QFile::exists(kStateFilePath) && !QFile::remove(kStateFilePath)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to remove state file"
                   << kStateFilePath;
        persistentRemoveFailed = true;
    }

    if (persistentRemoveFailed) {
        QJsonObject object;
        object["version"] = 1;
        object["enabled"] = false;
        object["last_nits"] = m_pendingNits;
        object["min_nits"] = m_minNits;
        object["restore_primary_mode"] = m_restorePrimaryMode;
        object["restore_primary_brightness"] = m_restorePrimaryBrightness;
        if (!writeDualDisplayStateFile(kSessionStateFilePath, object)) {
            qWarning() << "DualDisplayAbsoluteController: Failed to write disabled session state"
                       << kSessionStateFilePath;
        }
        return;
    }

    if (QFile::exists(kSessionStateFilePath) && !QFile::remove(kSessionStateFilePath)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to remove state file"
                   << kSessionStateFilePath;
    }
}

bool DualDisplayAbsoluteController::detectHardwareAvailability() const
{
    QDir usbDevices(kUsbDevicesPath);
    if (!usbDevices.exists()) {
        return false;
    }

    const QFileInfoList entries = usbDevices.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        QFile vendorFile(entry.filePath() + "/idVendor");
        QFile productFile(entry.filePath() + "/idProduct");
        if (!vendorFile.open(QIODevice::ReadOnly | QIODevice::Text) ||
            !productFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QString vendor = QString::fromLatin1(vendorFile.readAll()).trimmed().toLower();
        QString product = QString::fromLatin1(productFile.readAll()).trimmed().toLower();
        if (vendor == QLatin1String(kI2cTinyUsbVendor) &&
            product == QLatin1String(kI2cTinyUsbProduct)) {
            return true;
        }
    }

    return false;
}

void DualDisplayAbsoluteController::refreshHardwareAvailability()
{
    bool available = detectHardwareAvailability();
    bool wasAvailable = m_hardwareAvailable;
    setHardwareAvailable(available);
    if (available && !wasAvailable && !m_active && !m_busy) {
        QTimer::singleShot(0, this, &DualDisplayAbsoluteController::adoptSavedState);
    }
}

void DualDisplayAbsoluteController::adoptSavedState()
{
    if (m_active || m_busy) {
        return;
    }

    QString restoreMode;
    int restoreBrightness = 50;
    double savedNits = 0.0;
    bool stateEnabled = false;
    if (!loadStateFile(&restoreMode, &restoreBrightness, &savedNits, &stateEnabled) ||
        !stateEnabled) {
        setLoadingSavedState(false);
        return;
    }

    if (!m_hardwareAvailable) {
        qWarning() << "DualDisplayAbsoluteController: Saved dual mode skipped:"
                   << "i2c-tiny-usb controller not detected";
        setLoadingSavedState(false);
        return;
    }

    setLoadingSavedState(true);
    setBusy(true);
    setStatusText("Restoring dual display mode...");

    QString error;
    if (!runServiceAction("start", &error)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to start saved dual service:" << error;
        setStatusText("Dual absolute mode off");
        setLoadingSavedState(false);
        setBusy(false);
        return;
    }

    if (!setMode(kPrimaryPort, "manual", &error) ||
        !setMode(kSecondaryPort, "manual", &error)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to set saved dual mode:" << error;
        setStatusText("Dual absolute mode off");
        setLoadingSavedState(false);
        setBusy(false);
        return;
    }

    QJsonObject primaryStatus;
    QJsonObject secondaryStatus;
    if (!readStatus(kPrimaryPort, &primaryStatus, &error) ||
        !readStatus(kSecondaryPort, &secondaryStatus, &error)) {
        qWarning() << "DualDisplayAbsoluteController: Saved dual mode not active:" << error;
        setStatusText("Dual absolute mode off");
        setLoadingSavedState(false);
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
        setLoadingSavedState(false);
        setBusy(false);
        return;
    }

    double primaryMax = 0.0;
    double secondaryMax = 0.0;
    if (!readMaxBrightness(kPrimaryPort, &primaryMax, &error) ||
        !readMaxBrightness(kSecondaryPort, &secondaryMax, &error)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to read saved dual range:" << error;
        setStatusText("Dual absolute mode off");
        setLoadingSavedState(false);
        setBusy(false);
        return;
    }

    double sharedMax = std::min(primaryMax, secondaryMax);
    if (!qIsFinite(sharedMax) || sharedMax <= 0.0) {
        qWarning() << "DualDisplayAbsoluteController: Invalid saved dual range" << sharedMax;
        setStatusText("Dual absolute mode off");
        setLoadingSavedState(false);
        setBusy(false);
        return;
    }

    m_restorePrimaryMode = restoreMode.isEmpty() ? "manual" : restoreMode;
    m_restorePrimaryBrightness = qBound(0, restoreBrightness, 100);
    m_hasRestoreState = true;

    setMaxNits(sharedMax);
    double rounded = roundedNits(savedNits);
    if (!sendAbsoluteBrightnessToBoth(rounded, &error)) {
        qWarning() << "DualDisplayAbsoluteController: Failed to restore saved dual brightness:"
                   << error;
        setStatusText("Dual absolute mode off");
        setLoadingSavedState(false);
        setBusy(false);
        return;
    }

    m_pendingNits = rounded;
    m_brightnessUpdatePending = false;
    setEnabledState(true);
    setTargetActive(true);
    setCurrentNits(rounded);
    setStatusText(QString("Dual absolute mode: 0-%1 nits").arg(qRound(sharedMax)));
    saveStateFile();
    m_exitFlushed = false;
    setLoadingSavedState(false);
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
    scheduleStateSave();
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
    double lowerBound = qMin(m_minNits, m_maxNits);
    double clamped = qBound(lowerBound, nits, m_maxNits);
    double rounded = qRound(clamped / m_nitsStep) * m_nitsStep;
    return qBound(lowerBound, rounded, m_maxNits);
}

void DualDisplayAbsoluteController::setEnabledState(bool enabled)
{
    if (m_active == enabled) {
        return;
    }

    m_active = enabled;
    emit activeChanged();
}

void DualDisplayAbsoluteController::setTargetActive(bool active)
{
    if (m_targetActive == active) {
        return;
    }

    m_targetActive = active;
    emit targetActiveChanged();
}

void DualDisplayAbsoluteController::setHardwareAvailable(bool available)
{
    if (m_hardwareAvailable == available) {
        return;
    }

    m_hardwareAvailable = available;
    emit hardwareAvailableChanged();
}

void DualDisplayAbsoluteController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void DualDisplayAbsoluteController::setLoadingSavedState(bool loading)
{
    if (m_loadingSavedState == loading) {
        return;
    }

    m_loadingSavedState = loading;
    emit loadingSavedStateChanged();
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
