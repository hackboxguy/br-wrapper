################################################################################
#
# himax-touch-oled
#
# Himax HX8530 (OLED-OTS 17.3") touchscreen driver + DTS overlay for RPi4.
# Single-IC build; distinct module (himax_oled) and compatible (himax,hxoled)
# so it coexists with the multi-chip himax-touch package.
#
################################################################################

HIMAX_TOUCH_OLED_VERSION = 1.0
HIMAX_TOUCH_OLED_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/himax-touch-oled/src
HIMAX_TOUCH_OLED_SITE_METHOD = local
HIMAX_TOUCH_OLED_LICENSE = GPL-2.0
HIMAX_TOUCH_OLED_DEPENDENCIES = linux host-dtc

HIMAX_TOUCH_OLED_DTS_DIR = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/himax-touch-oled/dts

define HIMAX_TOUCH_OLED_BUILD_CMDS
	$(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) M=$(@D) modules
	$(HOST_DIR)/bin/dtc -@ -I dts -O dtb -o $(@D)/himax-touch-oled.dtbo \
		$(HIMAX_TOUCH_OLED_DTS_DIR)/himax-touch-oled-overlay.dts
endef

define HIMAX_TOUCH_OLED_INSTALL_TARGET_CMDS
	$(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) M=$(@D) modules_install
	$(INSTALL) -D -m 0644 $(@D)/himax-touch-oled.dtbo \
		$(BINARIES_DIR)/rpi-firmware/overlays/himax-touch-oled.dtbo
endef

$(eval $(kernel-module))
$(eval $(generic-package))
