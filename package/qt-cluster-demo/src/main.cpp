#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDebug>

#include "ClusterModel.h"
#include "CanReader.h"
#include "DemoSimulator.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("qt-cluster-demo");
    app.setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Automotive instrument cluster demo");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption canOption(
        "can", "CAN interface name (default: can0)", "interface", "can0");
    parser.addOption(canOption);

    QCommandLineOption demoOption(
        "demo", "Demo mode (synthetic values, no CAN)");
    parser.addOption(demoOption);

    QCommandLineOption fullscreenOption(
        "fullscreen", "Force fullscreen");
    parser.addOption(fullscreenOption);

    parser.process(app);

    ClusterModel model;
    CanReader *canReader = nullptr;
    DemoSimulator *demoSim = nullptr;
    bool demoMode = parser.isSet(demoOption);

    if (!demoMode) {
        canReader = new CanReader(parser.value(canOption));
        if (!canReader->open()) {
            qWarning() << "CAN open failed, falling back to demo mode";
            delete canReader;
            canReader = nullptr;
            demoMode = true;
        }
    }

    if (canReader) {
        // Connect CAN reader signals to model
        QObject::connect(canReader, &CanReader::speedChanged,
                         &model, &ClusterModel::setSpeed);
        QObject::connect(canReader, &CanReader::rpmChanged,
                         &model, &ClusterModel::setRpm);
        QObject::connect(canReader, &CanReader::coolantTempChanged,
                         &model, &ClusterModel::setCoolantTemp);
        QObject::connect(canReader, &CanReader::fuelLevelChanged,
                         &model, &ClusterModel::setFuelLevel);
        QObject::connect(canReader, &CanReader::batteryVoltageChanged,
                         &model, &ClusterModel::setBatteryVoltage);
        QObject::connect(canReader, &CanReader::telltalesChanged,
                         &model, &ClusterModel::setTelltales);
        QObject::connect(canReader, &CanReader::canTimeout, [&model]() {
            model.setCanConnected(false);
        });
        // Any successful data means connected
        QObject::connect(canReader, &CanReader::speedChanged, [&model](int) {
            model.setCanConnected(true);
        });

        model.setCanConnected(true);
    }

    if (demoMode) {
        demoSim = new DemoSimulator();
        QObject::connect(demoSim, &DemoSimulator::speedChanged,
                         &model, &ClusterModel::setSpeed);
        QObject::connect(demoSim, &DemoSimulator::rpmChanged,
                         &model, &ClusterModel::setRpm);
        QObject::connect(demoSim, &DemoSimulator::coolantTempChanged,
                         &model, &ClusterModel::setCoolantTemp);
        QObject::connect(demoSim, &DemoSimulator::fuelLevelChanged,
                         &model, &ClusterModel::setFuelLevel);
        QObject::connect(demoSim, &DemoSimulator::batteryVoltageChanged,
                         &model, &ClusterModel::setBatteryVoltage);
        QObject::connect(demoSim, &DemoSimulator::telltalesChanged,
                         &model, &ClusterModel::setTelltales);
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("cluster", &model);
    engine.load(QUrl("qrc:/qml/main.qml"));

    if (engine.rootObjects().isEmpty())
        return -1;

    // Run startup diagnostic sweep, then start data source
    QObject::connect(&model, &ClusterModel::startupFinished, [&]() {
        if (canReader)
            canReader->start();
        if (demoSim)
            demoSim->start();
    });
    model.startDiagnosticSweep();

    int ret = app.exec();

    // Cleanup
    if (canReader)
        canReader->stop();
    if (demoSim)
        demoSim->stop();

    delete canReader;
    delete demoSim;

    return ret;
}
