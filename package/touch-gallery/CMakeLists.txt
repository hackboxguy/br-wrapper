cmake_minimum_required(VERSION 3.10)

project(touch-gallery VERSION 1.0.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required Qt5 components
find_package(Qt5 REQUIRED COMPONENTS Core Qml Quick)

# Enable automoc, autorcc, autouic
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Source files
set(SOURCES
    src/main.cpp
    src/NetworkInterface.cpp
    src/GalleryController.cpp
)

# Header files
set(HEADERS
    src/NetworkInterface.h
    src/GalleryController.h
)

# QML resource files
set(RESOURCES
    src/qml.qrc
)

# Create executable
add_executable(touch-gallery ${SOURCES} ${HEADERS} ${RESOURCES})

# Link Qt5 libraries
target_link_libraries(touch-gallery
    PRIVATE
        Qt5::Core
        Qt5::Qml
        Qt5::Quick
)

# Include directories
target_include_directories(touch-gallery
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Installation rules
include(GNUInstallDirs)

# Install binary
install(TARGETS touch-gallery
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

message(STATUS "touch-gallery: configured")
message(STATUS "  Install binary to: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
