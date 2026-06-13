################################################################################
#
# himax-touch
#
# Himax TDDI touchscreen driver with DTS overlay for Raspberry Pi 4
#
################################################################################

HIMAX_TOUCH_VERSION = 1.0
HIMAX_TOUCH_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/himax-touch/src
HIMAX_TOUCH_SITE_METHOD = local
HIMAX_TOUCH_LICENSE = GPL-2.0
HIMAX_TOUCH_DEPENDENCIES = linux host-dtc

HIMAX_TOUCH_DTS_DIR = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/himax-touch/dts

# Build kernel module and compile DTS overlay
define HIMAX_TOUCH_BUILD_CMDS
	$(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) M=$(@D) modules
	$(HOST_DIR)/bin/dtc -@ -I dts -O dtb -o $(@D)/himax-touch.dtbo \
		$(HIMAX_TOUCH_DTS_DIR)/himax-touch-overlay.dts
endef

# Install kernel module and overlay
define HIMAX_TOUCH_INSTALL_TARGET_CMDS
	$(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) M=$(@D) modules_install
	$(INSTALL) -D -m 0644 $(@D)/himax-touch.dtbo \
		$(BINARIES_DIR)/rpi-firmware/overlays/himax-touch.dtbo
	# Install module load order configuration
	mkdir -p $(TARGET_DIR)/etc/modules-load.d
	echo "# Load HH983 serializer before Himax touch driver" > $(TARGET_DIR)/etc/modules-load.d/himax-touch.conf
	echo "hh983-serializer" >> $(TARGET_DIR)/etc/modules-load.d/himax-touch.conf
	echo "himax_mmi" >> $(TARGET_DIR)/etc/modules-load.d/himax-touch.conf
endef

$(eval $(kernel-module))
$(eval $(generic-package))
