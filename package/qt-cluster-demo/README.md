# qt-cluster-demo

Qt5/QML automotive instrument cluster that reads vehicle data from a CAN bus via standard OBD2 Mode 01 polling and renders gauges in real-time.

![Cluster Demo](../../package/qt-demo-launcher/rendered-dial-v3.jpg)

## Features

- **Speedometer** (0-260 km/h) with digital readout
- **Tachometer** (0-8000 RPM) with redline zone at 6500+
- **Fuel gauge** (E-F) with blinking LOW FUEL warning
- **Temperature gauge** (C-H) with blinking OVERHEAT warning
- **12 telltale indicators**: engine, oil, battery, brake, turn signals (blinking), high beam, door, seatbelt, ABS, traction control, TPMS
- **Startup diagnostic sweep** — all telltales ON, needles sweep to max and back (like real car key-on)
- **Demo mode** — synthetic drive cycle when no CAN hardware is available
- **Resolution-independent** layout (designed for 1920x720, adapts to any screen)
- **Tapered needle** with chrome multi-ring center hub
- **Touch exit button** — tap screen to reveal, auto-hides after 3s

## Architecture

### System overview

```
 ┌─────────────────────────────────────────────────────────────────────────┐
 │  OBD2 Source                                                            │
 │  (Real vehicle / OBD Simulator / car-can-emulator)                      │
 └──────────────────────────┬──────────────────────────────────────────────┘
                            │ CAN Bus (500 kbps)
                            │ ISO 15765-4, 11-bit IDs
 ┌──────────────────────────┴──────────────────────────────────────────────┐
 │  CANable USB-to-CAN Adapter                                             │
 │  (gs_usb driver → socketcan → can0)                                     │
 └──────────────────────────┬──────────────────────────────────────────────┘
                            │ /dev/can0 (or vcan0)
 ┌──────────────────────────┴──────────────────────────────────────────────┐
 │  Raspberry Pi 4                                                         │
 │                                                                         │
 │  ┌──────────────────────────────────────────────────────────────────┐   │
 │  │  qt-cluster-demo                                                 │   │
 │  │                                                                  │   │
 │  │  ┌──────────────────┐     ┌──────────────────┐                   │   │
 │  │  │   CanReader      │     │  DemoSimulator   │                   │   │
 │  │  │   (QThread)      │     │  (QTimer, 50ms)  │                   │   │
 │  │  │                  │     │                  │                   │   │
 │  │  │ PF_CAN/SOCK_RAW  │     │ 12-phase drive   │                   │   │
 │  │  │ CAN_RAW_FILTER:  │     │ cycle with       │                   │   │
 │  │  │  0x7E8 + 0x420   │     │ telltale events  │                   │   │
 │  │  │                  │     │                  │                   │   │
 │  │  │ TX: 0x7DF OBD2   │     │ (activated when  │                   │   │
 │  │  │ RX: 0x7E8 resp   │     │  --demo flag or  │                   │   │
 │  │  │ RX: 0x420 tellt  │     │  CAN open fails) │                   │   │
 │  │  └────────┬─────────┘     └─────────┬────────┘                   │   │
 │  │           │ Qt signals              │ Qt signals                 │   │
 │  │           │ (cross-thread,          │ (same signals:             │   │
 │  │           │  auto-queued)           │  speedChanged, etc.)       │   │
 │  │           └───────────┬─────────────┘                            │   │
 │  │                       ▼                                          │   │
 │  │           ┌──────────────────────┐                               │   │
 │  │           │    ClusterModel      │                               │   │
 │  │           │    (QObject)         │                               │   │
 │  │           │                      │                               │   │
 │  │           │ Q_PROPERTY:          │                               │   │
 │  │           │  speed (int)         │                               │   │
 │  │           │  rpm (int)           │                               │   │
 │  │           │  coolantTemp (int)   │                               │   │
 │  │           │  fuelLevel (int)     │                               │   │
 │  │           │  batteryVoltage (dbl)│                               │   │
 │  │           │  telltales (uint16)  │                               │   │
 │  │           │  canConnected (bool) │                               │   │
 │  │           │  startupActive (bool)│                               │   │
 │  │           │                      │                               │   │
 │  │           │ No smoothing — raw   │                               │   │
 │  │           │ values passed to QML │                               │   │
 │  │           └──────────┬───────────┘                               │   │
 │  │                      │ setContextProperty("cluster", &model)     │   │
 │  │                      ▼                                           │   │
 │  │           ┌───────────────────────────────────────────────┐      │   │
 │  │           │         QML Scene Graph (eglfs)               │      │   │
 │  │           │                                               │      │   │
 │  │           │  ┌─────────────────────────────────────────┐  │      │   │
 │  │           │  │ main.qml (Window, Screen.width x height)│  │      │   │
 │  │           │  │                                         │  │      │   │
 │  │           │  │ ┌─────────────────────────────────────┐ │  │      │   │
 │  │           │  │ │ TelltaleRow (12 indicators, blink)  │ │  │      │   │
 │  │           │  │ └─────────────────────────────────────┘ │  │      │   │
 │  │           │  │ ┌───────────┐ ┌────────┐ ┌───────────┐  │  │      │   │
 │  │           │  │ │Tachometer │ │  Fuel  │ │Speedometer│  │  │      │   │
 │  │           │  │ │ 0-8000RPM │ │ Gauge  │ │ 0-260km/h │  │  │      │   │
 │  │           │  │ │ (Canvas)  │ │(Canvas)│ │ (Canvas)  │  │  │      │   │
 │  │           │  │ │           │ ├────────┤ │           │  │  │      │   │
 │  │           │  │ │  Needle:  │ │  Temp  │ │  Needle:  │  │  │      │   │
 │  │           │  │ │  Canvas + │ │ Gauge  │ │  Canvas + │  │  │      │   │
 │  │           │  │ │  Smoothed │ │(Canvas)│ │  Smoothed │  │  │      │   │
 │  │           │  │ │  Anim     │ │        │ │  Anim     │  │  │      │   │
 │  │           │  │ └───────────┘ └────────┘ └───────────┘  │  │      │   │
 │  │           │  │ ┌─────────────────────────────────────┐ │  │      │   │
 │  │           │  │ │ InfoBar (voltage, CAN status)       │ │  │      │   │
 │  │           │  │ └─────────────────────────────────────┘ │  │      │   │
 │  │           │  │ ┌─────────────────────────────────────┐ │  │      │   │
 │  │           │  │ │ ExitButton (touch overlay, z=100)   │ │  │      │   │
 │  │           │  │ └─────────────────────────────────────┘ │  │      │   │
 │  │           │  └─────────────────────────────────────────┘  │      │   │
 │  │           └───────────────────────────────────────────────┘      │   │
 │  └──────────────────────────────────────────────────────────────────┘   │
 │                              │                                          │
 │                              ▼                                          │
 │                    ┌───────────────────┐                                │
 │                    │  DRM/KMS (eglfs)  │                                │
 │                    │  OpenGL ES 2.0    │                                │
 │                    │  GPU-accelerated  │                                │
 │                    └─────────┬─────────┘                                │
 │                              │ HDMI                                     │
 └──────────────────────────────┼──────────────────────────────────────────┘
                                ▼
                    ┌───────────────────────┐
                    │  12.3" Display        │
                    │  1920 x 720           │
                    └───────────────────────┘
```

### Rendering pipeline

The app uses Qt's **eglfs** platform plugin for direct GPU rendering without a windowing system:

```
QML Scene Graph
    │
    ▼
Qt Quick Renderer (threaded)
    │ Batches QML items into OpenGL draw calls
    │ Canvas items: rasterized to textures (dial faces, arcs)
    │ SmoothedAnimation: interpolated per-frame at display refresh rate
    ▼
OpenGL ES 2.0 (V3D GPU on Pi4)
    │ Fragment/vertex shaders for compositing
    │ Texture uploads for Canvas content
    ▼
DRM/KMS (direct rendering manager)
    │ Page-flipped to display without compositor overhead
    │ eglfs takes exclusive control of display output
    ▼
HDMI output → 1920x720 panel
```

Key rendering details:
- **eglfs** bypasses X11/Wayland — Qt renders directly to the DRM framebuffer via EGL
- **Canvas elements** (dial faces, arcs) are rendered once to offscreen textures, only repainted on value change
- **Needle animation** runs on the Qt scene graph render thread at display vsync rate (~60fps), driven by `SmoothedAnimation` which interpolates between C++ value updates
- **No compositor overhead** — single fullscreen surface, zero latency path from GPU to display
- The launcher (`qt-demo-launcher`) runs on `linuxfb`; this app overrides to `eglfs` via `cluster-launcher.sh`

### Data flow

```
                       ┌──────────────────────────────┐
                       │        Startup Sequence      │
                       │                              │
                       │  1. Parse CLI (--can/--demo) │
                       │  2. Open CAN socket or       │
                       │     fallback to demo mode    │
                       │  3. Load QML scene           │
                       │  4. Run diagnostic sweep     │
                       │     (all telltales ON,       │
                       │      needles to max → back)  │
                       │  5. Start data source        │
                       └──────────────┬───────────────┘
                                      ▼
    ┌──────────────────────────────────────────────────────────────┐
    │                     Normal Operation                         │
    │                                                              │
    │  CanReader thread          Main thread          Render thread│
    │  ┌──────────────┐    ┌──────────────────┐   ┌─────────────┐  │
    │  │ send 0x7DF   │    │ ClusterModel     │   │ QML Scene   │  │
    │  │ read 0x7E8   │───▶│ setSpeed(v)      │──▶│ Graph       │  │
    │  │ read 0x420   │    │ setRpm(v)        │   │             │  │
    │  │ decode PIDs  │    │ emit *Changed()  │   │ Smoothed    │  │
    │  │ 50ms sleep   │    │                  │   │ Animation   │  │
    │  │ repeat       │    │ (no smoothing,   │   │ @60fps      │  │
    │  └──────────────┘    │  raw values)     │   └─────────────┘  │
    │                      └──────────────────┘                    │
    └──────────────────────────────────────────────────────────────┘
```

### Component details

- **CanReader** — opens `PF_CAN/SOCK_RAW` socket, installs kernel-level `CAN_RAW_FILTER` for `0x7E8` and `0x420` only. Runs in a dedicated `QThread`. Sends OBD2 Mode 01 requests on `0x7DF`, validates DLC before decoding. `SO_RCVTIMEO` 100ms timeout. Emits `canTimeout()` after 3 consecutive missed cycles.
- **ClusterModel** — pure data bridge with `Q_PROPERTY` bindings. Handles startup diagnostic sweep via `QTimer`. Emits `startupFinished()` to trigger data source start.
- **DemoSimulator** — 12-phase drive cycle (pull away → 200 km/h → hard brake → stop), 50ms tick interval with linear interpolation, RPM jitter, fuel consumption, and telltale events throughout.
- **NeedleGauge** — reusable Canvas-based gauge with tapered gradient needle (Canvas-drawn), chrome 3-ring center hub, configurable ticks/labels/redline. `SmoothedAnimation` velocity switches between 200 deg/s (startup sweep) and 1500 deg/s (normal operation).
- **TelltaleRow** — 12 indicators with QML-driven blink timer (500ms) for turn signals (Option B: consistent blink regardless of CAN poll timing).

## OBD2 PIDs polled

Poll rates are approximate and depend on ECU response timing. Fast-responding ECUs produce shorter cycles; slow or missing responses stretch the cycle due to socket read timeouts.

| PID | Parameter | Decode | Poll rate (approx) |
|-----|-----------|--------|-----------|
| `0x0C` | RPM | `(A<<8\|B) / 4` | Every cycle (~300ms) |
| `0x0D` | Speed | `A` km/h | Every cycle |
| `0x05` | Coolant temp | `A - 40` C | Every 2nd cycle |
| `0x2F` | Fuel level | `A * 100 / 255` % | Every 5th cycle |
| `0x42` | Battery voltage | `(A<<8\|B) / 1000` V | Every 5th cycle |

## Telltale CAN frame (0x420)

Custom broadcast frame (not OBD2), 2 bytes. This bit layout is a shared contract between `car-can-emulator`, `DemoSimulator.cpp`, `TelltaleRow.qml`, and this documentation. Edits to the bit positions must be synchronized across all four locations.

| Bit | Indicator | Bit | Indicator |
|-----|-----------|-----|-----------|
| 0 | Check Engine | 6 | High Beam |
| 1 | Oil Pressure | 7 | Door Ajar |
| 2 | Battery | 8 | Seatbelt |
| 3 | Brake | 9 | ABS |
| 4 | Left Turn | 10 | Traction Control |
| 5 | Right Turn | 11 | TPMS |

## Command-line options

```
qt-cluster-demo [OPTIONS]
  --can <interface>    CAN interface name (default: can0)
  --demo               Demo mode (synthetic values, no CAN)
  --no-sweep           Skip startup diagnostic sweep
```

## Usage

### With real OBD2 hardware (e.g., OBD Simulator + CANable)

```bash
# Ensure can0 is configured (done by can0-setup.service at boot)
ip link set can0 type can bitrate 500000
ip link set can0 up

# Run cluster
qt-cluster-demo --can can0
```

### With car-can-emulator on virtual CAN

```bash
# vcan0 is auto-created at boot, car-can-emulator runs on it by default
qt-cluster-demo --can vcan0
```

### Demo mode (no CAN hardware needed)

```bash
qt-cluster-demo --demo
```

Demo mode mirrors the telltale activation and drive-cycle behavior expected from the paired `car-can-emulator` hardware setup. It uses the same signal interface as `CanReader`, so the QML layer is unaware of the data source. Useful for UI development, trade shows, and testing without CAN hardware.

### Fast development commands

```bash
# Quick iteration — no sweep delay, no CAN
qt-cluster-demo --demo --no-sweep

# Test with emulator on vcan0 — no sweep delay
qt-cluster-demo --can vcan0 --no-sweep
```

### Via qt-demo-launcher

Tap "Cluster Demo" button. The launcher runs `cluster-launcher.sh` which sets `QT_QPA_PLATFORM=eglfs` and starts the app on `can0`. Touch the screen to reveal the exit button, or press Escape.

## Project structure

```
qt-cluster-demo/
├── CMakeLists.txt              # Convenience wrapper
├── Config.in                   # Buildroot menu entry
├── qt-cluster-demo.mk          # Buildroot package makefile
├── README.md
└── src/
    ├── CMakeLists.txt          # Build config (Qt5 Core/Gui/Qml/Quick)
    ├── main.cpp                # CLI parsing, wiring, QML engine
    ├── CanReader.h/.cpp        # Socketcan OBD2 polling (QThread)
    ├── ClusterModel.h/.cpp     # QObject model exposed to QML
    ├── DemoSimulator.h/.cpp    # Synthetic drive cycle generator
    ├── cluster-launcher.sh     # eglfs wrapper for qt-demo-launcher
    ├── qml.qrc                 # Qt resource file
    └── qml/
        ├── main.qml            # Root layout (resolution-independent)
        ├── NeedleGauge.qml     # Reusable gauge (Canvas + animated needle)
        ├── Speedometer.qml     # 0-260 km/h dial
        ├── Tachometer.qml      # 0-8000 RPM dial with redline
        ├── FuelGauge.qml       # Arc gauge with low fuel warning
        ├── TempGauge.qml       # Arc gauge with overheat warning
        ├── TelltaleRow.qml     # 12 indicators with turn signal blink
        ├── InfoBar.qml         # Battery voltage + CAN status
        └── ExitButton.qml      # Touch-to-show exit overlay
```

## Build

### Buildroot (cross-compile for Pi4)

```bash
# Enable in menuconfig: BR2_PACKAGE_QT_CLUSTER_DEMO=y
make qt-cluster-demo
```

### Host (for development)

```bash
mkdir build && cd build
cmake ../package/qt-cluster-demo
make -j$(nproc)
./src/qt-cluster-demo --demo
```

**Note**: Host build requires Qt5 development packages. Running the app on the host additionally requires QML runtime modules (`QtQuick.Window`, `QtQuick`). On Debian/Ubuntu: `apt install qml-module-qtquick-window2 qml-module-qtquick2`. Without these, the build succeeds but the app fails at startup with "module not installed" errors.

## Dependencies

- Qt5: Core, Gui, Qml, Quick
- Linux: socketcan (`linux/can.h`, `linux/can/raw.h`)
- No additional libraries

## Testing with OBD simulator hardware

Tested with "OBD Simulator-B-V1.5" (10-pot USB-powered simulator) connected via CANable USB-to-CAN adapter to Raspberry Pi 4. Protocol: ISO 15765-4, 11-bit CAN IDs, 500 kbps.

**Important**: Stop any other services using `can0` before running:
```bash
kill $(pidof disp-can-ctrl) $(pidof car-can-emulator) 2>/dev/null
```

## Known limitations

- **Single-ECU only** — the app sends OBD2 requests on `0x7DF` and only accepts responses from `0x7E8`. Alternate ECU response IDs (`0x7E9`-`0x7EF`) are filtered out at the kernel level. Multi-ECU vehicles may not work correctly.
- **No request/response PID correlation** — the poll loop accepts the first `0x7E8` response after each request without verifying the response PID matches the requested PID. On buses with high latency or multiple responders, stale responses may be decoded in the wrong poll slot.
- **Fixed PID set** — only 5 PIDs are polled (RPM, speed, coolant temp, fuel level, battery voltage). No support for extended PIDs, ISO-TP multi-frame responses, or DBC file parsing.
- **Custom telltale protocol** — telltales use a non-standard CAN frame (`0x420`) specific to the paired `car-can-emulator`. Real vehicles do not broadcast telltale state on a single CAN frame.
- **No runtime CAN fallback** — if the CAN interface goes down after startup, the app shows "CAN: --" but does not automatically switch to demo mode.

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| `module "QtQuick.Window" is not installed` | Missing QML runtime modules on host | `apt install qml-module-qtquick-window2 qml-module-qtquick2` |
| "CAN: --" stays grey, no gauge movement | CAN interface not up, or no ECU responding | Check `ip link show can0`, verify bitrate, run `candump can0` |
| `can0` not visible in `ifconfig` | CANable USB adapter not plugged in or driver not loaded | Plug in adapter, check `dmesg` for `gs_usb` |
| Gauges stuck, CAN goes to ERROR-PASSIVE | Another service (disp-can-ctrl, car-can-emulator) using `can0` | `kill $(pidof disp-can-ctrl) $(pidof car-can-emulator)` |
| Gauges stuck, ERROR-PASSIVE, no other services | Missing CAN bus termination (120 ohm) | Enable termination jumper on CANable or add 120R resistor |
| App falls back to demo mode unexpectedly | CAN socket open failed | Check interface name matches `--can` argument |
