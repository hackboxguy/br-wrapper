#include "ClusterModel.h"

ClusterModel::ClusterModel(QObject *parent)
    : QObject(parent)
{
    connect(&m_sweepTimer, &QTimer::timeout, this, &ClusterModel::sweepTick);
}

void ClusterModel::startDiagnosticSweep()
{
    m_startupActive = true;
    emit startupActiveChanged(true);
    m_sweepPhase = 0;

    // Phase 0: set everything to max instantly — QML SmoothedAnimation
    // handles the visual sweep up smoothly
    setTelltales(0x0FFF);  // all 12 bits
    setBatteryVoltage(12.6);
    setSpeed(260);
    setRpm(8000);
    setCoolantTemp(130);
    setFuelLevel(100);

    // Wait for needles to reach max + hold, then trigger phase 1
    // At 250 deg/sec, 270 degrees takes ~1.1s + 0.7s hold
    m_sweepTimer.setSingleShot(true);
    m_sweepTimer.start(2500);
}

void ClusterModel::sweepTick()
{
    m_sweepPhase++;

    if (m_sweepPhase == 1) {
        // Set everything to zero — QML SmoothedAnimation sweeps back down
        setSpeed(0);
        setRpm(0);
        setCoolantTemp(20);
        setFuelLevel(0);
        setTelltales(0);

        // Wait for needles to settle at zero, then finish
        // ~1.1s travel + 0.4s settle
        m_sweepTimer.start(2500);
    }
    else {
        // Done — start data source
        m_startupActive = false;
        emit startupActiveChanged(false);
        emit startupFinished();
    }
}

void ClusterModel::setSpeed(int v)
{
    if (m_speed != v) {
        m_speed = v;
        emit speedChanged(v);
    }
}

void ClusterModel::setRpm(int v)
{
    if (m_rpm != v) {
        m_rpm = v;
        emit rpmChanged(v);
    }
}

void ClusterModel::setCoolantTemp(int v)
{
    if (m_coolantTemp != v) {
        m_coolantTemp = v;
        emit coolantTempChanged(v);
    }
}

void ClusterModel::setFuelLevel(int v)
{
    if (m_fuelLevel != v) {
        m_fuelLevel = v;
        emit fuelLevelChanged(v);
    }
}

void ClusterModel::setBatteryVoltage(double v)
{
    if (m_batteryVoltage != v) {
        m_batteryVoltage = v;
        emit batteryVoltageChanged(v);
    }
}

void ClusterModel::setTelltales(quint16 v)
{
    if (m_telltales != v) {
        m_telltales = v;
        emit telltalesChanged(v);
    }
}

void ClusterModel::setCanConnected(bool v)
{
    if (m_canConnected != v) {
        m_canConnected = v;
        emit canConnectedChanged(v);
    }
}
