# TODO: DS90UH984 (mode=0) support gaps in hh983-serializer driver

## 1. Link status readback — `hh983_check_link_status()`

Currently only reads deserializer status for mode 1 (983+988) using 988-specific
registers:

- `DES988_GP_STATUS_0` (0x53): `[0]=FPD4_LOCK, [1]=FPD3_LOCK, [2]=FPDTX_PLL_LOCK`
- `DES988_GP_STATUS_1` (0x54): `[0]=LOCK, [1]=SIG_DET, [6]=FPD_PLL_LOCK`

**TODO**: Find the 984 equivalents from the DS90UH984 datasheet and add an
`else if (data->mode == 0)` block in `hh983_check_link_status()`.

## 2. Display driver board reset — `hh983_recover_link()`

Mode 1 (983+988) now does a 988 GPIO4/GPIO6 toggle to reset the display driver
board before the 983 digital reset. The full sequence:

1. Enable 983 I2C passthrough (so we can reach 988)
2. 988 GPIO4 (0x19) = 0xC0 (forced LOW → display driver in reset)
3. 988 GPIO6 (0x1B) = 0xC0 (forced LOW)
4. 983 digital reset (0x01 = 0x01)
5. 500ms hold
6. 988 GPIO4 (0x19) = 0x9C (restore RX Lock indicator)
7. 988 GPIO6 (0x1B) = 0xC2 (restore Combined Lock indicator)
8. Re-enable I2C passthrough (983 + 988)
9. HPD toggle (APB 0x000: 0→1)

**TODO for mode 0 (983+984)**:

- Determine if the 984's display driver board uses equivalent GPIO-driven
  reset signals. Check the 984 display driver board schematic for:
  - Which 984 GPIO pins drive reset/lock signals
  - What register addresses control those GPIOs
  - What are the default (lock-indicator) and forced-LOW values

- If the 984 board does use GPIO-based reset:
  - Add `DES984_GPIOx_PIN_CTL` register defines
  - Add `DES984_GPIO_FORCED_LOW` and default value defines
  - Add a `mode == 0` block in `hh983_recover_link()` matching the mode 1 pattern

- If the 984 board does NOT use GPIO-based reset:
  - Document why it's not needed
  - The existing digital reset + HPD toggle may be sufficient for mode 0

## 3. I2C passthrough re-enable after recovery

Mode 1 re-enables both 983 and 988 passthrough after digital reset in
`hh983_recover_link()`. Mode 0 currently only re-enables the 983 side.

**TODO**: Verify if the 984 has an I2C passthrough register equivalent to
`DES988_I2C_CONTROL` (0x04) that also needs re-enabling after recovery.
The 984 may handle passthrough differently since it doesn't have the TDDI
I2C routing that the 988 needs.

## Reference

- All 984-specific code should use `DES984_*` defines
- 984 deserializer I2C address: `data->deser_addr` (default 0x2C)
- Use `hh983_read_deser_reg()` / `hh983_write_deser_reg()` for 984 access
- Driver source: `package/hh983-serializer/src/hh983-serializer.c`
