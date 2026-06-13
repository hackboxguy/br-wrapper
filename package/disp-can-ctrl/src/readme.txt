# disp-can-ctrl Daemon Interface Documentation

## Overview

The `disp-can-ctrl` daemon provides a bridge between CAN bus and TCP interfaces for display pattern control and hardware part number management. It supports both single-frame and multi-frame ISO-TP CAN messaging.

**Default Configuration:**
- **TCP Port:** 8085
- **CAN Interface:** can0
- **CAN Request ID:** 0x703
- **CAN Response ID:** 0x70B
- **Pattern Backend:** 127.0.0.1:8080

---

## TCP Interface (Port 8085)

### Pattern Control Commands

Control display patterns through TCP socket commands:

```bash
# Set display patterns
echo "pattern red" | nc -q 0 127.0.0.1 8085           # Response: "OK"
echo "pattern green" | nc -q 0 127.0.0.1 8085         # Response: "OK"
echo "pattern blue" | nc -q 0 127.0.0.1 8085          # Response: "OK"
echo "pattern colorbar" | nc -q 0 127.0.0.1 8085      # Response: "OK"
echo "pattern home" | nc -q 0 127.0.0.1 8085          # Response: "OK"
```

### Query Commands

Retrieve current system state:

```bash
# Get current pattern
echo "get-pattern" | nc -q 0 127.0.0.1 8085           # Response: "red"/"green"/"blue"/"colorbar"/"home"/"none"

# Get hardware part number
echo "get-hwpartnum" | nc -q 0 127.0.0.1 8085         # Response: "0000000000000000" (default) or configured value
```

### Configuration Commands

Set system parameters:

```bash
# Set hardware part number (max 16 ASCII characters)
echo "set-hwpartnum ABC123DEF456" | nc -q 0 127.0.0.1 8085    # Response: "OK"
echo "set-hwpartnum TEST12345678" | nc -q 0 127.0.0.1 8085    # Response: "OK"
echo "set-hwpartnum SHORT" | nc -q 0 127.0.0.1 8085          # Response: "OK"
```

### Error Responses

```bash
# Invalid commands return "Error"
echo "invalid-command" | nc -q 0 127.0.0.1 8085      # Response: "Error"
echo "pattern invalid" | nc -q 0 127.0.0.1 8085      # Response: "Error"
echo "set-hwpartnum TOOLONGMORETHAN16CHARS" | nc -q 0 127.0.0.1 8085  # Response: "Error"

# Communication errors (pattern backend unreachable) return "Comm-Error"
```

### Remote Access

Access daemon from other machines on the network:

```bash
# Replace 192.168.1.89 with your device's IP address
echo "pattern red" | nc -q 0 192.168.1.89 8085
echo "get-pattern" | nc -q 0 192.168.1.89 8085
```

---

## CAN Interface (0x703 → 0x70B)

### Single-Frame Pattern Commands

#### Red Pattern
```bash
cansend can0 703#042EFD3803000000
```
- **Request:** `[703] 04 2E FD 38 03 00 00 00`
- **Response:** `[70B] 03 6E FD 38 11 00 00 00`

#### Green Pattern
```bash
cansend can0 703#042EFD3804000000
```
- **Request:** `[703] 04 2E FD 38 04 00 00 00`
- **Response:** `[70B] 03 6E FD 38 11 00 00 00`

#### Blue Pattern
```bash
cansend can0 703#042EFD3805000000
```
- **Request:** `[703] 04 2E FD 38 05 00 00 00`
- **Response:** `[70B] 03 6E FD 38 11 00 00 00`

#### Colorbar Pattern
```bash
cansend can0 703#042EFD3806000000
```
- **Request:** `[703] 04 2E FD 38 06 00 00 00`
- **Response:** `[70B] 03 6E FD 38 11 00 00 00`

#### HMI HomeScreen
```bash
cansend can0 703#042EFDC001000000
```
- **Request:** `[703] 04 2E FD C0 01 00 00 00`
- **Response:** `[70B] 03 6E FD C0 01 00 00 00`

### Multi-Frame ISO-TP Commands

#### Get Hardware Part Number

**Request (Single Frame):**
```bash
cansend can0 703#0322FDBD00000000
```

**Response (Multi-Frame Sequence):**
```
[70B] 10 13 62 FD BD PN PN PN    # First frame: length=0x13, service=0x62, first 3 bytes
[703] 30 00 00 00 00 00 00 00    # Flow control: Continue (sent automatically by daemon)
[70B] 21 PN PN PN PN PN PN PN    # Consecutive frame 1: next 7 bytes
[70B] 22 PN PN PN PN PN PN 00    # Consecutive frame 2: remaining bytes + padding
```

Where `PN` represents part number bytes.

#### Set Hardware Part Number

**Example: Setting "TESTPART12345678"**

**Multi-Frame Request Sequence:**
```bash
cansend can0 703#10132EFDBD544553        # First: length=0x13, service=0x2E, "TES"
# Daemon responds with flow control: [70B] 30 00 00 00 00 00 00 00
cansend can0 703#21545041525431323334     # Consecutive 1: "TPART1234"
cansend can0 703#22353637380000000000     # Consecutive 2: "5678" + padding
```

**Success Response:**
```
[70B] 03 6E FD BD 00 00 00 00    # Positive response
```

---

## Command Reference Table

| **Function** | **TCP Command** | **CAN Request** | **CAN Response** |
|--------------|-----------------|-----------------|------------------|
| Red Pattern | `pattern red` | `703#042EFD3803000000` | `70B#036EFD3811000000` |
| Green Pattern | `pattern green` | `703#042EFD3804000000` | `70B#036EFD3811000000` |
| Blue Pattern | `pattern blue` | `703#042EFD3805000000` | `70B#036EFD3811000000` |
| Colorbar | `pattern colorbar` | `703#042EFD3806000000` | `70B#036EFD3811000000` |
| Home Screen | `pattern home` | `703#042EFDC001000000` | `70B#036EFDC001000000` |
| Get Pattern | `get-pattern` | N/A | N/A |
| Get Part Number | `get-hwpartnum` | `703#0322FDBD00000000` | Multi-frame ISO-TP |
| Set Part Number | `set-hwpartnum XXX` | Multi-frame ISO-TP | `70B#036EFDBD00000000` |

---

## State Synchronization

- ✅ **CAN → TCP:** Pattern changes via CAN are reflected in `get-pattern` TCP responses
- ✅ **TCP → CAN:** Pattern changes via TCP update internal daemon state  
- ✅ **Persistent:** Hardware part number persists in memory until daemon restart
- ✅ **Bidirectional:** Both interfaces maintain synchronized state

---

## Configuration

### Command-Line Options

```bash
./disp-can-ctrl [options]

Options:
  --node=<canx>              CAN interface (default: can0)
  --port=<port>              TCP listen port (default: 8082)
  --pattern_backend=<ip:port> Pattern daemon address (default: 127.0.0.1:8080)
  --debugprint=<flag>        Enable debug output (default: false)
  --help                     Show help message
```

### Examples

```bash
# Default configuration
./disp-can-ctrl --node=can0

# Custom configuration
./disp-can-ctrl --node=can0 --port=8085 --pattern_backend=192.168.1.100:8080 --debugprint=true
```

### System Service

The daemon is managed via init script:

```bash
/etc/init.d/S99DispCanCtrl start    # Start daemon
/etc/init.d/S99DispCanCtrl stop     # Stop daemon  
/etc/init.d/S99DispCanCtrl restart  # Restart daemon
/etc/init.d/S99DispCanCtrl status   # Check status
```

---

## Technical Details

### ISO-TP Implementation
- **Single Frame:** Commands ≤ 7 bytes of data
- **Multi-Frame:** Commands > 7 bytes using First Frame (FF) + Consecutive Frames (CF)
- **Flow Control:** Standard 30 00 (Continue), 30 01 (Wait), 30 02 (Overflow)

### Hardware Part Number
- **Storage:** In-memory only (resets on daemon restart)
- **Format:** ASCII string, max 16 characters
- **Padding:** Shorter strings are null-terminated internally

### Error Handling
- **Network errors:** Return "Comm-Error" when pattern backend unreachable
- **Invalid commands:** Return "Error" for malformed requests
- **CAN errors:** No response sent for invalid CAN frames

### Dependencies
- **Pattern Backend:** Requires pattern daemon on configured IP:port
- **CAN Interface:** Requires configured CAN interface (can0, can1, etc.)
- **Network:** TCP socket support for remote access

---

## Troubleshooting

### Common Issues

1. **"Comm-Error" responses:** Check pattern backend daemon status and connectivity
2. **No CAN responses:** Verify CAN interface is UP and properly configured
3. **TCP connection refused:** Check if daemon is running and port is correct
4. **Permission denied:** May need root privileges for CAN interface access

### Debug Mode

Enable debug output to troubleshoot issues:

```bash
./disp-can-ctrl --node=can0 --debugprint=true
```

### Log Files

Check daemon logs for detailed error information:

```bash
cat /var/log/disp-can-ctrl.log
```
