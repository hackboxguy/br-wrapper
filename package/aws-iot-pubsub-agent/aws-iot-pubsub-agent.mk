################################################################################
# aws-iot-pubsub-agent
# ################################################################################
AWS_IOT_PUBSUB_AGENT_VERSION = v1.2
AWS_IOT_PUBSUB_AGENT_SOURCE = $(AWS_IOT_PUBSUB_AGENT_VERSION).tar.gz
AWS_IOT_PUBSUB_AGENT_SITE = https://github.com/hackboxguy/aws-iot-pubsub-agent/archive
#AWS_IOT_PUBSUB_AGENT_SITE_METHOD = git
AWS_IOT_PUBSUB_AGENT_INSTALL_STAGING = NO
AWS_IOT_PUBSUB_AGENT_INSTALL_TARGET = YES
AWS_IOT_PUBSUB_AGENT_CONF_OPTS = -DCMAKE_INSTALL_PREFIX="/usr"
AWS_IOT_PUBSUB_AGENT_DEPENDENCIES = aws-iot-device-sdk-cpp-v2
#AWS_IOT_PUBSUB_AGENT_LICENSE = GPLv2
#AWS_IOT_PUBSUB_AGENT_LICENSE_FILES = LICENSE
$(eval $(cmake-package))
