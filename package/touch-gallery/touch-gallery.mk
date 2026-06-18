################################################################################
#
# touch-gallery
#
################################################################################

TOUCH_GALLERY_VERSION = 1.0
TOUCH_GALLERY_SITE_METHOD = local
TOUCH_GALLERY_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/touch-gallery/src
TOUCH_GALLERY_DEPENDENCIES = qt5base qt5declarative qt5quickcontrols2

define TOUCH_GALLERY_BUILD_CMDS
    cd $(@D) && $(TARGET_MAKE_ENV) $(HOST_DIR)/bin/qmake CONFIG+=release
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

define TOUCH_GALLERY_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/touch-gallery $(TARGET_DIR)/usr/bin/touch-gallery
    $(INSTALL) -D -m 0755 $(@D)/copy-image-to-usb.sh $(TARGET_DIR)/usr/bin/copy-image-to-usb.sh
endef

$(eval $(generic-package))
