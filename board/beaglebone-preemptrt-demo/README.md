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

