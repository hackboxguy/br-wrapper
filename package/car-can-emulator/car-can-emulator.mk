################################################################################
#
# car-can-emulator
#
################################################################################

CAR_CAN_EMULATOR_VERSION = v2-improvements
CAR_CAN_EMULATOR_SITE = https://github.com/hackboxguy/car-can-emulator.git
CAR_CAN_EMULATOR_SITE_METHOD = git
CAR_CAN_EMULATOR_LICENSE = MIT
CAR_CAN_EMULATOR_INSTALL_TARGET = YES

CAR_CAN_EMULATOR_CONF_OPTS = -DPLATFORM=systemd

ifeq ($(BR2_PACKAGE_CAR_CAN_EMULATOR_SYSTEMD),y)
CAR_CAN_EMULATOR_CONF_OPTS += -DPLATFORM=systemd
else
CAR_CAN_EMULATOR_CONF_OPTS += -DPLATFORM=local
endif

define CAR_CAN_EMULATOR_INSTALL_INIT_SYSTEMD
	$(INSTALL) -D -m 644 $(@D)/deploy/systemd/car-can-emulator.service \
		$(TARGET_DIR)/usr/lib/systemd/system/car-can-emulator.service
endef

define CAR_CAN_EMULATOR_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 755 $(@D)/car-can-emulator \
		$(TARGET_DIR)/usr/sbin/car-can-emulator
	$(INSTALL) -D -m 644 $(@D)/config/car-can-emulator.conf \
		$(TARGET_DIR)/etc/car-can-emulator.conf
endef

$(eval $(cmake-package))
