#############################################################
#
## xilinx test tools
#
##############################################################
XILINX_TEST_TOOLS_VERSION = 0.0.1
XILINX_TEST_TOOLS_SOURCE = $(XILINX_TEST_TOOLS_VERSION).tar.gz
XILINX_TEST_TOOLS_SITE = https://github.com/hackboxguy/xilinx-test-tools/archive
XILINX_TEST_TOOLS_INSTALL_STAGING = NO
XILINX_TEST_TOOLS_INSTALL_TARGET = YES
XILINX_TEST_TOOLS_CONF_OPTS=-DCMAKE_INSTALL_PREFIX="/usr"
$(eval $(cmake-package))
