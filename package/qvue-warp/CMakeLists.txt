cmake_minimum_required(VERSION 3.10)

project(qvue-warp VERSION 1.0.0 LANGUAGES C)

include(GNUInstallDirs)

# qvue-warpd: realtime warp of the 3x QVue 4500x298 HDMI output (fb0 sampled
# zero-copy by V3D, drawn through Catmull-Rom meshes into three overlay
# planes). libqvuewarp holds the parts the video player can reuse.
# Optional: built only where the libdrm, gbm, EGL and GLESv2 dev packages are
# installed (libdrm-dev libgbm-dev libegl-dev libgles-dev).
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(QVUEWARP_DEPS libdrm gbm egl glesv2)
endif()
if(NOT QVUEWARP_DEPS_FOUND)
    message(STATUS "  qvue-warpd: disabled (libdrm/gbm/egl/glesv2 dev packages not found)")
    return()
endif()

set(QVUEWARP_DATADIR "${CMAKE_INSTALL_FULL_DATADIR}/qvue-warp")
set(QVUEWARP_CONFIG_DEFAULT "/data/qvue-warp/warp.conf" CACHE STRING
    "qvue-warpd config file (persistent); falls back to <datadir>/qvue-warp/warp.conf")

add_library(qvuewarp STATIC lib/calib.c lib/mesh.c lib/trim.c lib/overlay.c lib/render.c lib/output.c
            lib/qw_config.c)
target_include_directories(qvuewarp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib ${QVUEWARP_DEPS_INCLUDE_DIRS})
target_link_libraries(qvuewarp PUBLIC ${QVUEWARP_DEPS_LIBRARIES} m)
target_compile_options(qvuewarp PRIVATE -Wall -Wextra)
# where the factory calibration and the default warp.conf are installed: the
# daemon and the video player read the same files
target_compile_definitions(qvuewarp PUBLIC QW_DATADIR="${QVUEWARP_DATADIR}")

add_executable(qvue-warpd src/qvue-warpd.c src/fb0src.c src/gridsrc.c src/persist.c)
target_link_libraries(qvue-warpd PRIVATE qvuewarp)
target_compile_options(qvue-warpd PRIVATE -Wall -Wextra)
target_compile_definitions(qvue-warpd PRIVATE QW_CONFIG_DEFAULT="${QVUEWARP_CONFIG_DEFAULT}")

# qvue-warpctl: plain C, no GL; used by the Stream Deck scripts and the launcher
add_executable(qvue-warpctl src/qvue-warpctl.c)
target_compile_options(qvue-warpctl PRIVATE -Wall -Wextra)

install(TARGETS qvue-warpd qvue-warpctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# factory calibration (recovered from the 3x QVue test pattern) and default config
install(FILES data/calib/3xqvue-panel1-21x9.txt data/calib/3xqvue-panel2-21x9.txt
              data/calib/3xqvue-panel3-21x9.txt data/warp.conf
        DESTINATION ${CMAKE_INSTALL_DATADIR}/qvue-warp)

if(INSTALL_SYSTEMD_SERVICES OR NOT DEFINED INSTALL_SYSTEMD_SERVICES)
    configure_file(data/qvue-warpd.service.in ${CMAKE_CURRENT_BINARY_DIR}/qvue-warpd.service @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qvue-warpd.service DESTINATION lib/systemd/system)
endif()
message(STATUS "  qvue-warpd: enabled (datadir ${QVUEWARP_DATADIR}, config ${QVUEWARP_CONFIG_DEFAULT})")
