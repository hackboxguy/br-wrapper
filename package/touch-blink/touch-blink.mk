#############################################################
#
## touch-blink
#
##############################################################
TOUCHBLINK_VERSION = 0.1
TOUCHBLINK_SITE_METHOD = local
TOUCHBLINK_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/sources/touch-blink
TOUCHBLINK_INSTALL_STAGING = NO
TOUCHBLINK_INSTALL_TARGET = YES
TOUCHBLINK_DEPENDENCIES = libevdev
TOUCHBLINK_CONF_OPTS=-DCMAKE_INSTALL_PREFIX="/usr"
$(eval $(cmake-package))
