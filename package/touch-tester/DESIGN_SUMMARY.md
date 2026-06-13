# Touch-Tester Design Summary

## Overview
Touch-tester is a comprehensive touch latency measurement and testing utility for embedded Linux systems (Buildroot & Raspberry Pi OS). It consists of three components:
1. **lib-touch-test**: Shared core library for GPIO control, touch event reading, and latency measurement
2. **touch-tester-cli**: Command-line tool for automated testing
3. **qt-touch-tester**: Qt5-based GUI application with network daemon mode

## Architecture

### Directory Structure
```
/home/testpc/git-repos/tmp/br-wrapper/package/touch-tester/
├── CMakeLists.txt                    # Root CMake configuration
├── Config.in                         # Buildroot menuconfig
├── touch-tester.mk                   # Buildroot package makefile
├── README.md                         # Dependencies & usage documentation
├── DESIGN_SUMMARY.md                 # This file
└── src/
    ├── CMakeLists.txt                # Source-level CMake
    ├── lib-touch-test/               # Core library
    │   ├── CMakeLists.txt
    │   ├── gpio_controller.cpp/.h    # GPIO control via libgpiod
    │   ├── touch_reader.cpp/.h       # Input event parsing
    │   ├── latency_measurer.cpp/.h   # Timing & measurement
    │   └── statistics.cpp/.h         # Statistical analysis
    ├── touch-tester-cli/             # CLI tool
    │   ├── CMakeLists.txt
    │   └── main.cpp
    └── qt-touch-tester/              # Qt GUI app
        ├── CMakeLists.txt
        ├── main.cpp
        ├── NetworkInterface.cpp/.h   # Socket API (from qt-demo-launcher)
        ├── config.h                  # Default port configuration
        └── qt-touch-tester.service   # Systemd service file
```

## Component Details

### 1. lib-touch-test (Shared Library)

#### gpio_controller.cpp/.h
- **Purpose**: Control GPIO pins to trigger capacitive auto-clicker
- **Technology**: libgpiod (character device `/dev/gpiochip0`)
- **Key Functions**:
  - `bool initGPIO(int gpio_num)` - Initialize GPIO line
  - `void setPulse(int gpio, int pulse_width_ms)` - Generate touch pulse
  - `void setHigh(int gpio)` / `void setLow(int gpio)` - Basic control
  - `void cleanup()` - Release GPIO resources
- **Timing**: Uses high-resolution timers for precise pulse width

#### touch_reader.cpp/.h
- **Purpose**: Read and parse touch events from `/dev/input/eventX`
- **Technology**: Linux input subsystem (evdev)
- **Key Functions**:
  - `bool openInputDevice(const QString &device)` - Open specific device
  - `QString autoDiscoverTouchDevice()` - Parse `/proc/bus/input/devices`
  - `TouchEvent readEvent()` - Non-blocking event read
  - `bool waitForEvent(int timeout_ms)` - Blocking wait with timeout
- **Event Types Tracked**:
  - `BTN_TOUCH` (value 1 = touch-down, 0 = touch-up)
  - `ABS_MT_POSITION_X/Y` - Touch coordinates
  - `ABS_MT_TRACKING_ID` - Multi-touch tracking (for future)
  - `SYN_REPORT` - Event synchronization

#### latency_measurer.cpp/.h
- **Purpose**: Measure time between GPIO trigger and touch event reception
- **Technology**: `clock_gettime(CLOCK_MONOTONIC_RAW)`
- **Key Functions**:
  - `void startMeasurement()` - Record GPIO trigger timestamp
  - `double endMeasurement()` - Calculate latency in milliseconds
  - `void reset()` - Clear measurement state
- **Resolution**: Microsecond-level precision
- **Notes**: Uses CLOCK_MONOTONIC_RAW (immune to NTP adjustments)

#### statistics.cpp/.h
- **Purpose**: Aggregate and analyze latency data
- **Key Functions**:
  - `void addSample(double latency_ms)` - Add measurement
  - `double getMin/Max/Avg()` - Basic stats
  - `double getStdDev()` - Standard deviation
  - `double getPercentile(int p)` - p50, p95, p99 percentiles
  - `void reset()` - Clear all samples
- **Output Formats**: Human-readable, JSON, CSV

### 2. touch-tester-cli (Command-Line Tool)

#### Command-Line Arguments
```bash
# Basic latency measurement
./touch-tester --testtype=latencymeasure --inputevent=/dev/input/event6 \
               --output-gpio=27 --loopcount=10 --pulsewidth-ms=25 --wait-ms=50

# Auto-discover input device
./touch-tester --testtype=latencymeasure --inputevent=auto \
               --output-gpio=27 --loopcount=10 --pulsewidth-ms=25 --wait-ms=50

# Touch count test (detect missed events)
./touch-tester --testtype=touchcount --inputevent=/dev/input/event6 \
               --output-gpio=27 --loopcount=100 --pulsewidth-ms=25 --wait-ms=20

# Pure trigger (no measurement)
./touch-tester --testtype=touchtrigger --output-gpio=27 \
               --loopcount=10 --pulsewidth-ms=25 --wait-ms=50

# Stress test (find breaking point)
./touch-tester --testtype=stresstest --inputevent=auto --output-gpio=27 \
               --duration=60 --pulsewidth-ms=25 --wait-ms=10

# Verification mode (single touch, verbose)
./touch-tester --testtype=verify --inputevent=auto --output-gpio=27 \
               --pulsewidth-ms=25 --verbose

# Calibration mode (with Rigol oscilloscope - FUTURE)
./touch-tester --testtype=calibrate --output-gpio=27 --pulsewidth-ms=25 \
               --rigol-script=/path/to/rigol_query.py
```

#### Argument Reference
| Argument | Description | Default | Notes |
|----------|-------------|---------|-------|
| `--testtype` | Test mode (latencymeasure, touchcount, touchtrigger, stresstest, verify, calibrate) | Required | |
| `--inputevent` | Input device path or "auto" | auto | Auto-discovers touchscreen |
| `--output-gpio` | GPIO pin number(s) for pulse output | Required | Comma-separated for multi-touch (future) |
| `--loopcount` | Number of test iterations | 1 | Alternative to --duration |
| `--duration` | Test duration in seconds | - | Alternative to --loopcount |
| `--pulsewidth-ms` | Touch pulse width in milliseconds | 25 | Typical range: 10-100ms |
| `--wait-ms` | Wait time between pulses | 50 | Minimum recommended: 50ms (warning if <50) |
| `--format` | Output format (human, json, csv) | human | |
| `--verbose` | Enable verbose debug output | false | |
| `--rtpriority` | Real-time scheduling priority (1-99) | 0 (disabled) | Requires PREEMPT_RT kernel |
| `--cpuaffinity` | Pin to specific CPU core | -1 (disabled) | |
| `--rigol-script` | Path to rigol_query.py for calibration | auto-detect | FUTURE FEATURE |

#### Output Examples

**Human-Readable (Default)**
```
Touch Latency Measurement
=========================
Configuration:
  Input Device: /dev/input/event6 (Himax Touchscreen)
  GPIO Pin: 27
  Pulse Width: 25ms
  Wait Time: 50ms
  Loop Count: 10

Results:
  Samples Collected: 10
  Successful Events: 10
  Missed Events: 0

  Latency Statistics:
    Min: 15.2ms
    Max: 24.8ms
    Avg: 19.3ms
    StdDev: 2.1ms
    p50 (Median): 19.1ms
    p95: 23.5ms
    p99: 24.6ms

Individual Measurements:
  #1: 19.2ms
  #2: 18.5ms
  #3: 20.1ms
  ...
```

**JSON Format**
```json
{
  "test_type": "latencymeasure",
  "config": {
    "input_device": "/dev/input/event6",
    "gpio_pin": 27,
    "pulse_width_ms": 25,
    "wait_ms": 50,
    "loop_count": 10
  },
  "results": {
    "samples_collected": 10,
    "successful_events": 10,
    "missed_events": 0,
    "latency_ms": {
      "min": 15.2,
      "max": 24.8,
      "avg": 19.3,
      "stddev": 2.1,
      "p50": 19.1,
      "p95": 23.5,
      "p99": 24.6
    },
    "measurements": [19.2, 18.5, 20.1, ...]
  }
}
```

**CSV Format**
```csv
timestamp,latency_ms,event_type,tracking_id
1762030744.480446,19.2,touch_down,10
1762030744.488983,8.5,touch_up,10
...
```

### 3. qt-touch-tester (Qt5 GUI Application)

#### UI Design
```
┌─────────────────────────────────────────────────────┐
│  Touch Tester                              [X]      │
├─────────────────────────────────────────────────────┤
│                                                     │
│  ┌───────────────────────────────────────────┐     │
│  │                                           │     │
│  │          TARGET AREA                      │     │
│  │          (Click Here)                     │     │
│  │             ⊕                             │     │
│  │                                           │     │
│  └───────────────────────────────────────────┘     │
│                                                     │
│  ┌─────────────────── Stats ──────────────────┐    │
│  │  Events: 42 / 50    Missed: 0              │    │
│  │  Current: 19.2ms    Avg: 19.5ms           │    │
│  │  Min: 15.1ms        Max: 24.3ms           │    │
│  └──────────────────────────────────────────┘    │
│                                                     │
│  ┌──────── Controls ─────────┐                     │
│  │  GPIO: [27] Pulse: [25]ms │                     │
│  │  Wait: [50]ms  Count: [10]│                     │
│  │  [Start Test] [Stop]      │                     │
│  └────────────────────────────┘                     │
│                                                     │
│  ┌─────────── Latency Graph ───────────┐           │
│  │  30ms ┤                              │           │
│  │  20ms ┤ ▂▃▅▄▃▂▄▅▃▂                  │           │
│  │  10ms ┤                              │           │
│  │   0ms └──────────────────────────────│           │
│  └──────────────────────────────────────┘           │
│                                                     │
│  ┌────────── Event Log ─────────┐                  │
│  │  [19:23:45] Test started      │                  │
│  │  [19:23:46] Touch detected... │                  │
│  │  [19:23:47] Latency: 19.2ms  │                  │
│  └────────────────────────────────┘                  │
└─────────────────────────────────────────────────────┘
```

#### Network Daemon Mode

**Protocol Design** (Based on qt-demo-launcher)
- TCP Socket, text-based protocol
- Default Port: 8082 (defined in config.h)
- Request/Response model: Send command, receive response
- Responses end with newline `\n`
- Connection closes after response sent

**API Commands**
```bash
# Start test
echo "start-test latencymeasure gpio=27 pulsewidth=25 wait=50 loopcount=10" | nc localhost 8082
# Response: OK

# Get current test status
echo "get-status" | nc localhost 8082
# Response: running|idle|error

# Get latest results
echo "get-results" | nc localhost 8082
# Response: {"samples":10,"avg_ms":19.3,"min_ms":15.2,...}

# Stop current test
echo "stop-test" | nc localhost 8082
# Response: OK

# Configure GPIO and parameters
echo "set-config gpio=27 pulsewidth=25 wait=50" | nc localhost 8082
# Response: OK

# Get current configuration
echo "get-config" | nc localhost 8082
# Response: gpio=27,pulsewidth=25,wait=50

# Trigger single pulse (manual mode)
echo "trigger-pulse gpio=27 width=25" | nc localhost 8082
# Response: OK

# List available input devices
echo "list-inputs" | nc localhost 8082
# Response: /dev/input/event6:Himax,/dev/input/event0:Mouse

# Set active input device
echo "set-input /dev/input/event6" | nc localhost 8082
# Response: OK
```

**NetworkInterface Implementation**
- Reuse NetworkInterface.cpp/.h from qt-demo-launcher
- Single client connection at a time
- Non-blocking socket I/O integrated with Qt event loop
- Uses QSocketNotifier for async socket events
- QTimer for periodic data checking (100ms interval)

**Systemd Service**
```ini
[Unit]
Description=Qt Touch Tester Daemon
After=network.target

[Service]
Type=simple
ExecStart=/usr/bin/qt-touch-tester --daemon --port=8082
Restart=on-failure
User=root
Group=root

[Install]
WantedBy=multi-user.target
```

## Build System (CMake)

### Root CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.10)
project(touch-tester VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(BUILD_QT_APP "Build Qt GUI application" ON)
option(INSTALL_SYSTEMD_SERVICES "Install systemd service files" ON)

# Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GPIOD REQUIRED libgpiod>=1.6)

if(BUILD_QT_APP)
    find_package(Qt5 COMPONENTS Core Gui Widgets Network)
    if(NOT Qt5_FOUND)
        message(WARNING "Qt5 not found, disabling Qt GUI application")
        set(BUILD_QT_APP OFF)
    endif()
endif()

# Subdirectories
add_subdirectory(src)
```

### Installation Paths
- Binaries: `/usr/bin/` (or `${CMAKE_INSTALL_PREFIX}/bin`)
- Library: `/usr/lib/` (or `${CMAKE_INSTALL_PREFIX}/lib`)
- Data: `/usr/share/touch-tester/`
- Config: `/etc/touch-tester.conf`
- Systemd: `/usr/lib/systemd/system/` (Buildroot) or `/lib/systemd/system/` (Pi OS)

## Buildroot Integration

### touch-tester.mk
```makefile
TOUCH_TESTER_VERSION = 1.0.0
TOUCH_TESTER_SITE = $(BR2_EXTERNAL_BR_WRAPPER_PATH)/package/touch-tester
TOUCH_TESTER_SITE_METHOD = local
TOUCH_TESTER_DEPENDENCIES = libgpiod

# Qt5 dependency (optional)
ifeq ($(BR2_PACKAGE_QT5),y)
TOUCH_TESTER_DEPENDENCIES += qt5base
TOUCH_TESTER_CONF_OPTS += -DBUILD_QT_APP=ON
else
TOUCH_TESTER_CONF_OPTS += -DBUILD_QT_APP=OFF
endif

# Systemd integration
ifeq ($(BR2_PACKAGE_SYSTEMD),y)
TOUCH_TESTER_CONF_OPTS += -DINSTALL_SYSTEMD_SERVICES=ON
endif

$(eval $(cmake-package))
```

### Config.in
```
config BR2_PACKAGE_TOUCH_TESTER
    bool "touch-tester"
    select BR2_PACKAGE_LIBGPIOD
    help
      Touch latency measurement and testing utility.
      Includes CLI tool and optional Qt GUI application.

      Components:
      - lib-touch-test: Core library
      - touch-tester-cli: Command-line tool
      - qt-touch-tester: Qt GUI (requires Qt5)

      https://github.com/your-org/br-wrapper
```

## Dependencies

### Required (All Components)
- **libgpiod** >= 1.6
  - Debian/Pi OS: `sudo apt install libgpiod-dev libgpiod2`
  - Buildroot: `BR2_PACKAGE_LIBGPIOD=y`

### Optional (Qt GUI Only)
- **Qt5** (Core, Gui, Widgets, Network)
  - Debian/Pi OS: `sudo apt install qtbase5-dev`
  - Buildroot: `BR2_PACKAGE_QT5=y`, `BR2_PACKAGE_QT5BASE=y`

### Runtime Permissions
- GPIO access: User must be in `gpio` group or run as root
- Input device access: User must be in `input` group
- Suggested: `sudo usermod -a -G gpio,input $USER`

### Udev Rules (Optional)
Create `/etc/udev/rules.d/99-gpio-input.rules`:
```
SUBSYSTEM=="gpio", GROUP="gpio", MODE="0660"
SUBSYSTEM=="input", GROUP="input", MODE="0660"
```

## Technical Specifications

### Timing & Precision
- **GPIO Pulse Timing**: libgpiod line events (microsecond precision)
- **Latency Measurement**: `clock_gettime(CLOCK_MONOTONIC_RAW)` (nanosecond resolution)
- **Expected Latency**: 15-25ms (Himax touch controller @ 120Hz sampling)
- **Minimum Wait Between Pulses**: 50ms (6 sample periods @ 120Hz)

### Touch Event Processing
```c
// Touch-down event
Event: type 3 (EV_ABS), code 57 (ABS_MT_TRACKING_ID), value 10
Event: type 3 (EV_ABS), code 53 (ABS_MT_POSITION_X), value 1367
Event: type 3 (EV_ABS), code 54 (ABS_MT_POSITION_Y), value 315
Event: type 1 (EV_KEY), code 330 (BTN_TOUCH), value 1
Event: SYN_REPORT

// Touch-up event
Event: type 3 (EV_ABS), code 57 (ABS_MT_TRACKING_ID), value -1
Event: type 1 (EV_KEY), code 330 (BTN_TOUCH), value 0
Event: SYN_REPORT
```

### Multi-Touch Support (Future)
- **Phase 1**: Single touch (current)
- **Phase 2**: Sequential multi-touch (`--output-gpio=27,28 --multitouch=sequential`)
- **Phase 3**: Simultaneous multi-touch (`--output-gpio=27,28 --multitouch=simultaneous`)
- Tracking via `ABS_MT_TRACKING_ID`

### PREEMPT_RT Support (Future)
- **Scheduler**: `SCHED_FIFO` via `--rtpriority=1-99`
- **CPU Affinity**: Pin to core via `--cpuaffinity=0-3`
- **Memory Locking**: `mlockall()` to prevent page faults
- **Note**: Not enabled in Phase 1 (standard Linux kernel)

## Error Handling

### GPIO Errors
- Device not found: Suggest checking `/dev/gpiochip0` exists
- Permission denied: Suggest adding user to `gpio` group
- Line busy: Check if another process is using GPIO
- Export failed: Provide detailed error message with errno

### Input Device Errors
- Auto-discovery fails: List all `/dev/input/event*` devices
- Permission denied: Suggest adding user to `input` group
- Device is not a touchscreen: Verify capabilities with `evtest`
- No events received: Check if device is working with `evtest /dev/input/eventX`

### Validation
- `--wait-ms < 50`: Warning (may cause event overlap)
- `--pulsewidth-ms > 200`: Warning (unusually long pulse)
- `--loopcount < 1`: Error
- `--output-gpio` invalid: Error (must be 0-27 for Raspberry Pi)

## Testing Strategy

### Integration Tests
- Mock GPIO controller (simulate pulse generation)
- Mock touch reader (inject synthetic events)
- Verify latency calculations against known delays
- Test all output formats (human, JSON, CSV)

### Hardware-in-Loop (Manual)
- Raspberry Pi 4 + Himax touchscreen
- Auto-clicker (capacitive coupling via GPIO)
- Rigol oscilloscope (optional, for calibration)

## Implementation Phases

### Phase 1: Core Functionality (Weeks 1-2)
- [x] Design summary (this document)
- [ ] lib-touch-test core library
  - [ ] gpio_controller.cpp/.h
  - [ ] touch_reader.cpp/.h
  - [ ] latency_measurer.cpp/.h
  - [ ] statistics.cpp/.h
- [ ] touch-tester-cli
  - [ ] Argument parsing
  - [ ] Test types: latencymeasure, touchcount, touchtrigger
  - [ ] Output formats: human, JSON, CSV
  - [ ] Auto-discovery of input devices
- [ ] CMake build system
- [ ] Buildroot package files

### Phase 2: Qt GUI (Weeks 3-4)
- [ ] qt-touch-tester UI
  - [ ] Target area widget
  - [ ] Live statistics display
  - [ ] Latency graph
  - [ ] Event log
- [ ] Network daemon mode
  - [ ] NetworkInterface integration
  - [ ] API command handlers
  - [ ] Systemd service file
- [ ] Documentation (README.md)

### Phase 3: Advanced Features (Future)
- [ ] Multi-touch support (sequential, simultaneous)
- [ ] PREEMPT_RT tuning (rtpriority, cpuaffinity)
- [ ] Calibration mode (Rigol integration)
- [ ] Stress test mode
- [ ] Additional test types

## Open Questions & Future Decisions

1. **Calibration Mode**: Defer rigol_query.py integration until Phase 3 (Python3 footprint in Buildroot)
2. **Config File**: Should CLI support config file for defaults? (e.g., `/etc/touch-tester.conf`)
3. **Logging**: Use printf or structured logging (spdlog)? Start with printf.
4. **Unit Tests**: Google Test integration? Defer to Phase 3.
5. **Multi-touch**: Priority for Phase 3+

## References

### External Documentation
- libgpiod: https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/
- Linux Input Subsystem: https://www.kernel.org/doc/html/latest/input/input.html
- Qt5 Documentation: https://doc.qt.io/qt-5/
- Buildroot Manual: https://buildroot.org/downloads/manual/manual.html

### Internal References
- qt-demo-launcher: `/home/testpc/git-repos/tmp/br-wrapper/package/qt-demo-launcher`
- NetworkInterface: Reused from qt-demo-launcher
- Build patterns: Follow qt-demo-launcher CMake structure

---

**Document Version**: 1.0
**Last Updated**: 2025-11-01
**Status**: Ready for Implementation
