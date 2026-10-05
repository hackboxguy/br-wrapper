#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QQuickWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QWindow>
#include <QScreen>
#include <QPixmap>
#include <QImage>
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

#include "NetTool.h"
#include "StatusController.h"
#include "WifiController.h"
#include "WiredController.h"

// SIGTERM (the launcher's stop-app), SIGINT, SIGHUP: quit through the event
// loop, so the controllers stop their net-ctl.sh processes on the way out
static int signalPipe[2] = {-1, -1};
static void onSignal(int)
{
    const char c = 1;
    ssize_t n = ::write(signalPipe[1], &c, 1);
    (void)n;
}

// Network for the display rig: what every interface is doing, WiFi, the wired
// ports' modes and bench diagnostics. Started by qt-demo-launcher ("Network").
// Everything it reads or changes goes through net-ctl.sh (NetworkManager).
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("network-manager-app");
    app.setApplicationVersion("0.2");

    // The helper sits beside this binary: /usr/bin (Buildroot) or
    // /home/pi/micropanel/bin (PiOS)
    const QString binDir = QCoreApplication::applicationDirPath();

    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, signalPipe) == 0) {
        auto *notifier = new QSocketNotifier(signalPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, [&app]() {
            char c;
            ssize_t n = ::read(signalPipe[0], &c, 1);
            (void)n;
            NetTool::log("signal: quitting");
            app.quit();
        });
        std::signal(SIGTERM, onSignal);
        std::signal(SIGINT, onSignal);
        std::signal(SIGHUP, onSignal);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription("Network: interfaces, WiFi, wired ports and diagnostics (NetworkManager)");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption toolOpt("net-tool", "net-ctl.sh to run (tests: tests/fake-net-ctl).", "path",
                               QDir(binDir).filePath("net-ctl.sh"));
    QCommandLineOption dryRunOpt("dry-run", "Never elevate, never change anything; changes show what would run.");
    QCommandLineOption sectionOpt("section", "Section to open: overview, wifi, wired or tools.", "name", "overview");
    QCommandLineOption sheetOpt("open-sheet", "Open an overlay at start (screenshots): detail[:if], keyboard[:layer[:shown]], hidden, scroll-end, "
                                "numpad[:field], probe-warning, wired-<mode>[:if] or apply-<mode>[:if].",
                                "name");
    QCommandLineOption logOpt("log-file", "Where the app logs net-ctl.sh's lines.", "path",
                              "/tmp/network-manager-app.log");
    QCommandLineOption sysfsOpt("sysfs", "Counter source instead of /sys/class/net (test seam).", "dir",
                                "/sys/class/net");
    QCommandLineOption sampleOpt("sample-ms", "Counter sampling interval (test seam).", "ms", "1000");
    QCommandLineOption autoConnectOpt("auto-connect",
                                      "Automated validation: join this SSID (as if tapped) once the first scan is "
                                      "in. Never used by the launcher button.", "ssid");
    QCommandLineOption shotOpt("screenshot",
                               "Grab the window to <file> once the shown section has its data, then quit "
                               "(docs, and QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software checks).", "file");
    QCommandLineOption shotDelayOpt("screenshot-delay", "Milliseconds between the data and the grab.", "ms", "1200");
    QCommandLineOption sizeOpt("window-size", "Window size WxH instead of full screen (with --screenshot).", "WxH");
    for (const QCommandLineOption &o : {toolOpt, dryRunOpt, sectionOpt, sheetOpt, logOpt, sysfsOpt, sampleOpt,
                                        autoConnectOpt, shotOpt, shotDelayOpt, sizeOpt})
        parser.addOption(o);
    parser.process(app);

    NetTool::setLogFile(parser.value(logOpt));
    NetTool::log(QString("network-manager-app %1 started%2").arg(app.applicationVersion(),
                                                                parser.isSet(dryRunOpt) ? " (dry run)" : ""));

    StatusController::Options statusOptions;
    statusOptions.tool = parser.value(toolOpt);
    statusOptions.sysfs = parser.value(sysfsOpt);
    statusOptions.dryRun = parser.isSet(dryRunOpt);
    statusOptions.sampleMs = qMax(50, parser.value(sampleOpt).toInt());
    StatusController status(statusOptions);

    WifiController::Options wifiOptions;
    wifiOptions.tool = parser.value(toolOpt);
    wifiOptions.dryRun = parser.isSet(dryRunOpt);
    WifiController wifi(wifiOptions, &status);

    WiredController::Options wiredOptions;
    wiredOptions.tool = parser.value(toolOpt);
    wiredOptions.dryRun = parser.isSet(dryRunOpt);
    WiredController wired(wiredOptions, &status);

    QString section = parser.value(sectionOpt);
    if (section != "overview" && section != "wifi" && section != "wired" && section != "tools") section = "overview";

    // Same face as the launcher when it is installed; QML falls back otherwise
    const bool haveRoboto = QFontDatabase().families().contains("Roboto");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("status", &status);
    engine.rootContext()->setContextProperty("wifi", &wifi);
    engine.rootContext()->setContextProperty("wired", &wired);
    engine.rootContext()->setContextProperty("initialSection", section);
    engine.rootContext()->setContextProperty("initialSheet", parser.value(sheetOpt));
    engine.rootContext()->setContextProperty("uiFont", haveRoboto ? QString("Roboto") : QString());
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    const QStringList size = parser.value(sizeOpt).split('x');
    if (window && size.size() == 2 && size[0].toInt() > 0 && size[1].toInt() > 0) {
        window->setVisibility(QWindow::Windowed);
        window->resize(size[0].toInt(), size[1].toInt());
    }

    if (parser.isSet(autoConnectOpt)) {
        const QString ssid = parser.value(autoConnectOpt);
        QObject::connect(&wifi, &WifiController::networksChanged, &app, [&wifi, ssid]() {
            static bool done = false;
            if (done) return;
            done = true;
            NetTool::log("auto-connect: " + ssid);
            wifi.choose(ssid);
        });
    }

    // --screenshot: wait until the page says its data is in ("shotReady"),
    // give it a moment to settle, grab, quit. 20 s at most.
    if (window && parser.isSet(shotOpt)) {
        const QString file = parser.value(shotOpt);
        const int delay = qMax(0, parser.value(shotDelayOpt).toInt());
        auto *poll = new QTimer(&app);
        auto *started = new QElapsedTimer;
        started->start();
        QObject::connect(poll, &QTimer::timeout, &app, [=, &app]() {
            if (!window->property("shotReady").toBool() && started->elapsed() < 20000) return;
            poll->stop();
            QTimer::singleShot(delay, &app, [=]() {
                // The scene graph's own grab, else the platform's (offscreen +
                // the software backend renders into a backing store, which only
                // the screen grab reaches)
                QImage image = window->grabWindow();
                if (image.isNull() && window->screen())
                    image = window->screen()->grabWindow(window->winId()).toImage();
                const bool ok = !image.isNull() && image.save(file);
                qInfo().noquote() << (ok ? "screenshot saved:" : "screenshot FAILED:") << file;
                QCoreApplication::exit(ok ? 0 : 1);
            });
        });
        poll->start(200);
    }

    status.start();
    return app.exec();
}
