################################################################################
#
# xc3sprog
#
################################################################################

# Use master branch with proper git settings  
XC3SPROG_VERSION = origin/master
XC3SPROG_SITE = https://github.com/hackboxguy/xc3sprog.git
XC3SPROG_SITE_METHOD = git
XC3SPROG_GIT_SUBMODULES = NO
XC3SPROG_LICENSE = GPL-2.0+
XC3SPROG_LICENSE_FILES = COPYING
XC3SPROG_INSTALL_STAGING = NO
XC3SPROG_INSTALL_TARGET = YES
XC3SPROG_SUPPORTS_IN_SOURCE_BUILD = NO

# xc3sprog uses CMake
XC3SPROG_DEPENDENCIES = host-cmake

# Required dependencies - Default to legacy libftdi for compatibility
ifeq ($(BR2_PACKAGE_XC3SPROG_LIBFTDI1),y)
XC3SPROG_DEPENDENCIES += libftdi1
XC3SPROG_CONF_OPTS += -DUSE_FTD2XX=OFF
else
XC3SPROG_DEPENDENCIES += libftdi libusb libusb-compat
XC3SPROG_CONF_OPTS += -DUSE_FTD2XX=OFF
endif

# Optional dependencies
ifeq ($(BR2_PACKAGE_XC3SPROG_GPIOD_SUPPORT),y)
XC3SPROG_DEPENDENCIES += libgpiod
endif

ifeq ($(BR2_PACKAGE_XC3SPROG_FTD2XX_SUPPORT),y)
XC3SPROG_CONF_OPTS += -DUSE_FTD2XX=ON
# Note: FTD2XX is proprietary and not available in Buildroot by default
# Users need to provide libftd2xx manually if this option is enabled
endif

# CMake configuration - Force dynamic linking for better performance
XC3SPROG_CONF_OPTS += \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX=/usr \
	-DBUILD_SHARED_LIBS=ON \
	-DCMAKE_EXE_LINKER_FLAGS="-Wl,--as-needed" \
	-DCMAKE_SHARED_LINKER_FLAGS="-Wl,--as-needed"

# Install bscan bitstreams and wrapper script
define XC3SPROG_INSTALL_TARGET_CMDS
	$(MAKE) -C $(@D)/buildroot-build install DESTDIR=$(TARGET_DIR)
endef

# Create a post-install hook to set proper permissions
define XC3SPROG_PERMISSIONS
	/usr/bin/xc3sprog f 4755 0 0 - - - - -
	/usr/bin/fpga-jtag-flasher.sh f 755 0 0 - - - - -
	/usr/bin/detectchain f 755 0 0 - - - - -
	/usr/bin/readdna f 755 0 0 - - - - -
	/usr/bin/xc2c_warp f 755 0 0 - - - - -
endef

# Add udev rules for JTAG adapters (optional)
define XC3SPROG_INSTALL_UDEV_RULES
	$(INSTALL) -D -m 0644 $(XC3SPROG_PKGDIR)/99-xc3sprog.rules \
		$(TARGET_DIR)/etc/udev/rules.d/99-xc3sprog.rules
endef

ifeq ($(BR2_PACKAGE_HAS_UDEV),y)
XC3SPROG_POST_INSTALL_TARGET_HOOKS += XC3SPROG_INSTALL_UDEV_RULES
endif

$(eval $(cmake-package))
