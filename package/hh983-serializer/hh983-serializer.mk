################################################################################
#
# hh983-serializer
#
################################################################################

HH983_SERIALIZER_VERSION = 1.0
#HH983_SERIALIZER_SITE = $(TOPDIR)/package/hh983-serializer/src
HH983_SERIALIZER_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/hh983-serializer/src
HH983_SERIALIZER_SITE_METHOD = local
HH983_SERIALIZER_LICENSE = GPL-2.0
HH983_SERIALIZER_DEPENDENCIES = linux host-dtc

define HH983_SERIALIZER_BUILD_CMDS
	$(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) M=$(@D) modules
	$(HOST_DIR)/bin/dtc -@ -I dts -O dtb -o $(@D)/hh983-serializer.dtbo \
		$(@D)/hh983-serializer-overlay.dts
endef

define HH983_SERIALIZER_INSTALL_TARGET_CMDS
	$(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) M=$(@D) modules_install
	$(INSTALL) -D -m 0644 $(@D)/hh983-serializer.dtbo \
		$(BINARIES_DIR)/rpi-firmware/overlays/hh983-serializer.dtbo
	$(INSTALL) -D -m 0755 $(@D)/scripts/re-init-983-pipeline.sh \
		$(TARGET_DIR)/usr/bin/re-init-983-pipeline.sh
	$(INSTALL) -D -m 0755 $(@D)/scripts/fpdlink-tool.sh \
		$(TARGET_DIR)/usr/bin/fpdlink-tool.sh
	$(INSTALL) -D -m 0755 $(@D)/scripts/toggle-pi-hdmi.sh \
		$(TARGET_DIR)/usr/bin/toggle-pi-hdmi.sh
endef

$(eval $(kernel-module))
$(eval $(generic-package))
