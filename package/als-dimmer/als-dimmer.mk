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

$(eval $(cmake-package))
