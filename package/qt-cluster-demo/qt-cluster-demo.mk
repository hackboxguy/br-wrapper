################################################################################
#
# qt-cluster-demo
#
################################################################################

QT_CLUSTER_DEMO_VERSION = 1.0
QT_CLUSTER_DEMO_SITE_METHOD = local
QT_CLUSTER_DEMO_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/qt-cluster-demo/src
QT_CLUSTER_DEMO_DEPENDENCIES = qt5base qt5declarative

define QT_CLUSTER_DEMO_CONFIGURE_CMDS
	(cd $(@D) && $(TARGET_MAKE_ENV) $(HOST_DIR)/bin/cmake \
		-DCMAKE_INSTALL_PREFIX=/usr \
		-DCMAKE_BUILD_TYPE=Release \
		.)
endef

define QT_CLUSTER_DEMO_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

define QT_CLUSTER_DEMO_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(TARGET_DIR) install
endef

$(eval $(generic-package))
