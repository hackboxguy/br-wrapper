################################################################################
#
# system-manager-app
#
################################################################################

SYSTEM_MANAGER_APP_VERSION = 1.0
SYSTEM_MANAGER_APP_SITE_METHOD = local
SYSTEM_MANAGER_APP_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/system-manager-app/src
SYSTEM_MANAGER_APP_DEPENDENCIES = qt5base qt5declarative

define SYSTEM_MANAGER_APP_BUILD_CMDS
	cd $(@D) && $(TARGET_MAKE_ENV) $(HOST_DIR)/bin/qmake CONFIG+=release
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

define SYSTEM_MANAGER_APP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/system-manager-app $(TARGET_DIR)/usr/bin/system-manager-app
	$(INSTALL) -D -m 0755 $(@D)/system-update-check.sh $(TARGET_DIR)/usr/bin/system-update-check.sh
endef

$(eval $(generic-package))
