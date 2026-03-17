#include "DemoSimulator.h"
#include <cstdlib>

// Telltale bit definitions (must match car-can-emulator and TelltaleRow.qml)
enum Telltale {
    TT_ENGINE    = (1 << 0),
    TT_OIL       = (1 << 1),
    TT_BATTERY   = (1 << 2),
    TT_BRAKE     = (1 << 3),
    TT_LEFT      = (1 << 4),
    TT_RIGHT     = (1 << 5),
    TT_HIGHBEAM  = (1 << 6),
    TT_DOOR      = (1 << 7),
    TT_SEATBELT  = (1 << 8),
    TT_ABS       = (1 << 9),
    TT_TRACTION  = (1 << 10),
    TT_TPMS      = (1 << 11),
};

// Drive cycle phases matching car-can-emulator simulate mode
//                          dur   spd0 spd1  rpm0  rpm1  tmp0 tmp1  set          clr
const DemoSimulator::Phase DemoSimulator::s_phases[] = {
    { 2000,    0,  40,   800, 2500,   70,  75,  TT_LEFT,     0            },  // Pull away
    { 2000,   40,  80,  2500, 3500,   75,  80,  0,           TT_LEFT      },  // Accel 1
    { 3000,   80, 120,  2000, 3500,   80,  85,  0,           0            },  // Accel 2
    { 8000,  120, 120,  2500, 2500,   85,  90,  TT_HIGHBEAM, 0            },  // Cruise
    { 3000,  120, 160,  2500, 4500,   90,  90,  0,           TT_HIGHBEAM  },  // Accel 3
    { 6000,  160, 160,  3200, 3200,   90,  90,  0,           0            },  // High cruise
    { 3000,  160, 200,  3200, 5500,   90,  92,  0,           0            },  // Accel 4
    { 4000,  200, 200,  4000, 4000,   92,  92,  0,           0            },  // Fast cruise
    { 4000,  200,  80,  4000, 1800,   92,  88,  TT_BRAKE,    0            },  // Hard brake
    { 3000,   80,  40,  1800, 1200,   88,  86,  TT_RIGHT,    0            },  // Coast
    { 3000,   40,   0,  1200,  800,   86,  85,  0,           TT_RIGHT     },  // Stop
    { 3000,    0,   0,   800,  800,   85,  80,  0,           TT_BRAKE     },  // Idle at stop
};

const int DemoSimulator::s_phaseCount = sizeof(s_phases) / sizeof(s_phases[0]);

DemoSimulator::DemoSimulator(QObject *parent)
    : QObject(parent)
    , m_phase(0)
    , m_phaseElapsed(0)
    , m_fuel(75)
    , m_telltales(0)
{
    connect(&m_timer, &QTimer::timeout, this, &DemoSimulator::tick);
}

void DemoSimulator::start()
{
    m_phase = 0;
    m_phaseElapsed = 0;
    m_fuel = 75;
    m_telltales = 0;
    m_timer.start(50);  // 50ms tick
}

void DemoSimulator::stop()
{
    m_timer.stop();
}

int DemoSimulator::lerp(int a, int b, double t) const
{
    return a + static_cast<int>((b - a) * t);
}

void DemoSimulator::tick()
{
    const Phase &p = s_phases[m_phase];

    // Apply telltale set/clear at phase start
    if (m_phaseElapsed == 0) {
        m_telltales |= p.telltaleSet;
        m_telltales &= ~p.telltaleClr;
        emit telltalesChanged(m_telltales);
    }

    double t = static_cast<double>(m_phaseElapsed) / p.durationMs;
    if (t > 1.0) t = 1.0;

    int rpm = lerp(p.rpmStart, p.rpmEnd, t);
    // Add small jitter for realism
    rpm += (std::rand() % 41) - 20;  // +/- 20
    if (rpm < 0) rpm = 0;

    emit speedChanged(lerp(p.speedStart, p.speedEnd, t));
    emit rpmChanged(rpm);
    emit coolantTempChanged(lerp(p.tempStart, p.tempEnd, t));

    // Fuel decreases slowly over the cycle
    int totalCycleMs = 0;
    for (int i = 0; i < s_phaseCount; i++)
        totalCycleMs += s_phases[i].durationMs;

    // Decrease fuel from 75 to ~60 over one full cycle
    int elapsedTotal = 0;
    for (int i = 0; i < m_phase; i++)
        elapsedTotal += s_phases[i].durationMs;
    elapsedTotal += m_phaseElapsed;
    m_fuel = 75 - (15 * elapsedTotal / totalCycleMs);
    emit fuelLevelChanged(m_fuel);

    // Battery voltage: ~12.6V when running, slight variation
    double batt = 12.6 + (std::rand() % 100 - 50) * 0.002;
    emit batteryVoltageChanged(batt);

    m_phaseElapsed += 50;
    if (m_phaseElapsed >= p.durationMs) {
        m_phaseElapsed = 0;
        m_phase++;
        if (m_phase >= s_phaseCount) {
            m_phase = 0;
            m_fuel = 75;  // Reset fuel on cycle restart
        }
    }
}
