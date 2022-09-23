################################################################################
# aws-iot-device-sdk-cpp-v2
# ################################################################################
AWS_IOT_DEVICE_SDK_CPP_V2_VERSION = v1.18.6
AWS_IOT_DEVICE_SDK_CPP_V2_SITE = https://github.com/aws/aws-iot-device-sdk-cpp-v2
AWS_IOT_DEVICE_SDK_CPP_V2_SITE_METHOD = git
AWS_IOT_DEVICE_SDK_CPP_V2_GIT_SUBMODULES = YES
AWS_IOT_DEVICE_SDK_CPP_V2_INSTALL_STAGING = YES
AWS_IOT_DEVICE_SDK_CPP_V2_INSTALL_TARGET = YES
#AWS_IOT_DEVICE_SDK_CPP_V2_CONF_OPT = -DBUILD_ONLY="aws-iot-sdk-cpp"
AWS_IOT_DEVICE_SDK_CPP_V2_CONF_OPTS = -DCMAKE_INSTALL_PREFIX="/usr"
AWS_IOT_DEVICE_SDK_CPP_V2_DEPENDENCIES = host-cmake libcurl openssl util-linux
AWS_IOT_DEVICE_SDK_CPP_V2_LICENSE = GPLv2
AWS_IOT_DEVICE_SDK_CPP_V2_LICENSE_FILES = LICENSE

#AWS_IOT_DEVICE_SDK_CPP_V2_PRE_CONFIGURE_HOOKS += AWS_IOT_DEVICE_SDK_CPP_CMAKE_MOVE_HOOK

#define AWS_IOT_DEVICE_SDK_CPP_V2_CMAKE_MOVE_HOOK
#    mkdir $(@D)/build
#endef

$(eval $(cmake-package))
