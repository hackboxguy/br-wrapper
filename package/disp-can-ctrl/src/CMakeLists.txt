cmake_minimum_required(VERSION 3.10)

# Project configuration
set(COMPONENT_NAME disp-can-ctrl)
project(${COMPONENT_NAME} VERSION 1.0.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# Check for required system libraries
find_package(Threads REQUIRED)

# Server executable (daemon)
add_executable(disp-can-ctrl disp-can-ctrl.cpp)
target_link_libraries(disp-can-ctrl 
    PRIVATE 
    Threads::Threads
)

# Client executable
add_executable(disp-can-client disp-can-client.cpp)
target_link_libraries(disp-can-client 
    PRIVATE 
    Threads::Threads
)

# Include directories
target_include_directories(disp-can-ctrl 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(disp-can-client 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Installation rules for daemon
install(TARGETS disp-can-ctrl 
    RUNTIME DESTINATION sbin
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# Installation rules for client
install(TARGETS disp-can-client 
    RUNTIME DESTINATION sbin
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# Install startup script (commented out - handled by Buildroot)
# install(FILES S99DispCanCtrl
#     DESTINATION /etc/init.d
#     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
# )

# Install documentation
install(FILES readme.txt
    DESTINATION usr/share/doc/${COMPONENT_NAME}
    OPTIONAL
)

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")

# Cross-compilation helpers
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compiling for: ${CMAKE_SYSTEM_NAME}")
    message(STATUS "Target architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Development targets (only for host builds)
if(NOT CMAKE_CROSSCOMPILING)
    # Add custom target for cleaning build artifacts
    add_custom_target(clean-all
        COMMAND ${CMAKE_BUILD_TOOL} clean
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CMakeFiles
        COMMENT "Cleaning all build artifacts"
    )
    
    # Add custom target for rebuilding
    add_custom_target(rebuild
        COMMAND ${CMAKE_BUILD_TOOL} clean
        COMMAND ${CMAKE_BUILD_TOOL}
        COMMENT "Clean and rebuild"
    )
endif()

# Packaging support
include(CPack)
set(CPACK_PACKAGE_NAME ${COMPONENT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Display CAN Control daemon with client utility")
set(CPACK_PACKAGE_VENDOR "Your Company")
set(CPACK_PACKAGE_CONTACT "your-email@example.com")

#=============================================================================
# Build Instructions
#=============================================================================
# For cross-compiling with Buildroot:
# cmake -H. -Bbuild -DCMAKE_TOOLCHAIN_FILE=/path/to/buildroot/toolchain.cmake
# cmake --build build
# cmake --build build --target install
#
# For local development build:
# cmake -H. -Bbuild
# cmake --build build
# 
# For cleaning:
# rm -rf build
#
# Build both daemon and client:
# cmake --build build --target disp-can-ctrl
# cmake --build build --target disp-can-client
