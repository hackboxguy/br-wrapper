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

define HH983_SERIALIZER_BUILD_CMDS
	$(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) M=$(@D) modules
endef

define HH983_SERIALIZER_INSTALL_TARGET_CMDS
	$(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) M=$(@D) modules_install
endef

$(eval $(kernel-module))
$(eval $(generic-package))
