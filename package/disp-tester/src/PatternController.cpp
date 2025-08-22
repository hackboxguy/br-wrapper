#include "PatternController.h"
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QRegExp>

PatternController::PatternController(QObject *parent)
    : QObject(parent)
    , m_currentIndex(0)
    , m_customColor(0, 0, 0)
    , m_showCustomColor(false)
    , m_networkInterface(nullptr)
    , m_metadataStatus("autohide")  // Default to autohide behavior
    , m_metadataText("")           // Empty = use default IP:port display
    , m_metadataAlign("center")    // Default center alignment
    , m_metadataFontSize(16)       // Default font size
    , m_metadataColor(255, 255, 255) // Default white color
    , m_userInteractionEnabled(true) // Default user interaction enabled
{
    // Initialize available patterns (added solid colors, removed white-text-black)
    m_patterns << "grayscale-ramp" << "ansi-checker" << "colorbar" << "white" << "black"
               << "red" << "green" << "blue" << "cyan" << "magenta" << "yellow"
               << "zone-boundary-grid" << "blooming-detection" << "cross-dimming";
    m_currentPattern = m_patterns[0];
}

PatternController::~PatternController()
{
    if (m_networkInterface) {
        delete m_networkInterface;
    }
}

bool PatternController::startNetworkInterface(int port)
{
    m_networkInterface = new NetworkInterface(port, this);
    connect(m_networkInterface, &NetworkInterface::commandReceived,
            this, &PatternController::handleNetworkCommand);

    return m_networkInterface->startServer();
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

    // Get port from network interface
    int port = 8080; // default
    if (m_networkInterface) {
        // We could store the port in NetworkInterface class, but for now use default
        port = 8080;
    }

    return QString("TCP:%1:%2").arg(ipAddress).arg(port);
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
        QGuiApplication::quit();
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
    }
    return "";
}

void PatternController::resetParameters()
{
    m_parameters = PatternParameters(); // Reset to defaults
    emit patternParamsChanged();
    qDebug() << "Parameters reset to defaults";
}
