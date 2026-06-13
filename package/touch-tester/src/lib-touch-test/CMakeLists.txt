# lib-touch-test - Core touch testing library

set(LIB_SOURCES
    gpio_controller.cpp
    touch_reader.cpp
    latency_measurer.cpp
    statistics.cpp
)

set(LIB_HEADERS
    gpio_controller.h
    touch_reader.h
    latency_measurer.h
    statistics.h
)

# Create shared library
add_library(touch-test SHARED ${LIB_SOURCES} ${LIB_HEADERS})

# Link with libgpiod
target_link_libraries(touch-test
    PUBLIC
        ${GPIOD_LIBRARIES}
)

# Include directories
target_include_directories(touch-test
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GPIOD_INCLUDE_DIRS}
)

# Set library properties
set_target_properties(touch-test PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER "${LIB_HEADERS}"
)

# Installation
include(GNUInstallDirs)

install(TARGETS touch-test
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/touch-test
)
