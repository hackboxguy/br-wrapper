#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QProcess>
#include <QStorageInfo>
#include <QQuickWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QWindow>
#include <QScreen>
#include <QPixmap>
#include <QImage>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

#include "UpdateController.h"
#include "SystemImageController.h"
#include "FpgaController.h"

// A key of pi-ab-update's board config, parsed the way the engine parses it
// (never sourced): the engine's paths are the board's to choose, and the
// README warns that a board may move its runtime directory.
static QString abConfigPath()
{
    const QByteArray env = qgetenv("AB_UPDATE_CONFIG");   // the engine's own test seam
    return env.isEmpty() ? QString("/usr/lib/pi-ab-update/ab-update.conf") : QString::fromLocal8Bit(env);
}

static QString abConfigValue(const QString &key)
{
    QFile f(abConfigPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QString value;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith('#') || !line.startsWith(key + "=")) continue;
        const QString v = line.mid(key.length() + 1);
        if (!v.isEmpty()) value = v;
    }
    return value;
}

// The lock marks a running update for the badge script. A reboot in the middle
// of one (the image update ends in one by design) can leave it behind on an
// image whose /tmp survives; a lock whose process is gone is dropped here.
static void dropStaleLock(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const qint64 pid = f.readAll().trimmed().toLongLong();
    f.close();
    if (pid <= 0 || ::kill(pid_t(pid), 0) != 0) QFile::remove(path);
}

// The app's own durable directory: logs, last-install, acknowledged-fallback.
// On the A/B images / is an overlay on RAM and only /data survives a reboot -
// and both kinds of update end in a power cycle or a reboot, which is exactly
// when the log is wanted. The image's data skeleton creates
// /data/system-manager (pi-owned); on images built before that, the app
// creates it once - without sudo first, then with sudo -n. Single-slot images
// have no /data and keep <prefix>/usr, i.e. logs in <prefix>/usr/logs.
// SYSTEM_MANAGER_DATA overrides it (tests; the badge script honours it too).
static QString dataRoot(const QString &prefix)
{
    const QByteArray env = qgetenv("SYSTEM_MANAGER_DATA");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    const QString dataDir = "/data/system-manager";
    auto usable = [&]() { return QFileInfo(dataDir).isDir() && QFileInfo(dataDir).isWritable(); };
    if (usable()) return dataDir;
    QStorageInfo data("/data");
    if (!QFileInfo(dataDir).exists() && data.isValid() && data.rootPath() == "/data" && !data.isReadOnly()) {
        if (!QDir().mkpath(dataDir) && ::geteuid() != 0) {
            QProcess::execute("sudo", {"-n", "install", "-d", "-m", "0755",
                                       "-o", QString::number(::getuid()), "-g", QString::number(::getgid()),
                                       dataDir});
        }
        if (usable()) return dataDir;
    }
    return QDir(prefix).filePath("usr");
}

// System Manager for the display rig. Its firmware section shows which firmware
// each board runs against what this system ships, and installs it through
// update-iocs.sh; its system image section installs a new SD-card image from a
// USB stick through pi-ab-update. Started by qt-demo-launcher ("System Manager").
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("system-manager-app");
    app.setApplicationVersion("1.1");

    // Defaults follow the install layout, so the same binary works in
    // /home/pi/micropanel/bin (PiOS) and /usr/bin (Buildroot): the update tool
    // sits beside this binary, the images in ../share/sp6bins.
    const QString binDir = QCoreApplication::applicationDirPath();
    const QString prefix = QFileInfo(binDir).absolutePath();

    const QString data = dataRoot(prefix);
    const QString logs = QDir(data).filePath("logs");
    const QString abManifest = abConfigValue("AB_MANIFEST");
    const QString abRuntime = abConfigValue("AB_RUNTIME_DIR");

    QCommandLineParser parser;
    parser.setApplicationDescription("System Manager: board firmware, and the system image (A/B)");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption toolOpt("update-tool", "update-iocs.sh to run.", "path",
                               QDir(binDir).filePath("update-iocs.sh"));
    QCommandLineOption imageOpt("image-dir", "Directory of shipped *_ota.bin/*_otaB.bin images.", "dir",
                                QDir(prefix).filePath("share/sp6bins/firmware/bios-bin"));
    QCommandLineOption logOpt("log-dir", "Where each firmware update run writes its log.", "dir",
                              QDir(logs).filePath("system-update"));
    QCommandLineOption noticeOpt("notice-file", "File the launcher shows as a header notice after an update.",
                                 "path", "/tmp/micropanel-notice");
    QCommandLineOption dryRunOpt("dry-run", "Check the images and show the flow, but write nothing.");
    QCommandLineOption autoOpt("auto-update",
                               "Automated validation: start the update as soon as the first check finds one "
                               "(no hold-to-confirm). Never used by the launcher button.");
    // System image (pi-ab-update)
    QCommandLineOption abUpdateOpt("ab-update", "pi-ab-update front end.", "path", "/usr/local/bin/ab-update");
    QCommandLineOption scanToolOpt("scan-tool", "USB bundle scanner.", "path",
                                   QDir(binDir).filePath("system-image-scan.sh"));
    QCommandLineOption runtimeOpt("runtime-dir", "The engine's runtime directory (progress, status).", "dir",
                                  abRuntime.isEmpty() ? QString("/run/ab-update") : abRuntime);
    QCommandLineOption manifestOpt("image-manifest", "The running image's manifest.", "path",
                                   abManifest.isEmpty()
                                       ? QDir(prefix).filePath("share/micropanel/image-manifest.env")
                                       : abManifest);
    QCommandLineOption imageLogOpt("image-log-dir", "Where each image update run writes its log.", "dir",
                                   QDir(logs).filePath("system-image-update"));
    // Display FPGA (update-fpga.sh)
    QCommandLineOption fpgaToolOpt("fpga-tool", "update-fpga.sh to run.", "path",
                                   QDir(binDir).filePath("update-fpga.sh"));
    QCommandLineOption fpgaImageOpt("fpga-image-dir", "Directory of the <name>_ota.bin FPGA images.", "dir",
                                    QDir(prefix).filePath("fpga/bitbin"));
    QCommandLineOption autoFpgaOpt("auto-fpga-update",
                                   "Automated validation: start the FPGA update once a check offers one (no hold).");
    QCommandLineOption autoFpgaActOpt("auto-fpga-activate",
                                      "Automated validation: activate the written FPGA image (restarts the system).");
    QCommandLineOption autoInstallOpt("auto-install",
                                      "Automated validation: install the image a scan offers, once, without the "
                                      "hold. Never used by the launcher button.");
    QCommandLineOption systemctlOpt("systemctl", "systemctl to ask about the engine's health units (test seam).",
                                    "path", "systemctl");
    QCommandLineOption shotOpt("screenshot",
                               "Grab the window to <file> once the shown section has its first result, then quit "
                               "(docs, and QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software checks).", "file");
    QCommandLineOption shotDelayOpt("screenshot-delay", "Milliseconds between the first result and the grab.",
                                    "ms", "1200");
    QCommandLineOption sizeOpt("window-size", "Window size WxH instead of full screen (with --screenshot).", "WxH");
    QCommandLineOption sectionOpt("section", "Section to open first: firmware, image or fpga.", "name");
    parser.addOption(toolOpt);
    parser.addOption(imageOpt);
    parser.addOption(logOpt);
    parser.addOption(noticeOpt);
    parser.addOption(dryRunOpt);
    parser.addOption(autoOpt);
    parser.addOption(abUpdateOpt);
    parser.addOption(scanToolOpt);
    parser.addOption(runtimeOpt);
    parser.addOption(manifestOpt);
    parser.addOption(imageLogOpt);
    parser.addOption(autoInstallOpt);
    parser.addOption(sectionOpt);
    parser.addOption(fpgaToolOpt);
    parser.addOption(fpgaImageOpt);
    parser.addOption(autoFpgaOpt);
    parser.addOption(autoFpgaActOpt);
    parser.addOption(systemctlOpt);
    parser.addOption(shotOpt);
    parser.addOption(sizeOpt);
    parser.addOption(shotDelayOpt);
    parser.process(app);

    const QString lockFile = "/tmp/system-update.lock";
    dropStaleLock(lockFile);

    UpdateController::Options options;
    options.tool = parser.value(toolOpt);
    options.imageDir = parser.value(imageOpt);
    options.logDir = parser.value(logOpt);
    options.noticeFile = parser.value(noticeOpt);
    options.lockFile = lockFile;
    options.dryRun = parser.isSet(dryRunOpt);
    options.autoUpdate = parser.isSet(autoOpt);

    UpdateController controller(options);

    SystemImageController::Options imageOptions;
    imageOptions.abUpdate = parser.value(abUpdateOpt);
    imageOptions.scanTool = parser.value(scanToolOpt);
    imageOptions.runtimeDir = parser.value(runtimeOpt);
    imageOptions.imageManifest = parser.value(manifestOpt);
    imageOptions.logDir = parser.value(imageLogOpt);
    imageOptions.stateFile = QDir(imageOptions.logDir).filePath("last-install");
    imageOptions.lockFile = lockFile;
    imageOptions.dryRun = parser.isSet(dryRunOpt);
    imageOptions.autoInstall = parser.isSet(autoInstallOpt);
    imageOptions.abConfig = abConfigPath();
    imageOptions.systemctl = parser.value(systemctlOpt);
    imageOptions.ackFile = QDir(data).filePath("acknowledged-fallback");

    SystemImageController imageController(imageOptions);

    FpgaController::Options fpgaOptions;
    fpgaOptions.tool = parser.value(fpgaToolOpt);
    fpgaOptions.imageDir = parser.value(fpgaImageOpt);
    fpgaOptions.logDir = QDir(logs).filePath("system-update");
    fpgaOptions.lockFile = lockFile;
    fpgaOptions.noticeFile = parser.value(noticeOpt);
    fpgaOptions.dryRun = parser.isSet(dryRunOpt);
    fpgaOptions.autoUpdate = parser.isSet(autoFpgaOpt);
    fpgaOptions.autoActivate = parser.isSet(autoFpgaActOpt);
    FpgaController fpgaController(fpgaOptions);
    // The FPGA section exists only where an FPGA with the update interface
    // answers. Probe once the firmware check is done: one I2C user at a time.
    QObject::connect(&controller, &UpdateController::stateChanged, &fpgaController, [&]() {
        static bool probed = false;
        if (!probed && controller.state() != "checking") {
            probed = true;
            fpgaController.probe();
        }
    });

    // Open the image section first when it has something to say
    QString section = parser.value(sectionOpt);
    if (section != "firmware" && section != "image" && section != "fpga")
        section = imageController.wantsAttention() ? "image" : "firmware";

    // Same face as the launcher when it is installed; QML falls back otherwise
    const bool haveRoboto = QFontDatabase().families().contains("Roboto");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("updater", &controller);
    engine.rootContext()->setContextProperty("imageUpdate", &imageController);
    engine.rootContext()->setContextProperty("fpgaUpdate", &fpgaController);
    engine.rootContext()->setContextProperty("initialSection", section);
    engine.rootContext()->setContextProperty("uiFont", haveRoboto ? QString("Roboto") : QString());
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    const QStringList size = parser.value(sizeOpt).split('x');
    if (window && size.size() == 2 && size[0].toInt() > 0 && size[1].toInt() > 0) {
        window->setVisibility(QWindow::Windowed);
        window->resize(size[0].toInt(), size[1].toInt());
    }

    // --screenshot: wait for the first result of the section on screen, give
    // it a moment to settle, grab, quit. 20 s at most.
    if (window && parser.isSet(shotOpt)) {
        const QString file = parser.value(shotOpt);
        const int delay = qMax(0, parser.value(shotDelayOpt).toInt());
        auto *poll = new QTimer(&app);
        auto *started = new QElapsedTimer;
        started->start();
        QObject::connect(poll, &QTimer::timeout, &app, [=, &app, &controller, &imageController, &fpgaController]() {
            const QString shown = window->property("section").toString();
            const bool ready = shown == "image"
                ? (!imageController.supported()
                   || (imageController.scanState() != "idle" && imageController.scanState() != "scanning"))
                : shown == "fpga"
                ? (fpgaController.state() != "probing" && fpgaController.state() != "checking")
                : controller.state() != "checking";
            if (!ready && started->elapsed() < 20000) return;
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

    controller.check();
    return app.exec();
}
