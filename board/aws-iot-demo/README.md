# AWS-IoT-Demo Linux Image

This is a buildroot config for generating bootable linux images for various hw boards. Purpose of this custom-linux-image is to evaluate AWS IoT connectivity. All required aws-sdk and demo utils are included in this image. An outer layer br-wrapper repo is used as ```BR2_EXTERN``` for buildroot. Final output of this buildroot config is a linux image which can be directly written on sdcard using Balena-Etcher or dd or other ISO image writing utils.

## Maintainer
	Albert David (albert.david@gmail.com)

## Build steps for creating sdcard image for raspberry-pi-4 hw
    mkdir -o ~/aws-iot-demo/
    cd ~/aws-iot-demo/
    git clone --recursive https://github.com/hackboxguy/br-wrapper.git
    cd br-wrapper
    make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output raspberrypi4_aws_iot_defconfig
    make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output
    ls -lah ~/aws-iot-demo/br-output/images/sdcard.img #this image can written to sdcard using dd

## Customization done on top of default buildroot config
1. Enabled dropbear for remote debugging without monitor/keyboard
2. Forked [basic_pub_sub](https://github.com/aws/aws-iot-device-sdk-cpp-v2/tree/main/samples/pub_sub/basic_pub_sub) from aws-iot-device-sdk-cpp-v2 as [aws-iot-pubsub-agent](https://github.com/hackboxguy/aws-iot-pubsub-agent)
3. Modified the cmake and main.cpp of aws-iot-pubsub-agent so that it can build as a separate package in buildroot with aws-iot-device-sdk-cpp-v2 as a dependency
4. Added buildroot package for [aws-iot-device-sdk-cpp-v2](https://github.com/hackboxguy/br-wrapper/tree/main/package/aws-iot-device-sdk-cpp-v2)
5. Added buildroot package for [aws-iot-pubsub-agent](https://github.com/hackboxguy/br-wrapper/tree/main/package/aws-iot-pubsub-agent)
6. Enabled chrony daemon for time sync(required for tls-handshake)
7. Enabled aws-iot-device-sdk-cpp-v2 dependencies(host-cmake/libcur/openssl/util-linux)
8. Included [AmazonRootCA1.pem](https://github.com/hackboxguy/br-wrapper/blob/main/board/aws-iot-demo/fs-overlay/etc/AmazonRootCA1.pem) in the rootfs(/etc/)
9. Added [aws-iot-pubsub-agent.conf](https://github.com/hackboxguy/br-wrapper/blob/main/board/aws-iot-demo/fs-overlay/etc/aws-iot-pubsub-agent.conf) in boot-partition which is required by the agent in startup script.
10. During linux boot, added /etc/init.d/S03MountBoot startup scirpt for mounting boot partition in /mnt/certs, this partition will have required certificate, key, and aws-iot-pubsub-agent.conf files
11. Included startup script [/etc/init.d/S99AwsPubSubDemo](https://github.com/hackboxguy/br-wrapper/blob/main/board/aws-iot-demo/fs-overlay/etc/init.d/S99AwsPubSubDemo) so that aws-iot-pubsub-agent starts publishing configured messages
