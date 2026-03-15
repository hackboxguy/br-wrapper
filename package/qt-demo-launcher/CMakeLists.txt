cmake_minimum_required(VERSION 3.10)

project(qt-demo-launcher VERSION 1.0.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required Qt5 components
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)

# Enable automoc for Qt meta-object compilation
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Source files
set(SOURCES
    src/main.cpp
    src/NetworkInterface.cpp
)

set(HEADERS
    src/NetworkInterface.h
    src/config.h
)

# Create executable
add_executable(qt-demo-launcher ${SOURCES} ${HEADERS})

# Link Qt5 libraries
target_link_libraries(qt-demo-launcher
    PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Network
)

# Include directories
target_include_directories(qt-demo-launcher
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Workaround for NetworkInterface.moc issue (needed for non-local compile)
# Comment out #include "NetworkInterface.moc" from NetworkInterface.cpp
# This is handled by CMAKE_AUTOMOC
add_definitions(-DQT_NO_DEBUG_OUTPUT)

# Installation rules
include(GNUInstallDirs)

# Install binary
install(TARGETS qt-demo-launcher
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# Install configuration files and scripts
set(DATA_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/qt-apps")

install(FILES
    src/qt-demo-launcher.json
    DESTINATION ${DATA_DIR}
)

install(PROGRAMS
    src/kodi-launcher.sh
    update-config-paths.sh
    update-service-paths.sh
    DESTINATION ${DATA_DIR}
)

# Install systemd service and udev rules based on option
# Check if parent set INSTALL_SYSTEMD_SERVICES variable
if(NOT DEFINED INSTALL_SYSTEMD_SERVICES)
    set(INSTALL_SYSTEMD_SERVICES ON)
endif()

if(INSTALL_SYSTEMD_SERVICES)
    # Install to systemd location within install prefix
    install(FILES
        src/qt-demo-launcher.service
        DESTINATION lib/systemd/system
    )

    # Install udev rules within install prefix
    install(FILES
        99-input-permissions.rules
        DESTINATION etc/udev/rules.d
    )

    message(STATUS "  Will install systemd service to: ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/")
    message(STATUS "  Will install udev rules to: ${CMAKE_INSTALL_PREFIX}/etc/udev/rules.d/")
else()
    # Install to data directory for manual installation
    install(FILES
        src/qt-demo-launcher.service
        DESTINATION ${DATA_DIR}
    )

    install(FILES
        99-input-permissions.rules
        DESTINATION ${DATA_DIR}
    )

    message(STATUS "  Will install service/rules to: ${DATA_DIR} (manual installation required)")
endif()

# Automatically update qt-demo-launcher.json paths after installation
# Check if parent set AUTO_UPDATE_CONFIG_PATHS variable
if(NOT DEFINED AUTO_UPDATE_CONFIG_PATHS)
    set(AUTO_UPDATE_CONFIG_PATHS ON)
endif()

if(AUTO_UPDATE_CONFIG_PATHS)
    # Update qt-demo-launcher.json paths
    install(CODE "
        message(STATUS \"Updating qt-demo-launcher.json paths...\")
        execute_process(
            COMMAND ${DATA_DIR}/update-config-paths.sh ${CMAKE_INSTALL_PREFIX}
            WORKING_DIRECTORY ${DATA_DIR}
            RESULT_VARIABLE UPDATE_RESULT
            OUTPUT_VARIABLE UPDATE_OUTPUT
            ERROR_VARIABLE UPDATE_ERROR
        )
        if(UPDATE_RESULT EQUAL 0)
            message(STATUS \"✓ Config paths updated successfully\")
        else()
            message(WARNING \"Failed to update config paths: \${UPDATE_ERROR}\")
        endif()
    ")

    # Update qt-demo-launcher.service paths (if systemd services are installed)
    if(INSTALL_SYSTEMD_SERVICES)
        install(CODE "
            message(STATUS \"Updating qt-demo-launcher.service paths...\")
            execute_process(
                COMMAND ${DATA_DIR}/update-service-paths.sh ${CMAKE_INSTALL_PREFIX}
                WORKING_DIRECTORY ${CMAKE_INSTALL_PREFIX}/lib/systemd/system
                RESULT_VARIABLE UPDATE_SERVICE_RESULT
                OUTPUT_VARIABLE UPDATE_SERVICE_OUTPUT
                ERROR_VARIABLE UPDATE_SERVICE_ERROR
            )
            if(UPDATE_SERVICE_RESULT EQUAL 0)
                message(STATUS \"✓ Service file paths updated successfully\")
            else()
                message(WARNING \"Failed to update service file paths: \${UPDATE_SERVICE_ERROR}\")
            endif()
        ")
    endif()

    message(STATUS "  Will auto-update JSON paths to: ${CMAKE_INSTALL_PREFIX}")
    if(INSTALL_SYSTEMD_SERVICES)
        message(STATUS "  Will auto-update service file paths to: ${CMAKE_INSTALL_PREFIX}")
    endif()
endif()

message(STATUS "qt-demo-launcher: configured")
message(STATUS "  Install binary to: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
message(STATUS "  Install configs to: ${DATA_DIR}")
