This is the Buildroot support for digilent's zynz-7000-zed-board(zedboard.org)
This configuration is tested on Rev-E of ZedBoard.

Steps to create a bootable sdcard.img for ZedBoard:
1) Configuration
   make zynq_zed_defconfig
2) make BRIMAGE_VERSION=0.1.1  (provided version is optional for 
   for including the version in OTA image for swupdate)
3) In output/images directory, there will 2 files which are required for
   zedboard.
	a)output/sdcard.img (this is the bootable sdcard image which can be
          written to sdcard using dd command or balena-etcher-app)
	b)output/update-image.swu (this is the over-the-air update file where
          linux system on sdcard can be updated remotely via network)
          Note: generated sdcard.img can be flashed to sdcare using,
          # dd if=output/images/sdcard.img of=/dev/sdX
	  Where 'sdX' is the device node of the uSD.
4) boot your board
5) next time when new image is built, no need to remove sdcard, instead use
   swupdate's OTA mechanism.


How to build zedboard bootable-sdcard image from scratch:
	a)git clone --recursive https://github.com/hackboxguy/br-wrapper.git
	b)cd br-wrapper
	c)make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output \
          BRIMAGE_VERSION=0.1.1 zynq_zed_defconfig
	d)make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output \
          BRIMAGE_VERSION=0.1.1

Sdcard image partitions(defined in board/zed/genimage.cfg):
+-------------+----------+------+---------------------------------------------+
| partition   | file     | Size |     Description                             |
|             | system   |      |                                             |
+-------------+----------+------+---------------------------------------------+
|    1        |  FAT     | 32MB | boot partition(boot.bin/u-boot.img/uEnv.txt)|
+-------------+----------+------+---------------------------------------------+
|    2        |  EXT4    | 64MB | rootfs-1(including uImage+dtb)              |
+-------------+----------+------+---------------------------------------------+
|    3        |  EXT4    | 64MB | rootfs-2(including uImage+dtb)              |
+-------------+----------+----------------------------------------------------+
|    4        |  EXT3    | 64MB | writing settings partition                  |
+-------------+----------+----------------------------------------------------+

As shows above, sdcard image uses dual copy rootfs where all sw-components including
Kernel are packed.
sdcard.img contains bootable files in 1st partition and ext4 rootfs in 2nd partition.
partition-3 is kept blank, and partition-4 is blank but ext3 formatted.

sdcard image preperation is required only for the first time, later when 
update is needed, just copy output/update-image.swu to /tmp of the zedboard
	1)"scp output/update-image.swu root@zed-board-ip:/tmp/"
	2)login to zedboard using : "ssh root@zed-board-ip"
	3)"swupdate-client -p /tmp/update-image.swu"
		OR
	1)using browser, open http://zed-board-ip:8080
		drag and drop the update-image.swu in the sw-update page,
		after update, system will flip the boot marker in boot-partition:uEnv.txt,
		so that during next reboot, u-boot would load uImage+rootfs from
		flipped rootfs partition.


What changes/extensions have been added to buildroot's zynq_zed_defconfig?
1)updated u-boot version to xilinx-v2019.2
2)updated kernel version to xlnx_rebase_v5.10
3)added automatic sdcard image creation with 4 partitions for dual-copy OTA mechanism
4)added required uEnv.txt for booting the system from sdcard with correct bootmarker.
5)added 4th partition as settings partition, i.e /mnt/.settings stores all persistent data
6)enabled dropbear ssh, and for the first boot-up from sdcard, required rsa and ecdsa
  keys are generated and stored at persistent location /mnt/.settings/etc/dropbear
7)configured the system to get the ip from dhcp server.
8)Enabled swupdate over-the-air sw update mechanism(OTA is only available for sdcard)
9)enabled webUI of swupdated for updating the system using web-browser.
10)added auto creation of OTA update image(i.e output/update-image.swu) which also
  includes the version spefied during make of buildroot(BRIMAGE_VERSION)

