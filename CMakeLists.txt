cmake_minimum_required(VERSION 3.10)

project(BRWrapper VERSION 1.0.0 LANGUAGES C CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Use GNUInstallDirs for standard installation directories
include(GNUInstallDirs)

# Option to install sample data (Patterns, Pictures, Videos)
# Default is ON to match setup-qt-apps.sh behavior
option(INSTALL_SAMPLE_DATA "Install sample data files (Patterns, Pictures, Videos)" ON)

# Option to install systemd services and udev rules to system directories
# Default is ON to install to standard locations (/lib/systemd/system, /etc/udev/rules.d)
option(INSTALL_SYSTEMD_SERVICES "Install systemd service files and udev rules to system directories" ON)

# Option to automatically update qt-demo-launcher.json paths during installation
# Default is ON to automatically adjust paths to match CMAKE_INSTALL_PREFIX
option(AUTO_UPDATE_CONFIG_PATHS "Automatically update qt-demo-launcher.json paths to match installation prefix" ON)

# Find Qt5 components required by all Qt apps
# We find all components here to avoid redundant searches in subdirectories
# Note: QuickControls2 is found separately in packages that need it (disp-settings)
find_package(Qt5 COMPONENTS Core Gui Widgets Network Qml Quick QUIET)

if(Qt5_FOUND)
    message(STATUS "Qt5 found: ${Qt5_VERSION}")
    message(STATUS "Qt5 Core: ${Qt5Core_VERSION}")
    message(STATUS "Qt5 Widgets: ${Qt5Widgets_VERSION}")
    message(STATUS "Qt5 Quick: ${Qt5Quick_VERSION}")
else()
    message(STATUS "Qt5 not found - Qt applications will be skipped")
endif()

# Print build information
message(STATUS "========================================")
message(STATUS "BR-Wrapper Build Configuration")
message(STATUS "========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Install sample data: ${INSTALL_SAMPLE_DATA}")
message(STATUS "Install systemd services: ${INSTALL_SYSTEMD_SERVICES}")
message(STATUS "Auto-update config paths: ${AUTO_UPDATE_CONFIG_PATHS}")
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compiling for: ${CMAKE_SYSTEM_NAME}")
    message(STATUS "Target architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()
message(STATUS "========================================")

# Add subdirectories for each package
# These packages don't require Qt5
add_subdirectory(package/disp-can-ctrl/src)
add_subdirectory(package/hh983-serializer/src)

# Add Qt packages if Qt5 was found
if(Qt5_FOUND)
    add_subdirectory(package/qt-demo-launcher)
    add_subdirectory(package/disp-tester)
    add_subdirectory(package/disp-settings)
    add_subdirectory(package/touch-gallery)
    add_subdirectory(package/qt-mpv-wrapper)
else()
    message(WARNING "Skipping Qt applications because Qt5 was not found")
endif()

# Install utility scripts from fs-overlay
set(FS_OVERLAY_SBIN "${CMAKE_SOURCE_DIR}/board/pi4-touch-demo/fs-overlay/usr/sbin")
if(EXISTS "${FS_OVERLAY_SBIN}")
    install(PROGRAMS
        "${FS_OVERLAY_SBIN}/measure-display-gamut.sh"
        "${FS_OVERLAY_SBIN}/measure-display.sh"
        "${FS_OVERLAY_SBIN}/usb-relay.sh"
        "${FS_OVERLAY_SBIN}/validate-launcher-config.sh"
        DESTINATION sbin
        OPTIONAL
    )
    message(STATUS "Will install utility scripts to: ${CMAKE_INSTALL_PREFIX}/sbin")
else()
    message(STATUS "fs-overlay sbin directory not found at: ${FS_OVERLAY_SBIN}")
endif()

# Install sample data if requested
if(INSTALL_SAMPLE_DATA)
    set(FS_OVERLAY_DIR "${CMAKE_SOURCE_DIR}/board/pi4-touch-demo/fs-overlay")
    set(DATA_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/qt-apps")

    # Install Patterns
    if(EXISTS "${FS_OVERLAY_DIR}/Patterns")
        install(DIRECTORY "${FS_OVERLAY_DIR}/Patterns/"
                DESTINATION "${DATA_INSTALL_DIR}/Patterns"
                OPTIONAL)
        message(STATUS "Will install Patterns from: ${FS_OVERLAY_DIR}/Patterns")
    else()
        message(STATUS "Patterns directory not found at: ${FS_OVERLAY_DIR}/Patterns")
    endif()

    # Install Pictures
    if(EXISTS "${FS_OVERLAY_DIR}/Pictures")
        install(DIRECTORY "${FS_OVERLAY_DIR}/Pictures/"
                DESTINATION "${DATA_INSTALL_DIR}/Pictures"
                OPTIONAL)
        message(STATUS "Will install Pictures from: ${FS_OVERLAY_DIR}/Pictures")
    else()
        message(STATUS "Pictures directory not found at: ${FS_OVERLAY_DIR}/Pictures")
    endif()

    # Create Videos directory (empty by default)
    install(DIRECTORY DESTINATION "${DATA_INSTALL_DIR}/Videos")
    message(STATUS "Will create empty Videos directory")
endif()

# Post-install message with instructions
install(CODE "message(STATUS \"\")")
install(CODE "message(STATUS \"============================================\")")
install(CODE "message(STATUS \"Qt Apps Installation Complete\")")
install(CODE "message(STATUS \"============================================\")")
install(CODE "message(STATUS \"\")")
install(CODE "message(STATUS \"IMPORTANT: Configuration Path Updates\")")
install(CODE "message(STATUS \"\")")
install(CODE "message(STATUS \"The installed qt-demo-launcher.json contains default paths.\")")
install(CODE "message(STATUS \"To update paths for your installation:\")")
install(CODE "message(STATUS \"\")")
install(CODE "message(STATUS \"  cd ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/qt-apps\")")
install(CODE "message(STATUS \"  ./update-config-paths.sh\")")
install(CODE "message(STATUS \"\")")
install(CODE "message(STATUS \"Or manually edit qt-demo-launcher.json to match your setup.\")")
install(CODE "message(STATUS \"\")")
install(CODE "message(STATUS \"Installed files:\")")
install(CODE "message(STATUS \"  Binaries: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/\")")
install(CODE "message(STATUS \"  Utilities: ${CMAKE_INSTALL_PREFIX}/sbin/\")")
install(CODE "message(STATUS \"  Configs:  ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/qt-apps/\")")
if(INSTALL_SAMPLE_DATA)
    install(CODE "message(STATUS \"  Data:     ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/qt-apps/{Patterns,Pictures,Videos}/\")")
endif()
if(INSTALL_SYSTEMD_SERVICES)
    install(CODE "message(STATUS \"  Systemd:  ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/\")")
    install(CODE "message(STATUS \"  Udev:     ${CMAKE_INSTALL_PREFIX}/etc/udev/rules.d/\")")
    install(CODE "message(STATUS \"\")")
    install(CODE "message(STATUS \"Post-install steps (if needed):\")")
    install(CODE "message(STATUS \"  # Copy service file to system location:\")")
    install(CODE "message(STATUS \"  sudo cp ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/qt-demo-launcher.service /lib/systemd/system/\")")
    install(CODE "message(STATUS \"  sudo cp ${CMAKE_INSTALL_PREFIX}/etc/udev/rules.d/99-input-permissions.rules /etc/udev/rules.d/\")")
    install(CODE "message(STATUS \"  sudo systemctl daemon-reload\")")
    install(CODE "message(STATUS \"  sudo udevadm control --reload-rules && sudo udevadm trigger\")")
    install(CODE "message(STATUS \"  sudo systemctl enable qt-demo-launcher.service\")")
    install(CODE "message(STATUS \"  sudo systemctl start qt-demo-launcher.service\")")
endif()
install(CODE "message(STATUS \"\")")

# Summary
message(STATUS "")
message(STATUS "Configured packages:")
message(STATUS "  - disp-can-ctrl (daemon)")
message(STATUS "  - hh983-serializer (FPDLink initialization)")
if(Qt5_FOUND)
    message(STATUS "  - qt-demo-launcher (Qt Widgets)")
    message(STATUS "  - disp-tester (Qt Quick)")
    message(STATUS "  - disp-settings (Qt Quick)")
    message(STATUS "  - touch-gallery (Qt Quick)")
    message(STATUS "  - qt-mpv-wrapper (Qt Quick)")
endif()
message(STATUS "")
