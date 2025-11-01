#ifndef TOUCH_READER_H
#define TOUCH_READER_H

#include <string>
#include <linux/input.h>

namespace TouchTest {

/**
 * @brief Touch event types
 */
enum class TouchEventType {
    None,
    TouchDown,
    TouchUp,
    Move
};

/**
 * @brief Touch event data
 */
struct TouchEvent {
    TouchEventType type;
    double timestamp;      // Seconds since epoch (with microsecond precision)
    int x;                 // Touch X coordinate
    int y;                 // Touch Y coordinate
    int tracking_id;       // Multi-touch tracking ID (-1 for touch-up)
    int pressure;          // Touch pressure

    TouchEvent()
        : type(TouchEventType::None)
        , timestamp(0.0)
        , x(0)
        , y(0)
        , tracking_id(-1)
        , pressure(0)
    {}
};

/**
 * @brief Touch input device reader
 *
 * Reads and parses touch events from Linux input event devices
 */
class TouchReader {
public:
    TouchReader();
    ~TouchReader();

    /**
     * @brief Open input device by path
     * @param device_path Path to input device (e.g., "/dev/input/event6")
     * @return true on success, false on failure
     */
    bool open(const std::string& device_path);

    /**
     * @brief Auto-discover and open touchscreen device
     * @return true on success, false on failure
     */
    bool autoDiscover();

    /**
     * @brief Read next touch event (non-blocking)
     * @param event Output event structure
     * @return true if event was read, false if no event available
     */
    bool readEvent(TouchEvent& event);

    /**
     * @brief Wait for next touch event (blocking with timeout)
     * @param event Output event structure
     * @param timeout_ms Timeout in milliseconds (0 = no timeout)
     * @return true if event was read, false on timeout or error
     */
    bool waitForEvent(TouchEvent& event, int timeout_ms = 0);

    /**
     * @brief Close input device
     */
    void close();

    /**
     * @brief Check if device is open and ready
     * @return true if open, false otherwise
     */
    bool isOpen() const { return m_fd >= 0; }

    /**
     * @brief Get last error message
     * @return Error message string
     */
    std::string getLastError() const { return m_lastError; }

    /**
     * @brief Get opened device path
     * @return Device path, or empty string if not open
     */
    std::string getDevicePath() const { return m_devicePath; }

    /**
     * @brief Get device name/description
     * @return Device name, or empty string if not open
     */
    std::string getDeviceName() const { return m_deviceName; }

private:
    int m_fd;
    std::string m_devicePath;
    std::string m_deviceName;
    std::string m_lastError;

    // State tracking for multi-event touch sequences
    int m_currentX;
    int m_currentY;
    int m_currentTrackingId;
    int m_currentPressure;
    bool m_hasTouchDown;

    void setError(const std::string& error);
    bool readDeviceName();
    std::string findTouchDevice();
    bool hasAbsMultiTouch(const std::string& device_path);
    double eventTimeToSeconds(const struct timeval& tv);
};

} // namespace TouchTest

#endif // TOUCH_READER_H
