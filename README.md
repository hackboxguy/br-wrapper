# br-wrapper

br-wrapper contains custom-br-configs-and-packages for mainline buildroot. Contents of br-wrapper are used as ```BR2_EXTERN``` folder for buildroot. Saperating the custom-br-configs-and-packages with ```BR2_EXTERN``` mechanism, removes the dependency on a fast moving buildroot project which is under constant development.
Using buildroot as a submodule makes it easy to build custom-br-configs-and-packages with different versions of mainline buildroot.

## Maintainer
	Albert David (albert.david@gmail.com)

## Build steps
    git clone --recursive https://github.com/hackboxguy/br-wrapper.git
    cd br-wrapper
    make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output <config_name>
    make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output

## Available Configs
1. ```zynqmp_zcu106_defconfig``` (Xilinx Zynq Ultrascale+ SoC evaluation board)
2. ```zynq_ebaz4205_defconfig``` (Refurbished crypto miner board based on Xilinx Zynq)
3. ```zynq_zed_defconfig``` (Xilinx Zynq-7000 based zed-board from digilent: www.zedboard.org)


## Build Options
```BR2_DL=/some/path``` : Keeping buildroot download folder in a out-of-tree location(using ```BR2_DL_DIR```) helps to reduce the build time by avoiding repeated downloads of source-packages.
    
```O=/some/path``` : Saperating the buildroot output folder using ```O=``` option helps in development where multiple build configurations are built.
    
## Build tips
Building the embedded linux image with buildroot is a time consuming task which may take few minutes to few hours depending on the pc-hw and board-configuration. Incase if you are bulding on a powerful remote server, use screen session to attach and detach from the buildroot shell terminal as shown below
        
    screen -R my-br-session
    cd ~/
    git clone --recursive https://github.com/hackboxguy/br-wrapper.git
    cd br-wrapper
    make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output zynqmp_zcu106_defconfig
    make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output
press ```Ctr+a``` and then ```d``` to detach from the buildroot-shell-terminal.

Use following command to re-attach to  buildroot-shell-terminal

    screen -r my-br-session

## How to build br-wrapper's custom-board-config with specific version of mainline-buildroot?
    git clone --recursive https://github.com/hackboxguy/br-wrapper.git
    cd br-wrapper/buildroot/
    git checkout 2021.02.x
    cd ..
    make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output zynqmp_zcu106_defconfig
    make -C buildroot BR2_EXTERNAL=../ BR2_DL_DIR=../../br-dl O=../../br-output
