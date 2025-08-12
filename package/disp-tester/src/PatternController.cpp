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
    // Initialize available patterns
    m_patterns << "grayscale-ramp" << "ansi-checker" << "white-text-black";
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
    // Only cycle through predefined patterns (ignore custom color)
    m_currentIndex = (m_currentIndex + 1) % m_patterns.size();
    m_currentPattern = m_patterns[m_currentIndex];
    m_showCustomColor = false;
    
    qDebug() << "Touch: switched to pattern:" << m_currentPattern;
    
    emit currentPatternChanged();
    emit showCustomColorChanged();
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
            updatePattern(pattern);
            m_networkInterface->sendResponse("OK");
        } else {
            m_networkInterface->sendResponse("ERROR: Unknown pattern");
        }
    } else if (cmd == "get-resolution") {
        m_networkInterface->sendResponse(getResolution());
    } else if (cmd == "get-pattern") {
        m_networkInterface->sendResponse(m_currentPattern);
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
        m_showCustomColor = false;
        
        emit currentPatternChanged();
        emit showCustomColorChanged();
    }
}
