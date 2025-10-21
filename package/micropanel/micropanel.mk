################################################################################
#
# micropanel
#
################################################################################
MICROPANEL_VERSION = main
MICROPANEL_SITE = https://github.com/hackboxguy/micropanel.git
MICROPANEL_SITE_METHOD = git
MICROPANEL_GIT_SUBMODULES = YES
MICROPANEL_INSTALL_STAGING = NO
MICROPANEL_INSTALL_TARGET = YES
MICROPANEL_DEPENDENCIES = i2c-tools libcurl json-for-modern-cpp
MICROPANEL_CONF_OPTS = \
	-DCMAKE_INSTALL_PREFIX="/" \
	-DINSTALL_SCREEN="config-pi-buildroot.json" \
	-DINSTALL_HELPER_SCRIPTS=ON \
	-DINSTALL_MEDIA_FILES=ON \
	-DINSTALL_ADDITIONAL_CONFIGS=ON \
	-DINSTALL_SYSTEMD_SERVICE=ON \
	-DSYSTEMD_UNITFILE_ARGS="-a -i gpio -s /dev/i2c-3"
$(eval $(cmake-package))
