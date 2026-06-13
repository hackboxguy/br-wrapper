# qt-cluster-demo: Automotive Instrument Cluster App

## Overview

A Qt/QML application that renders an automotive instrument cluster on a 1920x720 display. It reads vehicle data from a CAN bus interface using standard OBD2 Mode 01 polling and renders speedometer, tachometer, fuel gauge, temperature gauge, and telltale indicators.

This app is launched from `qt-demo-launcher` via a button press. It uses `QT_QPA_PLATFORM=eglfs` (GPU-accelerated) while the launcher uses `linuxfb` — no display conflict.

## Target hardware

- Raspberry Pi 4
- Display: 1920x720 (wide automotive format)
- CAN interface: socketcan (`can0` or `vcan0` for development)
- CAN data source: `car-can-emulator` (see `/misc-tools/car-can-emulator/PLAN-V2.md`) or real OBD2 adapter

## Architecture

```
can0/vcan0 → CanReader (C++, socketcan, OBD2 polling)
          → ClusterModel (C++ QObject, Q_PROPERTY bindings)
          → QML Engine (eglfs, OpenGL ES)
              ├── main.qml (root layout 1920x720)
              ├── Speedometer.qml (large dial, left)
              ├── Tachometer.qml (large dial, right)
              ├── FuelGauge.qml (small gauge, center-left)
              ├── TempGauge.qml (small gauge, center-right)
              ├── TelltaleRow.qml (indicator icons, top)
              └── NeedleGauge.qml (reusable base component)
```

## Display layout (1920x720)

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

- **Tachometer**: left side, ~500px diameter, 0-8000 RPM, redline zone at 6500+
- **Speedometer**: right side, ~500px diameter, 0-260 km/h
- **Fuel gauge**: center-top, small arc or vertical bar, 0-100%
- **Temp gauge**: center-bottom, small arc or vertical bar, 60-130°C with danger zone at 110+
- **Telltale row**: top strip, ~40px tall, icon indicators with on/off/blink states
- **Info bar**: bottom strip, gear indicator and odometer (optional)

## CAN bus communication

### OBD2 polling (standard Mode 01)

The `CanReader` class polls PIDs by sending requests on `0x7DF` and reading responses from `0x7E8`:

```
Request:  ID=0x7DF  DLC=8  [02 01 PID 00 00 00 00 00]
Response: ID=0x7E8  DLC=8  [bytes... per PID spec]
```

**Polling schedule** (round-robin, ~50ms between requests):
| PID | Parameter | Decode | Poll rate |
|-----|-----------|--------|-----------|
| `0x0C` | RPM | `(data[3]<<8 \| data[4]) / 4` | Every cycle (~300ms) |
| `0x0D` | Speed | `data[3]` km/h | Every cycle |
| `0x05` | Coolant temp | `data[3] - 40` °C | Every 2nd cycle |
| `0x2F` | Fuel level | `data[3] * 100 / 255` % | Every 5th cycle |
| `0x42` | Battery voltage | `(data[3]<<8 \| data[4]) / 1000.0` V | Every 5th cycle |

### Telltale CAN message (passive listener)

Listen for CAN ID `0x420` (broadcast by car-can-emulator every 200ms):
```
Byte 0 bits: [door|highbeam|right_turn|left_turn|brake|battery|oil|engine]
Byte 1 bits: [rsvd|rsvd|rsvd|rsvd|tpms|traction|abs|seatbelt]
```

No polling needed — just read when available.

### Fallback / demo mode

If no CAN interface is available (or `--demo` flag):
- Skip CAN initialization
- `ClusterModel` generates synthetic values internally
- Sweep pattern: idle → accelerate → cruise → brake → stop → repeat
- Telltales cycle through demo sequence

## C++ classes

### CanReader (src/CanReader.h/.cpp)
- Opens socketcan raw socket on specified interface
- Runs in a dedicated thread (QThread or std::thread)
- Sends OBD2 requests, reads responses, decodes PIDs
- Passively reads telltale frames on `0x420`
- Emits signals: `speedChanged(int)`, `rpmChanged(int)`, `tempChanged(int)`, `fuelChanged(int)`, `telltalesChanged(uint16_t)`
- Constructor: `CanReader(const QString &interface)` — e.g., "can0", "vcan0"

### ClusterModel (src/ClusterModel.h/.cpp)
- QObject exposed to QML via `setContextProperty("cluster", model)`
- Q_PROPERTY for each gauge value with NOTIFY signals:
  - `speed` (int, 0-260 km/h)
  - `rpm` (int, 0-8000)
  - `coolantTemp` (int, °C)
  - `fuelLevel` (int, 0-100%)
  - `batteryVoltage` (double, volts)
  - `telltales` (int, bitfield)
- Connects to CanReader signals or DemoSimulator
- Applies smoothing/damping to needle values (exponential moving average)

### DemoSimulator (src/DemoSimulator.h/.cpp)
- QTimer-based value generator for `--demo` mode
- Cycles through drive profile phases
- Outputs same signals as CanReader (same interface)
- Activated when `--demo` CLI flag is set or CAN interface not found

## QML components

### NeedleGauge.qml (reusable base)
- Properties: `value`, `minValue`, `maxValue`, `startAngle`, `endAngle`, `redlineValue`
- Procedural drawing using Canvas or Qt Quick Shapes:
  - Dial arc background (dark)
  - Tick marks (major every 10/1000 units, minor every 5/500)
  - Numeric labels at major ticks
  - Redline zone (colored arc segment)
  - Needle: rotated rectangle/triangle, pivot at center
  - Center cap circle
- Needle rotation: `Behavior on rotation { SmoothedAnimation { duration: 150 } }`
- Designed for easy replacement with image assets later:
  - Swap Canvas background with `Image { source: "dial-face.png" }`
  - Swap needle Canvas with `Image { source: "needle.png"; transformOrigin: Item.Bottom }`
  - Keep the same `rotation` binding — no logic changes

### Speedometer.qml
- Extends NeedleGauge: min=0, max=260, startAngle=-135, endAngle=135
- Major ticks every 20 km/h, labels: 0, 20, 40, ... 260
- Unit label: "km/h" below center
- Digital readout of current speed

### Tachometer.qml
- Extends NeedleGauge: min=0, max=8000, startAngle=-135, endAngle=135
- Major ticks every 1000 RPM, labels: 0, 1, 2, ... 8
- Redline zone: 6500-8000 (red arc)
- Unit label: "RPM x1000"

### FuelGauge.qml
- Small arc gauge or vertical bar
- Range: E (empty) to F (full)
- Warning indicator when < 15%
- Properties: `level` (0-100)

### TempGauge.qml
- Small arc gauge or vertical bar
- Range: 60°C to 130°C
- Normal zone: 70-100°C (white/blue)
- Warning zone: > 110°C (red)
- Properties: `temperature` (°C)

### TelltaleRow.qml
- Horizontal row of indicator icons across the top
- Each telltale is a colored shape/icon that shows/hides based on bitfield
- Blinking support for turn signals (QML Timer-based animation)
- Telltale list:
  - Check Engine (amber)
  - Oil Pressure (red)
  - Battery (red)
  - Brake (red)
  - Left Turn (green, blinks)
  - Right Turn (green, blinks)
  - High Beam (blue)
  - Door Ajar (amber)
  - Seatbelt (red)
  - ABS (amber)
  - Traction Control (amber)
  - TPMS (amber)

## Command-line options

```
./qt-cluster-demo [OPTIONS]
  --can <interface>    CAN interface (default: can0)
  --demo               Demo mode (no CAN, synthetic values)
  --fullscreen         Force fullscreen (default on eglfs)
  --width <px>         Window width (default: 1920)
  --height <px>        Window height (default: 720)
```

## Project structure

```
qt-cluster-demo/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── CanReader.cpp
│   ├── CanReader.h
│   ├── ClusterModel.cpp
│   ├── ClusterModel.h
│   ├── DemoSimulator.cpp
│   └── DemoSimulator.h
├── qml/
│   ├── main.qml
│   ├── NeedleGauge.qml
│   ├── Speedometer.qml
│   ├── Tachometer.qml
│   ├── FuelGauge.qml
│   ├── TempGauge.qml
│   └── TelltaleRow.qml
├── qml.qrc                    # Qt resource file
└── cluster-launcher.sh        # Wrapper script for qt-demo-launcher
```

## Build dependencies

- Qt5: Core, Gui, Qml, Quick, Network
- Linux headers: `linux/can.h`, `linux/can/raw.h` (socketcan)
- No additional libraries needed (CAN via kernel socketcan)

## CMake build

```bash
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/home/pi/micropanel ..
make -j$(nproc)
make install
```

## Integration with qt-demo-launcher

Add a button entry to `qt-demo-launcher.json`:
```json
{
  "id": "cluster-demo",
  "enabled": true,
  "text": "Cluster Demo",
  "icon": "/usr/share/icons/tools-large.png",
  "program": "/home/pi/micropanel/share/qt-apps/cluster-launcher.sh",
  "arguments": [],
  "working_directory": "/tmp",
  "position": { "row": 2, "column": 1 },
  ...
}
```

`cluster-launcher.sh`:
```bash
#!/bin/sh
export QT_QPA_PLATFORM=eglfs
export QT_QPA_EGLFS_ALWAYS_SET_MODE=1
exec /home/pi/micropanel/bin/qt-cluster-demo --can can0
```

The launcher hides on start, cluster-demo takes over DRM/KMS via eglfs, and when the user exits (e.g., press Escape or a back button), the launcher reappears.

## Testing with car-can-emulator

```bash
# Terminal 1: Start virtual CAN
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Terminal 2: Start emulator in simulate mode
./car-can-emulator --node=vcan0 --simulate

# Terminal 3: Start cluster demo
./qt-cluster-demo --can vcan0

# Terminal 4: Monitor/override
candump vcan0
echo -n "speed 200" | nc 127.0.0.1 8080
echo -n "telltale engine on" | nc 127.0.0.1 8080
```

## Future enhancements (not in v1)

- Pre-made gauge assets (SVG/PNG dial faces) — swap Canvas for Image in QML
- Gear indicator calculated from speed/RPM ratio
- Trip computer (fuel consumption, distance)
- Night mode (dim/amber color scheme)
- CAN DBC file parsing for non-OBD2 vehicles
- Touch support for settings overlay
