//Display CAN Control daemon with TCP and CAN bus interface
//TCP Commands (default port 8082, configurable via --port):
//echo "pattern red" | nc -q 0 127.0.0.1 8082
//echo "pattern green" | nc -q 0 127.0.0.1 8082
//echo "pattern blue" | nc -q 0 127.0.0.1 8082
//echo "pattern colorbar" | nc -q 0 127.0.0.1 8082
//echo "pattern home" | nc -q 0 127.0.0.1 8082
//echo "get-pattern" | nc -q 0 127.0.0.1 8082
//echo "get-hwpartnum" | nc -q 0 127.0.0.1 8082
//echo "set-hwpartnum 1234567890ABCDEF" | nc -q 0 127.0.0.1 8082
//Pattern backend default: 127.0.0.1:8080 (configurable via --pattern_backend)

#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <string>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <mutex>
#include <map>
#include <vector>
#include <chrono>

using namespace std;

// Global running flag
std::atomic<bool> running(true);

// Default TCP port
int tcp_port = 8082;

// Pattern backend configuration
std::string pattern_backend_ip = "127.0.0.1";
int pattern_backend_port = 8080;

// Launcher backend configuration
std::string launcher_backend_ip = "";
int launcher_backend_port = 8081;

// Shared state with mutex protection
std::mutex state_mutex;
std::string current_pattern = "none";  // Current display pattern
char hwpartnum[17] = "0000000000000000";  // 16-byte ASCII + null terminator

// ISO-TP state for multi-frame messages
struct isotp_state {
    bool receiving = false;
    bool sending = false;
    std::vector<uint8_t> rx_buffer;
    std::vector<uint8_t> tx_buffer;
    uint8_t sequence_number = 0;
    uint8_t total_length = 0;
    canid_t request_id = 0;
    canid_t response_id = 0;
};
isotp_state isotp;
std::mutex isotp_mutex;

//forward declaration
bool send_launcher_command(const std::string& command, std::string& response);
/*****************************************************************************/
// Check if CAN interface is available
bool is_can_interface_available(const std::string& node) {
    int sockfd;
    struct ifreq ifr;

    // Try to create CAN socket
    if ((sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        std::cout << "Warning: Cannot create CAN socket" << std::endl;
        return false;
    }

    // Try to get interface index
    strcpy(ifr.ifr_name, node.c_str());
    if (ioctl(sockfd, SIOCGIFINDEX, &ifr) < 0) {
        std::cout << "Warning: CAN interface " << node << " not found" << std::endl;
        close(sockfd);
        return false;
    }

    close(sockfd);
    return true;
}

/*****************************************************************************/
// Stop pattern-generator via launcher
bool stop_pattern_generator() {
    std::string response;
    if (send_launcher_command("stop-app", response)) {
        if (response == "OK") {
            return true;
        }
    }
    return false;
}
// Parse launcher backend address (ip:port format)
bool parse_launcher_backend(const std::string& backend_str) {
    size_t colon_pos = backend_str.find(':');

    if (colon_pos == std::string::npos) {
        // No port specified, use IP with default port
        launcher_backend_ip = backend_str;
        launcher_backend_port = 8081; // Default port
    } else {
        std::string ip = backend_str.substr(0, colon_pos);
        std::string port_str = backend_str.substr(colon_pos + 1);

        if (ip.empty() || port_str.empty()) {
            std::cerr << "Error: Invalid launcher_backend format. IP or port is empty.\n";
            return false;
        }

        int port = atoi(port_str.c_str());
        if (port <= 0 || port > 65535) {
            std::cerr << "Error: Invalid port number in launcher_backend: " << port_str << "\n";
            return false;
        }

        // Basic IP validation
        struct sockaddr_in sa;
        int result = inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr));
        if (result != 1) {
            std::cerr << "Error: Invalid IP address in launcher_backend: " << ip << "\n";
            return false;
        }

        launcher_backend_ip = ip;
        launcher_backend_port = port;
    }

    return true;
}

/*****************************************************************************/
// Send command to launcher backend and get response
bool send_launcher_command(const std::string& command, std::string& response) {
    int sockfd;
    struct sockaddr_in server_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return false;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(launcher_backend_ip.c_str());
    server_addr.sin_port = htons(launcher_backend_port);

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return false;
    }

    std::string full_cmd = command + "\n";
    if (write(sockfd, full_cmd.c_str(), full_cmd.length()) < 0) {
        close(sockfd);
        return false;
    }

    // Read response
    char buffer[1024] = {0};
    ssize_t bytes_read = read(sockfd, buffer, sizeof(buffer) - 1);
    close(sockfd);

    if (bytes_read > 0) {
        response = std::string(buffer, bytes_read);
        // Remove trailing newline if present
        if (!response.empty() && response.back() == '\n') {
            response.pop_back();
        }
        return true;
    }

    return false;
}

/*****************************************************************************/
// Get currently running app from launcher
std::string get_running_app() {
    std::string response;
    if (send_launcher_command("get-running-app", response)) {
        return response;
    }
    return "";
}

/*****************************************************************************/
// Start pattern-generator via launcher
bool start_pattern_generator() {
    std::string response;
    if (send_launcher_command("start-app pattern-generator", response)) {
        if (response == "OK") {
            // Wait for pattern-generator to start
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            return true;
        }
    }
    return false;
}

/*****************************************************************************/
// Ensure pattern-generator is running
bool ensure_pattern_generator_running() {
    // Check if launcher is configured
    if (launcher_backend_ip.empty()) {
        return true; // No launcher configured, skip check
    }

    // Get currently running app
    std::string running_app = get_running_app();
    if (running_app.empty()) {
        // Cannot communicate with launcher
        return false;
    }

    // Check if pattern-generator is already running
    if (running_app == "pattern-generator") {
        return true; // Already running
    }

    // Start pattern-generator
    return start_pattern_generator();
}

/*****************************************************************************/
// Signal handler to handle SIGINT (Ctrl+C) for graceful shutdown
void handle_signal(int signal) {
    if (signal == SIGINT) {
        std::cout << "\nSIGINT received. Shutting down gracefully...\n";
        running = false;
    }
}

/*****************************************************************************/
// Send pattern command to pattern-daemon
std::string send_pattern_command(const std::string& pattern_cmd) {
    int sockfd;
    struct sockaddr_in server_addr;

    std::cout << "DEBUG: Processing pattern command: " << pattern_cmd << std::endl;

    // Handle "home" pattern specially - stop pattern-generator via launcher
    if (pattern_cmd == "home" && !launcher_backend_ip.empty()) {
        std::cout << "DEBUG: Stopping pattern-generator via launcher backend" << std::endl;
        if (stop_pattern_generator()) {
            std::cout << "DEBUG: Successfully stopped pattern-generator" << std::endl;
            return "OK";
        } else {
            std::cout << "DEBUG: Failed to stop pattern-generator via launcher" << std::endl;
            // Fallback to sending pattern none to pattern-backend (for compatibility)
        }
    }

    std::cout << "DEBUG: Attempting to connect to pattern backend: " << pattern_backend_ip << ":" << pattern_backend_port << std::endl;

    // Ensure pattern-generator is running if launcher is configured (except for "home" pattern)
    if (!launcher_backend_ip.empty() && pattern_cmd != "home") {
        std::cout << "DEBUG: Checking launcher backend: " << launcher_backend_ip << ":" << launcher_backend_port << std::endl;
        if (!ensure_pattern_generator_running()) {
            std::cout << "DEBUG: Failed to ensure pattern-generator is running via launcher" << std::endl;
            // Continue with direct connection attempt (fallback)
        } else {
            std::cout << "DEBUG: Pattern-generator is running" << std::endl;
        }
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cout << "DEBUG: Socket creation failed" << std::endl;
        return "Comm-Error";
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(pattern_backend_ip.c_str());
    server_addr.sin_port = htons(pattern_backend_port);

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cout << "DEBUG: Connection failed to " << pattern_backend_ip << ":" << pattern_backend_port << std::endl;
        perror("DEBUG: Connect error");
        close(sockfd);
        return "Comm-Error";
    }

    // For "home" pattern, send "pattern none" to pattern-backend for fallback compatibility
    std::string backend_cmd = (pattern_cmd == "home") ? "none" : pattern_cmd;
    std::string full_cmd = "pattern " + backend_cmd + "\n";
    std::cout << "DEBUG: Sending command: '" << backend_cmd << "'" << std::endl;

    ssize_t bytes_written = write(sockfd, full_cmd.c_str(), full_cmd.length());
    if (bytes_written < 0) {
        std::cout << "DEBUG: Write failed" << std::endl;
        perror("DEBUG: Write error");
        close(sockfd);
        return "Comm-Error";
    }

    std::cout << "DEBUG: Successfully sent " << bytes_written << " bytes to pattern backend" << std::endl;

    // Add a small delay to ensure data is transmitted
    usleep(10000);  // 10ms delay

    close(sockfd);
    return "OK";
}

/*****************************************************************************/
// Update current pattern and send to pattern-daemon
std::string set_pattern(const std::string& pattern) {
    // No need to map "home" to "none" anymore - keep "home" as-is
    std::string result = send_pattern_command(pattern);
    if (result == "OK") {
        std::lock_guard<std::mutex> lock(state_mutex);
        current_pattern = pattern;
    }
    return result;
}

/*****************************************************************************/
// Get current pattern
std::string get_pattern() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return current_pattern;
}

/*****************************************************************************/
// Set hardware part number
std::string set_hwpartnum(const std::string& partnum) {
    if (partnum.length() > 16) {
        return "Error";
    }

    std::lock_guard<std::mutex> lock(state_mutex);
    memset(hwpartnum, 0, sizeof(hwpartnum));
    strncpy(hwpartnum, partnum.c_str(), 16);
    return "OK";
}

/*****************************************************************************/
// Get hardware part number
std::string get_hwpartnum() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return std::string(hwpartnum);
}

/*****************************************************************************/
// Send CAN frame
bool send_can_frame(int sockfd, canid_t id, const uint8_t* data, uint8_t dlc) {
    struct can_frame frame;
    frame.can_id = id;
    frame.can_dlc = dlc;
    memcpy(frame.data, data, dlc);

    bool result = write(sockfd, &frame, sizeof(struct can_frame)) == sizeof(struct can_frame);

    // Debug output for sent frames
    if (result) {
        printf("TX [%03X]: ", id);
        for (int i = 0; i < dlc; i++)
            printf("%02X ", data[i]);
        printf("\n");
    } else {
        printf("ERROR: Failed to send CAN frame [%03X]\n", id);
    }

    return result;
}

/*****************************************************************************/
// Send ISO-TP single frame
bool send_isotp_single(int sockfd, canid_t id, const uint8_t* data, uint8_t length) {
    uint8_t frame_data[8] = {0};
    frame_data[0] = length;  // Single frame: 0x0L where L is length
    memcpy(&frame_data[1], data, length);
    return send_can_frame(sockfd, id, frame_data, 8);
}

/*****************************************************************************/
// Send ISO-TP first frame
bool send_isotp_first(int sockfd, canid_t id, const uint8_t* data, uint8_t total_length) {
    uint8_t frame_data[8] = {0};
    frame_data[0] = 0x10 | (total_length >> 8);  // First frame: 0x1F where F is upper nibble of length
    frame_data[1] = total_length & 0xFF;         // Lower byte of length
    memcpy(&frame_data[2], data, 6);             // First 6 bytes of data
    return send_can_frame(sockfd, id, frame_data, 8);
}

/*****************************************************************************/
// Send ISO-TP consecutive frame
bool send_isotp_consecutive(int sockfd, canid_t id, const uint8_t* data, uint8_t seq_num, uint8_t length) {
    uint8_t frame_data[8] = {0};
    frame_data[0] = 0x20 | (seq_num & 0x0F);  // Consecutive frame: 0x2S where S is sequence number
    memcpy(&frame_data[1], data, length);
    return send_can_frame(sockfd, id, frame_data, 8);
}

/*****************************************************************************/
// Send flow control frame
bool send_flow_control(int sockfd, canid_t id, uint8_t flow_status) {
    uint8_t frame_data[8] = {0};
    frame_data[0] = 0x30 | (flow_status & 0x0F);  // Flow control: 0x3F where F is flow status
    frame_data[1] = 0x00;  // Block size (0 = no limit)
    frame_data[2] = 0x00;  // Separation time minimum
    return send_can_frame(sockfd, id, frame_data, 8);
}

/*****************************************************************************/
// Process CAN command and send response
void process_can_command(int sockfd, const struct can_frame& req_frame) {
    struct can_frame resp_frame;
    uint8_t response_data[8] = {0};

    // Standard single-frame commands
    if (req_frame.can_dlc >= 4 && req_frame.data[0] == 0x04) {
        // Single frame commands (length 4)
        if (req_frame.data[1] == 0x2E && req_frame.data[2] == 0xFD && req_frame.data[3] == 0x38) {
            // Pattern commands
            std::string pattern;
            switch (req_frame.data[4]) {
                case 0x01: pattern = "black"; break;
                case 0x02: pattern = "white"; break;
                case 0x03: pattern = "red"; break;
                case 0x04: pattern = "green"; break;
                case 0x05: pattern = "blue"; break;
                case 0x06: pattern = "colorbar"; break;
                case 0x07: pattern = "grayscale-ramp"; break;
                case 0x08: pattern = "ansi-checker"; break;
                default: return; // Invalid command
            }

            if (set_pattern(pattern) == "OK") {
                // Send positive response: 03 6E FD 38 11 00 00 00
                response_data[0] = 0x03;
                response_data[1] = 0x6E;
                response_data[2] = 0xFD;
                response_data[3] = 0x38;
                response_data[4] = 0x11;
                printf("DEBUG: Setting pattern to '%s' via CAN\n", pattern.c_str());
                send_can_frame(sockfd, 0x70B, response_data, 8);
            } else {
                printf("DEBUG: Failed to set pattern '%s' via CAN\n", pattern.c_str());
            }
        }
        else if (req_frame.data[1] == 0x2E && req_frame.data[2] == 0xFD && req_frame.data[3] == 0xC0) {
            // HMI HomeScreen command
            if (req_frame.data[4] == 0x01) {
                if (set_pattern("home") == "OK") {
                    // Send positive response: 03 6E FD C0 01 00 00 00
                    response_data[0] = 0x03;
                    response_data[1] = 0x6E;
                    response_data[2] = 0xFD;
                    response_data[3] = 0xC0;
                    response_data[4] = 0x01;
                    printf("DEBUG: Setting pattern to 'home' via CAN\n");
                    send_can_frame(sockfd, 0x70B, response_data, 8);
                } else {
                    printf("DEBUG: Failed to set pattern 'home' via CAN\n");
                }
            }
        }
    }
    // Get HWPartNum command (single frame request)
    else if (req_frame.can_dlc >= 4 && req_frame.data[0] == 0x03 &&
             req_frame.data[1] == 0x22 && req_frame.data[2] == 0xFD && req_frame.data[3] == 0xBD) {

        std::string partnum = get_hwpartnum();
        uint8_t partnum_bytes[16];
        memset(partnum_bytes, 0, sizeof(partnum_bytes));
        memcpy(partnum_bytes, partnum.c_str(), std::min((size_t)16, partnum.length()));

        // Send multi-frame response
        std::lock_guard<std::mutex> lock(isotp_mutex);
        isotp.sending = true;
        isotp.response_id = 0x70B;
        isotp.sequence_number = 1;

        // Prepare full response: 10 13 62 FD BD + 16 bytes of part number
        isotp.tx_buffer.clear();
        isotp.tx_buffer.push_back(0x62);
        isotp.tx_buffer.push_back(0xFD);
        isotp.tx_buffer.push_back(0xBD);
        for (int i = 0; i < 16; i++) {
            isotp.tx_buffer.push_back(partnum_bytes[i]);
        }

        // Send first frame: 10 13 62 FD BD + first 3 bytes of part number
        uint8_t first_frame[8] = {0x10, 0x13, 0x62, 0xFD, 0xBD,
                                  partnum_bytes[0], partnum_bytes[1], partnum_bytes[2]};
        send_can_frame(sockfd, 0x70B, first_frame, 8);
    }
    // Set HWPartNum command (multi-frame)
    else if (req_frame.data[0] == 0x10) {
        // First frame of multi-frame message
        uint8_t total_length = req_frame.data[1];
        if (total_length == 0x13 && req_frame.data[2] == 0x2E &&
            req_frame.data[3] == 0xFD && req_frame.data[4] == 0xBD) {

            std::lock_guard<std::mutex> lock(isotp_mutex);
            isotp.receiving = true;
            isotp.total_length = total_length;
            isotp.sequence_number = 1;
            isotp.request_id = req_frame.can_id;
            isotp.response_id = 0x70B;
            isotp.rx_buffer.clear();

            // Store service ID and parameter ID
            isotp.rx_buffer.push_back(req_frame.data[2]);  // 0x2E
            isotp.rx_buffer.push_back(req_frame.data[3]);  // 0xFD
            isotp.rx_buffer.push_back(req_frame.data[4]);  // 0xBD

            // Store first 3 bytes of part number
            for (int i = 5; i < 8; i++) {
                isotp.rx_buffer.push_back(req_frame.data[i]);
            }

            // Send flow control: Continue to Send
            send_flow_control(sockfd, 0x70B, 0x00);
        }
    }
    else if (req_frame.data[0] >= 0x21 && req_frame.data[0] <= 0x2F) {
        // Consecutive frame
        std::lock_guard<std::mutex> lock(isotp_mutex);
        if (isotp.receiving) {
            uint8_t expected_seq = 0x20 | isotp.sequence_number;
            if (req_frame.data[0] == expected_seq) {
                // Store remaining data
                for (int i = 1; i < 8 && isotp.rx_buffer.size() < isotp.total_length; i++) {
                    isotp.rx_buffer.push_back(req_frame.data[i]);
                }

                isotp.sequence_number++;
                if (isotp.sequence_number > 0x0F) isotp.sequence_number = 0;

                // Check if we received all data
                if (isotp.rx_buffer.size() >= isotp.total_length) {
                    // Extract part number (skip service bytes)
                    if (isotp.rx_buffer.size() >= 19) {  // 3 service bytes + 16 part number bytes
                        std::string new_partnum;
                        for (int i = 3; i < 19 && i < isotp.rx_buffer.size(); i++) {
                            if (isotp.rx_buffer[i] != 0) {
                                new_partnum += (char)isotp.rx_buffer[i];
                            }
                        }

                        if (set_hwpartnum(new_partnum) == "OK") {
                            // Send positive response: 03 6E FD BD 00 00 00 00
                            uint8_t pos_resp[8] = {0x03, 0x6E, 0xFD, 0xBD, 0x00, 0x00, 0x00, 0x00};
                            send_can_frame(sockfd, 0x70B, pos_resp, 8);
                        }
                    }

                    isotp.receiving = false;
                    isotp.rx_buffer.clear();
                }
            }
        }
    }
    else if (req_frame.data[0] == 0x30) {
        // Flow control received - continue sending multi-frame response
        std::lock_guard<std::mutex> lock(isotp_mutex);
        if (isotp.sending) {
            // Send consecutive frames
            size_t bytes_sent = 6; // Already sent in first frame (excluding length bytes)

            while (bytes_sent < isotp.tx_buffer.size()) {
                size_t remaining = isotp.tx_buffer.size() - bytes_sent;
                size_t to_send = std::min((size_t)7, remaining);

                uint8_t cons_frame[8] = {0};
                cons_frame[0] = 0x20 | (isotp.sequence_number & 0x0F);

                for (size_t i = 0; i < to_send; i++) {
                    cons_frame[i + 1] = isotp.tx_buffer[bytes_sent + i];
                }

                send_can_frame(sockfd, isotp.response_id, cons_frame, 8);

                bytes_sent += to_send;
                isotp.sequence_number++;
                if (isotp.sequence_number > 0x0F) isotp.sequence_number = 0;

                usleep(10000); // 10ms delay between frames
            }

            isotp.sending = false;
            isotp.tx_buffer.clear();
        }
    }
}

/*****************************************************************************/
// Function to listen on TCP socket
void socket_listener() {
    int sockfd;
    struct sockaddr_in server_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return;
    }

    // Enable socket reuse
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(tcp_port);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Socket bind failed");
        close(sockfd);
        return;
    }

    if (listen(sockfd, 3) < 0) {
        perror("Socket listen failed");
        close(sockfd);
        return;
    }

    std::cout << "Socket listener started on port " << tcp_port << ".\n";

    while (running) {
        int new_socket;
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0 && errno != EINTR) {
            perror("Select error");
            break;
        }

        if (FD_ISSET(sockfd, &read_fds)) {
            if ((new_socket = accept(sockfd, (struct sockaddr*)&client_addr, &addr_len)) < 0) {
                if (running)
                    perror("Socket accept failed");
                break;
            }

            char buffer[1024] = {0};
            ssize_t bytes_read = read(new_socket, buffer, 1023);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';

                std::string cmd, cmdArg;
                std::string buf(buffer);
                stringstream msgstream(buf);
                msgstream >> cmd;
                msgstream >> cmdArg;
                transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

                std::string response = "Error";

                if (cmd == "pattern") {
                    if (cmdArg == "black" || cmdArg == "white" || cmdArg == "red" || cmdArg == "green" ||
                        cmdArg == "blue" || cmdArg == "colorbar" || cmdArg == "grayscale-ramp" || cmdArg == "ansi-checker" ||
                        cmdArg == "home") {
                        response = set_pattern(cmdArg);
                    }
                }
                else if (cmd == "get-pattern") {
                    response = get_pattern();
                }
                else if (cmd == "get-hwpartnum") {
                    response = get_hwpartnum();
                }
                else if (cmd == "set-hwpartnum") {
                    if (!cmdArg.empty() && cmdArg.length() <= 16) {
                        response = set_hwpartnum(cmdArg);
                    }
                }

                response += "\n";
                if (write(new_socket, response.c_str(), response.length()) < 0) {
                    // Write failed, but we'll close the socket anyway
                }
            }
            close(new_socket);
        }
    }
    close(sockfd);
    std::cout << "Socket listener stopped.\n";
}

/*****************************************************************************/
// Function to listen on CAN bus
void canbus_listener(bool debugprint, std::string node) {
    int sockfd;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame;

    if ((sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("CAN socket creation failed");
        return;
    }

    strcpy(ifr.ifr_name, node.c_str());
    ioctl(sockfd, SIOCGIFINDEX, &ifr);

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("CAN socket bind failed");
        close(sockfd);
        return;
    }

    std::cout << "CAN bus listener started on interface: " << node << endl;

    while (running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0 && errno != EINTR) {
            perror("Select error");
            break;
        }

        if (FD_ISSET(sockfd, &read_fds)) {
            int nbytes = read(sockfd, &frame, sizeof(struct can_frame));
            if (nbytes < 0) {
                if (running)
                    perror("CAN read failed");
                break;
            }

            // Print incoming request data
            if (debugprint) {
                printf("RX [%03X]: ", frame.can_id);
                for (int i = 0; i < frame.can_dlc; i++)
                    printf("%02X ", frame.data[i]);
                printf("\n");
            }

            // Process commands on ID 0x703
            if (frame.can_id == 0x703) {
                process_can_command(sockfd, frame);

                // Print outgoing response data if debug enabled
                if (debugprint) {
                    printf("CAN command processed for ID 0x703\n");
                }
            }
        }
    }
    close(sockfd);
    std::cout << "CAN bus listener stopped.\n";
}

/*****************************************************************************/
// Parse pattern backend address (ip:port format)
bool parse_pattern_backend(const std::string& backend_str) {
    size_t colon_pos = backend_str.find(':');
    if (colon_pos == std::string::npos) {
        std::cerr << "Error: Invalid pattern_backend format. Expected ip:port (e.g., 192.168.1.1:8080)\n";
        return false;
    }

    std::string ip = backend_str.substr(0, colon_pos);
    std::string port_str = backend_str.substr(colon_pos + 1);

    if (ip.empty() || port_str.empty()) {
        std::cerr << "Error: Invalid pattern_backend format. IP or port is empty.\n";
        return false;
    }

    int port = atoi(port_str.c_str());
    if (port <= 0 || port > 65535) {
        std::cerr << "Error: Invalid port number in pattern_backend: " << port_str << "\n";
        return false;
    }

    // Basic IP validation (simple check for valid format)
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr));
    if (result != 1) {
        std::cerr << "Error: Invalid IP address in pattern_backend: " << ip << "\n";
        return false;
    }

    pattern_backend_ip = ip;
    pattern_backend_port = port;
    return true;
}

/*****************************************************************************/
void printHelp(std::string program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  --node=<canx>              Specify the can0/can1 node (or --node <canx>)\n"
              << "  --port=<port>              Specify the TCP listen port (or --port <port>) [default: 8082]\n"
              << "  --pattern_backend=<ip:port> Specify pattern backend address (or --pattern_backend <ip:port>) [default: 127.0.0.1:8080]\n"
              << "  --launcher_backend=<ip:port> Specify launcher backend address (or --launcher_backend <ip:port>) [default port: 8081]\n"
              << "  --debugprint=<flag>        Specify the true/false debug print (or --debugprint <flag>)\n"
              << "  --help                     Display this help message\n"
              << "\nExamples:\n"
              << "  " << program << " --node=can0 --port=8085 --pattern_backend=192.168.1.100:8080\n"
              << "  " << program << " --node can0 --pattern_backend 10.0.0.5:9090 --debugprint true\n"
              << "  " << program << " --node=can0 --launcher_backend=127.0.0.1:8081 --pattern_backend=127.0.0.1:8080\n";
}

/*****************************************************************************/
int main(int argc, char* argv[]) {
    std::string myname = "disp-can-ctrl";
    std::string node = "Unknown";
    std::string debugprint = "Unknown";
    std::string port = "Unknown";
    std::string pattern_backend = "Unknown";
    std::string launcher_backend = "Unknown";
    bool debugflag = false;

    // If no arguments or --help is passed, print the help message
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) {
        printHelp(myname);
        return 0;
    }

    // Iterate over command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        // Check for --node= format
        if (arg.rfind("--node=", 0) == 0)
            node = arg.substr(7);  // Extract the value after '='

        // Check for --port= format
        else if (arg.rfind("--port=", 0) == 0)
            port = arg.substr(7);  // Extract the value after '='

        // Check for --pattern_backend= format
        else if (arg.rfind("--pattern_backend=", 0) == 0)
            pattern_backend = arg.substr(18);  // Extract the value after '='

        // Check for --launcher_backend= format
        else if (arg.rfind("--launcher_backend=", 0) == 0)
            launcher_backend = arg.substr(19);  // Extract the value after '='

        // Check for --debugprint= format
        else if (arg.rfind("--debugprint=", 0) == 0)
            debugprint = arg.substr(13);  // Extract the value after '='

        // Check for --node followed by value
        else if (arg == "--node" && i + 1 < argc)
            node = argv[++i];  // Get the next argument as the node

        // Check for --port followed by value
        else if (arg == "--port" && i + 1 < argc)
            port = argv[++i];  // Get the next argument as the port

        // Check for --pattern_backend followed by value
        else if (arg == "--pattern_backend" && i + 1 < argc)
            pattern_backend = argv[++i];  // Get the next argument as the pattern_backend

        // Check for --launcher_backend followed by value
        else if (arg == "--launcher_backend" && i + 1 < argc)
            launcher_backend = argv[++i];  // Get the next argument as the launcher_backend

        // Check for --debugprint followed by value
        else if (arg == "--debugprint" && i + 1 < argc)
            debugprint = argv[++i];  // Get the next argument as the debugprint
    }

    // Set TCP port if specified
    if (port != "Unknown") {
        int port_num = atoi(port.c_str());
        if (port_num > 0 && port_num <= 65535) {
            tcp_port = port_num;
        } else {
            std::cerr << "Error: Invalid port number " << port << ". Using default port " << tcp_port << ".\n";
        }
    }

    // Set pattern backend if specified
    if (pattern_backend != "Unknown") {
        if (!parse_pattern_backend(pattern_backend)) {
            std::cerr << "Using default pattern backend: " << pattern_backend_ip << ":" << pattern_backend_port << "\n";
        }
    }

    // Set launcher backend if specified
    if (launcher_backend != "Unknown") {
        if (!parse_launcher_backend(launcher_backend)) {
            std::cerr << "Using default launcher backend: " << launcher_backend_ip << ":" << launcher_backend_port << "\n";
        }
    }

    for (auto& c : debugprint)
        c = tolower(c);
    if (debugprint == "true")
        debugflag = true;

    // Check CAN interface availability
    bool can_available = is_can_interface_available(node);

    // Print configuration
    std::cout << "Configuration:\n"
              << "  CAN Node: " << node << (can_available ? " (available)" : " (unavailable - TCP only mode)") << "\n"
              << "  TCP Listen Port: " << tcp_port << "\n"
              << "  Pattern Backend: " << pattern_backend_ip << ":" << pattern_backend_port << "\n";

    if (!launcher_backend_ip.empty()) {
        std::cout << "  Launcher Backend: " << launcher_backend_ip << ":" << launcher_backend_port << "\n";
    } else {
        std::cout << "  Launcher Backend: disabled\n";
    }

    std::cout << "  Debug Print: " << (debugflag ? "enabled" : "disabled") << "\n\n";

    // Log CAN unavailability warning
    if (!can_available) {
        std::cout << "WARNING: CAN interface " << node << " is unavailable." << std::endl;
        std::cout << "WARNING: Running in TCP-only mode. CAN bus functionality disabled." << std::endl;

        // Log to file if possible
        std::ofstream logfile("/var/log/disp-can-ctrl.log", std::ios::app);
        if (logfile.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            logfile << std::ctime(&time_t);
            logfile << "WARNING: CAN interface " << node << " unavailable. Running in TCP-only mode." << std::endl;
            logfile.close();
        }
    }

    // Set up the signal handler
    std::signal(SIGINT, handle_signal);

    // Start TCP thread (always available)
    std::thread socket_thread(socket_listener);

    // Start CAN thread only if interface is available
    std::thread canbus_thread;
    if (can_available) {
        std::cout << "Starting CAN bus listener..." << std::endl;
        canbus_thread = std::thread(canbus_listener, debugflag, node);
    } else {
        std::cout << "Skipping CAN bus listener (interface unavailable)" << std::endl;
    }

    // Wait for threads to complete
    socket_thread.join();
    if (can_available && canbus_thread.joinable()) {
        canbus_thread.join();
    }

    std::cout << "All threads have exited. Program terminated.\n";
    return 0;
}
