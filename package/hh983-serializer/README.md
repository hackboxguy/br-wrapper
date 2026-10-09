# hh983-serializer

Kernel driver for the TI DS90UH983 serializer on the 983HH adapter, with the 984 (`config_mode=0`),
the 988 (`config_mode=1`) or the 988 in video-only use (`config_mode=2`) behind it. It brings the
link up, routes the touch controller and the panel FPGA through the 983's target slots, and runs a
1 s poll that watches the link and the video. Built out of tree by misc-tools'
`custom-pi-kernel-builder` (`04-build-drivers.sh`, `DRIVERS_DIR`) for the image's kernel.

## Mode 1 (983 + 988): the two faults behind the 988, and what the driver does

| | Fault A (2026-10-05) | Fault B (2026-10-07) |
| --- | --- | --- |
| 988 registers (0x2C) | time out | answer (~20 ms) |
| FPGA 0x1D/0x1E, display MCU 0x66, PMICs, an empty address | time out (~500 ms) | time out (~500 ms) |
| Touch (988 port 1) | works, sluggish while the poll runs | works |
| Picture | fine | fine |
| What clears it | only a power cycle | a 988 digital reset (`0x2C` reg `0x01` = `0x01`), ~2 s |
| What the driver does | logs `988 unreachable over the back channel (port-0 I2C block gone, class W) …; a power cycle is needed`, counts `link_lost_count`, and backs its poll off to `link_lost_poll_s` (30 s) until the 988 answers again, so touch is not stalled every second | probes the FPGA every `bus_check_interval` polls; after `bus_wedge_polls` consecutive timed-out probes logs `988 port-0 I2C bus wedged …; digital reset of the 988` and resets it, bounded (backoff 1/2/5/10/30/60 s, at most `bus_reset_max` per wedge), then `988 port-0 bus recovered` or `… stays wedged after N resets; power cycle needed` |

Details and the measurements: `tmp-docs/non-responsive-mcu-fpga-debug.md` (wifi-app workspace) and
`tmp-docs/hh983-bus-recovery-report-v1.md`.

**The probe.** One combined `I2C_RDWR`-style transaction reading the FPGA's VERSION register
(offset `0x00`, read-only, latches nothing) on its page/register slave `0x1E`. That slave is chosen
because nobody else addresses it with a split write and read: als-dimmer reads its ambient sensor on
`0x1D` with a separate pointer write and read, and a probe between the two would move its pointer.
A board whose FPGA has no `0x1E` falls back to `0x1D` (4-byte pointer); a board with no FPGA turns
the check off after the first clean NACK. The transfer is timed with the adapter locked, so waiting
behind another program's long read does not count: an answer or a NACK in milliseconds is a healthy
bus, a failure after ≥250 ms is a suspect.

**Side effect of the reset.** The 988 restarts its port 1 too: once in five forced resets the
touch driver lost one read and reset the touch controller (about 1 s). The picture stayed (owner,
2026-10-07, and the FPGA's "video present" bit through every reset); after a reset the driver says
so in the log if the FPGA reports no video.

**Reloading the module costs the picture on rig 1** (`rmmod`/`insmod` by hand): the remove issues a
983 digital reset, after which the DP source behind the 983 does not re-train, and the panel shows
its test pattern until a power cycle. This is old behaviour, not the bus check's; a hand swap for a
test needs a power cycle afterwards.

## Mode 1 on DS90UH983 CS2.0: no DP sink events

The mode-1 recovery after a DP source change (display-board GPIO reset, 983 digital reset, HPD
toggle) is triggered by APB `SINK_0_INT_CAUSE` (0x194), a read-clear register on CS1.0. On the
CS2.0 that replaces the discontinued CS1.0 (983v3, 12.3"-NQ1.1, 2026-10-09) that address reads
`0x7FFF7FFF` on every poll and never clears, so every poll looked like a video event: the panel
showed its BIST pattern, the Qt launcher for 2-3 s, then BIST again (a recovery every 7.7 s).

The driver now reads the revision at probe as TI's script generator does (APB block 3,
`UNIQUE_ID_3`, bit 6 set = CS1.0) and logs `983 silicon: ...` and `DP sink-event recovery on|off`.
Only CS1.0 gets the sink-event recovery; on later silicon the poll keeps the FPD-Link, bus and DTG
checks and never touches 0x190/0x194. Measured: CS1.0 (17" OLED-OTS rig) `UNIQUE_ID_3=0x40`,
`MASK_ID_REV=0x10`; CS2.0 `UNIQUE_ID_3=0x00`, `MASK_ID_REV=0x30`. A failed read keeps CS1.0
behaviour. A CS2.0 board therefore has no automatic recovery after an HDMI switch until a CS2.0
video-event source is found.

## Parameters

`/sys/module/hh983_serializer/parameters/`; 0644 ones can be changed at runtime.

| Parameter | Default | Mode | |
| --- | --- | --- | --- |
| `config_mode` | 0 | all | 0 = 983+984, 1 = 983+988 (TDDI pass-through), 2 = 983+988 video only (read-only) |
| `poll_interval_ms` | 1000 | all | the poll; 0 stops it (the update tools do, around a write) |
| `dp_guard`, `dtg_check`, `dtg_tolerance`, `wedge_holdoff_s`, `dtg_recover`, `wedge_recovery` | | 0/2 | the DP video guard and the DTG-wedge check (`docs/hh983-984-black-screen/`) |
| `dtg_wedge_count`, `dtg_boot_wedge_count` | | 0 | read-only counters |
| `tddi_port`, `fpga_addr` | -1, 0x1D | 1 | where the touch controller and the panel FPGA are routed |
| **`dp_events`** | **-1** | 1 | the DP sink-event recovery (HDMI-switch case): -1 = only on CS1.0 silicon, 0 = off, 1 = on; see below |
| **`ser_unique_id3`**, **`ser_mask_id_rev`** | | 1 | read-only: the 983's revision bytes read at probe (UNIQUE_ID_3 bit 6 = CS1.0) |
| `link_retrain_after`, `link_retrain_max`, `link_retrain_reset`, `link_retrain_hpd_ms`, `force_retrain` | 0, 10, 2, 200, 0 | 1 | FPD-Link re-train, off by default (it recovered none of the natural losses) |
| `link_lost_count`, `link_retrain_count`, `link_retrain_ok_count` | | 1 | read-only: 988 losses (fault A), re-trains |
| **`link_lost_poll_s`** | **30** | 1 | poll interval while the 988 is unreachable (fault A); 0 = keep `poll_interval_ms` |
| **`bus_recover`** | **1** | 1 | the port-0 bus check and the 988 reset; 0 = off |
| **`bus_check_interval`** | **5** | 1 | probe every N polls in which the 988 answered (every 5 s) |
| **`bus_wedge_polls`** | **3** | 1 | consecutive timed-out probes that count as a wedge |
| **`bus_reset_max`** | **5** | 1 | resets per wedge before giving up until the FPGA answers again |
| **`force_bus_reset`** | 0 | 1 | test hook: 1 = one 988 reset at the next poll (reads back 0) |
| **`bus_probe_count`**, **`bus_probe_timeout_count`**, **`bus_wedge_count`**, **`bus_reset_count`**, **`bus_reset_ok_count`** | | 1 | read-only counters (resets include forced ones) |
| `ots_touch`, `ots_scl_high`, `ots_scl_low` | | 0 | the OTS board's HX8530 touch route and its I²C speed |

The update tools (`update-iocs.sh`, `update-fpga.sh`) set `poll_interval_ms=0`, `dtg_recover=0`,
`wedge_recovery=0` around a write and restore them; the bus check lives inside the poll, so it stops
with it. The read-only checks do not quiesce: the probe is one byte every 5 s beside them.
