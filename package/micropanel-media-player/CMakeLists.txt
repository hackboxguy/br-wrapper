cmake_minimum_required(VERSION 3.10)

project(micropanel-media-player VERSION 1.0.0 LANGUAGES C)

include(GNUInstallDirs)

# dual-video-player: synced two-file / two-display video player (GStreamer V4L2
# hw decode, frames presented with libdrm atomic commits). Used by the launcher's
# Dual Video button (package/qt-demo-launcher/src/dual-video.sh). Optional: built
# only where the GStreamer (core + plugins-base) and libdrm dev packages are
# installed (+ json-glib for playlists).
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(GST gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0
                      gstreamer-allocators-1.0 libdrm json-glib-1.0)
endif()
if(GST_FOUND)
    add_executable(dual-video-player src/dual-video-player.c src/drm.c src/input.c
                                     src/presenter.c src/probe.c
                                     src/item.c src/playlist.c src/mirror.c)
    target_include_directories(dual-video-player PRIVATE ${GST_INCLUDE_DIRS})
    target_link_libraries(dual-video-player PRIVATE ${GST_LIBRARIES} m)
    install(TARGETS dual-video-player
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    message(STATUS "  dual-video-player: enabled")
else()
    message(STATUS "  dual-video-player: disabled (gstreamer/libdrm dev packages not found)")
endif()
