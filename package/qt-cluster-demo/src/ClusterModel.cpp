#include "ClusterModel.h"

ClusterModel::ClusterModel(QObject *parent)
    : QObject(parent)
{
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
