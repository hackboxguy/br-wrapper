#ifndef LATENCY_MEASURER_H
#define LATENCY_MEASURER_H

#include <time.h>

namespace TouchTest {

/**
 * @brief High-precision latency measurement
 *
 * Uses CLOCK_MONOTONIC_RAW for precise timing measurements
 * (immune to NTP adjustments)
 */
class LatencyMeasurer {
public:
    LatencyMeasurer();
    ~LatencyMeasurer();

    /**
     * @brief Start latency measurement (record start timestamp)
     */
    void start();

    /**
     * @brief End latency measurement and calculate elapsed time
     * @return Elapsed time in milliseconds since start(), or -1.0 if not started
     */
    double end();

    /**
     * @brief Get current timestamp
     * @return Current time in milliseconds since epoch
     */
    static double getCurrentTime();

    /**
     * @brief Calculate time difference between two timestamps
     * @param start_time Start timestamp
     * @param end_time End timestamp
     * @return Time difference in milliseconds
     */
    static double timeDiff(double start_time, double end_time);

    /**
     * @brief Reset measurement state
     */
    void reset();

    /**
     * @brief Check if measurement is in progress
     * @return true if started, false otherwise
     */
    bool isStarted() const { return m_started; }

    /**
     * @brief Get start timestamp
     * @return Start timestamp in milliseconds, or 0.0 if not started
     */
    double getStartTime() const { return m_startTime; }

private:
    bool m_started;
    double m_startTime;  // Milliseconds

    static double timespecToMilliseconds(const struct timespec& ts);
};

} // namespace TouchTest

#endif // LATENCY_MEASURER_H
