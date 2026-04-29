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

    parser.process(app);

    // Get screen resolution for debugging
    QScreen *screen = app.primaryScreen();
    if (screen) {
        QSize screenSize = screen->size();
        qDebug() << "Display resolution:" << screenSize.width() << "x" << screenSize.height();
    }

    // Create pattern controller
    PatternController patternController;

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
