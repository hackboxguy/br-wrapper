#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QDebug>
#include <QScreen>
#include <QFile>
#include <QTextStream>
#include "config.h"
#include "AlsDimmerController.h"
#include "FpgaController.h"
#include "TemperatureController.h"
#include "TddiController.h"

// Parse VERSION and BUILD_DATE from a key=value version file
static void parseVersionFile(const QString &path, QString &version, QString &buildDate)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "disp-settings: Cannot open version file:" << path;
        version = "N/A";
        buildDate = "N/A";
        return;
    }
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith("VERSION="))
            version = line.mid(8);
        else if (line.startsWith("BUILD_DATE="))
            buildDate = line.mid(11);
    }
    if (version.isEmpty()) version = "N/A";
    if (buildDate.isEmpty()) buildDate = "N/A";
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("disp-settings");
    app.setApplicationVersion("1.0");

    // Parse command line arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("Display Settings for Embedded Systems");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption configOption(QStringList() << "c" << "config",
                                    QString("Configuration file path (default: %1)").arg(DEFAULT_CONFIG_FILE),
                                    "config", DEFAULT_CONFIG_FILE);
    parser.addOption(configOption);

    QCommandLineOption portOption(QStringList() << "p" << "port",
                                  QString("TCP server port (default: %1)").arg(DEFAULT_NETWORK_PORT),
                                  "port", QString::number(DEFAULT_NETWORK_PORT));
    parser.addOption(portOption);

    QCommandLineOption socketOption(QStringList() << "s" << "socket",
                                    QString("als-dimmer socket path (default: %1)").arg(DEFAULT_ALS_DIMMER_SOCKET),
                                    "socket", DEFAULT_ALS_DIMMER_SOCKET);
    parser.addOption(socketOption);

    QCommandLineOption i2cOption(QStringList() << "i" << "i2c",
                                 QString("I2C bus device (default: %1)").arg(DEFAULT_I2C_BUS),
                                 "i2c", DEFAULT_I2C_BUS);
    parser.addOption(i2cOption);

    parser.process(app);

    // Get screen resolution for debugging
    QScreen *screen = app.primaryScreen();
    if (screen) {
        QSize screenSize = screen->size();
        qDebug() << "disp-settings: Display resolution:" << screenSize.width() << "x" << screenSize.height();
    }

    // Get config file path
    QString configFile = parser.value(configOption);
    qDebug() << "disp-settings: Config file:" << configFile;

    // Create controllers
    AlsDimmerController alsDimmerController;
    alsDimmerController.setSocketPath(parser.value(socketOption));
    alsDimmerController.start();

    FpgaController fpgaController;
    fpgaController.setI2cBus(parser.value(i2cOption));
    fpgaController.start();

    TemperatureController temperatureController;
    temperatureController.start();

    TddiController tddiController;
    tddiController.start();

    // Parse OS and application version files
    QString osVersion, osBuildDate, incVersion, incBuildDate;
    parseVersionFile("/etc/base-version.txt", osVersion, osBuildDate);
    parseVersionFile("/etc/incremental-version.txt", incVersion, incBuildDate);

    // Setup QML engine
    QQmlApplicationEngine engine;

    // Expose application info and controllers to QML
    engine.rootContext()->setContextProperty("appVersion", app.applicationVersion());
    engine.rootContext()->setContextProperty("osVersion", osVersion);
    engine.rootContext()->setContextProperty("osBuildDate", osBuildDate);
    engine.rootContext()->setContextProperty("swVersion", incVersion);
    engine.rootContext()->setContextProperty("swBuildDate", incBuildDate);
    engine.rootContext()->setContextProperty("alsDimmer", &alsDimmerController);
    engine.rootContext()->setContextProperty("fpga", &fpgaController);
    engine.rootContext()->setContextProperty("tempSensors", &temperatureController);
    engine.rootContext()->setContextProperty("tddi", &tddiController);

    // Load main QML file
    const QUrl url(QStringLiteral("qrc:/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
            qCritical() << "disp-settings: Failed to load main.qml";
            QCoreApplication::exit(-1);
        }
    }, Qt::QueuedConnection);

    engine.load(url);

    qDebug() << "disp-settings: Application started";

    return app.exec();
}
