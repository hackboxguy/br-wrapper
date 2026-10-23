# Makefile for the Himax HX8530 touchscreen driver on the 17" OLED-IB
#
# Builds himax_oledib.ko from Himax's sample code A09.23 (HX8530, Harman
# SDC17, 2026-06-02) -- the driver Himax ships for the firmware they flashed
# on 2026-10-22. The A09.21-based himax_oled (himax-touch-oled package) reads
# this firmware's touch information from the wrong place, so the OLED-IB gets
# its own module; the OTS-OLED keeps himax_oled.
#
# Single IC, as in himax-touch-oled: CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530 is the
# only IC selected. Module name and DT compatible ("himax,hxoledib") are
# distinct from himax_oled and himax_mmi, which register the same i2c driver
# name: exactly one of the three may be loaded (micropanel pi-config-txt.sh).

ccflags-y += -D__HIMAX_MOD__
ccflags-y += -DCONFIG_TOUCHSCREEN_HIMAX_DEBUG
ccflags-y += -DCONFIG_TOUCHSCREEN_HIMAX_IC_HX8530
ccflags-y += -D__HIMAX_HX8530_MOD__

himax_oledib-objs := himax_common.o
himax_oledib-objs += himax_platform.o
himax_oledib-objs += himax_ic_core.o
himax_oledib-objs += himax_ic_HX8530.o
himax_oledib-objs += himax_debug.o

obj-m := himax_oledib.o
