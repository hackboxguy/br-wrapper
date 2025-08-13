################################################################################
#
# spotread - Extract working binary from Debian Bookworm (stable)
#
################################################################################

SPOTREAD_VERSION = 2.3.1+repack-1.1+b1
SPOTREAD_SITE = http://deb.debian.org/debian/pool/main/a/argyll
SPOTREAD_SOURCE = argyll_$(SPOTREAD_VERSION)_arm64.deb
SPOTREAD_LICENSE = AGPL-3.0
SPOTREAD_DEPENDENCIES = libusb xlib_libX11

# Skip hash checking for the Debian package
define SPOTREAD_EXTRACT_CMDS
	cd $(@D) && \
	ar x $(SPOTREAD_DL_DIR)/$(SPOTREAD_SOURCE) && \
	tar -xf data.tar.xz
endef

define SPOTREAD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/usr/bin/spotread $(TARGET_DIR)/usr/bin/spotread
	if [ -d "$(@D)/usr/share/argyll" ]; then \
		mkdir -p $(TARGET_DIR)/usr/share && \
		cp -r $(@D)/usr/share/argyll $(TARGET_DIR)/usr/share/; \
	fi
	$(INSTALL) -D -m 0644 $(SPOTREAD_PKGDIR)/99-colorimeters.rules \
		$(TARGET_DIR)/etc/udev/rules.d/99-colorimeters.rules
endef

$(eval $(generic-package))
