cmake_minimum_required(VERSION 3.10)

project(touch-tester VERSION 1.0.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(BUILD_QT_APP "Build Qt GUI application" ON)
option(INSTALL_SYSTEMD_SERVICES "Install systemd service files" ON)

# Find required dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GPIOD REQUIRED libgpiod>=1.6)

if(NOT GPIOD_FOUND)
    message(FATAL_ERROR "libgpiod not found. Please install libgpiod-dev (>= 1.6)")
endif()

# Qt5 dependency (optional)
if(BUILD_QT_APP)
    find_package(Qt5 COMPONENTS Core Gui Widgets Network)
    if(NOT Qt5_FOUND)
        message(WARNING "Qt5 not found. Disabling Qt GUI application.")
        message(WARNING "To build Qt app, install: qtbase5-dev")
        set(BUILD_QT_APP OFF)
    endif()
endif()

# Default installation prefix
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Default install prefix" FORCE)
endif()

message(STATUS "touch-tester configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  libgpiod: ${GPIOD_VERSION} (${GPIOD_LIBRARY_DIRS})")
message(STATUS "  Build Qt GUI: ${BUILD_QT_APP}")
message(STATUS "  Install systemd services: ${INSTALL_SYSTEMD_SERVICES}")

# Add source subdirectory
add_subdirectory(src)

# Add DSO scripts subdirectory (handles oscilloscope automation installation)
add_subdirectory(dso-scripts)

# Print installation summary
message(STATUS "")
message(STATUS "Installation paths:")
message(STATUS "  Binaries: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
message(STATUS "  Libraries: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
message(STATUS "  Headers: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/touch-test")
if(INSTALL_SYSTEMD_SERVICES)
    message(STATUS "  Systemd services: ${CMAKE_INSTALL_PREFIX}/lib/systemd/system")
endif()
