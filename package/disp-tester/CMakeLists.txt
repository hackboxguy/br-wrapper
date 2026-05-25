cmake_minimum_required(VERSION 3.10)

project(disp-tester VERSION 1.0.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required Qt5 components
find_package(Qt5 REQUIRED COMPONENTS Core Qml Quick Network)

# Enable automoc, autorcc, autouic
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Source files
set(SOURCES
    src/main.cpp
    src/PatternController.cpp
    src/NetworkInterface.cpp
    src/FpgaController.cpp
)

set(HEADERS
    src/PatternController.h
    src/NetworkInterface.h
    src/PatternParameters.h
    src/FpgaController.h
)

# QML resource files
set(RESOURCES
    src/qml.qrc
)

# Create executable
add_executable(disp-tester ${SOURCES} ${HEADERS} ${RESOURCES})

# Link Qt5 libraries
target_link_libraries(disp-tester
    PRIVATE
        Qt5::Core
        Qt5::Qml
        Qt5::Quick
        Qt5::Network
)

# Include directories
target_include_directories(disp-tester
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Installation rules
include(GNUInstallDirs)

# Install binary
install(TARGETS disp-tester
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

install(PROGRAMS
    src/als-dimmer-sweep-child.py
    src/live-measurements-child.py
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

message(STATUS "disp-tester: configured")
message(STATUS "  Install binary to: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
