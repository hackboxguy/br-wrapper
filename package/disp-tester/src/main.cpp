#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QDebug>
#include <QScreen>
#include "PatternController.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("disp-tester");
    app.setApplicationVersion("1.0");

    // Parse command line arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("Display Pattern Tester for Automotive Displays");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption portOption(QStringList() << "p" << "port",
                                  QString("TCP server port (default: %1)").arg(DEFAULT_NETWORK_PORT),
                                  "port", QString::number(DEFAULT_NETWORK_PORT));
    parser.addOption(portOption);

    QCommandLineOption fullscreenOption(QStringList() << "f" << "fullscreen",
                                        "Start in fullscreen mode");
    parser.addOption(fullscreenOption);

    parser.process(app);

    // Get screen resolution for debugging
    QScreen *screen = app.primaryScreen();
    if (screen) {
        QSize screenSize = screen->size();
        qDebug() << "Display resolution:" << screenSize.width() << "x" << screenSize.height();
    }

    // Create pattern controller
    PatternController patternController;
    
    // Parse port number
    bool portOk;
    int port = parser.value(portOption).toInt(&portOk);
    if (!portOk || port < 1024 || port > 65535) {
        qWarning() << "Invalid port number, using default" << DEFAULT_NETWORK_PORT;
        port = DEFAULT_NETWORK_PORT;
    }

    // Start network interface
    if (!patternController.startNetworkInterface(port)) {
        qWarning() << "Failed to start network interface on port" << port;
    } else {
        qDebug() << "Network interface started on port" << port;
    }

    // Setup QML engine
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("patternController", &patternController);

    // Load main QML file
    const QUrl url(QStringLiteral("qrc:/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
