#############################################################
#
## preemptrt-gpiotest
#
##############################################################
PREEMPTRT_GPIOTEST_VERSION = 0.0.1
PREEMPTRT_GPIOTEST_SOURCE = $(PREEMPTRT_GPIOTEST_VERSION).tar.gz
PREEMPTRT_GPIOTEST_SITE = https://github.com/hackboxguy/preemptrt-gpiotest/archive
PREEMPTRT_GPIOTEST_INSTALL_STAGING = NO
PREEMPTRT_GPIOTEST_INSTALL_TARGET = YES
PREEMPTRT_GPIOTEST_CONF_OPTS=-DCMAKE_INSTALL_PREFIX="/usr"
$(eval $(cmake-package))
