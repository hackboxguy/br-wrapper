cmake_minimum_required(VERSION 3.10)

project(network-manager-app VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Qml Quick)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(network-manager-app
    src/main.cpp
    src/NetTool.cpp
    src/NetTool.h
    src/StatusController.cpp
    src/StatusController.h
    src/WifiController.cpp
    src/WifiController.h
    src/WiredController.cpp
    src/WiredController.h
    src/qml.qrc
)

target_link_libraries(network-manager-app
    PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Qml
        Qt5::Quick
)

target_include_directories(network-manager-app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Host tests (cmake -DBUILD_TESTS=ON; ctest)
option(BUILD_TESTS "Build the host unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_executable(test_parser tests/test_parser.cpp src/NetTool.cpp src/NetTool.h)
    target_include_directories(test_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_parser PRIVATE Qt5::Core)
    add_test(NAME parser COMMAND test_parser)
    add_test(NAME net-ctl COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_net_ctl.sh)
endif()

include(GNUInstallDirs)

install(TARGETS network-manager-app
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# The app's only link to NetworkManager (reads as the user, changes through sudo -n)
install(PROGRAMS src/net-ctl.sh src/net-dhcp-probe.py DESTINATION ${CMAKE_INSTALL_BINDIR})

message(STATUS "network-manager-app: configured")
message(STATUS "  Install binary + net-ctl.sh to: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
