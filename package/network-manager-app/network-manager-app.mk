################################################################################
#
# network-manager-app
#
################################################################################

NETWORK_MANAGER_APP_VERSION = 0.2
NETWORK_MANAGER_APP_SITE_METHOD = local
NETWORK_MANAGER_APP_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/network-manager-app/src
NETWORK_MANAGER_APP_DEPENDENCIES = qt5base qt5declarative

define NETWORK_MANAGER_APP_BUILD_CMDS
	cd $(@D) && $(TARGET_MAKE_ENV) $(HOST_DIR)/bin/qmake CONFIG+=release
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

define NETWORK_MANAGER_APP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/network-manager-app $(TARGET_DIR)/usr/bin/network-manager-app
	$(INSTALL) -D -m 0755 $(@D)/net-ctl.sh $(TARGET_DIR)/usr/bin/net-ctl.sh
	$(INSTALL) -D -m 0755 $(@D)/net-dhcp-probe.py $(TARGET_DIR)/usr/bin/net-dhcp-probe.py
	$(INSTALL) -D -m 0755 $(@D)/net-badge.sh $(TARGET_DIR)/usr/bin/net-badge.sh
	sed 's|@NET_CTL@|/usr/bin/net-ctl.sh|' $(@D)/90-net-ctl-guard.in > $(@D)/90-net-ctl-guard
	$(INSTALL) -D -m 0755 $(@D)/90-net-ctl-guard $(TARGET_DIR)/etc/NetworkManager/dispatcher.d/90-net-ctl-guard
	mkdir -p $(TARGET_DIR)/etc/NetworkManager/dispatcher.d/pre-up.d
	ln -sf ../90-net-ctl-guard $(TARGET_DIR)/etc/NetworkManager/dispatcher.d/pre-up.d/90-net-ctl-guard
endef

$(eval $(generic-package))
