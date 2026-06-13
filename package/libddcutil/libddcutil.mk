################################################################################
#
# libddcutil
#
################################################################################

LIBDDCUTIL_VERSION = v2.2.5
LIBDDCUTIL_SITE = https://github.com/rockowitz/ddcutil.git
LIBDDCUTIL_SITE_METHOD = git
LIBDDCUTIL_LICENSE = GPL-2.0+
LIBDDCUTIL_LICENSE_FILES = COPYING
LIBDDCUTIL_INSTALL_STAGING = YES
LIBDDCUTIL_INSTALL_TARGET = YES
LIBDDCUTIL_AUTORECONF = YES
LIBDDCUTIL_DEPENDENCIES = host-pkgconf libglib2 jansson udev

LIBDDCUTIL_CONF_OPTS = \
	--disable-x11 \
	--disable-drm \
	--disable-usb \
	--enable-lib

$(eval $(autotools-package))
