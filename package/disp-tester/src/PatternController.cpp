#include "PatternController.h"
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>

PatternController::PatternController(QObject *parent)
    : QObject(parent)
    , m_currentIndex(0)
    , m_customColor(0, 0, 0)
    , m_showCustomColor(false)
    , m_networkInterface(nullptr)
{
    // Initialize available patterns (added solid colors)
    m_patterns << "grayscale-ramp" << "ansi-checker" << "white-text-black" 
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
    
    // If we're at index 0 and just wrapped around from cross-dimming, exit instead
    if (m_currentIndex == 0 && m_currentPattern == "grayscale-ramp") {
        // This means we wrapped around from cross-dimming, so exit
        qDebug() << "Wrapped around from last pattern, exiting to launcher";
        QGuiApplication::quit();
        return;
    }
    
    // Handle solid color patterns
    QStringList solidColors = {"red", "green", "blue", "cyan", "magenta", "yellow"};
    if (solidColors.contains(m_currentPattern)) {
        QColor color;
        if (m_currentPattern == "red") color = QColor(255, 0, 0);
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
        QStringList solidColors = {"red", "green", "blue", "cyan", "magenta", "yellow"};
        if (solidColors.contains(pattern)) {
            QColor color;
            if (pattern == "red") color = QColor(255, 0, 0);
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
