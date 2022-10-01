# AWS-IoT-Demo Linux Image

This is a buildroot config for generating bootable linux images for various hw boards. Purpose of this custom-linux-image is to evaluate AWS IoT connectivity. All required aws-sdk and demo utils are included in this image. An outer layer br-wrapper repo is used as ```BR2_EXTERN``` for buildroot. Final output of this buildroot config is a linux image which can be directly written on sdcard using Balena-Etcher or dd or other ISO image writing utils.

## Maintainer
	Albert David (albert.david@gmail.com)

## Build steps for creating AWS-IoT-Demo sdcard image for raspberry-pi-4 hw
    mkdir -p ~/aws-iot-demo/
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

## Configurable parameters of aws-iot-pubsub-agent utility(/etc/aws-iot-pubsub-agent.conf)
1. ```endpoint: replace.this.with.your.endpoint``` Endpoint specific to your aws-iot-core accoun: this parameter is a must
2. ```verbosefile: /tmp/aws-iot-pubsub-agent.log``` An optional parameter to specifiy the logfile path of aws-iot-pubsub-agent utility
3. ```verbosity: Info``` An optional parameter to specifiy the verbosity of aws-iot-pubsub-agent utility [Trace/Debug/Info/Warn/Error/Fatal/None]
4. ```publish-interval-sec: 5``` An optional parameter to specifiy the interval between two mqtt publish messages(default is 1sec)
5. ```total-publish-count: 5``` An optional parameter to specifiy total number of mqtt publish messages (default is 10)
6. ```publish-message: hello-world``` An optional parameter to specifiy mqtt publish message (default is Hello World)
7. ```publish-topic: test/topic``` An optional parameter to specifiy mqtt publish topice (default is test/topic)
8. ```clientid: test-1``` An optional parameter to specifiy the clientid which is set in AWS-IoT-Policies on the cloud (iot:Connect => arn:iot-useast-1:usr_id:client/test-*)

## How to compile and run modified aws-iot-pubsub-agent on Target Hardware?
1. ```wget https://github.com/hackboxguy/lfs-downloads/raw/main/aws-iot-demo-toolchain/arm-buildroot-linux-uclibcgnueabihf_sdk-buildroot.tar.gz -O ~/arm-buildroot-linux-uclibcgnueabihf_sdk-buildroot.tar.gz``` Download Cross-Toolchain of AWS-IoT-Demo Image for Raspi-4 Hw
2. ```tar -xvf ~/arm-buildroot-linux-uclibcgnueabihf_sdk-buildroot.tar.gz -C ~/``` untar downloaded Cross-Toolchain to HOME directory
3. ```~/arm-buildroot-linux-uclibcgnueabihf_sdk-buildroot/relocate-sdk.sh``` Relocate the toolchain path to this specific installation
4. ```git clone https://github.com/hackboxguy/aws-iot-pubsub-agent.git``` Clone the source-code of aws-iot-pubsub-agent
5. ```cd aws-iot-pubsub-agent``` cd to cloned souce directory
6. Modify the code of aws-iot-pubsub-agent
7. ```cmake -H. -BOutput -DCMAKE_INSTALL_PREFIX=~/aws-output/ -DBUILD_SHARED_LIBS=ON -DCMAKE_TOOLCHAIN_FILE=~/aws-iot-pubsub-agent/cmake/arm-buildroot-linux-uclibcgnueabihf.cmake``` configure with cmake
8. ```cmake --build Output -- install -j$(nproc)``` build with cmake(binary will be installed to ~/aws-output/sbin/)
9. ```scp ~/aws-output/sbin/aws-iot-pubsub-agent root@aws-iot-demo-target:/usr/sbin/``` using scp, copy the modified binary to the Raspi-4 hw
10. ```ssh root@aws-iot-demo-target``` using ssh, login to Raspi-4 hw (root pw is: brb0x)
11. ```/usr/sbin/aws-iot-pubsub-agent --help``` run modified aws-iot-pubsub-agent on Raspi-4 hw

## Future extensions/improvements(TODO-List)
1. Make root pw writable so that user can override default pw with custom pw
2. Add A/B upgrade mechanism using swupdate so that sd-card can be updated from a remote location without removing the card (dual rootfs with failsafe upgrade mechanism)
3. Extend aws-iot-pubsub-agent to support publishing cpu temperature at a given interval
4. Extend aws-iot-pubsub-agent to support subscribing for a topic to stress the cpu for simulating the cpu-temperature increase/decrese
5. Extend aws-iot-pubsub-agent to support USB-OBD2 adapter which can read parameters of a car and publish the topics to aws-iot
6. Extend aws-iot-pubsub-agent to support subscribing for a topic so that sw-upgrade can be triggered remotely
7. Extend the Linux image to support usb-tethering for internet access
8. Create multiple images of AWS-IoT-Demo sdcard for various opensource linux boards(e.g pi-1/pi-2/pi-3/beaglebone/openwrt-hw/etc)
