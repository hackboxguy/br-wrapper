#include "PatternController.h"
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QFile>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QProcess>
#include <QRegExp>
#include <QJsonDocument>
#include <QJsonObject>

PatternController::PatternController(QObject *parent)
    : QObject(parent)
    , m_currentIndex(0)
    , m_customColor(0, 0, 0)
    , m_showCustomColor(false)
    , m_networkInterface(nullptr)
    , m_childProcess(nullptr)
    , m_childShutdownTimer(new QTimer(this))
    , m_networkPort(DEFAULT_NETWORK_PORT)
    , m_shutdownRequested(false)
    , m_metadataStatus("autohide")  // Default to autohide behavior
    , m_metadataText("")           // Empty = use default IP:port display
    , m_metadataAlign("center")    // Default center alignment
    , m_metadataFontSize(16)       // Default font size
    , m_metadataColor(255, 255, 255) // Default white color
    , m_userInteractionEnabled(true) // Default user interaction enabled
    , m_patternNavigationEnabled(true)
    , m_uiAutoHideEnabled(true)
    , m_navigationHelpVisible(true)
    , m_childActionButtonVisible(false)
    , m_childActionActive(false)
    , m_exitDisabledWhileChildActionActive(false)
    , m_childActionStartText("Start Recording")
    , m_childActionStopText("Stop Recording")
    , m_childActionStartColor(0, 128, 0)
    , m_childActionStopColor(192, 0, 0)
{
    m_childShutdownTimer->setSingleShot(true);
    m_childShutdownTimer->setInterval(3000);
    connect(m_childShutdownTimer, &QTimer::timeout,
            this, &PatternController::forceStopChildScript);

    // Initialize available patterns (added solid colors, removed white-text-black)
    m_patterns << "grayscale-ramp" << "ansi-checker" << "colorbar" << "white" << "black"
               << "red" << "green" << "blue" << "cyan" << "magenta" << "yellow"
               << "zone-boundary-grid" << "blooming-detection" << "cross-dimming" << "whitebox"
               << "whiteboxmm";
    m_currentPattern = m_patterns[0];
}

PatternController::~PatternController()
{
    if (m_childProcess && m_childProcess->state() != QProcess::NotRunning) {
        qWarning() << "Child script still running during shutdown; killing it";
        m_childProcess->kill();
        m_childProcess->waitForFinished(1000);
    }

    if (m_networkInterface) {
        delete m_networkInterface;
    }
}

bool PatternController::startNetworkInterface(int port)
{
    m_networkPort = port;
    m_networkInterface = new NetworkInterface(port, this);
    connect(m_networkInterface, &NetworkInterface::commandReceived,
            this, &PatternController::handleNetworkCommand);

    return m_networkInterface->startServer();
}

bool PatternController::startChildScript(const QString &program, const QStringList &arguments)
{
    if (program.isEmpty()) {
        qWarning() << "No child script specified";
        return false;
    }

    if (isChildScriptRunning()) {
        qWarning() << "Child script is already running";
        return false;
    }

    cleanupChildProcess();

    m_childProcess = new QProcess(this);
    m_childProcess->setProcessChannelMode(QProcess::ForwardedChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("DISP_TESTER_HOST", "127.0.0.1");
    env.insert("DISP_TESTER_PORT", QString::number(m_networkPort));
    m_childProcess->setProcessEnvironment(env);

    connect(m_childProcess, &QProcess::started, this, [this, program]() {
        qDebug() << "Child script started:" << program << "pid:" << m_childProcess->processId();
    });

    connect(m_childProcess,
            static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &PatternController::handleChildFinished);

    connect(m_childProcess, &QProcess::errorOccurred,
            this, &PatternController::handleChildError);

    qDebug() << "Starting child script:" << program << "args:" << arguments;
    m_childProcess->start(program, arguments);
    return true;
}

void PatternController::configureChildActionButton(bool visible, const QString &startText, const QString &stopText,
                                                   const QColor &startColor, const QColor &stopColor)
{
    m_childActionButtonVisible = visible;
    if (!startText.isEmpty()) {
        m_childActionStartText = startText;
    }
    if (!stopText.isEmpty()) {
        m_childActionStopText = stopText;
    }
    if (startColor.isValid()) {
        m_childActionStartColor = startColor;
    }
    if (stopColor.isValid()) {
        m_childActionStopColor = stopColor;
    }
    setChildActionActive(false);
    emit childActionButtonChanged();
}

void PatternController::setPatternNavigationEnabled(bool enabled)
{
    if (m_patternNavigationEnabled == enabled) {
        return;
    }

    m_patternNavigationEnabled = enabled;
    emit patternNavigationEnabledChanged();
    qDebug() << "Pattern navigation" << (enabled ? "enabled" : "disabled");
}

void PatternController::setUiAutoHideEnabled(bool enabled)
{
    if (m_uiAutoHideEnabled == enabled) {
        return;
    }

    m_uiAutoHideEnabled = enabled;
    emit uiAutoHideEnabledChanged();
    qDebug() << "UI auto-hide" << (enabled ? "enabled" : "disabled");
}

void PatternController::setNavigationHelpVisible(bool visible)
{
    if (m_navigationHelpVisible == visible) {
        return;
    }

    m_navigationHelpVisible = visible;
    emit navigationHelpVisibleChanged();
    qDebug() << "Navigation help" << (visible ? "visible" : "hidden");
}

void PatternController::setExitDisabledWhileChildActionActive(bool enabled)
{
    if (m_exitDisabledWhileChildActionActive == enabled) {
        return;
    }

    m_exitDisabledWhileChildActionActive = enabled;
    emit exitBehaviorChanged();
    qDebug() << "Exit while child action is active" << (enabled ? "disabled" : "enabled");
}

void PatternController::configureStartupMetadata(const QString &status, const QString &text,
                                                 const QString &align, int fontSize,
                                                 const QColor &color)
{
    if (!status.isEmpty()) {
        setMetadataStatus(status);
    }
    if (!align.isEmpty()) {
        setMetadataAlign(align);
    }
    if (fontSize > 0) {
        setMetadataFontSize(fontSize);
    }
    if (color.isValid()) {
        setMetadataColor(color);
    }
    if (!text.isNull()) {
        setMetadataText(text);
    }
}

bool PatternController::isChildScriptRunning() const
{
    return m_childProcess && m_childProcess->state() != QProcess::NotRunning;
}

void PatternController::requestQuit()
{
    requestQuit("ui");
}

void PatternController::requestQuit(const QString &reason)
{
    if (m_shutdownRequested) {
        return;
    }

    m_shutdownRequested = true;
    qDebug() << "Graceful shutdown requested"
             << (reason.isEmpty() ? QString() : QString("(%1)").arg(reason));

    if (isChildScriptRunning()) {
        stopChildScript();
        return;
    }

    finishApplicationQuit();
}

void PatternController::stopChildScript()
{
    if (!isChildScriptRunning()) {
        finishApplicationQuit();
        return;
    }

    qDebug() << "Terminating child script";
    m_childProcess->terminate();
    m_childShutdownTimer->start();
}

void PatternController::forceStopChildScript()
{
    if (!isChildScriptRunning()) {
        return;
    }

    qWarning() << "Child script did not exit after SIGTERM; killing it";
    m_childProcess->kill();
}

void PatternController::handleChildFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug() << "Child script finished with exit code" << exitCode
             << "status" << exitStatus;

    if (m_childShutdownTimer->isActive()) {
        m_childShutdownTimer->stop();
    }

    cleanupChildProcess();

    if (m_shutdownRequested) {
        finishApplicationQuit();
    }
}

void PatternController::handleChildError(QProcess::ProcessError error)
{
    qWarning() << "Child script process error:" << error
               << (m_childProcess ? m_childProcess->errorString() : QString());

    if (error == QProcess::FailedToStart) {
        if (m_childShutdownTimer->isActive()) {
            m_childShutdownTimer->stop();
        }
        cleanupChildProcess();

        if (m_shutdownRequested) {
            finishApplicationQuit();
        }
    }
}

void PatternController::cleanupChildProcess()
{
    setChildActionActive(false);

    if (!m_childProcess) {
        return;
    }

    QProcess *process = m_childProcess;
    m_childProcess = nullptr;
    process->deleteLater();
}

void PatternController::finishApplicationQuit()
{
    qDebug() << "Exiting disp-tester";
    QGuiApplication::quit();
}

void PatternController::setChildActionActive(bool active)
{
    if (m_childActionActive == active) {
        return;
    }

    m_childActionActive = active;
    emit childActionStateChanged();
}

bool PatternController::sendChildControlCommand(const QString &command, bool enabled)
{
    if (!isChildScriptRunning()) {
        qWarning() << "Cannot send child control command; child script is not running";
        return false;
    }

    QJsonObject message;
    message["command"] = command;
    message["enabled"] = enabled;

    QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    payload.append('\n');

    qint64 written = m_childProcess->write(payload);
    if (written != payload.size()) {
        qWarning() << "Failed to write child control command"
                   << command << "enabled" << enabled;
        return false;
    }

    qDebug() << "Sent child control command:" << payload.trimmed();
    return true;
}

void PatternController::toggleChildAction()
{
    if (!m_childActionButtonVisible) {
        return;
    }

    bool nextState = !m_childActionActive;
    if (sendChildControlCommand("set_recording", nextState)) {
        setChildActionActive(nextState);
    }
}

void PatternController::nextPattern()
{
    // Move to next pattern
    m_currentIndex = (m_currentIndex + 1) % m_patterns.size();
    m_currentPattern = m_patterns[m_currentIndex];

    // Handle solid color patterns
    QStringList solidColors = {"white", "black", "red", "green", "blue", "cyan", "magenta", "yellow"};
    if (solidColors.contains(m_currentPattern)) {
        QColor color;
        if (m_currentPattern == "white") color = QColor(255, 255, 255);
        else if (m_currentPattern == "black") color = QColor(0, 0, 0);
        else if (m_currentPattern == "red") color = QColor(255, 0, 0);
        else if (m_currentPattern == "green") color = QColor(0, 255, 0);
        else if (m_currentPattern == "blue") color = QColor(0, 0, 255);
        else if (m_currentPattern == "cyan") color = QColor(0, 255, 255);
        else if (m_currentPattern == "magenta") color = QColor(255, 0, 255);
        else if (m_currentPattern == "yellow") color = QColor(255, 255, 0);

        m_customColor = color;
        m_showCustomColor = true;

        qDebug() << "Touch: switched to solid color:" << m_currentPattern;
    } else {
        m_showCustomColor = false;
        qDebug() << "Touch: switched to pattern:" << m_currentPattern;
    }

    emit currentPatternChanged();
    emit showCustomColorChanged();
    emit customColorChanged();
}

void PatternController::previousPattern()
{
    // Move to previous pattern
    m_currentIndex = m_currentIndex > 0 ? m_currentIndex - 1 : m_patterns.size() - 1;
    m_currentPattern = m_patterns[m_currentIndex];

    // Handle solid color patterns
    QStringList solidColors = {"white", "black", "red", "green", "blue", "cyan", "magenta", "yellow"};
    if (solidColors.contains(m_currentPattern)) {
        QColor color;
        if (m_currentPattern == "white") color = QColor(255, 255, 255);
        else if (m_currentPattern == "black") color = QColor(0, 0, 0);
        else if (m_currentPattern == "red") color = QColor(255, 0, 0);
        else if (m_currentPattern == "green") color = QColor(0, 255, 0);
        else if (m_currentPattern == "blue") color = QColor(0, 0, 255);
        else if (m_currentPattern == "cyan") color = QColor(0, 255, 255);
        else if (m_currentPattern == "magenta") color = QColor(255, 0, 255);
        else if (m_currentPattern == "yellow") color = QColor(255, 255, 0);

        m_customColor = color;
        m_showCustomColor = true;

        qDebug() << "Touch: switched to solid color:" << m_currentPattern;
    } else {
        m_showCustomColor = false;
        qDebug() << "Touch: switched to pattern:" << m_currentPattern;
    }

    emit currentPatternChanged();
    emit showCustomColorChanged();
    emit customColorChanged();
}

void PatternController::setPattern(const QString &pattern)
{
    updatePattern(pattern);
}

void PatternController::setCustomColor(int r, int g, int b)
{
    m_customColor = QColor(r, g, b);
    m_showCustomColor = true;
    m_currentPattern = QString("rgb-%1-%2-%3").arg(r).arg(g).arg(b);

    qDebug() << "Custom color set:" << r << g << b;

    emit customColorChanged();
    emit showCustomColorChanged();
    emit currentPatternChanged();
}

QString PatternController::getResolution()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        QSize size = screen->size();
        return QString("%1x%2").arg(size.width()).arg(size.height());
    }
    return "1920x1080"; // fallback
}

QString PatternController::listPatterns()
{
    return m_patterns.join(",");
}

QString PatternController::getNetworkInfo()
{
    // Return custom metadata text if set
    if (!m_metadataText.isEmpty()) {
        return m_metadataText;
    }

    // Otherwise return default IP:port information
    // Try to get the actual IP address of the machine
    QString ipAddress = "127.0.0.1"; // fallback

    // Read network interfaces to find non-loopback IP
    QFile netFile("/proc/net/route");
    if (netFile.open(QIODevice::ReadOnly)) {
        QTextStream stream(&netFile);
        QString line;

        // Skip header line
        stream.readLine();

        // Look for default route (destination 00000000)
        while (stream.readLineInto(&line)) {
            QStringList parts = line.split('\t');
            if (parts.size() >= 2 && parts[1] == "00000000") {
                QString interface = parts[0];

                // Found default interface, now get its IP
                QFile ifaceFile(QString("/sys/class/net/%1/address").arg(interface));
                if (ifaceFile.exists()) {
                    // Try to get IP from ip command output (simple approach)
                    QProcess process;
                    process.start("ip", QStringList() << "addr" << "show" << interface);
                    process.waitForFinished(1000);

                    QString output = process.readAllStandardOutput();
                    QRegExp ipRegex("inet (\\d+\\.\\d+\\.\\d+\\.\\d+)");
                    if (ipRegex.indexIn(output) != -1) {
                        QString foundIp = ipRegex.cap(1);
                        if (foundIp != "127.0.0.1") {
                            ipAddress = foundIp;
                            break;
                        }
                    }
                }
                break;
            }
        }
        netFile.close();
    }

    return QString("TCP:%1:%2").arg(ipAddress).arg(m_networkPort);
}

void PatternController::setMetadataStatus(const QString &status)
{
    QString newStatus = status.toLower();
    if (newStatus == "autohide" || newStatus == "enable" || newStatus == "disable") {
        if (m_metadataStatus != newStatus) {
            m_metadataStatus = newStatus;
            emit metadataStatusChanged();
            qDebug() << "Metadata status changed to:" << m_metadataStatus;
        }
    }
}

void PatternController::setMetadataText(const QString &text)
{
    if (m_metadataText != text) {
        m_metadataText = text;
        emit metadataTextChanged();  // Notify QML to update display
        emit networkInfoChanged();   // Notify that network info display should update
        qDebug() << "Metadata text changed to:" << m_metadataText;
    }
}

QString PatternController::getMetadataText() const
{
    return m_metadataText;
}

void PatternController::clearMetadataText()
{
    if (!m_metadataText.isEmpty()) {
        m_metadataText.clear();
        emit metadataTextChanged();  // Notify QML to update display
        emit networkInfoChanged();   // Notify that network info display should update
        qDebug() << "Metadata text cleared - reverting to default IP:port display";
    }
}

void PatternController::setMetadataAlign(const QString &align)
{
    QString newAlign = align.toLower();
    if (newAlign == "left" || newAlign == "center" || newAlign == "right") {
        if (m_metadataAlign != newAlign) {
            m_metadataAlign = newAlign;
            emit metadataAlignChanged();
            qDebug() << "Metadata alignment changed to:" << m_metadataAlign;
        }
    }
}

void PatternController::setMetadataFontSize(int size)
{
    if (size >= 8 && size <= 48) {
        if (m_metadataFontSize != size) {
            m_metadataFontSize = size;
            emit metadataFontSizeChanged();
            qDebug() << "Metadata font size changed to:" << m_metadataFontSize;
        }
    }
}

void PatternController::setMetadataColor(const QColor &color)
{
    if (m_metadataColor != color) {
        m_metadataColor = color;
        emit metadataColorChanged();
        qDebug() << "Metadata color changed to:" << m_metadataColor.name();
    }
}

void PatternController::setMetadataColor(int r, int g, int b)
{
    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
        QColor newColor(r, g, b);
        setMetadataColor(newColor);
    }
}

void PatternController::setMetadataColorByName(const QString &colorName)
{
    QString name = colorName.toLower();
    QColor color;

    if (name == "white") color = QColor(255, 255, 255);
    else if (name == "black") color = QColor(0, 0, 0);
    else if (name == "red") color = QColor(255, 0, 0);
    else if (name == "green") color = QColor(0, 255, 0);
    else if (name == "blue") color = QColor(0, 0, 255);
    else if (name == "cyan") color = QColor(0, 255, 255);
    else if (name == "magenta") color = QColor(255, 0, 255);
    else if (name == "yellow") color = QColor(255, 255, 0);
    else if (name == "orange") color = QColor(255, 165, 0);
    else if (name == "purple") color = QColor(128, 0, 128);
    else if (name == "gray" || name == "grey") color = QColor(128, 128, 128);
    else {
        return; // Invalid color name
    }

    setMetadataColor(color);
}

void PatternController::setUserInteractionEnabled(bool enabled)
{
    if (m_userInteractionEnabled != enabled) {
        m_userInteractionEnabled = enabled;
        emit userInteractionEnabledChanged();
        qDebug() << "User interaction" << (enabled ? "enabled" : "disabled");
    }
}

void PatternController::handleNetworkCommand(const QString &command)
{
    qDebug() << "Network command received:" << command;

    QStringList parts = command.split(' ');
    if (parts.isEmpty()) {
        m_networkInterface->sendResponse("ERROR: Empty command");
        return;
    }

    QString cmd = parts[0].toLower();

    if (cmd == "pattern" && parts.size() >= 2) {
        QString pattern = parts[1].toLower();

        if (pattern == "rgb" && parts.size() >= 5) {
            bool rOk, gOk, bOk;
            int r = parts[2].toInt(&rOk);
            int g = parts[3].toInt(&gOk);
            int b = parts[4].toInt(&bOk);

            if (rOk && gOk && bOk && r >= 0 && r <= 255 &&
                g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                setCustomColor(r, g, b);
                m_networkInterface->sendResponse("OK");
            } else {
                m_networkInterface->sendResponse("ERROR: Invalid RGB values");
            }
        } else if (m_patterns.contains(pattern)) {
            // Check for pattern parameters
            if (parts.size() > 2) {
                // First update the pattern, then set parameters
                updatePattern(pattern);

                QString param = parts[2].toLower();
                QStringList values = parts.mid(3);
                if (setPatternParameter(pattern, param, values)) {
                    m_networkInterface->sendResponse("OK");
                } else {
                    m_networkInterface->sendResponse("ERROR: Invalid parameter");
                }
            } else {
                updatePattern(pattern);
                m_networkInterface->sendResponse("OK");
            }
        } else {
            m_networkInterface->sendResponse("ERROR: Unknown pattern");
        }
    } else if (cmd == "get-metadata-status") {
        m_networkInterface->sendResponse(m_metadataStatus);
    } else if (cmd == "set-metadata-status" && parts.size() >= 2) {
        QString status = parts[1].toLower();
        if (status == "autohide" || status == "enable" || status == "disable") {
            setMetadataStatus(status);
            m_networkInterface->sendResponse("OK");
        } else {
            m_networkInterface->sendResponse("ERROR: Invalid status (use: autohide, enable, disable)");
        }
    } else if (cmd == "set-metadata-text" && parts.size() >= 2) {
        // Join all parts from index 1 onwards to handle spaces in text
        QStringList textParts = parts.mid(1);
        QString text = textParts.join(" ");

        // Replace literal \n with actual newlines
        text = text.replace("\\n", "\n");

        setMetadataText(text);
        m_networkInterface->sendResponse("OK");
    } else if (cmd == "get-metadata-text") {
        QString text = getMetadataText();
        if (text.isEmpty()) {
            m_networkInterface->sendResponse(""); // Empty response for default IP:port mode
        } else {
            // Replace actual newlines with literal \n for transmission
            QString response = text;
            response = response.replace("\n", "\\n");
            m_networkInterface->sendResponse(response);
        }
    } else if (cmd == "clear-metadata-text") {
        clearMetadataText();
        m_networkInterface->sendResponse("OK");
    } else if (cmd == "set-metadata-align" && parts.size() >= 2) {
        QString align = parts[1].toLower();
        if (align == "left" || align == "center" || align == "right") {
            setMetadataAlign(align);
            m_networkInterface->sendResponse("OK");
        } else {
            m_networkInterface->sendResponse("ERROR: Invalid alignment (use: left, center, right)");
        }
    } else if (cmd == "get-metadata-align") {
        m_networkInterface->sendResponse(m_metadataAlign);
    } else if (cmd == "set-metadata-fontsize" && parts.size() >= 2) {
        bool ok;
        int size = parts[1].toInt(&ok);
        if (ok && size >= 8 && size <= 48) {
            setMetadataFontSize(size);
            m_networkInterface->sendResponse("OK");
        } else {
            m_networkInterface->sendResponse("ERROR: Invalid font size (use: 8-48)");
        }
    } else if (cmd == "get-metadata-fontsize") {
        m_networkInterface->sendResponse(QString::number(m_metadataFontSize));
    } else if (cmd == "set-metadata-color") {
        if (parts.size() >= 4) {
            // RGB format: set-metadata-color 255 128 64
            bool rOk, gOk, bOk;
            int r = parts[1].toInt(&rOk);
            int g = parts[2].toInt(&gOk);
            int b = parts[3].toInt(&bOk);

            if (rOk && gOk && bOk && r >= 0 && r <= 255 &&
                g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                setMetadataColor(r, g, b);
                m_networkInterface->sendResponse("OK");
            } else {
                m_networkInterface->sendResponse("ERROR: Invalid RGB values (use: 0-255)");
            }
        } else if (parts.size() >= 2) {
            // Named color format: set-metadata-color red
            QString colorName = parts[1].toLower();
            QColor originalColor = m_metadataColor;
            setMetadataColorByName(colorName);

            if (m_metadataColor != originalColor) {
                m_networkInterface->sendResponse("OK");
            } else {
                m_networkInterface->sendResponse("ERROR: Invalid color name (use: white, red, green, blue, cyan, magenta, yellow, orange, purple, gray, black)");
            }
        } else {
            m_networkInterface->sendResponse("ERROR: Invalid color format (use: 'colorname' or 'R G B')");
        }
    } else if (cmd == "get-metadata-color") {
        m_networkInterface->sendResponse(QString("%1 %2 %3")
            .arg(m_metadataColor.red())
            .arg(m_metadataColor.green())
            .arg(m_metadataColor.blue()));
    } else if (cmd == "set-user-interaction" && parts.size() >= 2) {
        QString state = parts[1].toLower();
        if (state == "enable" || state == "disable") {
            setUserInteractionEnabled(state == "enable");
            m_networkInterface->sendResponse("OK");
        } else {
            m_networkInterface->sendResponse("ERROR: Invalid state (use: enable, disable)");
        }
    } else if (cmd == "get-user-interaction") {
        m_networkInterface->sendResponse(m_userInteractionEnabled ? "enable" : "disable");
    } else if (cmd == "set-child-action-active" && parts.size() >= 2) {
        QString state = parts[1].toLower();
        if (state == "enable" || state == "active" || state == "on" || state == "true" || state == "1") {
            setChildActionActive(true);
            m_networkInterface->sendResponse("OK");
        } else if (state == "disable" || state == "inactive" || state == "off" || state == "false" || state == "0") {
            setChildActionActive(false);
            m_networkInterface->sendResponse("OK");
        } else {
            m_networkInterface->sendResponse("ERROR: Invalid child action state (use: enable, disable)");
        }
    } else if (cmd == "get-resolution") {
        m_networkInterface->sendResponse(getResolution());
    } else if (cmd == "get-pattern") {
        m_networkInterface->sendResponse(m_currentPattern);
    } else if (cmd == "list-patterns") {
        m_networkInterface->sendResponse(listPatterns());
    } else if (cmd == "get" && parts.size() >= 3) {
        QString pattern = parts[1].toLower();
        QString param = parts[2].toLower();
        QString value = getPatternParameter(pattern, param);
        if (!value.isEmpty()) {
            m_networkInterface->sendResponse(value);
        } else {
            m_networkInterface->sendResponse("ERROR: Invalid parameter");
        }
    } else if (cmd == "reset-params") {
        resetParameters();
        m_networkInterface->sendResponse("OK");
    } else if (cmd == "quit") {
        m_networkInterface->sendResponse("OK");
        QTimer::singleShot(0, this, [this]() {
            requestQuit("network");
        });
    } else {
        m_networkInterface->sendResponse("ERROR: Unknown command");
    }
}

void PatternController::updatePattern(const QString &pattern)
{
    if (m_patterns.contains(pattern)) {
        m_currentPattern = pattern;
        m_currentIndex = m_patterns.indexOf(pattern);

        // Handle solid color patterns for network commands
        QStringList solidColors = {"white", "black", "red", "green", "blue", "cyan", "magenta", "yellow"};
        if (solidColors.contains(pattern)) {
            QColor color;
            if (pattern == "white") color = QColor(255, 255, 255);
            else if (pattern == "black") color = QColor(0, 0, 0);
            else if (pattern == "red") color = QColor(255, 0, 0);
            else if (pattern == "green") color = QColor(0, 255, 0);
            else if (pattern == "blue") color = QColor(0, 0, 255);
            else if (pattern == "cyan") color = QColor(0, 255, 255);
            else if (pattern == "magenta") color = QColor(255, 0, 255);
            else if (pattern == "yellow") color = QColor(255, 255, 0);

            m_customColor = color;
            m_showCustomColor = true;

            qDebug() << "Network: switched to solid color:" << pattern;
        } else {
            m_showCustomColor = false;
            qDebug() << "Network: switched to pattern:" << pattern;
        }

        // Note: Network commands do NOT trigger UI visibility
        // Only emit pattern/color changes, not UI changes
        emit currentPatternChanged();
        emit showCustomColorChanged();
        emit customColorChanged();
    }
}

bool PatternController::setPatternParameter(const QString &pattern, const QString &param, const QStringList &values)
{
    if (values.isEmpty()) return false;

    bool changed = false;

    if (pattern == "moving-ball") {
        if (param == "size" && !values[0].isEmpty()) {
            bool ok;
            int size = values[0].toInt(&ok);
            if (ok && size >= 10 && size <= 200) {
                m_parameters.ballSize = size;
                changed = true;
            }
        } else if (param == "speed" && !values[0].isEmpty()) {
            bool ok;
            int speed = values[0].toInt(&ok);
            if (ok && speed >= 1 && speed <= 10) {
                m_parameters.ballSpeed = speed;
                changed = true;
            }
        } else if (param == "direction" && !values[0].isEmpty()) {
            QString dir = values[0].toLower();
            if (dir == "horizontal" || dir == "vertical" || dir == "diagonal") {
                m_parameters.ballDirection = dir;
                changed = true;
            }
        } else if (param == "pause") {
            m_parameters.ballPaused = true;
            changed = true;
        } else if (param == "resume") {
            m_parameters.ballPaused = false;
            changed = true;
        }
    } else if (pattern == "starfield") {
        if (param == "density" && !values[0].isEmpty()) {
            bool ok;
            int density = values[0].toInt(&ok);
            if (ok && density >= 10 && density <= 1000) {
                m_parameters.starfieldDensity = density;
                changed = true;
            }
        } else if (param == "seed" && !values[0].isEmpty()) {
            bool ok;
            int seed = values[0].toInt(&ok);
            if (ok && seed >= 0) {
                m_parameters.starfieldSeed = seed;
                changed = true;
            }
        }
    } else if (pattern == "zone-boundary-grid") {
        if (param == "spacing" && !values[0].isEmpty()) {
            bool ok;
            int spacing = values[0].toInt(&ok);
            if (ok && spacing >= 20 && spacing <= 500) {
                m_parameters.gridSpacing = spacing;
                changed = true;
            }
        } else if (param == "highlight" && !values[0].isEmpty()) {
            bool ok;
            int zone = values[0].toInt(&ok);
            if (ok && zone >= -1 && zone < (m_parameters.gridZonesX * m_parameters.gridZonesY)) {
                m_parameters.gridHighlight = zone;
                changed = true;
            }
        }
    } else if (pattern == "blooming-detection") {
        if (param == "x" && !values[0].isEmpty()) {
            bool ok;
            int x = values[0].toInt(&ok);
            if (ok && x >= 0) {
                m_parameters.bloomingX = x;
                changed = true;
            }
        } else if (param == "y" && !values[0].isEmpty()) {
            bool ok;
            int y = values[0].toInt(&ok);
            if (ok && y >= 0) {
                m_parameters.bloomingY = y;
                changed = true;
            }
        }
    } else if (pattern == "cross-dimming") {
        if (param == "spots" && !values[0].isEmpty()) {
            bool ok;
            int spots = values[0].toInt(&ok);
            if (ok && spots >= 1 && spots <= 16) {
                m_parameters.crossDimmingSpots = spots;
                changed = true;
            }
        }
    } else if (pattern == "whitebox") {
        // Support multiple sizing modes: percent, pixels, mm
        if ((param == "size" || param == "percent") && !values[0].isEmpty()) {
            // Percent mode: whitebox size 10 OR whitebox percent 10
            bool ok;
            int size = values[0].toInt(&ok);
            if (ok && size >= 1 && size <= 50) {
                m_parameters.whiteboxMode = "percent";
                m_parameters.whiteboxSize = size;
                changed = true;
            }
        } else if (param == "pixels" && !values[0].isEmpty()) {
            // Pixels mode: whitebox pixels 200
            bool ok;
            int pixels = values[0].toInt(&ok);
            if (ok && pixels >= 1 && pixels <= 2000) {
                m_parameters.whiteboxMode = "pixels";
                m_parameters.whiteboxPixels = pixels;
                changed = true;
            }
        } else if (param == "mm" && !values[0].isEmpty()) {
            // MM mode: whitebox mm 50 diagonal-inch 15.6
            bool ok;
            float mm = values[0].toFloat(&ok);
            if (ok && mm >= 1.0f && mm <= 500.0f) {
                m_parameters.whiteboxMode = "mm";
                m_parameters.whiteboxMM = mm;
                changed = true;

                // Check for optional diagonal-inch parameter
                if (values.size() >= 3 && values[1] == "diagonal-inch") {
                    bool diagOk;
                    float diagonal = values[2].toFloat(&diagOk);
                    if (diagOk && diagonal >= 5.0f && diagonal <= 100.0f) {
                        m_parameters.whiteboxDiagonalInch = diagonal;
                    }
                }
            }
        }
    } else if (pattern == "whiteboxmm") {
        // Absolute physical-size box from explicit active-area dimensions.
        // Syntax: whiteboxmm <sizeMM> width-mm <W> height-mm <H>
        //   (an optional leading "size" keyword is also accepted)
        // Example: whiteboxmm 50 width-mm 292 height-mm 109.5
        QStringList tokens;
        tokens << param << values;          // full token list after the pattern name
        if (!tokens.isEmpty() && tokens[0] == "size")
            tokens.removeFirst();           // tolerate the optional "size" keyword

        bool sizeOk = false;
        float sizeMM = tokens.isEmpty() ? 0.0f : tokens[0].toFloat(&sizeOk);
        float physW = 0.0f, physH = 0.0f;
        bool haveW = false, haveH = false;
        for (int i = 1; i + 1 < tokens.size(); ++i) {
            if (tokens[i] == "width-mm") {
                bool ok; float w = tokens[i + 1].toFloat(&ok);
                if (ok) { physW = w; haveW = true; }
            } else if (tokens[i] == "height-mm") {
                bool ok; float h = tokens[i + 1].toFloat(&ok);
                if (ok) { physH = h; haveH = true; }
            }
        }

        if (sizeOk && haveW && haveH &&
            sizeMM >= 1.0f && sizeMM <= 500.0f &&
            physW >= 10.0f && physW <= 2000.0f &&
            physH >= 10.0f && physH <= 2000.0f &&
            sizeMM <= physW && sizeMM <= physH) {
            m_parameters.whiteboxmmSize = sizeMM;
            m_parameters.whiteboxmmPhysWidthMM = physW;
            m_parameters.whiteboxmmPhysHeightMM = physH;
            changed = true;
        }
    }

    if (changed) {
        emit patternParamsChanged();
        qDebug() << "Parameter updated:" << pattern << param << values[0];
    }

    return changed;
}

QString PatternController::getPatternParameter(const QString &pattern, const QString &param)
{
    if (pattern == "moving-ball") {
        if (param == "size") return QString::number(m_parameters.ballSize);
        if (param == "speed") return QString::number(m_parameters.ballSpeed);
        if (param == "direction") return m_parameters.ballDirection;
        if (param == "paused") return m_parameters.ballPaused ? "true" : "false";
    } else if (pattern == "starfield") {
        if (param == "density") return QString::number(m_parameters.starfieldDensity);
        if (param == "seed") return QString::number(m_parameters.starfieldSeed);
    } else if (pattern == "zone-boundary-grid") {
        if (param == "spacing") return QString::number(m_parameters.gridSpacing);
        if (param == "highlight") return QString::number(m_parameters.gridHighlight);
    } else if (pattern == "blooming-detection") {
        if (param == "x") return QString::number(m_parameters.bloomingX);
        if (param == "y") return QString::number(m_parameters.bloomingY);
    } else if (pattern == "cross-dimming") {
        if (param == "spots") return QString::number(m_parameters.crossDimmingSpots);
    } else if (pattern == "whitebox") {
        if (param == "size") return QString::number(m_parameters.whiteboxSize);
        if (param == "mode") return m_parameters.whiteboxMode;
        if (param == "pixels") return QString::number(m_parameters.whiteboxPixels);
        if (param == "mm") return QString::number(m_parameters.whiteboxMM);
        if (param == "diagonal-inch") return QString::number(m_parameters.whiteboxDiagonalInch);
    } else if (pattern == "whiteboxmm") {
        if (param == "size") return QString::number(m_parameters.whiteboxmmSize);
        if (param == "width-mm") return QString::number(m_parameters.whiteboxmmPhysWidthMM);
        if (param == "height-mm") return QString::number(m_parameters.whiteboxmmPhysHeightMM);
    }
    return "";
}

void PatternController::resetParameters()
{
    m_parameters = PatternParameters(); // Reset to defaults
    emit patternParamsChanged();
    qDebug() << "Parameters reset to defaults";
}
