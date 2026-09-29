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
    src/SystemImageController.cpp
    src/SystemImageController.h
    src/SystemImageText.h
    src/FpgaController.cpp
    src/FpgaController.h
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

# Host tests (cmake -DBUILD_TESTS=ON; ctest)
option(BUILD_TESTS "Build the host unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_executable(test_outcome_text tests/test_outcome_text.cpp)
    target_include_directories(test_outcome_text PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_outcome_text PRIVATE Qt5::Core)
    add_test(NAME outcome-text COMMAND test_outcome_text)
endif()

include(GNUInstallDirs)

install(TARGETS system-manager-app
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# The launcher's System Manager badge (qt-demo-launcher "badge_command")
install(PROGRAMS src/system-update-check.sh DESTINATION ${CMAKE_INSTALL_BINDIR})
# The System image section's USB scanner (run with sudo -n; read-only)
install(PROGRAMS src/system-image-scan.sh DESTINATION ${CMAKE_INSTALL_BINDIR})

message(STATUS "system-manager-app: configured")
message(STATUS "  Install binary + badge check to: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
message(STATUS "  Expects update-iocs.sh (space6-architecture disptool) beside it")
