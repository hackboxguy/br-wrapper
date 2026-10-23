################################################################################
#
# himax-touch-oledib
#
# Himax HX8530 (17" OLED-IB) touchscreen driver, Himax sample code A09.23,
# + DTS overlay for RPi4. Distinct module (himax_oledib) and compatible
# (himax,hxoledib) from himax-touch-oled, which the OTS-OLED keeps.
#
################################################################################

HIMAX_TOUCH_OLEDIB_VERSION = A09.23
HIMAX_TOUCH_OLEDIB_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/himax-touch-oledib/src
HIMAX_TOUCH_OLEDIB_SITE_METHOD = local
HIMAX_TOUCH_OLEDIB_LICENSE = GPL-2.0
HIMAX_TOUCH_OLEDIB_DEPENDENCIES = linux host-dtc

HIMAX_TOUCH_OLEDIB_DTS_DIR = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/himax-touch-oledib/dts

define HIMAX_TOUCH_OLEDIB_BUILD_CMDS
	$(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) M=$(@D) modules
	$(HOST_DIR)/bin/dtc -@ -I dts -O dtb -o $(@D)/himax-touch-oledib.dtbo \
		$(HIMAX_TOUCH_OLEDIB_DTS_DIR)/himax-touch-oledib-overlay.dts
endef

define HIMAX_TOUCH_OLEDIB_INSTALL_TARGET_CMDS
	$(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) M=$(@D) modules_install
	$(INSTALL) -D -m 0644 $(@D)/himax-touch-oledib.dtbo \
		$(BINARIES_DIR)/rpi-firmware/overlays/himax-touch-oledib.dtbo
endef

$(eval $(kernel-module))
$(eval $(generic-package))
