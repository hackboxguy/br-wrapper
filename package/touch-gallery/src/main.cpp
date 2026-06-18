#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QDir>
#include <QUrl>
#include <QFontDatabase>
#include <QDebug>
#include "GalleryController.h"
#include "FpgaController.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("touch-gallery");
    app.setApplicationVersion("1.0");

    // Debug: Check available fonts
    QFontDatabase fontDb;
    qDebug() << "Available font families:" << fontDb.families();

    // Parse command line arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("Touch-based Image Gallery with Network Control");
    parser.addHelpOption();
    parser.addVersionOption();

    // Pictures directory argument (positional)
    parser.addPositionalArgument("directory", "Pictures directory to display", "[directory]");

    // Network port option
    QCommandLineOption portOption(QStringList() << "p" << "port",
                                  QString("TCP server port (default: %1)").arg(DEFAULT_NETWORK_PORT),
                                  "port", QString::number(DEFAULT_NETWORK_PORT));
    parser.addOption(portOption);

    // Slideshow mode option
    QCommandLineOption slideshowOption(QStringList() << "s" << "slideshow",
                                       "Enable slideshow mode with interval in seconds",
                                       "interval");
    parser.addOption(slideshowOption);

    // FPGA I2C bus option (for local-dimming / pixel-compensation overlay toggles)
    QCommandLineOption i2cOption(QStringList() << "i" << "i2c",
                                 "FPGA I2C bus device (default: /dev/i2c-1)",
                                 "i2c", "/dev/i2c-1");
    parser.addOption(i2cOption);

    QCommandLineOption usbCopyOption(QStringList() << "enable-usb-copy",
                                     "Show COPY USB button for the current image.");
    parser.addOption(usbCopyOption);

    QCommandLineOption usbCopyScriptOption(QStringList() << "usb-copy-script",
                                           "Script used by COPY USB button.",
                                           "script", "/usr/bin/copy-image-to-usb.sh");
    parser.addOption(usbCopyScriptOption);

    parser.process(app);

    // Get pictures directory
    QString picturesDir = "/Pictures";
    QStringList positionalArgs = parser.positionalArguments();
    if (!positionalArgs.isEmpty()) {
        picturesDir = positionalArgs.first();
    }

    // Get slideshow settings
    bool slideshowMode = false;
    int slideshowInterval = 5; // default 5 seconds
    if (parser.isSet(slideshowOption)) {
        bool ok;
        int interval = parser.value(slideshowOption).toInt(&ok);
        if (ok && interval > 0) {
            slideshowMode = true;
            slideshowInterval = interval;
            qDebug() << "Slideshow mode enabled with" << interval << "second interval";
        }
    }

    // Parse port number
    bool portOk;
    int port = parser.value(portOption).toInt(&portOk);
    if (!portOk || port < 1024 || port > 65535) {
        qWarning() << "Invalid port number, using default" << DEFAULT_NETWORK_PORT;
        port = DEFAULT_NETWORK_PORT;
    }

    // Create gallery controller
    GalleryController galleryController;
    galleryController.setPicturesDirectory(picturesDir);
    galleryController.setUsbCopyScript(parser.value(usbCopyScriptOption));

    // Start network interface
    if (!galleryController.startNetworkInterface(port)) {
        qWarning() << "Failed to start network interface on port" << port;
    } else {
        qDebug() << "Network interface started on port" << port;
    }

    // FPGA controller for optional local-dimming / pixel-compensation overlay toggles
    FpgaController fpgaController;
    fpgaController.setI2cBus(parser.value(i2cOption));
    fpgaController.start();

    // Setup QML engine
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("galleryController", &galleryController);
    engine.rootContext()->setContextProperty("fpga", &fpgaController);
    engine.rootContext()->setContextProperty("picturesPath", QUrl::fromLocalFile(picturesDir));
    engine.rootContext()->setContextProperty("usbCopyEnabled", parser.isSet(usbCopyOption));
    engine.rootContext()->setContextProperty("slideshowMode", slideshowMode);
    engine.rootContext()->setContextProperty("slideshowInterval", slideshowInterval * 1000); // Convert to milliseconds

    // Load main QML file
    const QUrl url(QStringLiteral("qrc:/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.load(url);

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
