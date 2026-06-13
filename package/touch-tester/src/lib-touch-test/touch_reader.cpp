#include "touch_reader.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <fstream>
#include <sstream>
#include <dirent.h>

// Bit manipulation macros for input device capabilities
#ifndef NBITS
#define NBITS(x) ((((x)-1)/(sizeof(long)*8))+1)
#endif

#ifndef test_bit
#define test_bit(bit, array) ((array)[(bit)/(sizeof(long)*8)] >> ((bit)%(sizeof(long)*8)) & 1)
#endif

namespace TouchTest {

TouchReader::TouchReader()
    : m_fd(-1)
    , m_currentX(0)
    , m_currentY(0)
    , m_currentTrackingId(-1)
    , m_currentPressure(0)
    , m_hasTouchDown(false)
{
}

TouchReader::~TouchReader()
{
    close();
}

bool TouchReader::open(const std::string& device_path)
{
    // Close any existing device
    close();

    // Open device
    m_fd = ::open(device_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (m_fd < 0) {
        setError("Failed to open device '" + device_path + "': " + std::strerror(errno) +
                 "\nHint: Check permissions (add user to 'input' group)");
        return false;
    }

    m_devicePath = device_path;

    // Read device name
    if (!readDeviceName()) {
        // Not critical, continue anyway
    }

    // Reset state
    m_currentX = 0;
    m_currentY = 0;
    m_currentTrackingId = -1;
    m_currentPressure = 0;
    m_hasTouchDown = false;

    return true;
}

bool TouchReader::autoDiscover()
{
    std::string device = findTouchDevice();
    if (device.empty()) {
        setError("Auto-discovery failed: No touchscreen device found\n"
                 "Hint: Check /proc/bus/input/devices or use 'evtest' to find your device");
        return false;
    }

    return open(device);
}

bool TouchReader::readEvent(TouchEvent& event)
{
    if (m_fd < 0) {
        setError("Device not open");
        return false;
    }

    struct input_event ev;
    ssize_t bytes = read(m_fd, &ev, sizeof(ev));

    if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data available (non-blocking)
            return false;
        }
        setError("Read error: " + std::string(std::strerror(errno)));
        return false;
    }

    if (bytes != sizeof(ev)) {
        setError("Partial read: got " + std::to_string(bytes) + " bytes, expected " + std::to_string(sizeof(ev)));
        return false;
    }

    // Parse event
    switch (ev.type) {
        case EV_ABS:
            switch (ev.code) {
                case ABS_MT_POSITION_X:
                    m_currentX = ev.value;
                    break;
                case ABS_MT_POSITION_Y:
                    m_currentY = ev.value;
                    break;
                case ABS_MT_TRACKING_ID:
                    m_currentTrackingId = ev.value;
                    break;
                case ABS_MT_PRESSURE:
                    m_currentPressure = ev.value;
                    break;
                default:
                    break;
            }
            break;

        case EV_KEY:
            if (ev.code == BTN_TOUCH) {
                if (ev.value == 1) {
                    // Touch down
                    m_hasTouchDown = true;
                } else if (ev.value == 0) {
                    // Touch up - emit event immediately
                    event.type = TouchEventType::TouchUp;
                    event.timestamp = eventTimeToSeconds(ev.time);
                    event.x = m_currentX;
                    event.y = m_currentY;
                    event.tracking_id = m_currentTrackingId;
                    event.pressure = 0;
                    m_hasTouchDown = false;
                    return true;
                }
            }
            break;

        case EV_SYN:
            if (ev.code == SYN_REPORT) {
                // End of event sequence - emit touch-down if we have one
                if (m_hasTouchDown && m_currentTrackingId >= 0) {
                    event.type = TouchEventType::TouchDown;
                    event.timestamp = eventTimeToSeconds(ev.time);
                    event.x = m_currentX;
                    event.y = m_currentY;
                    event.tracking_id = m_currentTrackingId;
                    event.pressure = m_currentPressure;
                    return true;
                }
            }
            break;

        default:
            break;
    }

    return false;
}

bool TouchReader::waitForEvent(TouchEvent& event, int timeout_ms)
{
    if (m_fd < 0) {
        setError("Device not open");
        return false;
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(m_fd, &readfds);

    struct timeval timeout;
    struct timeval* timeout_ptr = nullptr;

    if (timeout_ms > 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        timeout_ptr = &timeout;
    }

    int ret = select(m_fd + 1, &readfds, nullptr, nullptr, timeout_ptr);

    if (ret < 0) {
        setError("Select error: " + std::string(std::strerror(errno)));
        return false;
    }

    if (ret == 0) {
        // Timeout
        return false;
    }

    // Data available, read events until we get a complete touch event
    while (true) {
        if (readEvent(event)) {
            return true;
        }

        // Check if more data is available
        fd_set check_fds;
        FD_ZERO(&check_fds);
        FD_SET(m_fd, &check_fds);

        struct timeval zero_timeout = {0, 0};
        ret = select(m_fd + 1, &check_fds, nullptr, nullptr, &zero_timeout);

        if (ret <= 0) {
            // No more data available
            break;
        }
    }

    return false;
}

void TouchReader::flush()
{
    if (m_fd < 0) {
        return;
    }

    // Flush at raw input_event level, not TouchEvent level
    // This ensures we drain ALL events including incomplete touch sequences
    struct input_event ev;
    while (read(m_fd, &ev, sizeof(ev)) > 0) {
        // Keep reading until buffer is empty (EAGAIN/EWOULDBLOCK)
    }

    // Reset state tracking to avoid carrying over partial state
    m_currentX = 0;
    m_currentY = 0;
    m_currentTrackingId = -1;
    m_currentPressure = 0;
    m_hasTouchDown = false;
}

void TouchReader::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
        m_devicePath.clear();
        m_deviceName.clear();
    }
}

bool TouchReader::readDeviceName()
{
    if (m_fd < 0) {
        return false;
    }

    char name[256] = "Unknown";
    if (ioctl(m_fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
        m_deviceName = name;
        return true;
    }

    return false;
}

std::string TouchReader::findTouchDevice()
{
    // Parse /proc/bus/input/devices to find touchscreen
    std::ifstream devices_file("/proc/bus/input/devices");
    if (!devices_file.is_open()) {
        return "";
    }

    std::string line;
    std::string current_name;
    std::string current_handlers;
    bool is_touchscreen = false;

    while (std::getline(devices_file, line)) {
        if (line.empty()) {
            // End of device entry
            if (is_touchscreen && !current_handlers.empty()) {
                // Extract event handler
                size_t event_pos = current_handlers.find("event");
                if (event_pos != std::string::npos) {
                    size_t space_pos = current_handlers.find(' ', event_pos);
                    std::string event_name;
                    if (space_pos != std::string::npos) {
                        event_name = current_handlers.substr(event_pos, space_pos - event_pos);
                    } else {
                        event_name = current_handlers.substr(event_pos);
                    }
                    return "/dev/input/" + event_name;
                }
            }

            // Reset for next device
            current_name.clear();
            current_handlers.clear();
            is_touchscreen = false;
            continue;
        }

        if (line[0] == 'N') {
            // Name line
            size_t quote1 = line.find('"');
            size_t quote2 = line.rfind('"');
            if (quote1 != std::string::npos && quote2 != std::string::npos && quote2 > quote1) {
                current_name = line.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        } else if (line[0] == 'H') {
            // Handlers line
            size_t handlers_pos = line.find("Handlers=");
            if (handlers_pos != std::string::npos) {
                current_handlers = line.substr(handlers_pos + 9);
            }
        } else if (line[0] == 'B' && line.find("EV=") != std::string::npos) {
            // Check if device has ABS (absolute) events (bit 3 set)
            // Touchscreens typically have EV=b or higher (1011 binary = ABS + KEY + SYN)
            size_t ev_pos = line.find("EV=");
            if (ev_pos != std::string::npos) {
                std::string ev_value = line.substr(ev_pos + 3);
                // Simple heuristic: if EV value contains 'b' or higher hex digit, likely a touchscreen
                if (ev_value.find_first_of("bcdef") != std::string::npos) {
                    is_touchscreen = true;
                }
            }
        }
    }

    // Fallback: try common event devices
    for (int i = 0; i < 10; i++) {
        std::string device = "/dev/input/event" + std::to_string(i);
        if (hasAbsMultiTouch(device)) {
            return device;
        }
    }

    return "";
}

bool TouchReader::hasAbsMultiTouch(const std::string& device_path)
{
    int fd = ::open(device_path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    // Check if device supports ABS_MT_POSITION_X
    unsigned long abs_bits[NBITS(ABS_MAX)] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) >= 0) {
        bool has_mt = test_bit(ABS_MT_POSITION_X, abs_bits) &&
                      test_bit(ABS_MT_POSITION_Y, abs_bits);
        ::close(fd);
        return has_mt;
    }

    ::close(fd);
    return false;
}

double TouchReader::eventTimeToSeconds(const struct timeval& tv)
{
    return tv.tv_sec + (tv.tv_usec / 1000000.0);
}

void TouchReader::setError(const std::string& error)
{
    m_lastError = error;
}

} // namespace TouchTest
