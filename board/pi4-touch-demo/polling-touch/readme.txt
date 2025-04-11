How to build pi4-touch-demo with himax polling i2c touch?
1)git clone --recursive https://github.com/hackboxguy/br-wrapper.git
2)cd br-wrapper
3)cp board/pi4-touch-demo/polling-touch/02-himax-83193-pi4-i2c-dts.patch board/pi4-touch-demo/linux-patches/
4)cp board/pi4-touch-demo/polling-touch/raspberrypi4_touch_demo_defconfig configs/
5)make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output raspberrypi4_touch_demo_defconfig
6)make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output
7)ls ../../br-output/images/sdcard.img


