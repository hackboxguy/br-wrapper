#include "gpio_controller.h"
#include "touch_reader.h"
#include "latency_measurer.h"
#include "statistics.h"

#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>

using namespace TouchTest;

// Configuration structure
struct Config {
    std::string testType;           // latencymeasure, touchcount, touchtrigger, verify
    std::string inputEvent;         // Device path or "auto"
    int outputGpio;                 // GPIO pin number
    int loopCount;                  // Number of iterations
    int pulseWidthMs;               // Pulse width in milliseconds
    int waitMs;                     // Wait time between pulses
    OutputFormat outputFormat;      // Output format
    bool verbose;                   // Verbose output

    Config()
        : testType("latencymeasure")
        , inputEvent("auto")
        , outputGpio(-1)
        , loopCount(1)
        , pulseWidthMs(25)
        , waitMs(50)
        , outputFormat(OutputFormat::Human)
        , verbose(false)
    {}
};

// Function prototypes
void printUsage(const char* progName);
bool parseArguments(int argc, char* argv[], Config& config);
int runLatencyMeasure(const Config& config);
int runTouchCount(const Config& config);
int runTouchTrigger(const Config& config);
int runVerify(const Config& config);

int main(int argc, char* argv[])
{
    Config config;

    if (!parseArguments(argc, argv, config)) {
        printUsage(argv[0]);
        return 1;
    }

    // Validate configuration
    if (config.outputGpio < 0) {
        std::cerr << "Error: --output-gpio is required\n";
        return 1;
    }

    if (config.waitMs < 50) {
        std::cerr << "Warning: --wait-ms is less than 50ms. This may cause event overlap.\n";
        std::cerr << "         Recommended minimum: 50ms (6 sample periods @ 120Hz)\n";
    }

    // Run appropriate test
    if (config.testType == "latencymeasure") {
        return runLatencyMeasure(config);
    } else if (config.testType == "touchcount") {
        return runTouchCount(config);
    } else if (config.testType == "touchtrigger") {
        return runTouchTrigger(config);
    } else if (config.testType == "verify") {
        return runVerify(config);
    } else {
        std::cerr << "Error: Unknown test type '" << config.testType << "'\n";
        return 1;
    }

    return 0;
}

void printUsage(const char* progName)
{
    std::cout << "Touch Tester - Touch latency measurement tool\n";
    std::cout << "\n";
    std::cout << "Usage: " << progName << " [OPTIONS]\n";
    std::cout << "\n";
    std::cout << "Required Options:\n";
    std::cout << "  --testtype=TYPE          Test type: latencymeasure, touchcount, touchtrigger, verify\n";
    std::cout << "  --output-gpio=NUM        GPIO pin number for pulse output\n";
    std::cout << "\n";
    std::cout << "Optional:\n";
    std::cout << "  --inputevent=PATH        Input device path or 'auto' (default: auto)\n";
    std::cout << "  --loopcount=NUM          Number of test iterations (default: 1)\n";
    std::cout << "  --pulsewidth-ms=NUM      Pulse width in milliseconds (default: 25)\n";
    std::cout << "  --wait-ms=NUM            Wait time between pulses in ms (default: 50)\n";
    std::cout << "  --format=FMT             Output format: human, json, csv (default: human)\n";
    std::cout << "  --verbose                Enable verbose output\n";
    std::cout << "  --help                   Show this help message\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  # Measure latency with auto-discovered touchscreen\n";
    std::cout << "  " << progName << " --testtype=latencymeasure --output-gpio=27 --loopcount=10\n";
    std::cout << "\n";
    std::cout << "  # Count touch events on specific device\n";
    std::cout << "  " << progName << " --testtype=touchcount --inputevent=/dev/input/event6 \\\n";
    std::cout << "    --output-gpio=27 --loopcount=100\n";
    std::cout << "\n";
    std::cout << "  # Generate touch pulses without measurement\n";
    std::cout << "  " << progName << " --testtype=touchtrigger --output-gpio=27 --loopcount=10\n";
    std::cout << "\n";
    std::cout << "  # Verify single touch with verbose output\n";
    std::cout << "  " << progName << " --testtype=verify --output-gpio=27 --verbose\n";
    std::cout << "\n";
}

bool parseArguments(int argc, char* argv[], Config& config)
{
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            return false;
        } else if (arg.find("--testtype=") == 0) {
            config.testType = arg.substr(11);
        } else if (arg.find("--inputevent=") == 0) {
            config.inputEvent = arg.substr(13);
        } else if (arg.find("--output-gpio=") == 0) {
            config.outputGpio = std::stoi(arg.substr(14));
        } else if (arg.find("--loopcount=") == 0) {
            config.loopCount = std::stoi(arg.substr(12));
        } else if (arg.find("--pulsewidth-ms=") == 0) {
            config.pulseWidthMs = std::stoi(arg.substr(16));
        } else if (arg.find("--wait-ms=") == 0) {
            config.waitMs = std::stoi(arg.substr(10));
        } else if (arg.find("--format=") == 0) {
            std::string fmt = arg.substr(9);
            if (fmt == "json") {
                config.outputFormat = OutputFormat::Json;
            } else if (fmt == "csv") {
                config.outputFormat = OutputFormat::Csv;
            } else {
                config.outputFormat = OutputFormat::Human;
            }
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }

    return true;
}

int runLatencyMeasure(const Config& config)
{
    if (config.verbose) {
        std::cout << "Touch Latency Measurement\n";
        std::cout << "=========================\n";
        std::cout << "Configuration:\n";
        std::cout << "  GPIO Pin: " << config.outputGpio << "\n";
        std::cout << "  Input Device: " << config.inputEvent << "\n";
        std::cout << "  Pulse Width: " << config.pulseWidthMs << " ms\n";
        std::cout << "  Wait Time: " << config.waitMs << " ms\n";
        std::cout << "  Loop Count: " << config.loopCount << "\n";
        std::cout << "\n";
    }

    // Initialize GPIO
    GpioController gpio;
    if (!gpio.init(config.outputGpio)) {
        std::cerr << "Error: " << gpio.getLastError() << "\n";
        return 1;
    }

    if (config.verbose) {
        std::cout << "GPIO " << config.outputGpio << " initialized\n";
    }

    // Initialize touch reader
    TouchReader touch;
    if (config.inputEvent == "auto") {
        if (!touch.autoDiscover()) {
            std::cerr << "Error: " << touch.getLastError() << "\n";
            return 1;
        }
        if (config.verbose) {
            std::cout << "Auto-discovered device: " << touch.getDevicePath()
                     << " (" << touch.getDeviceName() << ")\n";
        }
    } else {
        if (!touch.open(config.inputEvent)) {
            std::cerr << "Error: " << touch.getLastError() << "\n";
            return 1;
        }
        if (config.verbose) {
            std::cout << "Opened device: " << touch.getDevicePath() << "\n";
        }
    }

    // Run measurement loop
    Statistics stats;
    LatencyMeasurer measurer;
    int successCount = 0;
    int missedCount = 0;

    if (config.verbose) {
        std::cout << "\nStarting measurements...\n";
    }

    for (int i = 0; i < config.loopCount; i++) {
        if (config.verbose) {
            std::cout << "Test " << (i + 1) << "/" << config.loopCount << "... ";
            std::cout.flush();
        }

        // Start timing
        measurer.start();

        // Generate pulse
        if (!gpio.generatePulse(config.pulseWidthMs)) {
            std::cerr << "\nError generating pulse: " << gpio.getLastError() << "\n";
            return 1;
        }

        // Wait for touch event (timeout: wait time + pulse width + 100ms margin)
        int timeout = config.waitMs + config.pulseWidthMs + 100;
        TouchEvent event;

        if (touch.waitForEvent(event, timeout)) {
            if (event.type == TouchEventType::TouchDown) {
                // Calculate latency
                double latency = event.timestamp * 1000.0 - measurer.getStartTime();
                stats.addSample(latency);
                successCount++;

                if (config.verbose) {
                    std::cout << latency << " ms\n";
                }
            }
        } else {
            missedCount++;
            if (config.verbose) {
                std::cout << "MISSED\n";
            }
        }

        // Wait before next iteration (if not last)
        if (i < config.loopCount - 1) {
            usleep(config.waitMs * 1000);
        }
    }

    // Output results
    if (config.outputFormat == OutputFormat::Human) {
        std::cout << "\nResults:\n";
        std::cout << "  Successful Events: " << successCount << "\n";
        std::cout << "  Missed Events: " << missedCount << "\n";
        std::cout << "\n";
        std::cout << stats.format(OutputFormat::Human, "Latency Statistics");
    } else if (config.outputFormat == OutputFormat::Json) {
        std::cout << "{\n";
        std::cout << "  \"test_type\": \"latencymeasure\",\n";
        std::cout << "  \"successful_events\": " << successCount << ",\n";
        std::cout << "  \"missed_events\": " << missedCount << ",\n";
        std::cout << "  \"statistics\": ";
        std::cout << stats.format(OutputFormat::Json);
        std::cout << "}\n";
    } else if (config.outputFormat == OutputFormat::Csv) {
        std::cout << stats.formatCsv(true);
    }

    return (missedCount > 0) ? 2 : 0;
}

int runTouchCount(const Config& config)
{
    if (config.verbose) {
        std::cout << "Touch Count Test\n";
        std::cout << "================\n";
    }

    // Initialize GPIO
    GpioController gpio;
    if (!gpio.init(config.outputGpio)) {
        std::cerr << "Error: " << gpio.getLastError() << "\n";
        return 1;
    }

    // Initialize touch reader
    TouchReader touch;
    if (config.inputEvent == "auto") {
        if (!touch.autoDiscover()) {
            std::cerr << "Error: " << touch.getLastError() << "\n";
            return 1;
        }
    } else {
        if (!touch.open(config.inputEvent)) {
            std::cerr << "Error: " << touch.getLastError() << "\n";
            return 1;
        }
    }

    int generatedCount = 0;
    int receivedCount = 0;

    for (int i = 0; i < config.loopCount; i++) {
        // Generate pulse
        if (!gpio.generatePulse(config.pulseWidthMs)) {
            std::cerr << "Error generating pulse: " << gpio.getLastError() << "\n";
            return 1;
        }
        generatedCount++;

        // Wait for event
        int timeout = config.waitMs + config.pulseWidthMs + 100;
        TouchEvent event;

        if (touch.waitForEvent(event, timeout) && event.type == TouchEventType::TouchDown) {
            receivedCount++;
        }

        // Wait before next iteration
        if (i < config.loopCount - 1) {
            usleep(config.waitMs * 1000);
        }
    }

    // Output results
    int missedCount = generatedCount - receivedCount;

    if (config.outputFormat == OutputFormat::Human) {
        std::cout << "Touch Count Results:\n";
        std::cout << "  Generated: " << generatedCount << "\n";
        std::cout << "  Received:  " << receivedCount << "\n";
        std::cout << "  Missed:    " << missedCount << "\n";
        if (missedCount == 0) {
            std::cout << "  Status: PASS (no missed events)\n";
        } else {
            std::cout << "  Status: FAIL (" << missedCount << " missed events)\n";
        }
    } else if (config.outputFormat == OutputFormat::Json) {
        std::cout << "{\n";
        std::cout << "  \"test_type\": \"touchcount\",\n";
        std::cout << "  \"generated\": " << generatedCount << ",\n";
        std::cout << "  \"received\": " << receivedCount << ",\n";
        std::cout << "  \"missed\": " << missedCount << "\n";
        std::cout << "}\n";
    }

    return (missedCount > 0) ? 2 : 0;
}

int runTouchTrigger(const Config& config)
{
    if (config.verbose) {
        std::cout << "Touch Trigger Test\n";
        std::cout << "==================\n";
        std::cout << "Generating " << config.loopCount << " touch pulses...\n";
    }

    // Initialize GPIO
    GpioController gpio;
    if (!gpio.init(config.outputGpio)) {
        std::cerr << "Error: " << gpio.getLastError() << "\n";
        return 1;
    }

    for (int i = 0; i < config.loopCount; i++) {
        if (config.verbose) {
            std::cout << "Pulse " << (i + 1) << "/" << config.loopCount << "\n";
        }

        if (!gpio.generatePulse(config.pulseWidthMs)) {
            std::cerr << "Error generating pulse: " << gpio.getLastError() << "\n";
            return 1;
        }

        if (i < config.loopCount - 1) {
            usleep(config.waitMs * 1000);
        }
    }

    if (config.verbose) {
        std::cout << "Done.\n";
    }

    return 0;
}

int runVerify(const Config& config)
{
    std::cout << "Touch Verification Test\n";
    std::cout << "=======================\n";

    // Initialize GPIO
    GpioController gpio;
    if (!gpio.init(config.outputGpio)) {
        std::cerr << "Error: " << gpio.getLastError() << "\n";
        return 1;
    }
    std::cout << "GPIO " << config.outputGpio << " initialized\n";

    // Initialize touch reader
    TouchReader touch;
    if (config.inputEvent == "auto") {
        if (!touch.autoDiscover()) {
            std::cerr << "Error: " << touch.getLastError() << "\n";
            return 1;
        }
        std::cout << "Auto-discovered: " << touch.getDevicePath()
                 << " (" << touch.getDeviceName() << ")\n";
    } else {
        if (!touch.open(config.inputEvent)) {
            std::cerr << "Error: " << touch.getLastError() << "\n";
            return 1;
        }
        std::cout << "Opened: " << touch.getDevicePath() << "\n";
    }

    std::cout << "\nGenerating single touch pulse (" << config.pulseWidthMs << " ms)...\n";

    LatencyMeasurer measurer;
    measurer.start();
    double pulseStartTime = measurer.getStartTime();

    if (!gpio.generatePulse(config.pulseWidthMs)) {
        std::cerr << "Error: " << gpio.getLastError() << "\n";
        return 1;
    }

    std::cout << "Pulse generated, waiting for events...\n\n";

    // Wait for touch-down
    TouchEvent event;
    if (touch.waitForEvent(event, 2000)) {
        if (event.type == TouchEventType::TouchDown) {
            double touchDownTime = event.timestamp * 1000.0;
            double latency = touchDownTime - pulseStartTime;

            std::cout << "Touch-Down Event:\n";
            std::cout << "  Timestamp: " << event.timestamp << " s\n";
            std::cout << "  Position: (" << event.x << ", " << event.y << ")\n";
            std::cout << "  Tracking ID: " << event.tracking_id << "\n";
            std::cout << "  Pressure: " << event.pressure << "\n";
            std::cout << "  Latency: " << latency << " ms\n\n";
        }
    } else {
        std::cout << "No touch-down event received (timeout)\n";
        return 1;
    }

    // Wait for touch-up
    if (touch.waitForEvent(event, 2000)) {
        if (event.type == TouchEventType::TouchUp) {
            double touchUpTime = event.timestamp * 1000.0;
            double duration = touchUpTime - (pulseStartTime + config.pulseWidthMs);

            std::cout << "Touch-Up Event:\n";
            std::cout << "  Timestamp: " << event.timestamp << " s\n";
            std::cout << "  Duration: " << duration << " ms after pulse end\n\n";
        }
    }

    std::cout << "Verification complete.\n";
    return 0;
}
