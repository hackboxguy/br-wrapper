cmake_minimum_required(VERSION 3.10)
project(qt-cluster-demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Qml Quick)

add_executable(qt-cluster-demo
    main.cpp
    CanReader.cpp
    ClusterModel.cpp
    DemoSimulator.cpp
    FpgaController.cpp
    qml.qrc
)

target_link_libraries(qt-cluster-demo
    Qt5::Core
    Qt5::Gui
    Qt5::Qml
    Qt5::Quick
)

include(GNUInstallDirs)
install(TARGETS qt-cluster-demo DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES cluster-launcher.sh
    DESTINATION ${CMAKE_INSTALL_DATADIR}/qt-apps
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
)
