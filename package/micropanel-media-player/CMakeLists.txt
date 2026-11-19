cmake_minimum_required(VERSION 3.10)

project(micropanel-media-player VERSION 1.0.0 LANGUAGES C)

include(GNUInstallDirs)

# dual-video-player: synced two-file / two-display video player (GStreamer V4L2
# hw decode, frames presented with libdrm atomic commits). Used by the launcher's
# Dual Video button (package/qt-demo-launcher/src/dual-video.sh). Optional: built
# only where the GStreamer (core + plugins-base) and libdrm dev packages are
# installed (+ json-glib for playlists; gstreamer-net-1.0 and gio, for --sync,
# come with the GStreamer core and GLib dev packages).
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(GST gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0
                      gstreamer-allocators-1.0 gstreamer-net-1.0 gio-2.0
                      libdrm json-glib-1.0)
endif()
if(GST_FOUND)
    add_executable(dual-video-player src/dual-video-player.c src/drm.c src/input.c
                                     src/presenter.c src/probe.c
                                     src/item.c src/playlist.c src/mirror.c
                                     src/sync.c src/control.c src/warp.c)
    target_include_directories(dual-video-player PRIVATE ${GST_INCLUDE_DIRS})
    target_link_libraries(dual-video-player PRIVATE ${GST_LIBRARIES} m)
    # 3x QVue: the player warps its own frames through libqvuewarp (br-wrapper
    # package qvue-warp, built first by the top-level CMakeLists) when the
    # warp setting is on; without the library warp.c is a stub
    if(TARGET qvuewarp)
        target_link_libraries(dual-video-player PRIVATE qvuewarp)
        target_compile_definitions(dual-video-player PRIVATE HAVE_QVUEWARP)
        message(STATUS "  dual-video-player: 3x QVue warp enabled (libqvuewarp)")
    endif()
    install(TARGETS dual-video-player
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    # 4500x298 QVue videos -> stacked 1500x894 (hardware-decodable, 60 fps warped)
    install(PROGRAMS src/qvue-stack-video.sh DESTINATION ${CMAKE_INSTALL_BINDIR})
    message(STATUS "  dual-video-player: enabled")
else()
    message(STATUS "  dual-video-player: disabled (gstreamer/libdrm dev packages not found)")
endif()
