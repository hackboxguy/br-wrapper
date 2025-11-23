# HH983 FPDLink Serializer Initialization Script
cmake_minimum_required(VERSION 3.10)

# Installation directory for scripts
set(SCRIPT_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")

# Install the initialization script
install(PROGRAMS
    init-hh983-fpdlink.sh
    DESTINATION ${SCRIPT_INSTALL_DIR}
)

# Install documentation
install(FILES
    REGISTER-MAP.md
    README-INIT-SCRIPT.md
    DESTINATION "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DOCDIR}/hh983-serializer"
)

message(STATUS "hh983-serializer: configured")
message(STATUS "  Install script to: ${SCRIPT_INSTALL_DIR}")
message(STATUS "  Install docs to: ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DOCDIR}/hh983-serializer")
