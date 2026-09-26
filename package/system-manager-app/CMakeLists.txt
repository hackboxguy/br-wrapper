cmake_minimum_required(VERSION 3.10)

project(system-manager-app VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Qml Quick)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(system-manager-app
    src/main.cpp
    src/UpdateController.cpp
    src/UpdateController.h
    src/qml.qrc
)

target_link_libraries(system-manager-app
    PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Qml
        Qt5::Quick
)

target_include_directories(system-manager-app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

include(GNUInstallDirs)

install(TARGETS system-manager-app
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# The launcher's System Manager badge (qt-demo-launcher "badge_command")
install(PROGRAMS src/system-update-check.sh DESTINATION ${CMAKE_INSTALL_BINDIR})

message(STATUS "system-manager-app: configured")
message(STATUS "  Install binary + badge check to: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
message(STATUS "  Expects update-iocs.sh (space6-architecture disptool) beside it")
