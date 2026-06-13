# disp-can-ctrl System Documentation

## Overview

The `disp-can-ctrl` system provides a professional CAN bus to TCP/IP bridge for display pattern control and hardware configuration management. It consists of two main components:

- **`disp-can-ctrl`** - Multi-threaded daemon providing CAN ↔ TCP bridge functionality
- **`disp-can-client`** - Command-line utility for CAN bus testing and automation

The system supports ISO-TP multi-frame messaging, launcher integration for service management, and comprehensive error handling with fallback mechanisms.

---

## Architecture

```
┌─────────────────┐    ┌──────────────────┐    ┌────────────────────┐
│   CAN Client    │    │  disp-can-ctrl   │    │  QT-Pattern Backend│
│ (ID 0x703)   clt│◄──►│srv (Daemon)   clt│◄──►│srv (Port 8080)     │
└─────────────────┘    │                  │    └────────────────────┘
                       │  ┌─────────────┐ │    
┌─────────────────┐    │  │ TCP Server  │ │    ┌────────────────────┐
│  TCP Client  clt│◄──►│sr│(Port 8085)  │ │    │ QT-Launcher Backend│
│                 │    │  └─────────────┘c│◄──►│srv (Port 8081)     │
└─────────────────┘    └──────────────────┘    └────────────────────┘
```
[Here](../qt-demo-launcher/README.md) you can find more details on **QT-Launcher Backend** app.

[Here](../disp-tester/README.md) you can find more details on **QT-Pattern Backend** app.

---

## disp-can-ctrl Daemon

### Features

- **Dual Interface Support**: CAN bus (SocketCAN) and TCP/IP
- **ISO-TP Protocol**: Complete multi-frame CAN messaging support
- **Launcher Integration**: Automatic service management
- **Pattern Control**: Support for 9 display patterns
- **Hardware Management**: 16-byte part number storage with persistence
- **Thread-Safe Design**: Mutex-protected shared state
- **Robust Error Handling**: Comprehensive fallback mechanisms

### Command-Line Arguments

```bash
disp-can-ctrl [options]

Options:
  --node=<canx>                   CAN interface (required)
  --port=<port>                   TCP listen port (default: 8082)
  --pattern_backend=<ip:port>     Pattern daemon address (default: 127.0.0.1:8080)
  --launcher_backend=<ip:port>    Launcher daemon address (optional, default port: 8081)
  --debugprint=<flag>             Enable debug output (default: false)
  --help                          Show help message

Examples:
  # Basic usage
  disp-can-ctrl --node=can0
  
  # Custom TCP port
  disp-can-ctrl --node=can0 --port=8085
  
  # Remote pattern backend
  disp-can-ctrl --node=can0 --pattern_backend=192.168.1.100:8080
  
  # With launcher integration
  disp-can-ctrl --node=can0 --launcher_backend=127.0.0.1:8081
  
  # Full configuration with debug
  disp-can-ctrl --node=can0 --port=8085 --pattern_backend=192.168.1.86:8080 --launcher_backend=127.0.0.1:8081 --debugprint=true
```

### System Service Management

The daemon integrates with system init scripts for automatic startup:

```bash
# Service control
/etc/init.d/S99DispCanCtrl start    # Start daemon
/etc/init.d/S99DispCanCtrl stop     # Stop daemon
/etc/init.d/S99DispCanCtrl restart  # Restart daemon
/etc/init.d/S99DispCanCtrl status   # Check status

# Auto-start on boot (handled automatically by S99 priority)
```

---

## Network API (TCP Interface)

### Connection

Connect to the daemon's TCP interface:

```bash
# Default port
nc 127.0.0.1 8082

# Custom port (if configured)
nc 127.0.0.1 8085
```

### Pattern Control Commands

| Command | Description | Response | Example |
|---------|-------------|----------|---------|
| `pattern black` | Set display to black | `OK` | `echo "pattern black" \| nc -q 0 127.0.0.1 8082` |
| `pattern white` | Set display to white | `OK` | `echo "pattern white" \| nc -q 0 127.0.0.1 8082` |
| `pattern red` | Set display to red | `OK` | `echo "pattern red" \| nc -q 0 127.0.0.1 8082` |
| `pattern green` | Set display to green | `OK` | `echo "pattern green" \| nc -q 0 127.0.0.1 8082` |
| `pattern blue` | Set display to blue | `OK` | `echo "pattern blue" \| nc -q 0 127.0.0.1 8082` |
| `pattern colorbar` | Set display to colorbar | `OK` | `echo "pattern colorbar" \| nc -q 0 127.0.0.1 8082` |
| `pattern grayscale-ramp` | Set display to grayscale ramp | `OK` | `echo "pattern grayscale-ramp" \| nc -q 0 127.0.0.1 8082` |
| `pattern ansi-checker` | Set display to ANSI checker | `OK` | `echo "pattern ansi-checker" \| nc -q 0 127.0.0.1 8082` |
| `pattern home` | Return to launcher home | `OK` | `echo "pattern home" \| nc -q 0 127.0.0.1 8082` |

### Query Commands

| Command | Description | Response Example | Usage |
|---------|-------------|------------------|-------|
| `get-pattern` | Get current pattern | `red` | `echo "get-pattern" \| nc -q 0 127.0.0.1 8082` |
| `get-hwpartnum` | Get hardware part number | `ABC123DEF456` | `echo "get-hwpartnum" \| nc -q 0 127.0.0.1 8082` |

### Configuration Commands

| Command | Description | Response | Example |
|---------|-------------|----------|---------|
| `set-hwpartnum <value>` | Set hardware part number (max 16 chars) | `OK` | `echo "set-hwpartnum TESTPART123" \| nc -q 0 127.0.0.1 8082` |

### Error Responses

| Response | Description |
|----------|-------------|
| `OK` | Command executed successfully |
| `Error` | Invalid command or parameter |
| `Comm-Error` | Communication error with backend services |

### Remote Access

Access the daemon from other machines on the network:

```bash
# Replace with your device's IP address
echo "pattern red" | nc -q 0 192.168.1.89 8082
echo "get-pattern" | nc -q 0 192.168.1.89 8082
```

---

## CAN Bus Interface

### CAN Configuration

The daemon uses SocketCAN and requires proper CAN interface setup:

```bash
# Basic setup
sudo ip link set can0 up type can bitrate 500000

# With loopback (for development/testing)
sudo ip link set can0 up type can bitrate 500000 loopback on

# Monitor CAN traffic
candump can0
```

### CAN Message Format

- **Request ID**: `0x703`
- **Response ID**: `0x70B`
- **Protocol**: ISO-TP for multi-frame messages
- **Frame Format**: 8-byte CAN frames

### Single-Frame Pattern Commands

| Pattern | Request Frame | Response Frame | Description |
|---------|---------------|----------------|-------------|
| Black | `703#042EFD3801000000` | `70B#036EFD3811000000` | Set display to black |
| White | `703#042EFD3802000000` | `70B#036EFD3811000000` | Set display to white |
| Red | `703#042EFD3803000000` | `70B#036EFD3811000000` | Set display to red |
| Green | `703#042EFD3804000000` | `70B#036EFD3811000000` | Set display to green |
| Blue | `703#042EFD3805000000` | `70B#036EFD3811000000` | Set display to blue |
| Colorbar | `703#042EFD3806000000` | `70B#036EFD3811000000` | Set display to colorbar |
| Grayscale Ramp | `703#042EFD3807000000` | `70B#036EFD3811000000` | Set display to grayscale ramp |
| ANSI Checker | `703#042EFD3808000000` | `70B#036EFD3811000000` | Set display to ANSI checker |
| Home Screen | `703#042EFDC001000000` | `70B#036EFDC001000000` | Return to launcher home |

### Multi-Frame Commands (ISO-TP)

#### Get Hardware Part Number

**Request** (Single Frame):
```
703#0322FDBD00000000
```

**Response** (Multi-Frame):
```
70B#1013622EFDBD504E  # First Frame: length=0x13, service=0x62, first 3 bytes
703#3000000000000000  # Flow Control: Continue (sent by client)
70B#21504152544E554D  # Consecutive Frame 1: next 7 bytes
70B#22000000000000000  # Consecutive Frame 2: remaining bytes + padding
```

#### Set Hardware Part Number

**Example**: Setting "TESTPART123456"

**Request** (Multi-Frame):
```
703#10132EFDBD544553  # First Frame: length=0x13, service=0x2E, "TES"
70B#3000000000000000  # Flow Control: Continue (sent by daemon)
703#21545041525431323  # Consecutive Frame 1: "TPART12"  
703#22333435360000000  # Consecutive Frame 2: "3456" + padding
```

**Response**:
```
70B#036EFDBD00000000  # Positive response
```

### Raw CAN Testing

```bash
# Send pattern commands
cansend can0 703#042EFD3803000000  # Red
cansend can0 703#042EFD3804000000  # Green
cansend can0 703#042EFD3805000000  # Blue

# Monitor responses
candump can0 | grep 70B

# Test hardware part number
cansend can0 703#0322FDBD00000000  # Get part number
# Send flow control when you see first frame response
cansend can0 703#3000000000000000
```

---

## disp-can-client Utility

### Overview

A command-line utility for testing and automating CAN bus operations with the `disp-can-ctrl` daemon.

### Features

- **Pattern Control**: All 9 supported patterns
- **Hardware Management**: Get/set part numbers with automatic ISO-TP handling
- **Multiple Output Modes**: Minimal, verbose, and debug
- **Error Handling**: Comprehensive timeout and validation
- **Loopback Support**: Works in development environments

### Command-Line Usage

```bash
disp-can-client [options]

Options:
  --node=<canx>           CAN interface (required)
  --timeout=<seconds>     Response timeout (default: 2)
  --command=<cmd>         Command to execute (required)
  --value=<val>          Value for set commands
  --verbose              Show CAN frames
  --debug                Show detailed debug info
  --help                 Show help message

Supported Commands:
  set-pattern            Set display pattern
  get-hwpartnum          Get hardware part number  
  set-hwpartnum          Set hardware part number

Supported Pattern Values:
  black, white, red, green, blue, colorbar, grayscale-ramp, ansi-checker, home
```

### Pattern Control Examples

```bash
# Basic pattern setting
disp-can-client --node=can0 --command=set-pattern --value=red
disp-can-client --node=can0 --command=set-pattern --value=green
disp-can-client --node=can0 --command=set-pattern --value=blue
disp-can-client --node=can0 --command=set-pattern --value=colorbar
disp-can-client --node=can0 --command=set-pattern --value=home

# With verbose output (shows CAN frames)
disp-can-client --node=can0 --command=set-pattern --value=red --verbose

# With debug output (shows detailed execution)
disp-can-client --node=can0 --command=set-pattern --value=red --debug
```

### Hardware Part Number Examples

```bash
# Get current part number
disp-can-client --node=can0 --command=get-hwpartnum

# Set part number
disp-can-client --node=can0 --command=set-hwpartnum --value="ABC123DEF456"
disp-can-client --node=can0 --command=set-hwpartnum --value="TESTPART123"

# With verbose output (shows ISO-TP frames)
disp-can-client --node=can0 --command=get-hwpartnum --verbose

# With custom timeout
disp-can-client --node=can0 --timeout=5 --command=get-hwpartnum
```

### Output Examples

#### Minimal Output (Default)
```bash
$ disp-can-client --node=can0 --command=set-pattern --value=red
OK

$ disp-can-client --node=can0 --command=get-hwpartnum
ABC123DEF456
```

#### Verbose Output
```bash
$ disp-can-client --node=can0 --command=get-hwpartnum --verbose
TX: [703] 03 22 FD BD 00 00 00 00
RX: [70B] 10 13 62 FD BD 41 42 43
TX: [703] 30 00 00 00 00 00 00 00
RX: [70B] 21 31 32 33 44 45 46 34
RX: [70B] 22 35 36 00 00 00 00 00
ABC123DEF456
```

#### Debug Output
```bash
$ disp-can-client --node=can0 --command=set-pattern --value=red --debug
[DEBUG] Configuration:
[DEBUG]   Node: can0
[DEBUG]   Command: set-pattern
[DEBUG]   Value: red
[DEBUG]   Timeout: 2
[DEBUG] Opening CAN socket on can0
[DEBUG] CAN socket opened successfully
[DEBUG] Sending set-pattern command: red
TX: [703] 04 2E FD 38 03 00 00 00
RX: [70B] 03 6E FD 38 11 00 00 00
[DEBUG] Received positive response
[DEBUG] CAN socket closed
OK
```

### Error Handling

The client provides specific exit codes for automation:

| Exit Code | Description |
|-----------|-------------|
| 0 | Success |
| 1 | Timeout |
| 2 | Invalid command/usage |
| 3 | CAN interface error |
| 4 | Invalid response/protocol error |

---

## Launcher Integration

### Overview

The daemon can integrate with a launcher service to automatically manage the pattern-generator backend.

### Configuration

Enable launcher integration by providing the launcher backend address:

```bash
disp-can-ctrl --node=can0 --launcher_backend=127.0.0.1:8081
```

### Launcher Protocol

| Command | Response | Description |
|---------|----------|-------------|
| `get-running-app` | `none`/`pattern-generator`/`gallery`/etc | Get currently running application |
| `start-app pattern-generator` | `OK` | Start pattern-generator application |
| `stop-app` | `OK` | Stop current application and return to launcher home |

### Behavior with Launcher

- **Pattern Commands**: Automatically starts pattern-generator if not running
- **Home Command**: Stops pattern-generator and returns to launcher home screen
- **Fallback**: If launcher unreachable, connects directly to pattern-backend
- **Startup Delay**: 200ms wait after starting pattern-generator

### Examples

```bash
# Test launcher integration
echo "get-running-app" | nc 127.0.0.1 8081
echo "start-app pattern-generator" | nc 127.0.0.1 8081
echo "stop-app" | nc 127.0.0.1 8081

# Daemon with launcher
disp-can-ctrl --node=can0 --launcher_backend=127.0.0.1:8081 --debugprint=true
```

---

## Troubleshooting

### Common Issues

#### CAN Interface Problems
```bash
# Check CAN interface status
ip link show can0
ip -details link show can0

# Bring up CAN interface
sudo ip link set can0 up type can bitrate 500000

# Enable loopback for testing
sudo ip link set can0 up type can bitrate 500000 loopback on

# Check for CAN traffic
candump can0
```

#### Network Connectivity
```bash
# Test TCP connection
telnet 127.0.0.1 8082
nc -v 127.0.0.1 8082

# Test pattern backend
echo "pattern red" | nc -q 0 127.0.0.1 8080

# Test launcher backend
echo "get-running-app" | nc -q 0 127.0.0.1 8081
```

#### Permission Issues
```bash
# CAN interface access (may need root)
sudo disp-can-ctrl --node=can0

# Check user groups
groups $USER
# Add user to dialout group if needed
sudo usermod -a -G dialout $USER
```

### Debug Mode

Enable comprehensive debugging:

```bash
# Daemon debug mode
disp-can-ctrl --node=can0 --debugprint=true

# Client debug mode  
disp-can-client --node=can0 --command=get-hwpartnum --debug

# Monitor all CAN traffic
candump can0 | tee can_debug.log
```

### Log Files

Check system logs for daemon activity:

```bash
# Daemon log (if configured)
tail -f /var/log/disp-can-ctrl.log

# System log
dmesg | grep -i can
journalctl -u disp-can-ctrl
```

---

## Build and Installation

### Requirements

- **Host System**: Linux with CMake or Make
- **Target System**: Embedded Linux (Buildroot-based)
- **Dependencies**: pthread, linux/can.h, arpa/inet.h
- **CAN Interface**: SocketCAN support

### Buildroot Integration

```bash
# Enable in Buildroot configuration
make menuconfig
# → Package Selection → Custom packages → disp-can-ctrl

# Build
make disp-can-ctrl

# Clean rebuild
make disp-can-ctrl-dirclean
make disp-can-ctrl
```

### Manual Build

```bash
# Configure and build
cmake -H. -Bbuild
cmake --build build

# Install
sudo cmake --build build --target install

# Or using Make
make all
sudo make install
```

---

## Technical Specifications

### Performance

- **CAN Bitrate**: 500 kbps (configurable)
- **TCP Connections**: Multiple concurrent connections supported
- **Response Time**: < 10ms for single-frame commands
- **ISO-TP Timeout**: Configurable (default 2 seconds)
- **Memory Usage**: < 2MB RSS

### Compatibility

- **CAN Standards**: ISO 11898 (CAN 2.0A/2.0B)
- **ISO-TP**: ISO 14229-2 (multi-frame messaging)
- **Network**: IPv4 TCP/IP
- **Platforms**: Linux (ARM, x86, x64)
- **Byte Order**: Network byte order for CAN IDs

### Security Considerations

- **Network Binding**: Binds to all interfaces (0.0.0.0) by default
- **Authentication**: None (suitable for closed networks)
- **Validation**: Input validation on all commands
- **Privilege**: Requires CAP_NET_RAW for CAN access

---

## Support

For technical support and questions:

- **Documentation**: This README.md
- **Debug Mode**: Use `--debugprint=true` for detailed logging
- **CAN Analysis**: Use `candump` and `cansend` for low-level debugging
- **Network Testing**: Use `nc` (netcat) for TCP testing

---

**Version**: 1.0.0  
**Build Date**: 2025  
**Architecture**: Multi-threaded CAN/TCP bridge with launcher integration
