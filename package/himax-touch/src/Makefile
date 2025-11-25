# Makefile for Himax touchscreen driver (out-of-tree kernel module)
#
# This builds the himax_mmi.ko module with support for TDDI chips
# and debug features enabled.

# Enable module compilation mode
ccflags-y += -D__HIMAX_MOD__

# Enable debug output (I, D, W, E macros)
ccflags-y += -DCONFIG_TOUCHSCREEN_HIMAX_DEBUG

# Set default CONFIG to satisfy himax_common.h defines
# This sets HIMAX_PRODUCT_TYPE and other required globals
ccflags-y += -DCONFIG_TOUCHSCREEN_HIMAX_IC_HX83193

# Enable IC chip support defines
ccflags-y += -D__HIMAX_HX83180_MOD__
ccflags-y += -D__HIMAX_HX83181_MOD__
ccflags-y += -D__HIMAX_HX83192_MOD__
ccflags-y += -D__HIMAX_HX83193_MOD__
# HX83194, HX83195, HX8530 have compatibility issues with multi-chip build
# They require chip-specific struct members and PRODUCT_TYPE validation

# Core driver objects (always included)
himax_mmi-objs := himax_common.o
himax_mmi-objs += himax_platform.o

# In-cell core (required for TDDI chips)
himax_mmi-objs += himax_ic_incell_core.o

# IC-specific drivers (compatible with multi-chip build)
himax_mmi-objs += himax_ic_HX83180.o
himax_mmi-objs += himax_ic_HX83181.o
himax_mmi-objs += himax_ic_HX83192.o
himax_mmi-objs += himax_ic_HX83193.o

# Debug features
himax_mmi-objs += himax_debug.o

# Self-test features
himax_mmi-objs += himax_self_test.o

# Build as loadable module
obj-m := himax_mmi.o
