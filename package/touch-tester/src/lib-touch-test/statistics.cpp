#include "statistics.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace TouchTest {

Statistics::Statistics()
{
}

Statistics::~Statistics()
{
}

void Statistics::addSample(double value)
{
    m_samples.push_back(value);
}

void Statistics::reset()
{
    m_samples.clear();
}

double Statistics::getMin() const
{
    if (m_samples.empty()) {
        return 0.0;
    }
    return *std::min_element(m_samples.begin(), m_samples.end());
}

double Statistics::getMax() const
{
    if (m_samples.empty()) {
        return 0.0;
    }
    return *std::max_element(m_samples.begin(), m_samples.end());
}

double Statistics::getAverage() const
{
    if (m_samples.empty()) {
        return 0.0;
    }

    double sum = 0.0;
    for (double sample : m_samples) {
        sum += sample;
    }
    return sum / m_samples.size();
}

double Statistics::getStdDev() const
{
    if (m_samples.size() < 2) {
        return 0.0;
    }

    double avg = getAverage();
    double sum_sq_diff = 0.0;

    for (double sample : m_samples) {
        double diff = sample - avg;
        sum_sq_diff += diff * diff;
    }

    return std::sqrt(sum_sq_diff / (m_samples.size() - 1));
}

double Statistics::getPercentile(int percentile) const
{
    if (m_samples.empty() || percentile < 0 || percentile > 100) {
        return 0.0;
    }

    // Create sorted copy
    std::vector<double> sorted = m_samples;
    std::sort(sorted.begin(), sorted.end());

    // Calculate index
    double index = (percentile / 100.0) * (sorted.size() - 1);
    size_t lower = static_cast<size_t>(std::floor(index));
    size_t upper = static_cast<size_t>(std::ceil(index));

    if (lower == upper) {
        return sorted[lower];
    }

    // Linear interpolation
    double weight = index - lower;
    return sorted[lower] * (1.0 - weight) + sorted[upper] * weight;
}

std::string Statistics::format(OutputFormat format, const std::string& label) const
{
    switch (format) {
        case OutputFormat::Human:
            return formatHuman(label);
        case OutputFormat::Json:
            return formatJson(label);
        case OutputFormat::Csv:
            return formatCsv(true);
        default:
            return "";
    }
}

std::string Statistics::formatHuman(const std::string& label) const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (!label.empty()) {
        oss << label << "\n";
    }

    if (m_samples.empty()) {
        oss << "No samples collected\n";
        return oss.str();
    }

    oss << "Statistics:\n";
    oss << "  Samples: " << m_samples.size() << "\n";
    oss << "  Min:     " << getMin() << " ms\n";
    oss << "  Max:     " << getMax() << " ms\n";
    oss << "  Avg:     " << getAverage() << " ms\n";
    oss << "  StdDev:  " << getStdDev() << " ms\n";
    oss << "  p50:     " << getPercentile(50) << " ms\n";
    oss << "  p95:     " << getPercentile(95) << " ms\n";
    oss << "  p99:     " << getPercentile(99) << " ms\n";

    return oss.str();
}

std::string Statistics::formatJson(const std::string& label) const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    oss << "{\n";

    if (!label.empty()) {
        oss << "  \"label\": \"" << label << "\",\n";
    }

    oss << "  \"samples\": " << m_samples.size() << ",\n";

    if (!m_samples.empty()) {
        oss << "  \"min_ms\": " << getMin() << ",\n";
        oss << "  \"max_ms\": " << getMax() << ",\n";
        oss << "  \"avg_ms\": " << getAverage() << ",\n";
        oss << "  \"stddev_ms\": " << getStdDev() << ",\n";
        oss << "  \"p50_ms\": " << getPercentile(50) << ",\n";
        oss << "  \"p95_ms\": " << getPercentile(95) << ",\n";
        oss << "  \"p99_ms\": " << getPercentile(99) << ",\n";

        // Individual measurements
        oss << "  \"measurements\": [";
        for (size_t i = 0; i < m_samples.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << m_samples[i];
        }
        oss << "]\n";
    }

    oss << "}\n";

    return oss.str();
}

std::string Statistics::formatCsv(bool include_header) const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (include_header) {
        oss << "sample_number,latency_ms\n";
    }

    for (size_t i = 0; i < m_samples.size(); ++i) {
        oss << (i + 1) << "," << m_samples[i] << "\n";
    }

    return oss.str();
}

} // namespace TouchTest
