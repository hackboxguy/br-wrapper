cmake_minimum_required(VERSION 3.10)

project(qt-mpv-wrapper VERSION 1.0.0 LANGUAGES C CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# C standard for input-monitor
set(CMAKE_C_STANDARD 99)

# Find required Qt5 components
find_package(Qt5 REQUIRED COMPONENTS Core Qml Quick Network)

# Enable automoc, autorcc, autouic
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt application source files
set(QT_SOURCES
    src/main.cpp
    src/VideoPlayer.cpp
    src/VideoController.cpp
    src/NetworkInterface.cpp
)

set(QT_HEADERS
    src/VideoPlayer.h
    src/VideoController.h
    src/NetworkInterface.h
    src/config.h
)

# QML resource files
set(RESOURCES
    src/qml.qrc
)

# Create Qt application executable
add_executable(qt-mpv-wrapper ${QT_SOURCES} ${QT_HEADERS} ${RESOURCES})

# Link Qt5 libraries
target_link_libraries(qt-mpv-wrapper
    PRIVATE
        Qt5::Core
        Qt5::Qml
        Qt5::Quick
        Qt5::Network
)

# Include directories for Qt app
target_include_directories(qt-mpv-wrapper
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Create input-monitor C program
add_executable(input-monitor src/input-monitor.c)

# Compiler flags for input-monitor (as per .pro file)
target_compile_options(input-monitor PRIVATE -Wall -O2)

# Installation rules
include(GNUInstallDirs)

# Install Qt application binary
install(TARGETS qt-mpv-wrapper
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# Install input-monitor binary
install(TARGETS input-monitor
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# Install shell script and config files
set(DATA_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/qt-apps")

install(PROGRAMS
    src/qt-mpv-wrapper.sh
    DESTINATION ${DATA_DIR}
)

install(FILES
    src/mpv-input.conf
    DESTINATION ${DATA_DIR}
)

message(STATUS "qt-mpv-wrapper: configured")
message(STATUS "  Install binaries to: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
message(STATUS "  Install scripts to: ${DATA_DIR}")
