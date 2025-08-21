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

# Source files
set(SOURCES
    disp-can-ctrl.cpp
)

# Executable target
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
    Threads::Threads
)

# Include directories (if needed in future)
target_include_directories(${PROJECT_NAME} 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Installation rules
install(TARGETS ${COMPONENT_NAME} 
    RUNTIME DESTINATION sbin  # Will install to ${CMAKE_INSTALL_PREFIX}/sbin = /usr/sbin
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# Install startup script for Buildroot/embedded systems (absolute path to avoid CMAKE_INSTALL_PREFIX)
install(FILES S99DispCanCtrl
    DESTINATION /etc/init.d
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

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

# Packaging support (for creating tarballs, etc.)
include(CPack)
set(CPACK_PACKAGE_NAME ${COMPONENT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Display CAN Control daemon")
set(CPACK_PACKAGE_VENDOR "Your Company")
set(CPACK_PACKAGE_CONTACT "your-email@example.com")

#=============================================================================
# Build Instructions (kept as comments for reference)
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
# For verbose build:
# cmake --build build --verbose
#
# For parallel build:
# cmake --build build -- -j$(nproc)
