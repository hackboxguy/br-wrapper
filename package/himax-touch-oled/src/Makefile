# Makefile for the Himax HX8530 (OLED-OTS) touchscreen driver
#
# Builds himax_oled.ko: a SINGLE-IC build for the HX8530(C) TDDI on the
# OLED-OTS 17.3" panel. HX8530 is deliberately kept out of the multi-chip
# himax_mmi build (himax-touch package) because it needs chip-specific struct
# members and PRODUCT_TYPE validation that conflict with the HX8318x/9x cores.
# Here CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530 is the ONLY IC selected, so those
# defines are consistent.
#
# The module and its DT compatible ("himax,hxoled") are distinct from
# himax_mmi / "himax,hxcommon" so the two can be installed side by side; only
# the board whose overlay is enabled loads its driver.

ccflags-y += -D__HIMAX_MOD__
ccflags-y += -DCONFIG_TOUCHSCREEN_HIMAX_DEBUG

# Select HX8530 as the sole IC (drives HIMAX_PRODUCT_TYPE etc. in himax_common.h)
ccflags-y += -DCONFIG_TOUCHSCREEN_HIMAX_IC_HX8530
ccflags-y += -D__HIMAX_HX8530_MOD__

# Core driver objects
himax_oled-objs := himax_common.o
himax_oled-objs += himax_platform.o
himax_oled-objs += himax_ic_incell_core.o

# IC-specific driver (single chip)
himax_oled-objs += himax_ic_HX8530.o

# Debug + self-test
himax_oled-objs += himax_debug.o
himax_oled-objs += himax_self_test.o

obj-m := himax_oled.o
