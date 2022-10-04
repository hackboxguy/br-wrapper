# Linux-PREEMPT_RT-Demo Image for beaglebone-black or green

This is a buildroot config for generating bootable linux image for beaglebone. Purpose of this custom-linux-image is to evaluate behaviour of Linux-PREEMPT_RT patches. All required preemptrt-patches/kernel-config/libraries/demo-utils for evaluating the PREEMPT_RT behaviour are included in this image. An outer layer br-wrapper repo is used as ```BR2_EXTERN``` for buildroot. Final output of this buildroot config is a linux image which can be directly written on sdcard using Balena-Etcher or dd or other ISO image writing utils.

## Maintainer
	Albert David (albert.david@gmail.com)

## Build steps for creating Linux-PREEMPT_RT-Demo sdcard image for beaglebone hw
    mkdir -p ~/preempt-rt-demo/
    cd ~/preempt-rt-demo/
    git clone --recursive https://github.com/hackboxguy/br-wrapper.git
    cd br-wrapper
    make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output beaglebone_preemptrt_defconfig
    make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output
    ls -lah ~/preempt-rt-demo/br-output/images/sdcard.img #this image can written to sdcard using dd

## How to cross-compile(on x86 host pc) and run modified preemptrt-gpiotest on Target(beaglebone) Hardware?
1. ```wget https://github.com/hackboxguy/lfs-downloads/raw/main/beaglebone-preemptrt-demo/arm-buildroot-linux-gnueabihf_sdk-buildroot.tar.gz -O ~/arm-buildroot-linux-gnueabihf_sdk-buildroot.tar.gz``` Download Cross-Toolchain of beaglebone-preemptrt-demo
2. ```tar -xvf ~/arm-buildroot-linux-gnueabihf_sdk-buildroot.tar.gz -C ~/``` untar downloaded Cross-Toolchain to HOME directory
3. ```~/arm-buildroot-linux-gnueabihf_sdk-buildroot/relocate-sdk.sh``` Relocate the toolchain path to this specific installation
4. ```git clone https://github.com/hackboxguy/preemptrt-gpiotest.git``` Clone the source-code of preemptrt-gpiotest
5. ```cd preemptrt-gpiotest``` cd to cloned souce directory
6. Modify the code of preemptrt-gpiotest
7. ```cmake -H. -BOutput -DCMAKE_INSTALL_PREFIX=~/preemptrt-demo-output/ -DCMAKE_TOOLCHAIN_FILE=~/preemptrt-gpiotest/cmake/arm-buildroot-linux-uclibcgnueabihf.cmake``` configure with cmake
8. ```cmake --build Output -- install -j$(nproc)``` build with cmake(binary will be installed to ~/preemptrt-demo-output/sbin/)
9. ```scp ~/preemptrt-demo-output/sbin/preemptrt-gpiotest root@preemptrt-demo-target:/usr/sbin/``` using scp, copy the modified binary to the beaglebone hw
10. ```ssh root@preemprt-demo-target``` using ssh, login to beaglebone hw (root pw is: brb0x)
11. ```/etc/init.d/S99PreemptrtGpio stop``` stop existing instance of preemptr-gpiotest
12. ```/usr/sbin/preemptrt-gpiotest gpiochip0 28 gpiochip0 17 h p``` run modified preemptrt-gpiotest on beaglebone
