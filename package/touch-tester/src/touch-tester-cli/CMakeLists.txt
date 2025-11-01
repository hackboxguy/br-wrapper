# touch-tester-cli - Command-line tool

add_executable(touch-tester
    main.cpp
)

# Link with touch-test library
target_link_libraries(touch-tester
    PRIVATE
        touch-test
)

# Installation
include(GNUInstallDirs)

install(TARGETS touch-tester
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)
