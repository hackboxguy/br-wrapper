# himax-touch-oled

Single-IC Linux touchscreen driver for the **Himax HX8530(C)** TDDI on the
**OLED-OTS 17.3"** panel, reached over a TI DS90UB983 serializer + DS90UB984
deserializer (FPD-Link IV) from a Raspberry Pi 4.

## Why a separate package (not part of himax-touch)

`himax-touch` builds `himax_mmi.ko` as a *multi-chip* driver (HX83180/81/92/93).
HX8530 is deliberately excluded from that build: it needs chip-specific struct
members and PRODUCT_TYPE validation that conflict with the HX831xx cores. This
package builds HX8530 as the **only** IC, so those defines are consistent.

To let the two coexist on one image, this package uses a distinct module name
(`himax_oled`) and DT compatible (`himax,hxoled`). Only the board whose overlay
is enabled loads its driver.

## Hardware path

The HX8530 sits on the **984's second local I2C bus (Port 1)**, physically at
**0x49**. Plain FPD-Link I2C pass-through only reaches the 984's Port 0, so the
touch is invisible by default. The `hh983-serializer` driver, in **mode 0**
(983+984) with **`ots_touch=1`**, programs a 983 target-alias that hops the
transaction to the deserializer's I2C Port 1 and remaps physical 0x49 to the
host-visible **0x48** this driver probes. Proven on the bench: the driver reads
the chip ID back as `HX8530C`.

It is a multi-die part (master + slave). All dies are reached through the single
external address via a SID prefix byte (`0xC0|device`); there is only ever one
I2C address on the bus.

## Enabling on the OTS-OLED board (only)

In `/boot/firmware/config.txt`:

```
dtoverlay=hh983-serializer
dtoverlay=himax-touch-oled
```

In `/etc/modprobe.d/` (note: config_mode=0, not the 1 used by the 988 boards):

```
options hh983-serializer config_mode=0 ots_touch=1
```

Do **not** also enable `dtoverlay=himax-touch` or force-load `himax_mmi` on this
board; the two touch drivers are mutually exclusive per board.

## Files

- `src/` – HX8530-only build (`obj-m := himax_oled.o`, `CONFIG_TOUCHSCREEN_HIMAX_IC_HX8530`).
- `dts/himax-touch-oled-overlay.dts` – binds `himax,hxoled` at 0x48, IRQ GPIO17.
- `himax-touch-oled.mk`, `Config.in` – Buildroot metadata (misc-tools builds it
  through `custom-pi-kernel-builder` when `KERNEL=1`).
