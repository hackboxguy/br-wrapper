cmake_minimum_required(VERSION 3.10)

# DSO Scripts Installation
# This CMakeLists.txt handles installation of Rigol oscilloscope automation scripts
# and configuration files for touch latency measurement

# Installation directories
set(DSO_CONFIGS_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/share/touch-tester/dso-configs")

# Install all DSO scripts to /usr/bin (in PATH)
install(PROGRAMS
    automated-latency-test.sh
    rigol-tool.sh
    rigol-scpi.sh
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)

# Install DSO configuration files
# Preserves directory structure: dso-configs/touch-tester/*.json
install(DIRECTORY
    dso-configs/
    DESTINATION ${DSO_CONFIGS_INSTALL_DIR}
    FILES_MATCHING PATTERN "*.json" PATTERN "*.md"
)

# Print installation paths
message(STATUS "DSO Scripts Configuration:")
message(STATUS "  Scripts:         ${CMAKE_INSTALL_PREFIX}/bin/")
message(STATUS "  Config files:    ${DSO_CONFIGS_INSTALL_DIR}")
