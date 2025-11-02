cmake_minimum_required(VERSION 3.10)

# DSO Scripts Installation
# This CMakeLists.txt handles installation of Rigol oscilloscope automation scripts
# and configuration files for touch latency measurement

# Installation directories
set(DSO_SCRIPTS_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/share/touch-tester/dso-scripts")
set(DSO_CONFIGS_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/share/touch-tester/dso-configs")

# Install main automation script to /usr/bin (in PATH)
install(PROGRAMS
    automated-latency-test.sh
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)

# Install helper DSO scripts to /usr/share (library scripts)
install(PROGRAMS
    rigol-tool.sh
    rigol-scpi.sh
    DESTINATION ${DSO_SCRIPTS_INSTALL_DIR}
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
message(STATUS "  Main script:     ${CMAKE_INSTALL_PREFIX}/bin/automated-latency-test.sh")
message(STATUS "  Helper scripts:  ${DSO_SCRIPTS_INSTALL_DIR}")
message(STATUS "  Config files:    ${DSO_CONFIGS_INSTALL_DIR}")
