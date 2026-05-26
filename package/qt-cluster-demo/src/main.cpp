#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDebug>

#include "ClusterModel.h"
#include "CanReader.h"
#include "DemoSimulator.h"
#include "FpgaController.h"

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

    QCommandLineOption noSweepOption(
        "no-sweep", "Skip startup diagnostic sweep");
    parser.addOption(noSweepOption);

    QCommandLineOption i2cOption(
        QStringList() << "i" << "i2c",
        "FPGA I2C bus device (default: /dev/i2c-1)", "i2c", "/dev/i2c-1");
    parser.addOption(i2cOption);

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
        // Any successful OBD2 response means connected
        QObject::connect(canReader, &CanReader::speedChanged, [&model](int) {
            model.setCanConnected(true);
        });
        QObject::connect(canReader, &CanReader::rpmChanged, [&model](int) {
            model.setCanConnected(true);
        });
        QObject::connect(canReader, &CanReader::coolantTempChanged, [&model](int) {
            model.setCanConnected(true);
        });
        QObject::connect(canReader, &CanReader::fuelLevelChanged, [&model](int) {
            model.setCanConnected(true);
        });
        QObject::connect(canReader, &CanReader::batteryVoltageChanged, [&model](double) {
            model.setCanConnected(true);
        });
        // Don't pre-set canConnected — wait for actual data
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

    // FPGA controller for optional local-dimming / pixel-compensation overlay toggles
    FpgaController fpgaController;
    fpgaController.setI2cBus(parser.value(i2cOption));
    fpgaController.start();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("cluster", &model);
    engine.rootContext()->setContextProperty("fpga", &fpgaController);
    engine.load(QUrl("qrc:/qml/main.qml"));

    if (engine.rootObjects().isEmpty())
        return -1;

    // Start data source (after sweep if enabled)
    auto startDataSource = [&]() {
        if (canReader)
            canReader->start();
        if (demoSim)
            demoSim->start();
    };

    if (parser.isSet(noSweepOption)) {
        startDataSource();
    } else {
        QObject::connect(&model, &ClusterModel::startupFinished, startDataSource);
        model.startDiagnosticSweep();
    }

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
