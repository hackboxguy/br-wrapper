################################################################################
#
# als-dimmer
#
################################################################################

ALS_DIMMER_VERSION = main
ALS_DIMMER_SITE = https://github.com/hackboxguy/als-dimmer.git
ALS_DIMMER_SITE_METHOD = git
ALS_DIMMER_LICENSE = Proprietary
ALS_DIMMER_LICENSE_FILES =
ALS_DIMMER_DEPENDENCIES = host-pkgconf libddcutil

ALS_DIMMER_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX=/usr \
	-DCONFIG_FILE="config_fpga_opti4001_dimmer800.json" \
	-DUSE_DDCUTIL=ON \
	-DINSTALL_SYSTEMD_SERVICE=ON

# The OTS-OLED head unit needs a different als-dimmer config from the FPGA
# displays, and als-dimmer itself is deliberately display-agnostic (plan D6.2).
# The mapping therefore lives here: a launcher that asks pi-config-txt.sh which
# display config.txt was written for, and a drop-in that makes it the unit's
# ExecStart. Displays not named in the launcher keep the config they have
# always used, so this cannot change an existing image's behaviour.
define ALS_DIMMER_INSTALL_CONFIG_SELECTOR
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/als-dimmer/als-dimmer-select-config.sh \
		$(TARGET_DIR)/usr/bin/als-dimmer-select-config.sh
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/als-dimmer/10-select-config.conf \
		$(TARGET_DIR)/usr/lib/systemd/system/als-dimmer.service.d/10-select-config.conf
endef
ALS_DIMMER_POST_INSTALL_TARGET_HOOKS += ALS_DIMMER_INSTALL_CONFIG_SELECTOR

$(eval $(cmake-package))
