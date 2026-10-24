#include "TddiController.h"
#include "config.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

TddiController::TddiController(QObject *parent)
    : QObject(parent)
    , m_basePath(DEFAULT_TDDI_PATH)
    , m_available(false)
{
}

TddiController::~TddiController()
{
}

void TddiController::setBasePath(const QString &path)
{
    m_basePath = path;
}

void TddiController::start()
{
    qDebug() << "TddiController: Starting with base path" << m_basePath;
    // Read once, on open, and again only when asked (refresh()). Never poll:
    // in the Himax drivers, reading "vendor" re-initialises the chip
    // (power_on_init, a reset), so a 10 s poll reset the OLED-IB's HX8530
    // every 10 s -- touches dropped, and a firmware flash running beside it
    // failed ("Check WEL Fail").
    refresh();
}

QString TddiController::readFile(const QString &filename)
{
    QString path = m_basePath + "/" + filename;
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QTextStream in(&file);
    QString content = in.readAll().trimmed();
    file.close();

    return content;
}

void TddiController::parseVersionInfo(const QString &content)
{
    // Parse Himax TDDI vendor file format:
    // IC = HX83192A
    // FW Architecture Version = 0x80A0
    // FW Display Config Version = D01
    // FW Touch Config Version = C01
    // Customer = Harman_FCA
    // Project = BOEVX_146
    // Panel Version = 0x3A
    // FW Config Date = 20230720

    QStringList lines = content.split('\n');

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();

        // Try to parse "key = value" pairs (Himax format)
        int eqPos = trimmed.indexOf('=');
        if (eqPos > 0) {
            QString key = trimmed.left(eqPos).trimmed().toLower();
            QString value = trimmed.mid(eqPos + 1).trimmed();

            if (key == "ic") {
                if (value != m_icType) {
                    m_icType = value;
                    emit icTypeChanged();
                }
            } else if (key.contains("architecture") && key.contains("version")) {
                if (value != m_fwVersion) {
                    m_fwVersion = value;
                    emit fwVersionChanged();
                }
            } else if (key.contains("display") && key.contains("config")) {
                if (value != m_displayConfig) {
                    m_displayConfig = value;
                    emit displayConfigChanged();
                }
            } else if (key.contains("touch") && key.contains("config")) {
                if (value != m_touchConfig) {
                    m_touchConfig = value;
                    emit touchConfigChanged();
                }
            } else if (key == "customer") {
                if (value != m_customer) {
                    m_customer = value;
                    emit customerChanged();
                }
            } else if (key == "project") {
                if (value != m_project) {
                    m_project = value;
                    emit projectChanged();
                }
            } else if (key.contains("panel") && key.contains("version")) {
                if (value != m_panelVersion) {
                    m_panelVersion = value;
                    emit panelVersionChanged();
                }
            } else if (key.contains("config") && key.contains("date")) {
                if (value != m_configDate) {
                    m_configDate = value;
                    emit configDateChanged();
                }
            }
        }
    }
}

void TddiController::refresh()
{
    QDir dir(m_basePath);

    if (!dir.exists()) {
        if (m_available) {
            m_available = false;
            emit availableChanged();
        }
        return;
    }

    // Read from "vendor" file (Himax TDDI format)
    QString vendorContent = readFile("vendor");

    if (!vendorContent.isEmpty()) {
        parseVersionInfo(vendorContent);

        if (!m_available) {
            m_available = true;
            emit availableChanged();
        }
    } else {
        if (m_available) {
            m_available = false;
            emit availableChanged();
        }
    }
}
