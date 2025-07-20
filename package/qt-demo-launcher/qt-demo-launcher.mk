################################################################################
#
# qt-demo-launcher
#
################################################################################

QT_DEMO_LAUNCHER_VERSION = 1.0
QT_DEMO_LAUNCHER_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/qt-demo-launcher/src
QT_DEMO_LAUNCHER_SITE_METHOD = local
QT_DEMO_LAUNCHER_DEPENDENCIES = qt5base
QT_DEMO_LAUNCHER_CONF_OPTS = -DQT_HOST_PATH=$(HOST_DIR)

define QT_DEMO_LAUNCHER_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CXX="$(TARGET_CXX)" \
		CXXFLAGS="$(TARGET_CXXFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		STAGING_DIR="$(STAGING_DIR)" \
		HOST_DIR="$(HOST_DIR)"
endef

define QT_DEMO_LAUNCHER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 755 $(@D)/qt_demo_launcher $(TARGET_DIR)/usr/bin/qt_demo_launcher
	$(INSTALL) -D -m 755 $(@D)/S99qt-launcher $(TARGET_DIR)/etc/init.d/S99qt-launcher
endef

$(eval $(generic-package))
