################################################################################
#
# disp-can-ctrl
#
################################################################################

DISP_CAN_CTRL_VERSION = 1.0.0
DISP_CAN_CTRL_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/disp-can-ctrl/src
DISP_CAN_CTRL_SITE_METHOD = local
DISP_CAN_CTRL_LICENSE = Proprietary
DISP_CAN_CTRL_LICENSE_FILES = 
DISP_CAN_CTRL_DEPENDENCIES = 

DISP_CAN_CTRL_CONF_OPTS = \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

define DISP_CAN_CTRL_CONFIGURE_CMDS
    (cd $(@D) && \
        $(TARGET_CONFIGURE_OPTS) \
        $(HOST_DIR)/bin/cmake \
        -DCMAKE_TOOLCHAIN_FILE="$(HOST_DIR)/share/buildroot/toolchainfile.cmake" \
        -DCMAKE_INSTALL_PREFIX="/usr" \
        -DCMAKE_BUILD_TYPE=Release \
        .)
endef

define DISP_CAN_CTRL_BUILD_CMDS
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

define DISP_CAN_CTRL_INSTALL_TARGET_CMDS
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(TARGET_DIR) install
endef

# Post-install: ensure startup script is executable
define DISP_CAN_CTRL_INSTALL_INIT_SYSTEMD
    # No systemd service for this package
endef

define DISP_CAN_CTRL_INSTALL_INIT_SYSV
    $(INSTALL) -D -m 0755 $(@D)/S99DispCanCtrl \
        $(TARGET_DIR)/etc/init.d/S99DispCanCtrl
endef

$(eval $(generic-package))
