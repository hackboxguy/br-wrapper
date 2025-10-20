#include "config.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>
#include "VideoPlayer.h"
#include "VideoController.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    // Parse command line arguments
    // Usage: qt-mpv-wrapper [video_directory] [--port PORT]
    QString videoPath = "Videos"; // Default: relative path (can be overridden via command-line)
    int networkPort = DEFAULT_VIDEO_PLAYER_PORT; // Default port

    for (int i = 1; i < argc; i++) {
        QString arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            networkPort = QString(argv[i + 1]).toInt();
            i++; // Skip next argument
        } else if (!arg.startsWith("--")) {
            videoPath = arg;
        }
    }

    qDebug() << "Video directory:" << videoPath;
    qDebug() << "Network port:" << networkPort;

    // Create VideoPlayer instance
    VideoPlayer *videoPlayer = new VideoPlayer(&app);

    // Create VideoController (handles network API)
    VideoController *videoController = new VideoController(videoPlayer, videoPath, networkPort, &app);

    // Make video path and player available to QML
    engine.rootContext()->setContextProperty("videoBasePath", QUrl::fromLocalFile(videoPath));
    engine.rootContext()->setContextProperty("videoPlayer", videoPlayer);
    engine.rootContext()->setContextProperty("videoController", videoController);

    const QUrl url(QStringLiteral("qrc:/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
