This is the Buildroot support for Zynq ebaz4205 crypto miner board.
Refurbished version of this board is available from aliexpress or ebay
Basic board support packages are taken from Lukas Lichtl's github repo
	https://github.com/embed-me/ebaz4205_buildroot

Steps to create a working system for ebaz4205 board:

1) Configuration
    make zynq_ebaz4205_defconfig
2) make
3) All needed files will be available in the output/images directory.
   The sdcard.img file is a complete bootable image ready to be written
   on the boot medium. To install it, simply copy the image to an SD
   card:

       # dd if=output/images/sdcard.img of=/dev/sdX

   Where 'sdX' is the device node of the uSD.
4) boot your board
