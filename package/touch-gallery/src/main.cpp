#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QUrl>
#include <QFontDatabase>
#include <QDebug>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // Debug: Check available fonts
    QFontDatabase fontDb;
    qDebug() << "Available font families:" << fontDb.families();

    QQmlApplicationEngine engine;

    // Set the pictures directory
    QString picturesDir = "/Pictures";
    bool slideshowMode = false;
    int slideshowInterval = 5; // default 5 seconds

    if (argc > 1) {
        picturesDir = argv[1];

        // Check for slideshow arguments
        if (argc >= 4) {
            QString arg2 = QString(argv[2]);
            QString arg3 = QString(argv[3]);

            if (arg2.toLower() == "slideshow") {
                bool ok;
                int interval = arg3.toInt(&ok);
                if (ok && interval > 0) {
                    slideshowMode = true;
                    slideshowInterval = interval;
                    qDebug() << "Slideshow mode enabled with" << interval << "second interval";
                }
            }
        }
    }

    engine.rootContext()->setContextProperty("picturesPath", QUrl::fromLocalFile(picturesDir));
    engine.rootContext()->setContextProperty("slideshowMode", slideshowMode);
    engine.rootContext()->setContextProperty("slideshowInterval", slideshowInterval * 1000); // Convert to milliseconds

    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
