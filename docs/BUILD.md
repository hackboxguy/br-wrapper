# CMake Build Instructions for BR-Wrapper Qt Applications

This repository contains Qt applications and system daemons that can be built using CMake.

## Table of Contents
- [Prerequisites](#prerequisites)
- [Building](#building)
- [Installation](#installation)
- [Package Overview](#package-overview)
- [CMake Options](#cmake-options)
- [Buildroot Integration](#buildroot-integration)

## Prerequisites

### Ubuntu/Debian Development Environment

#### Build Dependencies (for compilation)

Install these packages to build the Qt applications:

```bash
sudo apt-get update
sudo apt-get install -y \
    cmake \
    build-essential \
    qtbase5-dev \
    qtbase5-dev-tools \
    qtdeclarative5-dev \
    qtdeclarative5-dev-tools
```

#### Runtime Dependencies (for execution)

After building, you can purge the build dependencies and install only runtime packages:

```bash
# Runtime dependencies for executing the Qt applications
sudo apt-get install -y \
    libqt5core5t64 \
    libqt5gui5t64 \
    libqt5widgets5t64 \
    libqt5network5t64 \
    libqt5qml5 \
    libqt5quick5 \
    qml-module-qtquick2 \
    qml-module-qtquick-controls2 \
    fonts-dejavu
```

#### Purging Build Dependencies (optional)

After building, you can remove the development packages to save space:

```bash
sudo apt-get purge -y \
    qtbase5-dev \
    qtbase5-dev-tools \
    qtdeclarative5-dev \
    qtdeclarative5-dev-tools

sudo apt-get autoremove -y
```

**Note**: Keep `cmake` and `build-essential` if you need them for other projects.

### Buildroot Cross-Compilation

When building within Buildroot, the Qt5 dependencies are automatically handled by the existing `.mk` package files. No additional setup is required.

## Building

### Standard Build

```bash
# Configure
cmake -S . -B build

# Build all packages
cmake --build build

# Build specific package
cmake --build build --target qt-demo-launcher
cmake --build build --target disp-tester
cmake --build build --target touch-gallery
cmake --build build --target qt-mpv-wrapper
cmake --build build --target disp-can-ctrl
cmake --build build --target input-monitor
```

### Build WITHOUT Sample Data (opt-out)

By default, sample data (Patterns, Pictures) is installed to match `setup-qt-apps.sh` behavior. To disable:

```bash
cmake -S . -B build -DINSTALL_SAMPLE_DATA=OFF
cmake --build build
```

### Custom Install Prefix

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/opt/qt-apps
cmake --build build
```

### Cross-Compilation

```bash
cmake -S . -B build \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
```

## Installation

```bash
# Install to CMAKE_INSTALL_PREFIX (default: /usr/local)
# No root access required - all files installed within the prefix
cmake --build build --target install

# Install to custom directory with DESTDIR
cmake --build build --target install -- DESTDIR=/tmp/staging
```

When `INSTALL_SYSTEMD_SERVICES=ON` (default), systemd service files and udev rules are installed to:
- `${CMAKE_INSTALL_PREFIX}/lib/systemd/system/qt-demo-launcher.service`
- `${CMAKE_INSTALL_PREFIX}/etc/udev/rules.d/99-input-permissions.rules`

**These files are installed within your installation prefix and do NOT require root access.**

### Post-Installation Configuration

After installation, you need to update the configuration file paths to match your installation prefix:

```bash
# Navigate to the installation directory
cd ${CMAKE_INSTALL_PREFIX}/share/qt-apps

# Run the path update script
./update-config-paths.sh
```

This script updates `qt-demo-launcher.json` to use paths relative to `/home/pi/micropanel/qt-apps`. If you need different paths, manually edit the JSON file or modify the `update-config-paths.sh` script.

### Activating Systemd Service and Udev Rules

When `INSTALL_SYSTEMD_SERVICES=ON` (default), service files are installed within your installation prefix. To activate them system-wide, copy them to system directories:

```bash
# Copy service and udev files to system locations (requires root)
sudo cp ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/qt-demo-launcher.service /lib/systemd/system/
sudo cp ${CMAKE_INSTALL_PREFIX}/etc/udev/rules.d/99-input-permissions.rules /etc/udev/rules.d/

# Reload systemd and udev
sudo systemctl daemon-reload
sudo udevadm control --reload-rules
sudo udevadm trigger

# Enable and start qt-demo-launcher service
sudo systemctl enable qt-demo-launcher.service
sudo systemctl start qt-demo-launcher.service

# Check service status
sudo systemctl status qt-demo-launcher.service
```

**Installed Service Files (within installation prefix):**
- `${CMAKE_INSTALL_PREFIX}/lib/systemd/system/qt-demo-launcher.service` - Systemd service unit
- `${CMAKE_INSTALL_PREFIX}/etc/udev/rules.d/99-input-permissions.rules` - Udev rules for input device permissions

**Note:** The SysVinit script `S99DispCanCtrl` for disp-can-ctrl is available in `package/disp-can-ctrl/src/` but is NOT installed automatically. For Buildroot environments, this is handled by the `.mk` package file. For manual installation, copy it to `/etc/init.d/` as needed.

**If `INSTALL_SYSTEMD_SERVICES=OFF`:**
Service files are installed to `${CMAKE_INSTALL_PREFIX}/share/qt-apps/` instead of `lib/systemd/system/` and `etc/udev/rules.d/`.

### Installation Layout

```
${CMAKE_INSTALL_PREFIX}/
├── bin/                          # Application binaries
│   ├── qt-demo-launcher
│   ├── disp-tester
│   ├── touch-gallery
│   ├── qt-mpv-wrapper
│   └── input-monitor
├── sbin/                         # System daemons
│   ├── disp-can-ctrl
│   └── disp-can-client
└── share/qt-apps/                # Configuration files and data
    ├── qt-demo-launcher.json
    ├── qt-demo-launcher.service
    ├── 99-input-permissions.rules
    ├── update-config-paths.sh
    ├── qt-mpv-wrapper.sh
    ├── mpv-input.conf
    ├── Patterns/                 # Sample data (if INSTALL_SAMPLE_DATA=ON)
    ├── Pictures/                 # Sample data (if INSTALL_SAMPLE_DATA=ON)
    └── Videos/                   # Empty directory for videos
```

## Package Overview

### Qt Applications

1. **qt-demo-launcher** - Main launcher application
   - Technology: Qt Widgets + Network
   - Source: [package/qt-demo-launcher](package/qt-demo-launcher)
   - Installs: Binary, JSON config, systemd service, udev rules, helper script

2. **disp-tester** - Display testing tool
   - Technology: Qt Quick/QML + Network
   - Source: [package/disp-tester](package/disp-tester)
   - Installs: Binary with embedded QML resources

3. **touch-gallery** - Touch-enabled image gallery
   - Technology: Qt Quick/QML
   - Source: [package/touch-gallery](package/touch-gallery)
   - Installs: Binary with embedded QML resources

4. **qt-mpv-wrapper** - Video player wrapper
   - Technology: Qt Quick/QML + Network + C utility
   - Source: [package/qt-mpv-wrapper](package/qt-mpv-wrapper)
   - Installs: Qt binary, C utility (input-monitor), shell script, mpv config

### System Daemons

5. **disp-can-ctrl** - Display CAN control daemon
   - Technology: C++ (no Qt dependency)
   - Source: [package/disp-can-ctrl](package/disp-can-ctrl)
   - Installs: Server daemon, client utility

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | `Release` | Build type (Debug/Release) |
| `CMAKE_INSTALL_PREFIX` | `/usr/local` | Installation prefix |
| `INSTALL_SAMPLE_DATA` | `ON` | Install sample Patterns, Pictures, Videos (matches setup-qt-apps.sh) |
| `INSTALL_SYSTEMD_SERVICES` | `ON` | Install systemd services and udev rules to system directories |

### Examples

```bash
# Debug build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Disable sample data installation
cmake -S . -B build -DINSTALL_SAMPLE_DATA=OFF

# Disable systemd service installation (install to share/qt-apps instead)
cmake -S . -B build -DINSTALL_SYSTEMD_SERVICES=OFF

# Custom prefix (sample data and systemd services installed by default)
cmake -S . -B build \
    -DCMAKE_INSTALL_PREFIX=/home/pi/micropanel/qt-apps
```

## Buildroot Integration

The CMake build system works alongside the existing Buildroot `.mk` package files. Each package maintains its own `.mk` file for Buildroot integration:

- `package/qt-demo-launcher/qt-demo-launcher.mk`
- `package/disp-tester/disp-tester.mk`
- `package/touch-gallery/touch-gallery.mk`
- `package/qt-mpv-wrapper/qt-mpv-wrapper.mk`
- `package/disp-can-ctrl/disp-can-ctrl.mk`

These files are **NOT** modified by the CMake build system and continue to work as before.

### Comparison with setup-qt-apps.sh

The CMake build provides similar functionality to `setup-qt-apps.sh`:

| Feature | setup-qt-apps.sh | CMake Build |
|---------|------------------|-------------|
| Qt5 dependency check | Manual apt-get | find_package() |
| Build system | qmake | CMake |
| Sample data install | Always | Default ON (can disable with -DINSTALL_SAMPLE_DATA=OFF) |
| Install prefix | Fixed `/home/pi/micropanel/qt-apps` | Configurable CMAKE_INSTALL_PREFIX |
| Output structure | Flat (all in OUTPUT_DIR) | FHS-compliant (bin/, sbin/, share/) |
| Privilege handling | Custom run_as_user() | Standard CMake DESTDIR |
| Config path updates | Automatic (runs update-config-paths.sh) | Manual post-install step |

## Development Workflow

### Quick development cycle:

```bash
# One-time setup
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Edit code, then rebuild
cmake --build build

# Run from build directory
./build/package/qt-demo-launcher/qt-demo-launcher
./build/package/disp-tester/disp-tester
```

### Clean rebuild:

```bash
rm -rf build
cmake -S . -B build
cmake --build build
```

## Notes

1. **NetworkInterface.moc**: The qt-demo-launcher package has a known issue with `#include "NetworkInterface.moc"` during cross-compilation. CMake's `AUTOMOC` feature handles this automatically.

2. **QML Resources**: QML files are embedded in binaries using Qt's resource system (`.qrc` files). CMake's `AUTORCC` handles this automatically.

3. **Sample Data**: The Patterns and Pictures directories are sourced from `board/pi4-touch-demo/fs-overlay/` when `INSTALL_SAMPLE_DATA=ON`.

4. **Parallel Builds**: Speed up compilation with `cmake --build build -j$(nproc)`

## Troubleshooting

### Qt5 not found

```bash
CMake Warning: Skipping Qt applications because Qt5 was not found
```

**Solution**: Install Qt5 development packages (see [Prerequisites](#prerequisites))

### Missing Qt5Qml or Qt5Quick

```bash
Could NOT find Qt5Qml (missing: Qt5Qml_DIR)
```

**Solution**: Install `qtdeclarative5-dev`:
```bash
sudo apt-get install qtdeclarative5-dev qtdeclarative5-dev-tools
```

### Permission denied during install

**Solution**: Use sudo or specify DESTDIR:
```bash
cmake --build build --target install -- DESTDIR=/tmp/staging
```
