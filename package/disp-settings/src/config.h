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

// Config file location
#define DEFAULT_CONFIG_FILE "/usr/share/qt-apps/disp-settings.json"

#endif // CONFIG_H
