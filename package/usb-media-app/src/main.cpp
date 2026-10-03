// usb-media-app - build a playlist from the images and videos on a USB stick
//
// Run by usb-media.sh (qt-demo-launcher's "USB Media" button), which plays
// the playlist with dual-video-player when this app exits with code 10 and
// brings the app back afterwards. Exit 0 = back to the launcher.

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

#include "MediaController.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("usb-media-app");
    app.setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("USB Media: pick images and videos on a USB stick and play them as a "
                                     "playlist on both displays (exit 10 = play, 0 = back).");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption rootOpt("root", "The USB stick's mount point.", "dir");
    QCommandLineOption playerOpt("player", "dual-video-player (used with --probe to classify files).", "path",
                                 QDir(QCoreApplication::applicationDirPath()).filePath("dual-video-player"));
    QCommandLineOption tempOpt("temp-playlist",
                               "Where Play writes the playlist when the stick is read-only.", "file");
    QCommandLineOption messageOpt("message", "Shown when the app opens (why playback ended).", "text");
    QCommandLineOption shotOpt("screenshot", "Grab the window to <file> once every file is classified, then "
                               "quit (docs; QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software).", "file");
    QCommandLineOption sizeOpt("window-size", "Window size WxH instead of full screen (with --screenshot).", "WxH");
    parser.addOption(rootOpt);
    parser.addOption(playerOpt);
    parser.addOption(tempOpt);
    parser.addOption(messageOpt);
    parser.addOption(shotOpt);
    parser.addOption(sizeOpt);
    parser.process(app);

    if (!parser.isSet(rootOpt)) {
        fprintf(stderr, "usb-media-app: --root DIR is required\n");
        return 2;
    }

    MediaController::Options options;
    options.root = QDir::cleanPath(QDir(parser.value(rootOpt)).absolutePath());
    options.player = parser.value(playerOpt);
    options.tempPlaylist = parser.value(tempOpt);
    options.message = parser.value(messageOpt);
    MediaController controller(options);

    // Same face as the launcher when it is installed; QML falls back otherwise
    const bool haveRoboto = QFontDatabase().families().contains("Roboto");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("media", &controller);
    engine.rootContext()->setContextProperty("uiFont", haveRoboto ? QString("Roboto") : QString());
    // Autostart on boot is phase 2 (usb-media-autostart.service); until it
    // ships, the checkbox stays hidden (the playlist key is kept as it is)
    engine.rootContext()->setContextProperty("autostartSupported", false);
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    const QStringList size = parser.value(sizeOpt).split('x');
    if (window && size.size() == 2 && size[0].toInt() > 0 && size[1].toInt() > 0) {
        window->setVisibility(QWindow::Windowed);
        window->resize(size[0].toInt(), size[1].toInt());
    }

    if (window && parser.isSet(shotOpt)) {
        const QString file = parser.value(shotOpt);
        auto *poll = new QTimer(&app);
        auto *started = new QElapsedTimer;
        started->start();
        QObject::connect(poll, &QTimer::timeout, &app, [=, &controller]() {
            if (controller.state() != "ready" && started->elapsed() < 30000)
                return;
            poll->stop();
            QTimer::singleShot(800, window, [=]() {
                QImage image = window->grabWindow();
                if (image.isNull() && window->screen())
                    image = window->screen()->grabWindow(window->winId()).toImage();
                const bool ok = !image.isNull() && image.save(file);
                fprintf(stderr, "screenshot %s: %s\n", ok ? "saved" : "FAILED", qPrintable(file));
                QCoreApplication::exit(ok ? 0 : 1);
            });
        });
        poll->start(200);
    }

    QTimer::singleShot(0, &controller, &MediaController::start);
    return app.exec();
}
