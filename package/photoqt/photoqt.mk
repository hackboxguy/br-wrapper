################################################################################
#
# photoqt
#
################################################################################

PHOTOQT_VERSION = 3.4
PHOTOQT_SOURCE = photoqt-$(PHOTOQT_VERSION).tar.gz
PHOTOQT_SITE = https://photoqt.org/downloads/source
PHOTOQT_LICENSE = GPL-2.0+
PHOTOQT_LICENSE_FILES = COPYING
PHOTOQT_DEPENDENCIES = qt5base qt5declarative qt5multimedia qt5imageformats qt5svg qt5graphicaleffects qt5quickcontrols

# Override CMake to skip LinguistTools, ECM, but keep QML resources
define PHOTOQT_FIX_CMAKE
	sed -i 's/LinguistTools //g' $(@D)/CMakeLists.txt
	sed -i 's/find_package(ECM REQUIRED NO_MODULE)/#find_package(ECM REQUIRED NO_MODULE)/' $(@D)/CMakeLists.txt
	sed -i 's/set(CMAKE_MODULE_PATH \$${ECM_MODULE_PATH}/#set(CMAKE_MODULE_PATH \$${ECM_MODULE_PATH}/' $(@D)/CMakeLists.txt
	sed -i '/qt5_add_translation/s/^/#/' $(@D)/CMakeLists.txt
	sed -i '/add_custom_target(translations/s/^/#/' $(@D)/CMakeLists.txt
	sed -i '/composeLangResourceFile/s/^/#/' $(@D)/CMakeLists.txt
	sed -i '/include.*ComposeLangResourceFile/s/^/#/' $(@D)/CMakeLists.txt
	# Remove qm_files but keep photoqt_RESOURCES for QML files
	sed -i 's/\$${qm_files}//' $(@D)/CMakeLists.txt
	echo "** Fixed CMakeLists.txt to remove LinguistTools, ECM, and translations but keep QML resources"
	echo '<!DOCTYPE RCC><RCC version="1.0"></RCC>' > $(@D)/lang.qrc
	echo "** Created dummy lang.qrc file"
endef
PHOTOQT_POST_EXTRACT_HOOKS += PHOTOQT_FIX_CMAKE

PHOTOQT_CONF_OPTS = \
    -DCMAKE_BUILD_TYPE=Release \
    -DDEVIL=OFF \
    -DFREEIMAGE=OFF \
    -DMAGICK=OFF \
    -DIMAGEMAGICK=OFF \
    -DRAW=OFF \
    -DEXIV2=OFF \
    -DPOPPLER=OFF \
    -DLIBARCHIVE=OFF \
    -DPUGIXML=OFF \
    -DCHROMECAST=OFF \
    -DLIBVIPS=OFF \
    -DVIDEO_MPV=OFF \
    -DLOCATION=OFF \
    -DRESVG=OFF

$(eval $(cmake-package))
