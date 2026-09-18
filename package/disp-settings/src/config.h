#ifndef CONFIG_H
#define CONFIG_H

// Network configuration
#define DEFAULT_NETWORK_PORT 8087

// als-dimmer socket configuration
#define DEFAULT_ALS_DIMMER_SOCKET "/tmp/als-dimmer.sock"
#define DEFAULT_ALS_POLL_INTERVAL_MS 500

// FPGA I2C configuration
#define DEFAULT_I2C_BUS "/dev/i2c-1"
#define FPGA_I2C_ADDR 0x1D

// FPGA register addresses
#define FPGA_REG_VERSION     0x00
#define FPGA_REG_LIGHT_SENSOR 0x0C
#define FPGA_REG_FPGA_ID     0x10
#define FPGA_REG_GLOBAL_DIM  0x28
#define FPGA_REG_PATTERN     0x2B
#define FPGA_REG_PRIVACY     0x34
#define FPGA_REG_GLOBAL_DIM2 0x35

// Temperature sensor configuration
#define DEFAULT_TEMP_POLL_INTERVAL_MS 1000
#define DS18B20_SYSFS_PATH "/sys/bus/w1/devices"
#define DEFAULT_W1_PATH "/sys/bus/w1/devices"

// TDDI touch controller configuration
#define DEFAULT_TDDI_PATH "/proc/android_touch"

// IOC firmware reference images, used to decide whether an update is
// available. These are the images the sp6bins package installs; the IOC
// carries the same slot image, so a CRC mismatch means the board is running
// something other than what this rootfs ships.
#define DEFAULT_IOC_REF_IMAGE   "/home/pi/micropanel/share/sp6bins/firmware/bios-bin/REMOTE_DISP_OTS_display_manager_ota.bin"
#define DEFAULT_HH983_REF_IMAGE "/home/pi/micropanel/share/sp6bins/firmware/bios-bin/983HH_983_manager_ota.bin"

// Config file location
#define DEFAULT_CONFIG_FILE "/usr/share/qt-apps/disp-settings.json"

#endif // CONFIG_H
