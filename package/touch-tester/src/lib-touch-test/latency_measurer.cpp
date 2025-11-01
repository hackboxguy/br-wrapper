#include "latency_measurer.h"

namespace TouchTest {

LatencyMeasurer::LatencyMeasurer()
    : m_started(false)
    , m_startTime(0.0)
{
}

LatencyMeasurer::~LatencyMeasurer()
{
}

void LatencyMeasurer::start()
{
    m_startTime = getCurrentTime();
    m_started = true;
}

double LatencyMeasurer::end()
{
    if (!m_started) {
        return -1.0;
    }

    double endTime = getCurrentTime();
    double latency = endTime - m_startTime;

    // Reset state after measurement
    m_started = false;

    return latency;
}

double LatencyMeasurer::getCurrentTime()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return timespecToMilliseconds(ts);
}

double LatencyMeasurer::timeDiff(double start_time, double end_time)
{
    return end_time - start_time;
}

void LatencyMeasurer::reset()
{
    m_started = false;
    m_startTime = 0.0;
}

double LatencyMeasurer::timespecToMilliseconds(const struct timespec& ts)
{
    return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
}

} // namespace TouchTest
