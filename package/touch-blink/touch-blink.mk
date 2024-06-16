#############################################################
#
## touch-blink
#
##############################################################
TOUCH_BLINK_VERSION = 0.0.3
TOUCH_BLINK_SOURCE = $(TOUCH_BLINK_VERSION).tar.gz
TOUCH_BLINK_SITE = https://github.com/hackboxguy/touch-blink/archive
TOUCH_BLINK_INSTALL_STAGING = NO
TOUCH_BLINK_INSTALL_TARGET = YES
TOUCH_BLINK_DEPENDENCIES = libevdev
TOUCH_BLINK_CONF_OPTS=-DCMAKE_INSTALL_PREFIX="/"
$(eval $(cmake-package))
