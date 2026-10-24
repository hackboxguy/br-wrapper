# himax-touch-oledib

Linux touchscreen driver for the **Himax HX8530** on the **17" OLED-IB**
(2560x1440), reached over a TI DS90UH983 serializer + DS90UH988 deserializer
(FPD-Link IV) from a Raspberry Pi 4. Built from Himax's sample code **A09.23**
(`Sample_code_A09.23_HX8530_Harman_SDC17_20260602`) as `himax_oledib.ko`, DT
compatible `himax,hxoledib`.

## Why not himax-touch-oled

The OLED-IB's controller shipped without valid firmware; Himax provided
`HX8530-LC118 HARMAN SDC_1700 20261022` (255 KB), flashed on 2026-10-23 through
this driver's debug node (`echo t <file> > /proc/android_touch/debug`, file in
`/lib/firmware`; 22.6 s, both CRCs pass, survives a power cycle). The
A09.21-based `himax_oled` reads that firmware's touch information from the
wrong addresses (16704 x 61022, 128 TX) and refuses it. The OTS-OLED keeps
`himax_oled`, which carries its register map and mirrored axes.

`himax_oled`, `himax_mmi` and this module register the same i2c driver name
and the same `/proc/android_touch`: exactly one may be loaded. micropanel's
`pi-config-txt.sh` selects this one for display type `oled-ib-17`.

## Changes to the Himax sample code

- `himax_ic_HX8530.c`: `hx8530_cascaded_arch_check()` falls off its end
  without `return ret;`; `strlcpy` -> `strscpy` (gone from 6.12).
- `himax_platform.c`: compatible `himax,hxoledib` (was `himax,hxcommon`);
  the I2C platform variant is the one built (`himax_platform_I2C.*`).
  It also binds `himax,hxoled`, the himax-touch-oled node: the display file
  that names the overlay is device-owned and survives an A/B update, so an
  OLED-IB updated in place keeps that node. Without `himax,display-coords`
  in the DT (that node has none) the panel's 2560x1440 is used. micropanel
  loads this module only for `oled-ib-17`, so the OTS-OLED, whose node this
  also is, never gets it.
- `himax_common.c`: **sense on after probe when there is no reset line.**
  The board routes no RESX to the controller. The flash check leaves the chip
  in safe mode (`0x900000A8 = 0x0C`) and the driver's reset cannot take it out,
  so it never senses. With no `himax,RESX` in the DT the driver now calls
  `fp_sense_on()` (what `echo senseonoff,1 > /proc/android_touch/debug` does):
  `0x0C` -> `0x05`, touches reported.
- `himax_common.c`: **`fixed_touch_size` module parameter, default 64.** The
  20261022 firmware reports each contact's real size (one finger ~56, five
  fingers median 129, up to 225, past the declared 0..200). The driver sends it
  as touch major, width major and pressure, and Qt draws a touch that many
  pixels wide, so lines were thick and uneven. Every contact now reports 64,
  as the OTS-OLED's firmware does. `0` reports the chip's own size, clamped to
  200. Changeable at run time:
  `echo 0 | sudo tee /sys/module/himax_oledib/parameters/fixed_touch_size`.

## Known

- `echo v > /proc/android_touch/debug` reports all-zero versions ("FW fail
  reload done"): the reload flag the driver polls never appears. Touch is not
  affected; asked of Himax.
- Orientation not yet verified corner by corner; A09.23 does not parse
  `touchscreen-inverted-x/y`.

## Hardware path and config

As `himax-touch-oled`'s OLED-IB section: `dtoverlay=himax-touch-oledib`,
`options hh983-serializer config_mode=2 ib_touch=1`,
`softdep himax_oledib pre: hh983-serializer`.
