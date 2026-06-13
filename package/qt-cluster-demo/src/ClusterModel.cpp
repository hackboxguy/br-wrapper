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
    m_sweepElapsed = 0;

    // All telltales ON
    setTelltales(0x0FFF);  // all 12 bits
    setBatteryVoltage(12.6);

    m_sweepTimer.setSingleShot(false);
    m_sweepTimer.start(50);  // 50ms tick
}

void ClusterModel::sweepTick()
{
    m_sweepElapsed += 50;
    int totalMs = SWEEP_UP_MS + SWEEP_HOLD_MS + SWEEP_DOWN_MS;

    if (m_sweepElapsed <= SWEEP_UP_MS) {
        // Phase 1: sweep up to max
        double t = (double)m_sweepElapsed / SWEEP_UP_MS;
        setSpeed((int)(260 * t));
        setRpm((int)(8000 * t));
        setCoolantTemp((int)(60 + 70 * t));  // 60 -> 130
        setFuelLevel((int)(100 * t));
    }
    else if (m_sweepElapsed <= SWEEP_UP_MS + SWEEP_HOLD_MS) {
        // Phase 2: hold at max
        setSpeed(260);
        setRpm(8000);
        setCoolantTemp(130);
        setFuelLevel(100);
    }
    else if (m_sweepElapsed <= totalMs) {
        // Phase 3: sweep back down
        double t = (double)(m_sweepElapsed - SWEEP_UP_MS - SWEEP_HOLD_MS) / SWEEP_DOWN_MS;
        setSpeed((int)(260 * (1.0 - t)));
        setRpm((int)(8000 * (1.0 - t)));
        setCoolantTemp((int)(130 - 70 * t));  // 130 -> 60
        setFuelLevel((int)(100 * (1.0 - t)));
        // Turn off telltales partway through the down sweep
        if (t > 0.3)
            setTelltales(0);
    }
    else {
        // Done
        m_sweepTimer.stop();
        setSpeed(0);
        setRpm(0);
        setCoolantTemp(20);
        setFuelLevel(0);
        setTelltales(0);
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
