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
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

#include "UpdateController.h"
#include "SystemImageController.h"

// A key of pi-ab-update's board config, parsed the way the engine parses it
// (never sourced): the engine's paths are the board's to choose, and the
// README warns that a board may move its runtime directory.
static QString abConfigValue(const QString &key)
{
    QFile f("/usr/lib/pi-ab-update/ab-update.conf");
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

// Where logs go. On the A/B images / is an overlay on RAM and only /data
// survives a reboot - and both kinds of update end in a power cycle or a
// reboot, which is exactly when the log is wanted. /data holds one directory
// per app, owned by pi; this app's is created once (sudo -n, as the app does
// for everything that needs root). Elsewhere: <prefix>/usr/logs, as before.
static QString logRoot(const QString &prefix)
{
    const QString dataDir = "/data/system-manager";
    QStorageInfo data("/data");
    if (data.isValid() && data.rootPath() == "/data" && !data.isReadOnly()) {
        if (!QFileInfo(dataDir).isWritable() && ::geteuid() != 0) {
            QProcess::execute("sudo", {"-n", "install", "-d", "-m", "0755",
                                       "-o", QString::number(::getuid()), "-g", QString::number(::getgid()),
                                       dataDir});
        } else if (!QFileInfo(dataDir).exists()) {
            QDir().mkpath(dataDir);
        }
        if (QFileInfo(dataDir).isDir() && QFileInfo(dataDir).isWritable()) return dataDir + "/logs";
    }
    return QDir(prefix).filePath("usr/logs");
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

    const QString logs = logRoot(prefix);
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
    QCommandLineOption autoInstallOpt("auto-install",
                                      "Automated validation: install the image a scan offers, once, without the "
                                      "hold. Never used by the launcher button.");
    QCommandLineOption sectionOpt("section", "Section to open first: firmware or image.", "name");
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

    SystemImageController imageController(imageOptions);

    // Open the image section first when it has something to say
    QString section = parser.value(sectionOpt);
    if (section != "firmware" && section != "image")
        section = imageController.wantsAttention() ? "image" : "firmware";

    // Same face as the launcher when it is installed; QML falls back otherwise
    const bool haveRoboto = QFontDatabase().families().contains("Roboto");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("updater", &controller);
    engine.rootContext()->setContextProperty("imageUpdate", &imageController);
    engine.rootContext()->setContextProperty("initialSection", section);
    engine.rootContext()->setContextProperty("uiFont", haveRoboto ? QString("Roboto") : QString());
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;

    controller.check();
    return app.exec();
}
