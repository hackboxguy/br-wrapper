################################################################################
#
# qt-cluster-demo
#
################################################################################

QT_CLUSTER_DEMO_VERSION = 1.0
QT_CLUSTER_DEMO_SITE_METHOD = local
QT_CLUSTER_DEMO_SITE = $(BR2_EXTERNAL_BRWRAPPER_PATH)/package/qt-cluster-demo/src
QT_CLUSTER_DEMO_DEPENDENCIES = qt5base qt5declarative
QT_CLUSTER_DEMO_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX=/usr

$(eval $(cmake-package))
