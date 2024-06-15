#############################################################
#
## touch-blink
#
##############################################################
TOUCHBLINK_VERSION = 0.0.1
TOUCHBLINK_SOURCE = $(TOUCHBLINK_VERSION).tar.gz
TOUCHBLINK_SITE = https://github.com/hackboxguy/touch-blink/archive
TOUCHBLINK_INSTALL_STAGING = NO
TOUCHBLINK_INSTALL_TARGET = YES
TOUCHBLINK_DEPENDENCIES = libevdev
TOUCHBLINK_CONF_OPTS=-DCMAKE_INSTALL_PREFIX="/usr"
$(eval $(cmake-package))
