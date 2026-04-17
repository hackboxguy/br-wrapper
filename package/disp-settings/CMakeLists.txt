cmake_minimum_required(VERSION 3.10)

project(disp-settings VERSION 1.0.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required Qt5 components
find_package(Qt5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Concurrent)

# Enable automoc for Qt meta-object compilation
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Source files
set(SOURCES
    src/main.cpp
    src/AlsDimmerController.cpp
    src/FpgaController.cpp
    src/TemperatureController.cpp
    src/TddiController.cpp
    src/McuController.cpp
    src/PmicController.cpp
)

set(HEADERS
    src/config.h
    src/AlsDimmerController.h
    src/FpgaController.h
    src/TemperatureController.h
    src/TddiController.h
    src/McuController.h
    src/PmicController.h
)

set(RESOURCES
    src/qml.qrc
)

# Create executable
add_executable(disp-settings ${SOURCES} ${HEADERS} ${RESOURCES})

# Link Qt5 libraries
target_link_libraries(disp-settings
    PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Qml
        Qt5::Quick
        Qt5::QuickControls2
        Qt5::Network
        Qt5::Concurrent
)

# Include directories
target_include_directories(disp-settings
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Installation rules
include(GNUInstallDirs)

# Install binary
install(TARGETS disp-settings
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# Install configuration files
set(DATA_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/qt-apps")

install(FILES
    src/disp-settings.json
    DESTINATION ${DATA_DIR}
)

message(STATUS "disp-settings: configured")
message(STATUS "  Install binary to: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
message(STATUS "  Install configs to: ${DATA_DIR}")
