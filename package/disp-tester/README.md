# Qt Pattern Generator(QT-Pattern Backend)

A Qt5-based display pattern generator application designed for testing displays color accuracy, and display uniformity. Features touch navigation and TCP network interface for automated testing integration.
![Sample-Pattern.](images/sample-pattern.png "Sample-Pattern.")

## Features

- **15 Test Patterns**: Comprehensive pattern library for display testing
- **Touch Navigation**: Intuitive touch interface with forward/backward navigation  
- **Network Control**: TCP socket interface for automated testing
- **Auto-scaling**: Adapts to any display resolution
- **Fullscreen Mode**: Clean, distraction-free pattern display
- **Embedded Optimized**: Designed for embedded Linux systems

## Supported Patterns

### **Basic Test Patterns**
- **grayscale-ramp**: 16-step grayscale gradient for brightness calibration
- **ansi-checker**: Black/white checkerboard for uniformity testing  
- **colorbar**: SMPTE color bars for color accuracy testing

### **Solid Color Patterns**
- **white**: Full white screen (255,255,255)
- **black**: Full black screen (0,0,0)
- **red**: Full red screen (255,0,0)
- **green**: Full green screen (0,255,0)
- **blue**: Full blue screen (0,0,255)
- **cyan**: Full cyan screen (0,255,255)
- **magenta**: Full magenta screen (255,0,255)
- **yellow**: Full yellow screen (255,255,0)

### **Advanced Display Testing**
- **zone-boundary-grid**: Grid pattern for local dimming zone mapping (1008 zones, configurable)
- **blooming-detection**: Single pulsing pixel for blooming measurement
- **cross-dimming**: 4 corner spots with pulsing animation for zone interference testing

### **Custom Patterns**
- **RGB patches**: Custom colors with `pattern rgb R G B` (values 0-255)

## Installation

### Dependencies
- **Qt5**: qt5base, qt5declarative, qt5quickcontrols2
- **Platform**: Linux with framebuffer support (linuxfb)
- **Touch**: Standard Linux input events (`/dev/input/eventX`)

### Build Methods

#### **Option 1: Buildroot Package**
The application can be integrated as a Buildroot package for embedded systems.

#### **Option 2: Standalone Build**
```bash
cd src/
qmake
make
./disp-tester --port=8080
```

For cross-compilation, ensure Qt5 development tools and target toolchain are configured.

## Usage

### Command Line Options
```bash
qt-pattern-generator [options]

Options:
  -p, --port <port>     TCP server port (default: 8080)
  -h, --help            Display help
  -v, --version         Display version
```

### Touch Navigation
- **Left edge tap** (25%): Previous pattern
- **Right edge tap** (25%): Next pattern  
- **Center tap** (50%): Toggle UI overlay
- **Swipe left**: Next pattern
- **Swipe right**: Previous pattern

### UI Elements
- **Pattern counter**: Shows current position (e.g., "5/15")
- **Pattern name**: Displays current pattern (e.g., "RED")
- **EXIT button**: Always available in top-right corner
- **Resolution info**: Display dimensions (e.g., "2560x1440")
- **Network info**: TCP server status (e.g., "TCP:192.168.1.100:8080")
- **Auto-hide**: UI disappears after 4 seconds, tap center to toggle

## Network API

### Connection
```bash
# Telnet (interactive)
telnet <display-ip> 8080

# Netcat (scripting) 
echo "pattern red" | nc -q 1 <display-ip> 8080
```

### Pattern Commands
```bash
# Basic patterns
pattern grayscale-ramp      # 16-step grayscale gradient
pattern ansi-checker        # Black/white checkerboard
pattern colorbar            # SMPTE color bars

# Solid colors
pattern white               # Full white screen
pattern black               # Full black screen  
pattern red                 # Full red screen
pattern green               # Full green screen
pattern blue                # Full blue screen
pattern cyan                # Full cyan screen
pattern magenta             # Full magenta screen
pattern yellow              # Full yellow screen

# Advanced patterns
pattern zone-boundary-grid  # Local dimming zone grid
pattern blooming-detection  # Single pixel blooming test
pattern cross-dimming       # Zone interference test

# Custom RGB
pattern rgb 255 128 64      # Custom color (R G B values 0-255)

# Text overlay
text <message>              # Display custom text overlay
text-clear                  # Remove text overlay
```

### Information Commands
```bash
get-resolution              # Returns display resolution
get-pattern                 # Returns current pattern name  
list-patterns              # Returns all available patterns
quit                       # Exit application
```

### Response Format
```bash
OK                         # Command successful
ERROR: <description>       # Command failed
<data>                     # Information response
```

## Text Overlay Feature

### Overview
The application supports displaying custom text messages in a semi-transparent overlay box positioned at the bottom-right corner of the screen. This feature is useful for:
- **Test identification**: Label different test sequences
- **Measurement notes**: Display current test parameters
- **Status information**: Show testing progress or conditions
- **Documentation**: Add context to recorded test results

### Text Overlay Commands
```bash
# Display custom text
text "Test Sequence 1: Color Accuracy"
text "Brightness: 100% | Contrast: 85%"
text "Zone Test: Local Dimming Validation"

# Multi-line text (use \n for line breaks)
text "Test Parameters:\nBrightness: 400 nits\nContrast: 1000:1"

# Clear the text overlay
text-clear
```

### Text Overlay Behavior
- **Position**: Bottom-right corner with 20px margins
- **Background**: Semi-transparent black (80% opacity)
- **Text color**: White for maximum contrast
- **Font size**: 16px, readable at typical viewing distances
- **Auto-sizing**: Box automatically adjusts to text content
- **Persistence**: Text remains visible across pattern changes
- **UI interaction**: Text overlay is independent of main UI visibility

### Text Overlay Examples

#### Test Sequence Labeling
```bash
#!/bin/bash
HOST="192.168.1.100"
PORT="8080"

# Label and execute color accuracy test
echo 'text "Color Accuracy Test - Sequence 1"' | nc -q 1 $HOST $PORT
echo "pattern red" | nc -q 1 $HOST $PORT
sleep 3

echo 'text "Color Accuracy Test - Sequence 2"' | nc -q 1 $HOST $PORT  
echo "pattern green" | nc -q 1 $HOST $PORT
sleep 3

echo 'text "Color Accuracy Test - Sequence 3"' | nc -q 1 $HOST $PORT
echo "pattern blue" | nc -q 1 $HOST $PORT
sleep 3

# Clear text and finish
echo "text-clear" | nc -q 1 $HOST $PORT
echo "quit" | nc -q 1 $HOST $PORT
```

#### Measurement Parameter Display
```bash
# Display current test conditions
echo 'text "Ambient: 2 lux\nViewing Angle: 0°\nDistance: 60cm"' | nc -q 1 $HOST $PORT
echo "pattern white" | nc -q 1 $HOST $PORT

# Update parameters during test
echo 'text "Ambient: 200 lux\nViewing Angle: 30°\nDistance: 60cm"' | nc -q 1 $HOST $PORT
echo "pattern zone-boundary-grid" | nc -q 1 $HOST $PORT
```

#### Zone Testing Documentation
```bash
# Document zone-specific tests
echo 'text "Zone Test: Corners\nLED Array: 42x24\nTarget Zones: 0,41,966,1007"' | nc -q 1 $HOST $PORT
echo "pattern cross-dimming" | nc -q 1 $HOST $PORT

echo 'text "Blooming Test: Center\nPixel Position: 1280,720\nExpected: <2% spillover"' | nc -q 1 $HOST $PORT
echo "pattern blooming-detection" | nc -q 1 $HOST $PORT
```

### Text Formatting Guidelines
- **Keep text concise**: Overlay should not obscure test patterns
- **Use line breaks**: Separate different types of information with `\n`
- **Avoid special characters**: Stick to alphanumeric, spaces, and basic punctuation
- **Consider contrast**: Text is white on semi-transparent black background
- **Length limits**: While not enforced, keep messages under 200 characters for readability

### Integration with Test Automation
The text overlay feature integrates seamlessly with automated testing workflows:

```python
import socket
import time

def send_command(host, port, command):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))
    sock.send(f"{command}\n".encode())
    response = sock.recv(1024).decode().strip()
    sock.close()
    return response

def run_color_test(host, port):
    colors = ['red', 'green', 'blue', 'cyan', 'magenta', 'yellow']
    
    for i, color in enumerate(colors):
        # Update progress text
        progress_text = f"Color Test Progress: {i+1}/{len(colors)}\\nCurrent: {color.upper()}\\nRemaining: {len(colors)-i-1}"
        send_command(host, port, f'text "{progress_text}"')
        
        # Display pattern
        send_command(host, port, f'pattern {color}')
        
        # Simulate measurement time
        time.sleep(2)
    
    # Clear text when done
    send_command(host, port, 'text-clear')

# Usage
run_color_test('192.168.1.100', 8080)
```

## Pattern Details

### Zone Boundary Grid
- **Default**: 42×24 grid (1008 zones)
- **Zone size**: 60×60 pixels on 2560×1440 displays
- **Alignment**: Centered grid with perfect LED alignment
- **Configurable**: Grid dimensions can be modified for different display types
- **Zone numbering**: 0-1007, matches physical LED array indexing

### Blooming Detection
- **Single pixel**: 2×2 white pixel in display center
- **Pulsing animation**: Opacity varies between 70%-100% for visibility
- **Crosshairs**: Dim positioning guides for precise measurement
- **Purpose**: Detect light bleeding around bright pixels

### Cross Dimming
- **Four spots**: 60×60 white circles in display corners  
- **Pulsing behavior**: Each spot pulses at staggered intervals
- **Animation timing**: 1500ms, 1700ms, 1900ms, 2100ms cycles
- **Purpose**: Test local dimming zone interference and interaction

## Network Integration Examples

### Basic Pattern Testing
```bash
#!/bin/bash
HOST="192.168.1.100"
PORT="8080"

# Test primary colors
for color in red green blue; do
    echo "Testing $color..."
    echo "pattern $color" | nc -q 1 $HOST $PORT
    sleep 2
done

echo "quit" | nc -q 1 $HOST $PORT
```

### Display Information
```bash
# Get display specs
RESOLUTION=$(echo "get-resolution" | nc -q 1 192.168.1.100 8080)
PATTERNS=$(echo "list-patterns" | nc -q 1 192.168.1.100 8080)

echo "Display: $RESOLUTION"  
echo "Available: $PATTERNS"
```

### Automated Testing Session
```bash
# Multi-command session
{
    echo "pattern white"
    echo "pattern black"  
    echo "pattern zone-boundary-grid"
    echo "get-pattern"
    echo "quit"
} | nc 192.168.1.100 8080
```

## Technical Architecture

### Application Structure
- **Qt5 + QML**: Modern UI framework with hardware acceleration
- **C++ Backend**: Pattern management and network interface
- **POSIX Sockets**: Dependency-free TCP server implementation
- **Touch Input**: Standard Linux input event handling
- **Framebuffer**: Direct rendering via linuxfb platform

### Pattern System
- **Modular Design**: Individual QML files for each pattern
- **Auto-scaling**: Patterns adapt to any display resolution
- **Resource Efficient**: Optimized for embedded systems
- **Clean Display**: UI elements hidden during pattern display

### Network Protocol
- **Text-based**: Simple command/response format
- **Single client**: One connection at a time
- **Line-terminated**: Commands end with newline
- **Immediate feedback**: Instant OK/ERROR responses

## Development

### Source Structure
```
src/
├── main.cpp                # Application entry point
├── PatternController.cpp/h # Pattern management and network
├── NetworkInterface.cpp/h  # TCP server implementation  
├── main.qml               # Main UI and navigation
├── patterns/              # Pattern QML files
└── qml.qrc               # Resource compilation
```

### Build Configuration
- **qmake project**: Standard Qt5 build system
- **Cross-compilation**: ARM toolchain supported
- **Resource embedding**: All QML files compiled into binary
- **Platform**: Targets linuxfb for embedded deployment

### Customization
- **Add patterns**: Create new QML files in `patterns/` directory
- **Modify grid**: Adjust zone dimensions in `ZoneBoundaryGrid.qml`  
- **Network port**: Configurable via command line option
- **UI styling**: Modify overlay appearance in `main.qml`

## Platform Support

### Tested Configurations
- **Raspberry Pi 4**: HDMI output, capacitive touch input
- **Custom embedded**: ARM-based systems with Qt5 support
- **Display interfaces**: HDMI, DSI, and framebuffer-compatible outputs
- **Input devices**: Touch screens via `/dev/input/event*`

### Requirements
- **RAM**: Minimum 512MB, recommended 1GB+
- **Storage**: ~10MB application + Qt5 runtime libraries
- **Display**: Any resolution, auto-scaling support
- **Network**: Ethernet or WiFi for remote control (optional)

---

**Qt Pattern Generator** - Professional display testing made simple.
