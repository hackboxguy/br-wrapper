//CAN client utility for disp-can-ctrl daemon
//Usage examples:
//disp-can-client --node=can0 --command=set-pattern --value=red
//disp-can-client --node=can0 --command=get-hwpartnum --verbose
//disp-can-client --node=can0 --command=set-hwpartnum --value="ABC123DEF456" --debug

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <algorithm>
#include <chrono>
#include <thread>

using namespace std;

// Global configuration
struct Config {
    std::string node = "";
    std::string command = "";
    std::string value = "";
    int timeout = 2;
    bool verbose = false;
    bool debug = false;
};

// ISO-TP state for multi-frame handling
struct IsoTpState {
    bool receiving = false;
    std::vector<uint8_t> rx_buffer;
    uint8_t expected_sequence = 1;
    uint8_t total_length = 0;
};

/*****************************************************************************/
// Print help message
void printHelp(const std::string& program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  --node=<canx>           CAN interface (required)\n"
              << "  --timeout=<seconds>     Response timeout (default: 2)\n"
              << "  --command=<cmd>         Command to execute (required)\n"
              << "  --value=<val>          Value for set commands\n"
              << "  --verbose              Show CAN frames\n"
              << "  --debug                Show detailed debug info\n"
              << "  --help                 Show this help\n"
              << "\nSupported commands:\n"
              << "  set-pattern            Set display pattern (values: red, green, blue, colorbar, home)\n"
              << "  get-hwpartnum          Get hardware part number\n"
              << "  set-hwpartnum          Set hardware part number (max 16 ASCII chars)\n"
              << "\nExamples:\n"
              << "  " << program << " --node=can0 --command=set-pattern --value=red\n"
              << "  " << program << " --node=can0 --command=get-hwpartnum --verbose\n"
              << "  " << program << " --node=can0 --command=set-hwpartnum --value=\"TEST123456\"\n";
}

/*****************************************************************************/
// Debug/verbose print function
void debugPrint(const Config& config, const std::string& message, bool force_verbose = false) {
    if (config.debug || (force_verbose && config.verbose)) {
        std::cout << message << std::endl;
    }
}

/*****************************************************************************/
// Print CAN frame
void printCanFrame(const Config& config, const std::string& direction, const struct can_frame& frame) {
    if (config.verbose || config.debug) {
        printf("%s: [%03X] ", direction.c_str(), frame.can_id);
        for (int i = 0; i < frame.can_dlc; i++) {
            printf("%02X ", frame.data[i]);
        }
        printf("\n");
    }
}

/*****************************************************************************/
// Send CAN frame
bool sendCanFrame(int sockfd, const Config& config, canid_t id, const uint8_t* data, uint8_t dlc) {
    struct can_frame frame;
    frame.can_id = id;
    frame.can_dlc = dlc;
    memcpy(frame.data, data, dlc);

    debugPrint(config, "[DEBUG] Sending CAN frame");
    printCanFrame(config, "TX", frame);

    return write(sockfd, &frame, sizeof(struct can_frame)) == sizeof(struct can_frame);
}

/*****************************************************************************/
// Receive CAN frame with timeout, filtering out loopback messages
bool receiveCanFrame(int sockfd, const Config& config, struct can_frame& frame, int timeout_ms, canid_t expected_id = 0) {
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        // Check timeout
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
        if (elapsed >= timeout_ms) {
            return false; // Timeout
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        int remaining_timeout = timeout_ms - elapsed;
        struct timeval timeout;
        timeout.tv_sec = remaining_timeout / 1000;
        timeout.tv_usec = (remaining_timeout % 1000) * 1000;

        int activity = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity <= 0) {
            return false; // Timeout or error
        }

        if (FD_ISSET(sockfd, &read_fds)) {
            int nbytes = read(sockfd, &frame, sizeof(struct can_frame));
            if (nbytes == sizeof(struct can_frame)) {
                printCanFrame(config, "RX", frame);

                // Filter out loopback messages (our own transmissions on ID 0x703)
                if (frame.can_id == 0x703) {
                    debugPrint(config, "[DEBUG] Ignoring loopback message on ID 0x703");
                    continue; // Ignore our own transmitted message
                }

                // If we're looking for a specific ID, check it
                if (expected_id != 0 && frame.can_id != expected_id) {
                    debugPrint(config, "[DEBUG] Ignoring unexpected ID: " + std::to_string(frame.can_id) + ", expected: " + std::to_string(expected_id));
                    continue; // Not the ID we're looking for
                }

                return true; // Valid frame received
            }
        }
    }

    return false;
}

/*****************************************************************************/
// Send flow control frame
bool sendFlowControl(int sockfd, const Config& config) {
    uint8_t fc_data[8] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    debugPrint(config, "[DEBUG] Sending flow control");
    return sendCanFrame(sockfd, config, 0x703, fc_data, 8);
}

/*****************************************************************************/
// Open CAN socket
int openCanSocket(const Config& config) {
    int sockfd;
    struct sockaddr_can addr;
    struct ifreq ifr;

    debugPrint(config, "[DEBUG] Opening CAN socket on " + config.node);

    if ((sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        std::cerr << "Error: CAN socket creation failed" << std::endl;
        return -1;
    }

    strcpy(ifr.ifr_name, config.node.c_str());
    if (ioctl(sockfd, SIOCGIFINDEX, &ifr) < 0) {
        std::cerr << "Error: CAN interface " << config.node << " not found" << std::endl;
        close(sockfd);
        return -1;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Error: CAN socket bind failed" << std::endl;
        close(sockfd);
        return -1;
    }

    debugPrint(config, "[DEBUG] CAN socket opened successfully");
    return sockfd;
}

/*****************************************************************************/
// Handle set-pattern command
int handleSetPattern(int sockfd, const Config& config) {
    uint8_t cmd_data[8] = {0x04, 0x2E, 0xFD, 0x38, 0x00, 0x00, 0x00, 0x00};

    // Map pattern value to command byte
    if (config.value == "red") {
        cmd_data[4] = 0x03;
    } else if (config.value == "green") {
        cmd_data[4] = 0x04;
    } else if (config.value == "blue") {
        cmd_data[4] = 0x05;
    } else if (config.value == "colorbar") {
        cmd_data[4] = 0x06;
    } else if (config.value == "home") {
        cmd_data[0] = 0x04; cmd_data[1] = 0x2E; cmd_data[2] = 0xFD; cmd_data[3] = 0xC0; cmd_data[4] = 0x01;
    } else {
        std::cerr << "Error: Invalid pattern value. Supported: red, green, blue, colorbar, home" << std::endl;
        return 2; // Invalid command
    }

    debugPrint(config, "[DEBUG] Sending set-pattern command: " + config.value);

    // Send command
    if (!sendCanFrame(sockfd, config, 0x703, cmd_data, 8)) {
        std::cerr << "Error: Failed to send CAN frame" << std::endl;
        return 3; // CAN interface error
    }

    // Wait for response
    struct can_frame response;
    int timeout_ms = config.timeout * 1000;

    if (!receiveCanFrame(sockfd, config, response, timeout_ms, 0x70B)) {
        std::cerr << "Error: Timeout waiting for response" << std::endl;
        return 1; // Timeout
    }

    // Validate response
    if (response.can_id != 0x70B) {
        std::cerr << "Error: Invalid response ID: " << std::hex << response.can_id << std::endl;
        return 4; // Invalid response
    }

    // Check for positive response
    if (response.can_dlc >= 2 && response.data[0] == 0x03 && response.data[1] == 0x6E) {
        debugPrint(config, "[DEBUG] Received positive response");
        if (!config.verbose && !config.debug) {
            std::cout << "OK" << std::endl;
        }
        return 0; // Success
    } else {
        std::cerr << "Error: Negative response from daemon" << std::endl;
        return 4; // Invalid response
    }
}

/*****************************************************************************/
// Handle get-hwpartnum command
int handleGetHwPartNum(int sockfd, const Config& config) {
    uint8_t cmd_data[8] = {0x03, 0x22, 0xFD, 0xBD, 0x00, 0x00, 0x00, 0x00};

    debugPrint(config, "[DEBUG] Sending get-hwpartnum command");

    // Send command
    if (!sendCanFrame(sockfd, config, 0x703, cmd_data, 8)) {
        std::cerr << "Error: Failed to send CAN frame" << std::endl;
        return 3; // CAN interface error
    }

    // Handle multi-frame response
    IsoTpState isotp;
    std::string partNumber;
    bool flow_control_sent = false;

    int timeout_ms = config.timeout * 1000;
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        // Check overall timeout
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
        if (elapsed >= timeout_ms) {
            std::cerr << "Error: Timeout waiting for complete response" << std::endl;
            return 1; // Timeout
        }

        struct can_frame response;
        int remaining_timeout = timeout_ms - elapsed;

        if (!receiveCanFrame(sockfd, config, response, remaining_timeout, 0x70B)) {
            std::cerr << "Error: Timeout waiting for response frame" << std::endl;
            return 1; // Timeout
        }

        // Handle first frame
        if ((response.data[0] & 0xF0) == 0x10) {
            debugPrint(config, "[DEBUG] Received first frame");

            // Only process if we haven't started receiving yet
            if (!isotp.receiving) {
                isotp.total_length = response.data[1];
                isotp.receiving = true;
                isotp.expected_sequence = 1;
                isotp.rx_buffer.clear();
                flow_control_sent = false;

                // Validate service response
                if (response.data[2] != 0x62 || response.data[3] != 0xFD || response.data[4] != 0xBD) {
                    std::cerr << "Error: Invalid service response" << std::endl;
                    return 4; // Invalid response
                }

                // Store first data bytes (skip length and service bytes)
                for (int i = 5; i < 8; i++) {
                    isotp.rx_buffer.push_back(response.data[i]);
                }

                // Send flow control only once
                if (!flow_control_sent) {
                    if (!sendFlowControl(sockfd, config)) {
                        std::cerr << "Error: Failed to send flow control" << std::endl;
                        return 3; // CAN interface error
                    }
                    flow_control_sent = true;
                }
            } else {
                debugPrint(config, "[DEBUG] Ignoring duplicate first frame");
            }
        }
        // Handle consecutive frames
        else if ((response.data[0] & 0xF0) == 0x20) {
            debugPrint(config, "[DEBUG] Received consecutive frame");

            if (!isotp.receiving) {
                std::cerr << "Error: Unexpected consecutive frame" << std::endl;
                return 4; // Invalid response
            }

            uint8_t sequence = response.data[0] & 0x0F;
            debugPrint(config, "[DEBUG] Frame sequence: " + std::to_string(sequence) + ", expected: " + std::to_string(isotp.expected_sequence));

            if (sequence == isotp.expected_sequence) {
                // Store data bytes
                for (int i = 1; i < 8 && isotp.rx_buffer.size() < (isotp.total_length - 3); i++) {
                    isotp.rx_buffer.push_back(response.data[i]);
                }

                isotp.expected_sequence++;
                if (isotp.expected_sequence > 0x0F) isotp.expected_sequence = 0;

                // Check if we have all data (total_length includes 3 service bytes)
                if (isotp.rx_buffer.size() >= (isotp.total_length - 3)) {
                    debugPrint(config, "[DEBUG] Complete response received");

                    // Extract part number (remove any trailing nulls)
                    partNumber.clear();
                    for (size_t i = 0; i < isotp.rx_buffer.size() && i < 16; i++) {
                        if (isotp.rx_buffer[i] != 0) {
                            partNumber += (char)isotp.rx_buffer[i];
                        }
                    }

                    // Output result
                    if (!config.verbose && !config.debug) {
                        std::cout << partNumber << std::endl;
                    } else {
                        std::cout << partNumber << std::endl;
                    }

                    return 0; // Success
                }
            } else if (sequence < isotp.expected_sequence) {
                debugPrint(config, "[DEBUG] Ignoring duplicate consecutive frame");
                // Ignore duplicate frame - continue waiting
            } else {
                std::cerr << "Error: Invalid sequence number. Expected: " << (int)isotp.expected_sequence
                         << ", Got: " << (int)sequence << std::endl;
                return 4; // Invalid response
            }
        }
        else {
            std::cerr << "Error: Invalid frame type: " << std::hex << (int)response.data[0] << std::endl;
            return 4; // Invalid response
        }
    }
}

/*****************************************************************************/
// Handle set-hwpartnum command
int handleSetHwPartNum(int sockfd, const Config& config) {
    if (config.value.length() > 16) {
        std::cerr << "Error: Hardware part number too long (max 16 characters)" << std::endl;
        return 2; // Invalid command
    }

    debugPrint(config, "[DEBUG] Sending set-hwpartnum command: " + config.value);

    // Prepare part number data (16 bytes, padded with zeros)
    uint8_t partnum_data[16];
    memset(partnum_data, 0, sizeof(partnum_data));
    memcpy(partnum_data, config.value.c_str(), std::min((size_t)16, config.value.length()));

    // Send first frame: 10 13 2E FD BD + first 3 bytes of part number
    uint8_t first_frame[8] = {0x10, 0x13, 0x2E, 0xFD, 0xBD,
                              partnum_data[0], partnum_data[1], partnum_data[2]};

    if (!sendCanFrame(sockfd, config, 0x703, first_frame, 8)) {
        std::cerr << "Error: Failed to send first frame" << std::endl;
        return 3; // CAN interface error
    }

    // Wait for flow control
    struct can_frame response;
    int timeout_ms = config.timeout * 1000;

    if (!receiveCanFrame(sockfd, config, response, timeout_ms, 0x70B)) {
        std::cerr << "Error: Timeout waiting for flow control" << std::endl;
        return 1; // Timeout
    }

    // Validate flow control
    if (response.can_id != 0x70B || response.data[0] != 0x30) {
        std::cerr << "Error: Invalid flow control response" << std::endl;
        return 4; // Invalid response
    }

    debugPrint(config, "[DEBUG] Received flow control, sending consecutive frames");

    // Send consecutive frame 1: 21 + next 7 bytes
    uint8_t cons_frame1[8] = {0x21, partnum_data[3], partnum_data[4], partnum_data[5],
                              partnum_data[6], partnum_data[7], partnum_data[8], partnum_data[9]};

    if (!sendCanFrame(sockfd, config, 0x703, cons_frame1, 8)) {
        std::cerr << "Error: Failed to send consecutive frame 1" << std::endl;
        return 3; // CAN interface error
    }

    // Send consecutive frame 2: 22 + last 6 bytes + padding
    uint8_t cons_frame2[8] = {0x22, partnum_data[10], partnum_data[11], partnum_data[12],
                              partnum_data[13], partnum_data[14], partnum_data[15], 0x00};

    if (!sendCanFrame(sockfd, config, 0x703, cons_frame2, 8)) {
        std::cerr << "Error: Failed to send consecutive frame 2" << std::endl;
        return 3; // CAN interface error
    }

    // Wait for positive response
    if (!receiveCanFrame(sockfd, config, response, timeout_ms, 0x70B)) {
        std::cerr << "Error: Timeout waiting for final response" << std::endl;
        return 1; // Timeout
    }

    // Validate positive response: 03 6E FD BD 00 00 00 00
    if (response.can_id != 0x70B || response.can_dlc < 4 ||
        response.data[0] != 0x03 || response.data[1] != 0x6E ||
        response.data[2] != 0xFD || response.data[3] != 0xBD) {

        // Check if we received another flow control frame instead of positive response
        if (response.data[0] == 0x30) {
            debugPrint(config, "[DEBUG] Received duplicate flow control, waiting for positive response");
            // Try to receive the actual positive response
            if (!receiveCanFrame(sockfd, config, response, timeout_ms, 0x70B)) {
                std::cerr << "Error: Timeout waiting for final response after flow control" << std::endl;
                return 1; // Timeout
            }

            // Check again for positive response
            if (response.can_id != 0x70B || response.can_dlc < 4 ||
                response.data[0] != 0x03 || response.data[1] != 0x6E ||
                response.data[2] != 0xFD || response.data[3] != 0xBD) {
                std::cerr << "Error: Invalid or negative response from daemon" << std::endl;
                return 4; // Invalid response
            }
        } else {
            std::cerr << "Error: Invalid or negative response from daemon" << std::endl;
            return 4; // Invalid response
        }
    }

    debugPrint(config, "[DEBUG] Received positive response");
    if (!config.verbose && !config.debug) {
        std::cout << "OK" << std::endl;
    }

    return 0; // Success
}

/*****************************************************************************/
// Parse command line arguments
bool parseArguments(int argc, char* argv[], Config& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            printHelp(argv[0]);
            exit(0);
        }
        else if (arg.rfind("--node=", 0) == 0) {
            config.node = arg.substr(7);
        }
        else if (arg.rfind("--timeout=", 0) == 0) {
            config.timeout = std::stoi(arg.substr(10));
        }
        else if (arg.rfind("--command=", 0) == 0) {
            config.command = arg.substr(10);
        }
        else if (arg.rfind("--value=", 0) == 0) {
            config.value = arg.substr(8);
        }
        else if (arg == "--verbose") {
            config.verbose = true;
        }
        else if (arg == "--debug") {
            config.debug = true;
        }
        else if (arg == "--node" && i + 1 < argc) {
            config.node = argv[++i];
        }
        else if (arg == "--timeout" && i + 1 < argc) {
            config.timeout = std::stoi(argv[++i]);
        }
        else if (arg == "--command" && i + 1 < argc) {
            config.command = argv[++i];
        }
        else if (arg == "--value" && i + 1 < argc) {
            config.value = argv[++i];
        }
        else {
            std::cerr << "Error: Unknown argument: " << arg << std::endl;
            return false;
        }
    }

    // Validate required arguments
    if (config.node.empty()) {
        std::cerr << "Error: --node is required" << std::endl;
        return false;
    }

    if (config.command.empty()) {
        std::cerr << "Error: --command is required" << std::endl;
        return false;
    }

    // Validate timeout
    if (config.timeout <= 0) {
        std::cerr << "Error: Invalid timeout value" << std::endl;
        return false;
    }

    return true;
}

/*****************************************************************************/
// Validate command and value combinations
bool validateCommand(const Config& config) {
    if (config.command == "set-pattern") {
        if (config.value.empty()) {
            std::cerr << "Error: set-pattern requires --value" << std::endl;
            return false;
        }
        if (config.value != "red" && config.value != "green" && config.value != "blue" &&
            config.value != "colorbar" && config.value != "home") {
            std::cerr << "Error: Invalid pattern value. Supported: red, green, blue, colorbar, home" << std::endl;
            return false;
        }
    }
    else if (config.command == "set-hwpartnum") {
        if (config.value.empty()) {
            std::cerr << "Error: set-hwpartnum requires --value" << std::endl;
            return false;
        }
        if (config.value.length() > 16) {
            std::cerr << "Error: Hardware part number too long (max 16 characters)" << std::endl;
            return false;
        }
    }
    else if (config.command == "get-hwpartnum") {
        // No additional validation needed
    }
    else if (config.command == "get-pattern") {
        std::cerr << "Error: get-pattern command not supported via CAN interface" << std::endl;
        return false;
    }
    else {
        std::cerr << "Error: Unknown command: " << config.command << std::endl;
        std::cerr << "Supported commands: set-pattern, get-hwpartnum, set-hwpartnum" << std::endl;
        return false;
    }

    return true;
}

/*****************************************************************************/
int main(int argc, char* argv[]) {
    Config config;

    // Parse arguments
    if (!parseArguments(argc, argv, config)) {
        printHelp(argv[0]);
        return 2; // Invalid command
    }

    // Validate command
    if (!validateCommand(config)) {
        return 2; // Invalid command
    }

    debugPrint(config, "[DEBUG] Configuration:");
    debugPrint(config, "[DEBUG]   Node: " + config.node);
    debugPrint(config, "[DEBUG]   Command: " + config.command);
    debugPrint(config, "[DEBUG]   Value: " + config.value);
    debugPrint(config, "[DEBUG]   Timeout: " + std::to_string(config.timeout));

    // Open CAN socket
    int sockfd = openCanSocket(config);
    if (sockfd < 0) {
        return 3; // CAN interface error
    }

    int result = 0;

    // Execute command
    try {
        if (config.command == "set-pattern") {
            result = handleSetPattern(sockfd, config);
        }
        else if (config.command == "get-hwpartnum") {
            result = handleGetHwPartNum(sockfd, config);
        }
        else if (config.command == "set-hwpartnum") {
            result = handleSetHwPartNum(sockfd, config);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        result = 4; // Protocol error
    }

    // Close socket
    close(sockfd);
    debugPrint(config, "[DEBUG] CAN socket closed");

    return result;
}
