################################################################################
#
# touch-tester
#
################################################################################
TOUCH_TESTER_VERSION = 1.0.0
TOUCH_TESTER_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/touch-tester
TOUCH_TESTER_SITE_METHOD = local
TOUCH_TESTER_INSTALL_STAGING = YES

# Required dependencies
TOUCH_TESTER_DEPENDENCIES = libgpiod

# Qt5 support - PHASE 2 (not yet implemented)
# TODO: Enable when qt-touch-tester is implemented
# For now, always disable Qt GUI regardless of BR2_PACKAGE_QT5
TOUCH_TESTER_CONF_OPTS += -DBUILD_QT_APP=OFF

# Uncomment these lines when Phase 2 (Qt GUI) is ready:
#ifeq ($(BR2_PACKAGE_QT5),y)
#TOUCH_TESTER_DEPENDENCIES += qt5base
#TOUCH_TESTER_CONF_OPTS += -DBUILD_QT_APP=ON
#else
#TOUCH_TESTER_CONF_OPTS += -DBUILD_QT_APP=OFF
#endif

# Systemd integration - PHASE 2 (not yet implemented)
# TODO: Enable when qt-touch-tester service file exists
# For now, always disable systemd service installation
TOUCH_TESTER_CONF_OPTS += -DINSTALL_SYSTEMD_SERVICES=OFF

# Uncomment when Phase 2 (Qt GUI + systemd service) is ready:
#ifeq ($(BR2_PACKAGE_SYSTEMD),y)
#TOUCH_TESTER_CONF_OPTS += -DINSTALL_SYSTEMD_SERVICES=ON
#define TOUCH_TESTER_INSTALL_INIT_SYSTEMD
#	$(INSTALL) -D -m 0644 $(@D)/src/qt-touch-tester/qt-touch-tester.service \
#		$(TARGET_DIR)/lib/systemd/system/qt-touch-tester.service
#endef
#else
#TOUCH_TESTER_CONF_OPTS += -DINSTALL_SYSTEMD_SERVICES=OFF
#endif

# GPIO and input device permissions via udev rules
define TOUCH_TESTER_INSTALL_UDEV_RULES
	$(INSTALL) -D -m 0644 $(TOUCH_TESTER_PKGDIR)/99-gpio-input.rules \
		$(TARGET_DIR)/etc/udev/rules.d/99-gpio-input.rules
endef

TOUCH_TESTER_POST_INSTALL_TARGET_HOOKS += TOUCH_TESTER_INSTALL_UDEV_RULES

$(eval $(cmake-package))
