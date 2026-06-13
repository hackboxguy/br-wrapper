#ifndef STATISTICS_H
#define STATISTICS_H

#include <vector>
#include <string>

namespace TouchTest {

/**
 * @brief Output format for statistics
 */
enum class OutputFormat {
    Human,      // Human-readable text
    Json,       // JSON format
    Csv         // CSV format
};

/**
 * @brief Statistical analysis of latency measurements
 */
class Statistics {
public:
    Statistics();
    ~Statistics();

    /**
     * @brief Add a measurement sample
     * @param value Measurement value (typically latency in ms)
     */
    void addSample(double value);

    /**
     * @brief Clear all samples
     */
    void reset();

    /**
     * @brief Get number of samples
     * @return Sample count
     */
    size_t getSampleCount() const { return m_samples.size(); }

    /**
     * @brief Get all samples
     * @return Vector of all samples
     */
    const std::vector<double>& getSamples() const { return m_samples; }

    /**
     * @brief Get minimum value
     * @return Minimum value, or 0.0 if no samples
     */
    double getMin() const;

    /**
     * @brief Get maximum value
     * @return Maximum value, or 0.0 if no samples
     */
    double getMax() const;

    /**
     * @brief Get average (mean) value
     * @return Average value, or 0.0 if no samples
     */
    double getAverage() const;

    /**
     * @brief Get standard deviation
     * @return Standard deviation, or 0.0 if no samples
     */
    double getStdDev() const;

    /**
     * @brief Get percentile value
     * @param percentile Percentile (0-100), e.g., 50 for median, 95 for p95
     * @return Percentile value, or 0.0 if no samples
     */
    double getPercentile(int percentile) const;

    /**
     * @brief Format statistics as string
     * @param format Output format (Human, Json, or Csv)
     * @param label Optional label/description for the data
     * @return Formatted string
     */
    std::string format(OutputFormat format, const std::string& label = "") const;

    /**
     * @brief Format individual measurements as CSV
     * @param include_header Include CSV header row
     * @return CSV formatted string
     */
    std::string formatCsv(bool include_header = true) const;

private:
    std::vector<double> m_samples;

    std::string formatHuman(const std::string& label) const;
    std::string formatJson(const std::string& label) const;
};

} // namespace TouchTest

#endif // STATISTICS_H
