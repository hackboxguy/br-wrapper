cmake_minimum_required(VERSION 3.10)

# DSO Scripts Installation
# This CMakeLists.txt handles installation of Rigol oscilloscope automation scripts
# and configuration files for touch latency measurement

# Installation directories
set(DSO_SCRIPTS_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/share/touch-tester/dso-scripts")
set(DSO_CONFIGS_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/share/touch-tester/dso-configs")

# Install DSO automation scripts
install(PROGRAMS
    rigol-tool.sh
    rigol-scpi.sh
    automated-latency-test.sh
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
message(STATUS "  Scripts install to: ${DSO_SCRIPTS_INSTALL_DIR}")
message(STATUS "  Configs install to: ${DSO_CONFIGS_INSTALL_DIR}")
