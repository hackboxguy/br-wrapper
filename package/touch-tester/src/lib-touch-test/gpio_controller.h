#ifndef GPIO_CONTROLLER_H
#define GPIO_CONTROLLER_H

#include <string>
#include <gpiod.h>

namespace TouchTest {

/**
 * @brief GPIO controller for triggering capacitive auto-clicker
 *
 * Uses libgpiod for modern GPIO control via character device interface
 */
class GpioController {
public:
    GpioController();
    ~GpioController();

    /**
     * @brief Initialize GPIO line for output
     * @param gpio_num GPIO line number (e.g., 27 for GPIO27)
     * @param chip_name GPIO chip name (default: "gpiochip0")
     * @return true on success, false on failure
     */
    bool init(unsigned int gpio_num, const std::string& chip_name = "gpiochip0");

    /**
     * @brief Initialize probe GPIO line for oscilloscope timing
     * @param probe_num Probe GPIO line number (e.g., 7 for GPIO7)
     * @param chip_name GPIO chip name (default: "gpiochip0")
     * @return true on success, false on failure
     */
    bool initProbe(unsigned int probe_num, const std::string& chip_name = "gpiochip0");

    /**
     * @brief Set GPIO line high (1)
     * @return true on success, false on failure
     */
    bool setHigh();

    /**
     * @brief Set GPIO line low (0)
     * @return true on success, false on failure
     */
    bool setLow();

    /**
     * @brief Generate a pulse on the GPIO line
     * @param pulse_width_ms Pulse width in milliseconds
     * @return true on success, false on failure
     */
    bool generatePulse(unsigned int pulse_width_ms);

    /**
     * @brief Set probe GPIO high (for oscilloscope timing)
     * @return true on success, false on failure
     */
    bool setProbeHigh();

    /**
     * @brief Set probe GPIO low (for oscilloscope timing)
     * @return true on success, false on failure
     */
    bool setProbeLow();

    /**
     * @brief Check if probe GPIO is initialized
     * @return true if probe is initialized, false otherwise
     */
    bool isProbeInitialized() const { return m_probeLine != nullptr; }

    /**
     * @brief Release GPIO resources
     */
    void cleanup();

    /**
     * @brief Check if GPIO is initialized and ready
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const { return m_line != nullptr; }

    /**
     * @brief Get last error message
     * @return Error message string
     */
    std::string getLastError() const { return m_lastError; }

    /**
     * @brief Get GPIO line number
     * @return GPIO line number, or -1 if not initialized
     */
    int getGpioNumber() const { return m_gpioNum; }

private:
    struct gpiod_chip* m_chip;
    struct gpiod_line* m_line;
    unsigned int m_gpioNum;

    struct gpiod_chip* m_probeChip;
    struct gpiod_line* m_probeLine;
    unsigned int m_probeNum;

    std::string m_lastError;

    void setError(const std::string& error);
};

} // namespace TouchTest

#endif // GPIO_CONTROLLER_H
