#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QDebug>
#include <QScreen>
#include <QSocketNotifier>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "PatternController.h"
#include "FpgaController.h"

static int signalPipe[2] = {-1, -1};

static void handleUnixSignal(int signalNumber)
{
    char byte = static_cast<char>(signalNumber);
    if (signalPipe[1] >= 0) {
        ssize_t ignored = write(signalPipe[1], &byte, sizeof(byte));
        (void)ignored;
    }
}

static bool setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool setupUnixSignalHandlers()
{
    if (pipe(signalPipe) != 0) {
        qWarning() << "Failed to create signal pipe:" << strerror(errno);
        return false;
    }

    if (!setNonBlocking(signalPipe[0]) || !setNonBlocking(signalPipe[1])) {
        qWarning() << "Failed to configure signal pipe:" << strerror(errno);
        close(signalPipe[0]);
        close(signalPipe[1]);
        signalPipe[0] = -1;
        signalPipe[1] = -1;
        return false;
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handleUnixSignal;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGTERM, &action, nullptr) != 0 ||
        sigaction(SIGINT, &action, nullptr) != 0) {
        qWarning() << "Failed to install signal handlers:" << strerror(errno);
        close(signalPipe[0]);
        close(signalPipe[1]);
        signalPipe[0] = -1;
        signalPipe[1] = -1;
        return false;
    }

    return true;
}

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

    QCommandLineOption scriptOption(QStringList() << "script",
                                    "Start a supervised child script/program.",
                                    "program");
    parser.addOption(scriptOption);

    QCommandLineOption scriptArgOption(QStringList() << "script-arg",
                                       "Argument to pass to the supervised child script. Can be repeated.",
                                       "argument");
    parser.addOption(scriptArgOption);

    QCommandLineOption childActionButtonOption(QStringList() << "child-action-button",
                                               "Show an auto-hide button that toggles a child-script action.");
    parser.addOption(childActionButtonOption);

    QCommandLineOption childActionStartTextOption(QStringList() << "child-action-start-text",
                                                  "Text shown when the child action is inactive.",
                                                  "text", "Start Recording");
    parser.addOption(childActionStartTextOption);

    QCommandLineOption childActionStopTextOption(QStringList() << "child-action-stop-text",
                                                 "Text shown when the child action is active.",
                                                 "text", "Stop Recording");
    parser.addOption(childActionStopTextOption);

    QCommandLineOption childActionStartColorOption(QStringList() << "child-action-start-color",
                                                   "Button color when the child action is inactive.",
                                                   "color", "green");
    parser.addOption(childActionStartColorOption);

    QCommandLineOption childActionStopColorOption(QStringList() << "child-action-stop-color",
                                                  "Button color when the child action is active.",
                                                  "color", "red");
    parser.addOption(childActionStopColorOption);

    QCommandLineOption disablePatternNavigationOption(QStringList() << "disable-pattern-navigation",
                                                      "Disable background tap/swipe pattern navigation.");
    parser.addOption(disablePatternNavigationOption);

    QCommandLineOption disableUiAutoHideOption(QStringList() << "disable-ui-autohide",
                                               "Keep overlay controls visible instead of auto-hiding them.");
    parser.addOption(disableUiAutoHideOption);

    QCommandLineOption hideNavigationHelpOption(QStringList() << "hide-navigation-help",
                                                "Hide the tap/swipe navigation help box.");
    parser.addOption(hideNavigationHelpOption);

    QCommandLineOption disableUserInteractionOption(QStringList() << "disable-user-interaction",
                                                    "Disable touch controls and UI overlays.");
    parser.addOption(disableUserInteractionOption);

    QCommandLineOption initialPatternOption(QStringList() << "initial-pattern",
                                            "Initial pattern shown before the UI is loaded.",
                                            "pattern");
    parser.addOption(initialPatternOption);

    QCommandLineOption initialMetadataStatusOption(QStringList() << "initial-metadata-status",
                                                   "Initial metadata visibility: autohide, enable, or disable.",
                                                   "status");
    parser.addOption(initialMetadataStatusOption);

    QCommandLineOption initialMetadataTextOption(QStringList() << "initial-metadata-text",
                                                 "Initial metadata overlay text. Use literal \\n for new lines.",
                                                 "text");
    parser.addOption(initialMetadataTextOption);

    QCommandLineOption initialMetadataAlignOption(QStringList() << "initial-metadata-align",
                                                  "Initial metadata alignment: left, center, or right.",
                                                  "align");
    parser.addOption(initialMetadataAlignOption);

    QCommandLineOption initialMetadataFontSizeOption(QStringList() << "initial-metadata-fontsize",
                                                     "Initial metadata font size.",
                                                     "size");
    parser.addOption(initialMetadataFontSizeOption);

    QCommandLineOption initialMetadataColorOption(QStringList() << "initial-metadata-color",
                                                  "Initial metadata text color.",
                                                  "color");
    parser.addOption(initialMetadataColorOption);

    QCommandLineOption i2cOption(QStringList() << "i" << "i2c",
                                 "FPGA I2C bus device (default: /dev/i2c-1)",
                                 "i2c", "/dev/i2c-1");
    parser.addOption(i2cOption);

    parser.process(app);

    // Get screen resolution for debugging
    QScreen *screen = app.primaryScreen();
    if (screen) {
        QSize screenSize = screen->size();
        qDebug() << "Display resolution:" << screenSize.width() << "x" << screenSize.height();
    }

    // Create pattern controller
    PatternController patternController;
    QColor childActionStartColor(parser.value(childActionStartColorOption));
    if (!childActionStartColor.isValid()) {
        qWarning() << "Invalid child action start color; using green";
        childActionStartColor = QColor(0, 128, 0);
    }

    QColor childActionStopColor(parser.value(childActionStopColorOption));
    if (!childActionStopColor.isValid()) {
        qWarning() << "Invalid child action stop color; using red";
        childActionStopColor = QColor(192, 0, 0);
    }

    patternController.configureChildActionButton(
        parser.isSet(childActionButtonOption),
        parser.value(childActionStartTextOption),
        parser.value(childActionStopTextOption),
        childActionStartColor,
        childActionStopColor);
    patternController.setPatternNavigationEnabled(!parser.isSet(disablePatternNavigationOption));
    patternController.setUiAutoHideEnabled(!parser.isSet(disableUiAutoHideOption));
    patternController.setNavigationHelpVisible(!parser.isSet(hideNavigationHelpOption));
    patternController.setUserInteractionEnabled(!parser.isSet(disableUserInteractionOption));

    QString initialPattern = parser.value(initialPatternOption).trimmed().toLower();
    if (!initialPattern.isEmpty()) {
        patternController.setPattern(initialPattern);
    }

    QString initialMetadataText;
    if (parser.isSet(initialMetadataTextOption)) {
        initialMetadataText = parser.value(initialMetadataTextOption);
        initialMetadataText.replace("\\n", "\n");
    }

    int initialMetadataFontSize = -1;
    if (parser.isSet(initialMetadataFontSizeOption)) {
        bool fontOk = false;
        int parsedFontSize = parser.value(initialMetadataFontSizeOption).toInt(&fontOk);
        if (fontOk) {
            initialMetadataFontSize = parsedFontSize;
        } else {
            qWarning() << "Invalid initial metadata font size; using default";
        }
    }

    QColor initialMetadataColor;
    if (parser.isSet(initialMetadataColorOption)) {
        initialMetadataColor = QColor(parser.value(initialMetadataColorOption));
        if (!initialMetadataColor.isValid()) {
            qWarning() << "Invalid initial metadata color; using default";
            initialMetadataColor = QColor();
        }
    }

    patternController.configureStartupMetadata(
        parser.isSet(initialMetadataStatusOption) ? parser.value(initialMetadataStatusOption) : QString(),
        parser.isSet(initialMetadataTextOption) ? initialMetadataText : QString(),
        parser.isSet(initialMetadataAlignOption) ? parser.value(initialMetadataAlignOption) : QString(),
        initialMetadataFontSize,
        initialMetadataColor);

    QSocketNotifier *signalNotifier = nullptr;
    if (setupUnixSignalHandlers()) {
        signalNotifier = new QSocketNotifier(signalPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(signalNotifier, &QSocketNotifier::activated,
                         &app, [&patternController, signalNotifier](int) {
            signalNotifier->setEnabled(false);

            char buffer[32];
            while (read(signalPipe[0], buffer, sizeof(buffer)) > 0) {
            }

            patternController.requestQuit("signal");
            signalNotifier->setEnabled(true);
        });
    }
    
    // Parse port number
    bool portOk;
    int port = parser.value(portOption).toInt(&portOk);
    if (!portOk || port < 1024 || port > 65535) {
        qWarning() << "Invalid port number, using default" << DEFAULT_NETWORK_PORT;
        port = DEFAULT_NETWORK_PORT;
    }

    // Start network interface
    bool networkStarted = patternController.startNetworkInterface(port);
    if (!networkStarted) {
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
    engine.rootContext()->setContextProperty("patternController", &patternController);
    engine.rootContext()->setContextProperty("fpga", &fpgaController);

    // Load main QML file
    const QUrl url(QStringLiteral("qrc:/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.load(url);

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    QString childScript = parser.value(scriptOption);
    if (!childScript.isEmpty()) {
        if (!networkStarted) {
            qWarning() << "Not starting child script because network interface is unavailable";
        } else {
            QStringList childArguments = parser.values(scriptArgOption);
            if (!patternController.startChildScript(childScript, childArguments)) {
                qWarning() << "Failed to start child script:" << childScript;
            }
        }
    }

    int result = app.exec();

    if (signalPipe[0] >= 0) {
        close(signalPipe[0]);
        signalPipe[0] = -1;
    }
    if (signalPipe[1] >= 0) {
        close(signalPipe[1]);
        signalPipe[1] = -1;
    }

    return result;
}
