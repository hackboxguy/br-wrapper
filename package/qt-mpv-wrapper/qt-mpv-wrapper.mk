################################################################################
#
# qt-mpv-wrapper
#
################################################################################
QT_MPV_WRAPPER_VERSION = 1.0
QT_MPV_WRAPPER_SITE_METHOD = local
QT_MPV_WRAPPER_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/qt-mpv-wrapper/src
QT_MPV_WRAPPER_DEPENDENCIES = qt5base qt5declarative qt5quickcontrols2

define QT_MPV_WRAPPER_BUILD_CMDS
    cd $(@D) && $(TARGET_MAKE_ENV) $(HOST_DIR)/bin/qmake CONFIG+=release
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

define QT_MPV_WRAPPER_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/qt-mpv-wrapper $(TARGET_DIR)/usr/bin/qt-mpv-wrapper
endef

$(eval $(generic-package))
