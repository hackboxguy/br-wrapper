cmake_minimum_required(VERSION 3.10)

project(usb-media-app VERSION 1.0.0 LANGUAGES CXX)

# USB Media: playlist editor for the images and videos on a USB stick.
# Playback is dual-video-player --playlist (package/micropanel-media-player),
# started by qt-demo-launcher's usb-media.sh.

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Qml Quick)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(usb-media-app
    src/main.cpp
    src/MediaController.cpp
    src/MediaController.h
    src/qml.qrc
)

target_link_libraries(usb-media-app
    PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Qml
        Qt5::Quick
)

target_include_directories(usb-media-app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

include(GNUInstallDirs)

install(TARGETS usb-media-app
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

message(STATUS "usb-media-app: configured")
