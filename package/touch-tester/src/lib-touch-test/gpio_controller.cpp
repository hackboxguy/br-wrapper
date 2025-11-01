#include "gpio_controller.h"
#include <unistd.h>
#include <cstring>
#include <cerrno>

namespace TouchTest {

GpioController::GpioController()
    : m_chip(nullptr)
    , m_line(nullptr)
    , m_gpioNum(0)
{
}

GpioController::~GpioController()
{
    cleanup();
}

bool GpioController::init(unsigned int gpio_num, const std::string& chip_name)
{
    // Clean up any existing resources
    cleanup();

    m_gpioNum = gpio_num;

    // Open GPIO chip
    m_chip = gpiod_chip_open_by_name(chip_name.c_str());
    if (!m_chip) {
        setError("Failed to open GPIO chip '" + chip_name + "': " + std::strerror(errno) +
                 "\nHint: Check if /dev/" + chip_name + " exists");
        return false;
    }

    // Get GPIO line
    m_line = gpiod_chip_get_line(m_chip, gpio_num);
    if (!m_line) {
        setError("Failed to get GPIO line " + std::to_string(gpio_num) + ": " + std::strerror(errno));
        cleanup();
        return false;
    }

    // Request line for output, initially low
    int ret = gpiod_line_request_output(m_line, "touch-tester", 0);
    if (ret < 0) {
        setError("Failed to request GPIO line " + std::to_string(gpio_num) + " as output: " +
                 std::strerror(errno) +
                 "\nHint: Check permissions (add user to 'gpio' group) or if line is already in use");
        cleanup();
        return false;
    }

    return true;
}

bool GpioController::setHigh()
{
    if (!m_line) {
        setError("GPIO not initialized");
        return false;
    }

    int ret = gpiod_line_set_value(m_line, 1);
    if (ret < 0) {
        setError("Failed to set GPIO high: " + std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

bool GpioController::setLow()
{
    if (!m_line) {
        setError("GPIO not initialized");
        return false;
    }

    int ret = gpiod_line_set_value(m_line, 0);
    if (ret < 0) {
        setError("Failed to set GPIO low: " + std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

bool GpioController::generatePulse(unsigned int pulse_width_ms)
{
    if (!m_line) {
        setError("GPIO not initialized");
        return false;
    }

    // Set high
    if (!setHigh()) {
        return false;
    }

    // Wait for pulse width (convert ms to microseconds)
    usleep(pulse_width_ms * 1000);

    // Set low
    if (!setLow()) {
        return false;
    }

    return true;
}

void GpioController::cleanup()
{
    if (m_line) {
        // Release the line
        gpiod_line_release(m_line);
        m_line = nullptr;
    }

    if (m_chip) {
        // Close the chip
        gpiod_chip_close(m_chip);
        m_chip = nullptr;
    }
}

void GpioController::setError(const std::string& error)
{
    m_lastError = error;
}

} // namespace TouchTest
