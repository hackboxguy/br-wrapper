This is the Buildroot support for Zynq ebaz4205 crypto miner board.
Refurbished version of this board is available from aliexpress or ebay.
Basic board support packages are taken from Lukas Lichtl's github repo
https://github.com/embed-me/ebaz4205_buildroot

Before using this buildroot generated image, ensure that your board is
configured to boot from sdcard - i.e modify hw resistors as given below.
Remove Resistor R2584 and place it at empty resistor position R2577.

Steps to create a bootable sdcard.img for zynq-ebaz4205-board:
1) Configuration
   make BR2_EXTERNAL=../ zynq_ebaz4205_defconfig
2) make BR2_EXTERNAL=../ BRIMAGE_VERSION=0.1.1  (provided version is optional for 
   for including the version in OTA image for swupdate)
3) In output/images directory, there will 2 files which are required for
   ebaz4205-board.
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


How to build bootable-sdcard image from scratch for ebaz4205-board:
	a)git clone --recursive https://github.com/hackboxguy/br-wrapper.git
	b)cd br-wrapper
	c)make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output \
          BRIMAGE_VERSION=0.1.1 zynq_ebaz4205_defconfig
	d)make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output \
          BRIMAGE_VERSION=0.1.1

Automatically generated Sdcard image partitions(defined in board/ebaz4205/genimage.cfg):
+-------------+----------+------+---------------------------------------------+
| partition   | file     | Size |     Description                             |
|             | system   |      |                                             |
+-------------+----------+------+---------------------------------------------+
|    1        |  FAT     | 32MB | boot partition(boot.bin/u-boot.img/uEnv.txt)|
+-------------+----------+------+---------------------------------------------+
|    2        |  EXT4    | 64MB | rootfs-1(including uImage+dtb+fpga_top.bin) |
+-------------+----------+------+---------------------------------------------+
|    3        |  EXT4    | 64MB | rootfs-2(including uImage+dtb+fpga_top.bin) |
+-------------+----------+----------------------------------------------------+
|    4        |  EXT3    | 64MB | persistent settings partition for apps      |
+-------------+----------+----------------------------------------------------+

As shown above, sdcard image uses dual copy rootfs where all sw-components including
Kernel are packed.
sdcard.img contains bootable files in 1st partition and ext4 rootfs in 2nd partition.
partition-3 is kept blank, and partition-4 is blank but ext3 formatted(persistent
partition for saving appliction data).

sdcard image preperation is required only for the first time, later when sw update 
is needed, just copy output/update-image.swu to /tmp of the ebaz4205-board
	1)"scp output/update-image.swu root@ebaz4205-board-ip:/tmp/"
	2)login to ebaz4205-board using : "ssh root@ebaz4205-board-ip"
	3)"swupdate-client -p /tmp/update-image.swu"
		OR
	1)using browser, open http://ez4205-board-ip:8080
		drag and drop the update-image.swu in the sw-update page,
		after update, system will flip the boot marker in boot-partition:uEnv.txt,
		so that during next reboot, u-boot would load uImage+rootfs+fpga_top.bin from
		flipped rootfs partition(as of now, whole update process takes around
                37seconds starting from drag_n_drop_in_browser+update+reboot, this may
                increas if rootfs size increases due to added packages).


What changes/extensions have been added to buildroot's zynq_ebaz4205_defconfig?
1)Added automatic sdcard image creation with 4 partitions for dual-copy OTA mechanism
2)Added required uEnv.txt for booting the system from sdcard with correct bootmarker.
3)Added 4th partition as settings partition, i.e /mnt/.settings stores all persistent data
4)Enabled dropbear ssh, and for the first boot-up from sdcard, required rsa and ecdsa
  keys are generated and stored at persistent location /mnt/.settings/etc/dropbear
5)Configured the system to get the ip from dhcp server.
6)Enabled swupdate over-the-air sw update mechanism(OTA is only available for sdcard)
7)Enabled webUI of swupdate for updating the system using web-browser.
8)Added auto creation of OTA update image(i.e output/update-image.swu) which also
  includes the version spefied during buildroot make(BRIMAGE_VERSION)

