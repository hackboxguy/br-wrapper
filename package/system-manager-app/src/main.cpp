#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>

#include "UpdateController.h"

// System Manager for the display rig. Its firmware section shows which firmware
// each board runs against what this system ships, and installs it through
// update-iocs.sh. Started by qt-demo-launcher ("System Manager" button).
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("system-manager-app");
    app.setApplicationVersion("1.0");

    // Defaults follow the install layout, so the same binary works in
    // /home/pi/micropanel/bin (PiOS) and /usr/bin (Buildroot): the update tool
    // sits beside this binary, the images in ../share/sp6bins.
    const QString binDir = QCoreApplication::applicationDirPath();
    const QString prefix = QFileInfo(binDir).absolutePath();

    QCommandLineParser parser;
    parser.setApplicationDescription("System Manager: firmware of the 983HH board and the display controller");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption toolOpt("update-tool", "update-iocs.sh to run.", "path",
                               QDir(binDir).filePath("update-iocs.sh"));
    QCommandLineOption imageOpt("image-dir", "Directory of shipped *_ota.bin/*_otaB.bin images.", "dir",
                                QDir(prefix).filePath("share/sp6bins/firmware/bios-bin"));
    QCommandLineOption logOpt("log-dir", "Where each update run writes its log.", "dir",
                              QDir(prefix).filePath("usr/logs/system-update"));
    QCommandLineOption noticeOpt("notice-file", "File the launcher shows as a header notice after an update.",
                                 "path", "/tmp/micropanel-notice");
    QCommandLineOption dryRunOpt("dry-run", "Check the images and show the flow, but write nothing.");
    QCommandLineOption autoOpt("auto-update",
                               "Automated validation: start the update as soon as the first check finds one "
                               "(no hold-to-confirm). Never used by the launcher button.");
    parser.addOption(toolOpt);
    parser.addOption(imageOpt);
    parser.addOption(logOpt);
    parser.addOption(noticeOpt);
    parser.addOption(dryRunOpt);
    parser.addOption(autoOpt);
    parser.process(app);

    UpdateController::Options options;
    options.tool = parser.value(toolOpt);
    options.imageDir = parser.value(imageOpt);
    options.logDir = parser.value(logOpt);
    options.noticeFile = parser.value(noticeOpt);
    options.lockFile = "/tmp/system-update.lock";
    options.dryRun = parser.isSet(dryRunOpt);
    options.autoUpdate = parser.isSet(autoOpt);

    UpdateController controller(options);

    // Same face as the launcher when it is installed; QML falls back otherwise
    const bool haveRoboto = QFontDatabase().families().contains("Roboto");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("updater", &controller);
    engine.rootContext()->setContextProperty("uiFont", haveRoboto ? QString("Roboto") : QString());
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;

    controller.check();
    return app.exec();
}
