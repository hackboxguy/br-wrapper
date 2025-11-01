# Source directory CMakeLists.txt

# Always build core library and CLI tool
add_subdirectory(lib-touch-test)
add_subdirectory(touch-tester-cli)

# Optionally build Qt GUI application
if(BUILD_QT_APP)
    add_subdirectory(qt-touch-tester)
endif()
