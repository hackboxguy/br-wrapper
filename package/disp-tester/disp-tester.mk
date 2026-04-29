################################################################################
#
# disp-tester
#
################################################################################
DISP_TESTER_VERSION = 1.0
DISP_TESTER_SITE_METHOD = local
DISP_TESTER_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/disp-tester/src
DISP_TESTER_DEPENDENCIES = qt5base qt5declarative qt5quickcontrols2

define DISP_TESTER_BUILD_CMDS
    cd $(@D) && $(TARGET_MAKE_ENV) $(HOST_DIR)/bin/qmake CONFIG+=release
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

define DISP_TESTER_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/disp-tester $(TARGET_DIR)/usr/bin/disp-tester
    $(INSTALL) -D -m 0755 $(@D)/als-dimmer-sweep-child.py $(TARGET_DIR)/usr/bin/als-dimmer-sweep-child.py
endef

$(eval $(generic-package))
