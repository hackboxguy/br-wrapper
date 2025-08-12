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
    if (argc > 1) {
        picturesDir = argv[1];
    }
    
    engine.rootContext()->setContextProperty("picturesPath", QUrl::fromLocalFile(picturesDir));
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    
    if (engine.rootObjects().isEmpty())
        return -1;
    
    return app.exec();
}
