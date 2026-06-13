# qt-cluster-demo: Refined Implementation Plan

## Overview

A Qt5/QML application that renders an automotive instrument cluster. It reads vehicle data from a CAN bus interface using standard OBD2 Mode 01 polling and renders speedometer, tachometer, fuel gauge, temperature gauge, and telltale indicators in real-time.

Launched from `qt-demo-launcher` via button press. Uses `QT_QPA_PLATFORM=eglfs` (GPU-accelerated) while the launcher uses `linuxfb` — no display conflict. The launcher manages the process lifecycle via `QProcess` (hides on app start, reappears on app exit).

## Target hardware

- Raspberry Pi 4
- Display: detected at runtime via `QScreen::geometry()` (designed for 1920x720 wide automotive format, scales to any resolution)
- CAN interface: socketcan (`can0` or `vcan0` for development)
- CAN data source: `car-can-emulator` (repo: `/home/testpc/git-repos/claude/2026/car-can-emulator`) or real OBD2 adapter

## Architecture

```
can0/vcan0 → CanReader (C++, QThread, socketcan, OBD2 polling)
           → ClusterModel (C++ QObject, Q_PROPERTY, raw values — no C++ smoothing)
           → QML Engine (eglfs, OpenGL ES)
               ├── main.qml (root layout, fills screen geometry)
               ├── Speedometer.qml (large dial, right)
               ├── Tachometer.qml (large dial, left)
               ├── FuelGauge.qml (small gauge, center-top)
               ├── TempGauge.qml (small gauge, center-bottom)
               ├── TelltaleRow.qml (indicator icons, top bar)
               ├── InfoBar.qml (gear/odo, bottom bar)
               ├── ExitButton.qml (touch-activated overlay)
               └── NeedleGauge.qml (reusable base component)
```

**Key design decisions:**
- **No C++ smoothing** — raw values passed to QML; `SmoothedAnimation` handles all visual needle damping
- **QThread + signals/slots** for CanReader — clean Qt event loop integration, no manual thread-safety needed
- **CMake only** — no `.pro` files (matches newer packages in repo)
- **Resolution-independent** — layout reads `Screen.width`/`Screen.height`, gauge sizes expressed as proportions

## Display layout

```
┌──────────────────────────────────────────────────────────────┐
│ [ENG] [OIL] [BATT] [BRAKE] [⟵] [⟶] [HIGH] [DOOR] [BELT]   │ ← telltales (top bar)
├────────────────┬──────────────┬───────────────────────────────┤
│                │    FUEL      │                               │
│  TACHOMETER    │   ┌─────┐   │   SPEEDOMETER                 │
│    0-8000      │   │/////│   │     0-260                     │
│     RPM        │   │/////│   │     km/h                      │
│                │   └─────┘   │                               │
│   (large       │    TEMP     │   (large                      │
│    dial)       │   ┌─────┐   │    dial)                      │
│                │   │/////│   │                               │
│                │   └─────┘   │                               │
├────────────────┴──────────────┴───────────────────────────────┤
│              Gear: [N] 1 2 3 4 5    ODO: 12345 km            │ ← info bar (bottom)
└──────────────────────────────────────────────────────────────┘
```

Gauge sizing (proportional to screen dimensions):
- **Tachometer**: left side, diameter ~70% of screen height, 0-8000 RPM, redline at 6500+
- **Speedometer**: right side, diameter ~70% of screen height, 0-260 km/h
- **Fuel gauge**: center-top, small arc, 0-100%
- **Temp gauge**: center-bottom, small arc, 60-130 C with danger zone at 110+
- **Telltale row**: top strip, ~6% of screen height, icon indicators
- **Info bar**: bottom strip, ~6% of screen height, gear indicator and odometer

## CAN bus communication

### OBD2 polling (standard Mode 01)

CanReader polls PIDs by sending requests on `0x7DF` and reading responses from `0x7E8`:

```
Request:  ID=0x7DF  DLC=8  [02 01 PID 00 00 00 00 00]
Response: ID=0x7E8  DLC=8  [len 41 PID data...]
```

**Polling schedule** (round-robin, ~50ms between requests):

| PID    | Parameter       | Response decode                           | Poll rate         |
|--------|-----------------|-------------------------------------------|-------------------|
| `0x0C` | RPM             | `(data[3]<<8 | data[4]) / 4`             | Every cycle (~300ms) |
| `0x0D` | Speed           | `data[3]` km/h                            | Every cycle       |
| `0x05` | Coolant temp    | `data[3] - 40` C                          | Every 2nd cycle   |
| `0x2F` | Fuel level      | `data[3] * 100 / 255` %                  | Every 5th cycle   |
| `0x42` | Battery voltage | `(data[3]<<8 | data[4]) / 1000.0` V      | Every 5th cycle   |

**CAN socket configuration:**
- `SO_RCVTIMEO` set to 100ms — prevents blocking forever if CAN goes silent
- If no response received for 3 consecutive poll cycles, emit `canTimeout()` signal
- ClusterModel can use this to show a "CAN disconnected" indicator or fall back to demo mode

### Telltale CAN message (passive listener)

Listen for CAN ID `0x420` (broadcast by car-can-emulator every 200ms):

```
Byte 0 bits (LSB first): [engine|oil|battery|brake|left_turn|right_turn|highbeam|door]
  bit 0: Check Engine
  bit 1: Oil Pressure
  bit 2: Battery
  bit 3: Brake
  bit 4: Left Turn
  bit 5: Right Turn
  bit 6: High Beam
  bit 7: Door Ajar

Byte 1 bits (LSB first): [seatbelt|abs|traction|tpms|rsvd|rsvd|rsvd|rsvd]
  bit 0 (8):  Seatbelt
  bit 1 (9):  ABS
  bit 2 (10): Traction Control
  bit 3 (11): TPMS
```

No polling needed — read passively alongside OBD2 responses in the same socket read loop (filter by CAN ID).

### Fallback / demo mode

If no CAN interface is available (or `--demo` flag):
- Skip CAN socket initialization
- `DemoSimulator` generates synthetic values via QTimer
- Drive cycle: idle -> accelerate -> cruise -> brake -> stop -> repeat (~60s cycle)
- Telltales cycle through demo sequence (turn signals, headlights, etc.)

## C++ classes

### CanReader (src/CanReader.h/.cpp)

```cpp
class CanReader : public QObject {
    Q_OBJECT
public:
    explicit CanReader(const QString &interface, QObject *parent = nullptr);
    ~CanReader();

    void start();  // Creates QThread internally, moves self to thread, begins poll loop
    void stop();   // Signals thread to stop, waits for finish

signals:
    void speedChanged(int kmh);           // 0-255
    void rpmChanged(int rpm);             // 0-16383
    void coolantTempChanged(int celsius);  // -40 to 215
    void fuelLevelChanged(int percent);    // 0-100
    void batteryVoltageChanged(double volts);
    void telltalesChanged(quint16 bits);   // 16-bit bitfield
    void canTimeout();                     // No response for 3 cycles

private:
    void pollLoop();           // Runs in worker thread
    void sendObd2Request(uint8_t pid);
    void processFrame(const struct can_frame &frame);
    void decodeObd2Response(const struct can_frame &frame);
    void decodeTelltales(const struct can_frame &frame);

    int m_socket;
    QString m_interface;
    QThread *m_thread;
    std::atomic<bool> m_running;
    int m_cycleCount;          // For poll rate scheduling
};
```

- Opens socketcan raw socket with `CAN_RAW` protocol
- Sets `SO_RCVTIMEO` to 100ms
- Poll loop: send request, read with timeout, process any frames (OBD2 or telltale)
- Round-robin PID scheduling based on `m_cycleCount`

### ClusterModel (src/ClusterModel.h/.cpp)

```cpp
class ClusterModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(int rpm READ rpm NOTIFY rpmChanged)
    Q_PROPERTY(int coolantTemp READ coolantTemp NOTIFY coolantTempChanged)
    Q_PROPERTY(int fuelLevel READ fuelLevel NOTIFY fuelLevelChanged)
    Q_PROPERTY(double batteryVoltage READ batteryVoltage NOTIFY batteryVoltageChanged)
    Q_PROPERTY(quint16 telltales READ telltales NOTIFY telltalesChanged)
    Q_PROPERTY(bool canConnected READ canConnected NOTIFY canConnectedChanged)

    // ... getters, signals, private members
};
```

- Exposed to QML via `setContextProperty("cluster", model)`
- **No smoothing** — stores and forwards raw values directly
- Connects to either CanReader or DemoSimulator signals (same signal signatures)
- `canConnected` property: true when receiving CAN data, false on timeout

### DemoSimulator (src/DemoSimulator.h/.cpp)

```cpp
class DemoSimulator : public QObject {
    Q_OBJECT
public:
    explicit DemoSimulator(QObject *parent = nullptr);
    void start();
    void stop();

signals:
    // Same signal signatures as CanReader
    void speedChanged(int kmh);
    void rpmChanged(int rpm);
    void coolantTempChanged(int celsius);
    void fuelLevelChanged(int percent);
    void batteryVoltageChanged(double volts);
    void telltalesChanged(quint16 bits);

private:
    void tick();               // Called every 50ms by m_timer
    QTimer *m_timer;
    int m_phase;               // Current drive cycle phase
    int m_phaseElapsed;        // ms elapsed in current phase
};
```

Drive cycle phases (matching car-can-emulator simulate mode):

| Phase          | Duration | Speed     | RPM        | Temp      | Telltales           |
|----------------|----------|-----------|------------|-----------|---------------------|
| Cold start     | 3s       | 0         | 800        | 20->40 C  | Seatbelt ON         |
| Warmup idle    | 5s       | 0         | 800        | 40->70 C  | Seatbelt OFF        |
| Accelerate 1   | 3s       | 0->30     | 800->3500  | 70->75 C  | Left turn ON        |
| Accelerate 2   | 3s       | 30->60    | 2000->3500 | 75->80 C  | Left turn OFF       |
| Accelerate 3   | 4s       | 60->100   | 2000->3500 | 80->85 C  | —                   |
| Cruise         | 15s      | 100       | 2200       | 85->90 C  | High beam ON        |
| Accelerate 4   | 4s       | 100->140  | 2200->4000 | 90 C      | High beam OFF       |
| High cruise    | 10s      | 140       | 3000       | 90 C      | —                   |
| Decelerate     | 5s       | 140->60   | 3000->1500 | 90->88 C  | Brake ON            |
| Coast          | 4s       | 60->30    | 1500->1000 | 88->86 C  | Right turn ON       |
| Stop           | 4s       | 30->0     | 1000->800  | 86->85 C  | Right turn OFF      |
| Idle at stop   | 5s       | 0         | 800        | 85->83 C  | Brake OFF           |

- 50ms tick interval with linear interpolation between phase start/end values
- RPM jitter: +/-20 for realism
- Fuel: starts at 75%, decreases to ~60% over cycle, resets on loop

### main.cpp

```cpp
int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    // Parse CLI args
    QCommandLineParser parser;
    parser.addOption({"can", "CAN interface", "can0"});
    parser.addOption({"demo", "Demo mode"});
    parser.addOption({"fullscreen", "Force fullscreen"});
    parser.process(app);

    ClusterModel model;

    bool demoMode = parser.isSet("demo");
    CanReader *canReader = nullptr;
    DemoSimulator *demoSim = nullptr;

    if (!demoMode) {
        canReader = new CanReader(parser.value("can"));
        // Connect CanReader signals to ClusterModel slots
        // If CAN open fails, fall back to demo mode
    }

    if (demoMode || !canReader) {
        demoSim = new DemoSimulator();
        // Connect DemoSimulator signals to ClusterModel slots
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("cluster", &model);
    engine.load(QUrl("qrc:/qml/main.qml"));

    // Start data source
    if (canReader) canReader->start();
    if (demoSim) demoSim->start();

    int ret = app.exec();

    // Cleanup
    if (canReader) canReader->stop();
    if (demoSim) demoSim->stop();
    return ret;
}
```

## QML components

### NeedleGauge.qml (reusable base)

Properties:
- `value`, `minValue`, `maxValue` — data range
- `startAngle`, `endAngle` — arc sweep in degrees (e.g., -135 to 135 for 270-degree sweep)
- `redlineValue` — value above which the arc is drawn red (optional, -1 to disable)
- `label` — unit text below center (e.g., "km/h")
- `majorTickInterval`, `minorTickInterval` — tick spacing in value units
- `labelDivisor` — divide label values by this (e.g., 1000 for tachometer labels "0, 1, 2...")

Drawing (Canvas-based, replaceable with Image assets later):
- Dark dial arc background
- Minor tick marks (thin, short lines)
- Major tick marks (thicker, longer lines) with numeric labels
- Redline arc segment (red) from `redlineValue` to `maxValue`
- **Needle**: rotated `Rectangle` (not Canvas), pivot at center
- Center cap: filled `Circle` (Rectangle with radius)

**Needle animation** (QML SmoothedAnimation — sole source of visual smoothing):
```qml
property real targetRotation: /* computed from value */

Behavior on targetRotation {
    SmoothedAnimation {
        velocity: 800  // degrees per second — scales naturally with change magnitude
    }
}
```

Velocity-based (not duration-based) so small changes are quick and large sweeps are proportionally smooth.

**Asset swap path** (future, no code changes needed):
- Replace Canvas dial face with `Image { source: "dial-face.png" }`
- Replace needle Rectangle with `Image { source: "needle.png"; transformOrigin: Item.Bottom }`
- Rotation binding stays identical

### Speedometer.qml

- Uses NeedleGauge: min=0, max=260, startAngle=-135, endAngle=135
- Major ticks every 20 km/h, minor every 10
- Labels: 0, 20, 40, ... 260
- `label: "km/h"`
- Digital readout: large `Text` element showing current speed number below center

### Tachometer.qml

- Uses NeedleGauge: min=0, max=8000, startAngle=-135, endAngle=135
- Major ticks every 1000 RPM, minor every 500
- Labels: 0, 1, 2, ... 8 (labelDivisor: 1000)
- Redline zone: 6500-8000 (red arc)
- `label: "RPM x1000"`

### FuelGauge.qml

- Small arc gauge (~120 degree sweep)
- Range: E (empty, 0%) to F (full, 100%)
- Warning color when < 15% (arc turns amber/red)
- Fuel pump icon or "E"/"F" labels at ends
- Property: `level` bound to `cluster.fuelLevel`

### TempGauge.qml

- Small arc gauge (~120 degree sweep)
- Range: 60 C to 130 C
- Normal zone: 70-100 C (white/blue ticks)
- Warning zone: >110 C (red ticks and arc)
- "C"/"H" labels at ends
- Property: `temperature` bound to `cluster.coolantTemp`

### TelltaleRow.qml

Horizontal row of indicator icons across the top bar.

Each telltale is a simple colored shape (circle/rectangle with text or unicode symbol) that:
- Shows when its bit is set in `cluster.telltales`
- Hides (opacity 0) when its bit is clear
- Color per indicator type (see table below)

**Turn signal blink (Option B — QML-driven):**
- When the turn signal bit transitions from OFF to ON, start a QML `Timer` that toggles visibility at 500ms interval (1 Hz blink)
- When the bit transitions to OFF, stop timer and hide
- This gives consistent visual blink regardless of CAN poll timing or demo mode

Telltale definitions:

| Bit | Name             | Color  | Symbol/Text |
|-----|------------------|--------|-------------|
| 0   | Check Engine     | Amber  | ENG         |
| 1   | Oil Pressure     | Red    | OIL         |
| 2   | Battery          | Red    | BATT        |
| 3   | Brake            | Red    | BRAKE       |
| 4   | Left Turn        | Green  | <-- (blinks)|
| 5   | Right Turn       | Green  | --> (blinks)|
| 6   | High Beam        | Blue   | HIGH        |
| 7   | Door Ajar        | Amber  | DOOR        |
| 8   | Seatbelt         | Red    | BELT        |
| 9   | ABS              | Amber  | ABS         |
| 10  | Traction Control | Amber  | TC          |
| 11  | TPMS             | Amber  | TPMS        |

### InfoBar.qml

Bottom strip with:
- Odometer display (static placeholder for v1, or calculated from cumulative speed)
- Battery voltage readout (from `cluster.batteryVoltage`)

### ExitButton.qml

Touch-activated exit overlay:

- **Invisible `MouseArea`** covering the full screen, `z` above all gauge content
- On touch anywhere: show a semi-transparent dark "X" button in the top-right corner
- Auto-hide after 3 seconds via `Timer` if not tapped
- Tapping the "X" button calls `Qt.quit()`
- Visual: rounded rectangle with "X" text, semi-transparent black background (#80000000)
- Size: ~60x60px (scaled proportionally)

### main.qml

Root layout:
```qml
Window {
    id: root
    visible: true
    width: Screen.width    // Read from screen geometry at runtime
    height: Screen.height
    flags: Qt.FramelessWindowHint
    color: "black"

    // Layout proportional to screen size
    TelltaleRow {
        anchors.top: parent.top
        height: parent.height * 0.06
        width: parent.width
    }

    Tachometer {
        // Left side, vertically centered in gauge area
        width: parent.height * 0.70
        height: width
    }

    FuelGauge { /* center-top */ }
    TempGauge { /* center-bottom */ }

    Speedometer {
        // Right side, vertically centered in gauge area
        width: parent.height * 0.70
        height: width
    }

    InfoBar {
        anchors.bottom: parent.bottom
        height: parent.height * 0.06
        width: parent.width
    }

    ExitButton {
        anchors.fill: parent
        z: 100
    }
}
```

## Command-line options

```
./qt-cluster-demo [OPTIONS]
  --can <interface>    CAN interface name (default: can0)
  --demo               Demo mode — synthetic values, no CAN
  --fullscreen         Force fullscreen (default on eglfs)
```

No `--width`/`--height` — resolution is always read from `Screen.geometry`.

## Project structure

```
package/qt-cluster-demo/
├── PLAN.md                        # Original plan
├── REFINED-PLAN.md                # This file
├── qt-cluster-demo.mk             # Buildroot package makefile
├── Config.in                      # Buildroot config menu entry
├── CMakeLists.txt                 # Top-level CMake (points to src/)
└── src/
    ├── CMakeLists.txt             # Build config
    ├── main.cpp
    ├── CanReader.h
    ├── CanReader.cpp
    ├── ClusterModel.h
    ├── ClusterModel.cpp
    ├── DemoSimulator.h
    ├── DemoSimulator.cpp
    ├── qml/
    │   ├── main.qml
    │   ├── NeedleGauge.qml
    │   ├── Speedometer.qml
    │   ├── Tachometer.qml
    │   ├── FuelGauge.qml
    │   ├── TempGauge.qml
    │   ├── TelltaleRow.qml
    │   ├── InfoBar.qml
    │   └── ExitButton.qml
    ├── qml.qrc                    # Qt resource file
    └── cluster-launcher.sh        # Wrapper script for qt-demo-launcher
```

Note: `src/` is the buildroot package source directory (matches `_SITE` in `.mk` file). `CMakeLists.txt` at top level is a thin wrapper pointing to `src/CMakeLists.txt` for convenience.

## Build system

### CMakeLists.txt (src/)

```cmake
cmake_minimum_required(VERSION 3.10)
project(qt-cluster-demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Qml Quick)

add_executable(qt-cluster-demo
    main.cpp
    CanReader.cpp
    ClusterModel.cpp
    DemoSimulator.cpp
    qml.qrc
)

target_link_libraries(qt-cluster-demo
    Qt5::Core Qt5::Gui Qt5::Qml Qt5::Quick
)

include(GNUInstallDirs)
install(TARGETS qt-cluster-demo DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES cluster-launcher.sh
    DESTINATION ${CMAKE_INSTALL_DATADIR}/qt-apps
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)
```

### qt-cluster-demo.mk (Buildroot)

```makefile
QT_CLUSTER_DEMO_VERSION = 1.0
QT_CLUSTER_DEMO_SITE_METHOD = local
QT_CLUSTER_DEMO_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/qt-cluster-demo/src
QT_CLUSTER_DEMO_DEPENDENCIES = qt5base qt5declarative

define QT_CLUSTER_DEMO_CONFIGURE_CMDS
    (cd $(@D) && $(TARGET_MAKE_ENV) $(HOST_DIR)/bin/cmake \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_BUILD_TYPE=Release \
        $(BR_CMAKE_OPTS) .)
endef

define QT_CLUSTER_DEMO_BUILD_CMDS
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

define QT_CLUSTER_DEMO_INSTALL_TARGET_CMDS
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(TARGET_DIR) install
endef

$(eval $(generic-package))
```

### Config.in

```
config BR2_PACKAGE_QT_CLUSTER_DEMO
    bool "qt-cluster-demo"
    depends on BR2_PACKAGE_QT5BASE
    select BR2_PACKAGE_QT5BASE_OPENGL
    select BR2_PACKAGE_QT5DECLARATIVE
    help
      Qt/QML automotive instrument cluster demo.
      Reads OBD2 data from CAN bus and renders
      speedometer, tachometer, and gauges.
```

## Integration with qt-demo-launcher

### Button entry for qt-demo-launcher.json

```json
{
    "id": "cluster-demo",
    "enabled": true,
    "text": "Cluster Demo",
    "icon": "/usr/share/icons/tools-large.png",
    "program": "/usr/share/qt-apps/cluster-launcher.sh",
    "arguments": [],
    "working_directory": "/tmp",
    "position": { "row": 2, "column": 1 }
}
```

### cluster-launcher.sh

```bash
#!/bin/sh
export QT_QPA_PLATFORM=eglfs
export QT_QPA_EGLFS_ALWAYS_SET_MODE=1
exec /usr/bin/qt-cluster-demo --can can0
```

**Lifecycle:**
1. User taps "Cluster Demo" button in qt-demo-launcher
2. Launcher calls `QProcess::start("cluster-launcher.sh")`
3. Launcher hides itself (`this->hide()`)
4. cluster-launcher.sh sets eglfs environment and `exec`s qt-cluster-demo
5. qt-cluster-demo takes over DRM/KMS via eglfs, renders cluster UI
6. User touches screen -> exit button appears -> taps X -> `Qt.quit()`
7. Process exits -> launcher's `QProcess::finished()` signal fires -> launcher shows itself

## Testing with car-can-emulator

```bash
# Terminal 1: Start virtual CAN
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Terminal 2: Start emulator in simulate mode
cd /home/testpc/git-repos/claude/2026/car-can-emulator
./car-can-emulator --node=vcan0 --simulate

# Terminal 3: Start cluster demo
./qt-cluster-demo --can vcan0

# Terminal 4: Monitor or override values
candump vcan0
echo -n "speed 200" | nc 127.0.0.1 8080
echo -n "rpm 6000" | nc 127.0.0.1 8080
echo -n "telltale engine on" | nc 127.0.0.1 8080
```

For demo mode (no CAN needed):
```bash
./qt-cluster-demo --demo
```

## Implementation order

1. **C++ backend** (can build and test without QML):
   - `ClusterModel.h/.cpp` — Q_PROPERTY boilerplate, simple getters/setters
   - `DemoSimulator.h/.cpp` — QTimer + phase table, emits signals
   - `CanReader.h/.cpp` — socketcan socket, poll loop, OBD2 decode
   - `main.cpp` — CLI parsing, wiring, QML engine setup

2. **QML UI** (can test with DemoSimulator immediately):
   - `NeedleGauge.qml` — reusable gauge with Canvas + animated needle
   - `Speedometer.qml` — NeedleGauge configured for speed
   - `Tachometer.qml` — NeedleGauge configured for RPM
   - `FuelGauge.qml` — small arc gauge
   - `TempGauge.qml` — small arc gauge
   - `TelltaleRow.qml` — indicator row with blink timers
   - `InfoBar.qml` — bottom info strip
   - `ExitButton.qml` — touch overlay
   - `main.qml` — root layout composing everything

3. **Build/packaging**:
   - `qml.qrc` — resource file listing QML files
   - `CMakeLists.txt` — build configuration
   - `qt-cluster-demo.mk` + `Config.in` — buildroot integration
   - `cluster-launcher.sh` — eglfs wrapper script
   - Add button entry to qt-demo-launcher.json

## Future enhancements (not in v1)

- Pre-made gauge assets (SVG/PNG dial faces) — swap Canvas for Image in QML
- Gear indicator calculated from speed/RPM ratio
- Trip computer (fuel consumption, distance)
- Night mode (dim/amber color scheme)
- CAN DBC file parsing for non-OBD2 vehicles
- Touch settings overlay (brightness, units, gauge style)
