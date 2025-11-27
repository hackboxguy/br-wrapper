################################################################################
#
# disp-settings
#
################################################################################

DISP_SETTINGS_VERSION = 1.0
DISP_SETTINGS_SITE_METHOD = local
DISP_SETTINGS_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/disp-settings/src
DISP_SETTINGS_DEPENDENCIES = qt5base qt5declarative qt5quickcontrols2

define DISP_SETTINGS_BUILD_CMDS
	cd $(@D) && $(TARGET_MAKE_ENV) $(HOST_DIR)/bin/qmake CONFIG+=release
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

define DISP_SETTINGS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/disp-settings $(TARGET_DIR)/usr/bin/disp-settings
	$(INSTALL) -D -m 0644 $(@D)/disp-settings.json $(TARGET_DIR)/usr/share/qt-apps/disp-settings.json
endef

$(eval $(generic-package))
