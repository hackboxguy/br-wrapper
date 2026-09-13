# TASK FOR THE FIXING SESSION (read this block first, then the analysis below)

You are working in `/home/adav/extended-disk/git-repos/tmp12/15-6-0OD-black-screen-issue-fable`.
Fix the intermittent black screen of the 15.6" 2K5 display on the Pi4 + DS90UH983 +
DS90HH984 rig, prove the fix with repeated power cycles, and write a report. The root
cause is already established below (sections 1 to 6); do not re-investigate it unless
your own measurements contradict it.

## T1. Ground rules

- Rig: head-unit Pi `pi@192.168.1.243`, password and generic rig recipes in
  `tmp-docs/how-to-manage-remote-rig.md` (its Pi addresses are other rigs; the ssh/scp
  one-liners, Tasmota usage, `sync` before power cut, and colorimeter commands apply).
  Tasmota 12 V socket for display + 983 + Pi: `http://192.168.1.186/cm?cmnd=Power`
  (`Power%20OFF`, `Power%20ON`). It was just attached; `curl` it once to confirm.
- On the Pi, `i2cget`/`i2cset` live in `/usr/sbin`, which is not on the PATH of a
  non-login ssh shell. Prefix every remote command with `export PATH=/usr/sbin:/sbin:$PATH`
  or `fpdlink-tool.sh` silently reports FAIL.
- Never `rmmod`/`modprobe hh983_serializer` with the panel lit; install the new `.ko`
  and reboot (`sync` first).
- Run `sync` on the Pi before any Tasmota power cut.
- One automation at a time on the rig. Wrap remote commands in `timeout`.
- Passwords stay out of committed files and reports.

## T2. Code change (git repo `br-wrapper`, branch from `main` at f1b3251)

1. `git -C br-wrapper checkout -b fix/984-htotal-torn-read main`.
2. Edit `br-wrapper/package/hh983-serializer/src/hh983-serializer.c` per section 7:
   - make `hh983_read_htotals()` tear-proof (MSB, LSB, MSB again; accept only if both
     MSBs agree, up to 3 attempts; same for the V total read in the snapshot);
   - add the plausibility rule before a wedge is declared: the two consecutive
     out-of-tolerance polls must agree with each other within 64 px and lie on the same
     side of the programmed value. Torn artefacts (2560 / 3070 alternating) fail this,
     real wedges (thousands of px away, drifting slowly) pass;
   - the boot/resync restore path (`force_wedged=false`) must use the same read;
   - keep the OLED restore order (cut, pulse only if wedged, settle, enable) unchanged.
   Keep the diff small and commented in the style of the file. Do not touch modes 1/2
   behaviour beyond what the shared helper gives them.
3. Also update `br-wrapper/package/hh983-serializer/src/scripts/fpdlink-tool.sh`
   `ind_read15_be()` the same way (it feeds `--timings` / `--diagnose`; a torn value
   there produces a false "DTG stuck on corrupt H_TOTAL" verdict).
4. Commit on the branch with a message that references this document.

## T3. Build and install (verified recipe, 2026-09-13)

The Pi runs a custom kernel `6.12.109-v8+` built on this host; the Pi has **no**
headers for it (`/lib/modules/6.12.109-v8+/build` is a dangling symlink to
`/home/adav/pi-image-workspace/kernel-build/linux`, and `make` is not installed there).
Cross-build on this host:

```bash
K=/home/adav/pi-image-workspace/kernel-build/linux        # UTS_RELEASE 6.12.109-v8+, .config and Module.symvers present
SRC=$PWD/br-wrapper/package/hh983-serializer/src
make -C $K ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$SRC modules
strings $SRC/hh983-serializer.ko | grep vermagic       # must say 6.12.109-v8+ SMP preempt mod_unload modversions aarch64
make -C $K ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$SRC clean   # before committing, so build products stay out of git
```

This exact command built the unmodified source to a 41976-byte `.ko`, byte-for-byte
the size of the one installed on the Pi (md5 there: 376df2961f4f2abd8e7693cd2942cd7b).

Install:

```bash
scp hh983-serializer.ko pi@192.168.1.243:/home/pi/hh983-serializer.ko.fix
ssh pi@192.168.1.243 'sudo cp /lib/modules/6.12.109-v8+/extra/hh983-serializer.ko /home/pi/hh983-serializer.ko.before-torn-fix && \
  sudo cp /home/pi/hh983-serializer.ko.fix /lib/modules/6.12.109-v8+/extra/hh983-serializer.ko && sudo depmod -a && sync && sudo reboot'
```

`/etc/modprobe.d/hh983.conf` stays `options hh983-serializer config_mode=0` (defaults,
no stop-gap) so the validation runs against the real fix.

## T4. Validation script (must stop at the first black screen)

Write `br-wrapper/package/hh983-serializer/src/scripts/power-cycle-validate.sh`
(host side, bash, commit it on the same branch) and run it. Full specification in
section 11.2 below. Non-negotiable points:

- the screen-on decision is made with the attached i1Display Pro (white pattern up,
  five samples 3 s apart, all ≥ 800 nits = pass), never from 983/984 registers;
- on the first failing cycle the script does **not** power-cycle again: it dumps the
  evidence (samples, `dtg_wedge_count`, `dmesg | grep hh983`, `fpdlink-tool.sh
  --target=984 --diagnose`, the 150-read H total histogram) and exits non-zero, so the
  black state can be inspected live;
- per-cycle log lines to a file under `tmp-docs/fable-prompt-v1-data/` (timestamp, cycle
  number, wedge count, five samples, pass/fail);
- parameters: number of cycles, soak minutes, Pi/Tasmota addresses.

Run order:

1. **Before installing the fix** (the current, buggy module is on the Pi): run 2 cycles.
   Expected: it stops on cycle 1 or 2 with a black screen. That proves the detection.
   If it does not stop within 2 cycles, run the 150-read histogram (section 8) and check
   the rig before continuing.
2. Install the fixed module (T3). Run 10 cycles with a 10-minute white-pattern soak per
   cycle (or 20 cycles without soak if time is short). Pass = no failure and
   `dtg_wedge_count == 0` at the end of every cycle.
3. Leave the rig lit on the desktop (`stop-app` / `stop-pattern.sh`) with the fixed
   module installed and the poll running.

## T5. Report

Write `tmp-docs/opus-report-v1.md` containing: the git branch and commit ids, the full
diff, the build command and the vermagic line, the install steps and the backup file
name on the Pi, the pre-fix run (which cycle failed, its evidence), the post-fix table
(cycle, time, wedge count, min/max of the five samples, result), the final rig state,
and anything that did not go as this document expected. Do not edit this document;
put corrections in the report.

---

# 15.6" 2K5 (0OD) intermittent black screen on the Pi4 + DS90UH983 + DS90HH984 rig

Analysis v1, 2026-09-13, written for the engineer who will change the code.
Rig: pi@192.168.1.243, kernel 6.12.109-v8+, `hh983-serializer` mode 0 (983+984),
Pi HDMI profile `15.6-2k5`. All register values below were read on the live rig
while the screen was black, before anything was reset.

## 1. Summary

The black screen is **not the OTS-OLED "DTG wedge"**. It is a **false wedge detection
inside the `hh983-serializer` DP guard**, caused by a **torn 16-bit read of the 984's
measured H total**, followed by the guard's recovery action (cut stream, pulse the DTG,
re-enable stream) being executed every 30 to 120 seconds on a perfectly healthy pipeline.

Chain of events:

1. The 15.6-2k5 profile gives an H total of exactly **2816 = 0x0B00**.
2. The 984's `MEAS_HTOTAL` jitters by a few pixels around the true value
   (2811..2817), i.e. it flips between `0x0AFx` and `0x0B0x` many times per second.
3. The driver reads MSB (0x50:0x40) and LSB (0x50:0x41) as two separate I2C
   transactions through the 983 pass-through. If the value crosses the byte boundary
   between the two reads, the result is either **2560 (0x0A00)** or **3070 (0x0BFE)**.
   Measured on the rig: about **15 % of all reads are torn**.
4. `hh983_guard_check_dtg()` treats |meas − 2816| > 32 as a wedge after two consecutive
   bad polls, increments `dtg_wedge_count`, and runs `hh983_guard_restore_stream(force_wedged=true)`
   which cuts APB 0x084, holds DTG P0/P1 reset for 200 ms, waits 500 ms, re-enables.
5. On this panel that DTG pulse knocks the panel off the eDP video and into its TDDI
   BIST (colour cycle, sometimes parked on black) about 80 % of the time (section 4.3).
   In this session the driver did it **25 times in 30 minutes** (`dtg_wedge_count=25`,
   first one 2 s after probe, at 15:30:37).
6. The user-visible result is exactly what was reported: the Pi boot screen is fine
   (driver not loaded yet), the picture disappears around the time the desktop comes up
   (first false wedge fires 2 s after probe and again 20 s later), and the Stream Deck
   "Sync Video" button (984 digital reset, `fpdlink-tool.sh --recover --target=984`)
   brings it back.

Why it is intermittent: the torn read only happens when the measured value sits on a
256-boundary. Whether the jitter window straddles 0x0B00 depends on the exact
Pi pixel clock / 984 reference clock relationship after each power cycle, so some
boots are clean and some are not. Of all display profiles on the rig, **only 15.6-2k5
has an H total on a byte boundary** (section 6), which is why the other 984-based
panels never showed this.

Why it is *not* the OLED wedge: on the OTS-OLED the measured H total drifted to
4400..5400 against a programmed 3440 and stayed there (a real DTG fault). Here the
measured value is always within 2 px of the programmed value whenever it is read
correctly; the "bad" values are exactly the two torn-read artefacts 0x0A00 and
0x0BFx, and nothing else.

## 2. What the Stream Deck "Sync Video" button does

`streamdeck-ctrl/screens/display-control/scripts/sync-video.sh` maps the current HDMI
profile to a deserializer target (`15.6-2k5` → 984) and runs
`fpdlink-tool.sh --recover --target=984`. `cmd_recover()` → `recover_deser_video()` does one
thing: **984 main-page reg 0x01 = 0x01 (soft digital reset, self-clearing, registers
preserved)** followed by 0.5 s sleep. That re-initialises the 984 output pipeline and
re-trains the eDP link to the panel, which is why the picture comes back.

It does not touch the 983, the DTG registers, or the driver state. The driver keeps
polling and keeps generating false wedges afterwards; the button only masks the symptom.

## 3. Evidence captured on the rig (black-screen state, 16:00:51 to 16:01:08)

Driver poll was paused first (`poll_interval_ms=0`) so the dumps are not racing the
driver's own indirect-register accesses. Full dumps, kernel log, experiment logs and the scripts used are in
`tmp-docs/fable-prompt-v1-data/` (`pi-state-black-1.txt`, `capture-black.txt`,
`capture-after-sync.txt`, `experiment-1..3.txt`, `capture.sh`, `exp*.sh`).

### 3.1 Kernel log since boot (15:30:32)

```
15:30:35 HH983 FPDLink serializer probe (config_mode=0) ... initialization successful (mode=0, poll=on, dp_guard, dtg_check)
15:30:37 DP video back (VP_STS=0x01), restoring 984 video stream
15:30:37 DP guard restore: 984 stream_en=1, DTG measured Htotal=2560, 983 Htotal=2816 (DTG wedged)
15:30:56 DP guard: DTG wedge without video loss (measured 3070, programmed 2816), restoring
15:30:56 DP guard restore: 984 stream_en=1, DTG measured Htotal=2814, 983 Htotal=2816 (DTG wedged)
15:31:27 DP guard: DTG wedge without video loss (measured 3070, programmed 2816), restoring
15:32:33 ... (measured 3070 ...) restoring
15:35:01 ... (measured 2560 ...) restoring
15:36:09 ... (measured 2560 ...) restoring
15:37:15 ... (measured 3070 ...) restoring
15:38:08 ... (measured 3070 ...) restoring
15:39:08, 15:41:20, 15:42:13, 15:42:46, 15:43:34, 15:44:53, 15:45:28, 15:46:00,
15:47:57, 15:49:32, 15:51:03, 15:53:03, 15:53:42, 15:55:18, 15:57:08, 15:58:13, 16:00:23 ... restoring
```

Every "wedge" value is 2560, or 3066..3071. Every value the *restore* path re-reads a
second later is 2814/2815 (i.e. healthy) but it pulses anyway because
`force_wedged=true`. Every snapshot line shows 983 VP synced (`VP_STS=0x01`), FPD-Link
locked, stream on: nothing is wrong with the pipeline at any of these moments.

Note the very first restore at 15:30:37 (boot path, `force_wedged=false`) also saw a
torn 2560 and pulsed. So the DTG is pulsed on a healthy link 2 s after every boot on
this profile, with probability ≈ 15 %.

### 3.2 Register state while black (driver paused)

`fpdlink-tool.sh --target=984 --diagnose` verdict: **[OK] Pipeline healthy**.

| Item | Value |
|---|---|
| 983 GENERAL_STS 0x0C | 0x41 (RX_LOCK, LINK_DET) |
| 983 APB 0x000 HPD | 1 |
| 983 VP0 (page 0x32) | 2560x1440, H total 2816 (0x16=0x00, 0x17=0x0B), V total 1492, HBP 100, HSW 56 |
| 983 VID_PROC_CFG 0x32:0x01 | 0xA8 (VP_WAIT4LINE=1, default) |
| 984 GP_STATUS_0/1 0x53/0x54 | 0x01 / 0xC1 (FPD4 lock, PLL lock, LOCK) |
| 984 GENERAL_CFG 0x04 | 0xC1 |
| 984 DTG_CTL 0x50:0x20 | 0x93 (measured-timing mode) |
| 984 DTG_RESET_CTL P0/P1 0x50:0x32/0x62 | 0x04 / 0x04 (released, lock-reset enabled) |
| 984 MEAS_HTOTAL 0x50:0x40/0x41 | 0x0A / 0xFE = 2814 |
| 984 MEAS_VTOTAL 0x50:0x42/0x43 | 0x05 / 0xD4 = 1492 |
| 984 MEAS_HACTIVE/VACTIVE | 2560 / 1440 |
| 984 APB 0x084 MAIN_STREAM_EN | 1 |
| 984 APB 0x080 | 1 |
| 984 PGCTL/PGCFG 0x50:0x00/0x01 | 0x08 / 0x08 (PATGEN off) |
| 984 0x48:0x69 | 0x02 (HACTIVE_CHNG latched) |
| 983 CRC_ERROR0 0x0A | 0x00 |

There is no register-visible fault. This is the same picture as OTS-OLED "event C"
(forced DTG pulse on a healthy link: everything healthy, 0 nits).

### 3.3 Torn-read demonstration (same black state, 150 reads each)

Reading MSB then LSB (the driver's order, `hh983_read_htotals()`):

```
 67 x 2814    47 x 2815    5 x 2813    4 x 2816    2 x 2812    1 x 2811
 16 x 2560   <-- torn: MSB 0x0A from 28xx, LSB 0x00 from 2816
  4 x 3070    3 x 3071    1 x 3065   <-- torn: MSB 0x0B from 2816/2817, LSB 0xFE.. from 2814..
```

Reading LSB then MSB gives the same distribution (13 x 2560, 3 x 3070, ...), so
swapping the order does not help. Single-byte sampling shows why:

```
MSB alone (100 reads): 95 x 0x0A, 5 x 0x0B
LSB alone (100 reads): 50 x 0xFE, 32 x 0xFF, 10 x 0x00, 5 x 0xFD, 1 x 0x01, 1 x 0xFC, 1 x 0xFB
```

The live value really does straddle 0x0AFF / 0x0B00. About 24/150 = 16 % of two-byte
reads are torn. The driver needs two consecutive bad polls (`DP_GUARD_WEDGE_POLLS=2`),
so p ≈ 2.5 % per poll pair, one false wedge every ~40 s on average, which matches the
log (23 restores in 28 minutes, holdoff 30 s).

The datasheet (SNLS726, 7.6.2.16.29/30, `MEAS_HTOTAL_MSB_P0` / `MEAS_HTOTAL_LSB_P0`)
describes them as two plain read-only bytes; there is no documented latch / shadow
mechanism, so the software has to make the read atomic itself.

### 3.4 After "Sync Video" (984 digital reset) at 16:01:38

Same tool, same dumps. Differences versus the black state are only the clear-on-read
event flags that the reset itself produces: 984 0x53 bit 7 FPD_DECODE_ERROR, 0x54 bit 2
LOCK_STS_CHG, 983 0x0C bits 4/1 LINK_LOST / BC_CRC_ERROR, 983 0x0A CRC_ERROR0 = 0x0C.
Programmed and measured timings unchanged; the torn-read distribution unchanged
(10 x 2560, 7 x 307x out of 150). I.e. the reset fixes the picture, not the cause.

## 4. Controlled experiments (i1Display Pro on the panel, pattern generator white)

Calibration: white pattern = 1034 nits, black pattern = 0.0 nits, so the sensor is on
the 15.6" panel and reads black as 0.

### 4.1 Driver paused, manual replay of the restore sequence (16:04)

Desktop content under the sensor ≈ 6.4 nits.

| Step | Action | Luminance after |
|---|---|---|
| A | APB 0x084=0, 0.7 s, APB 0x084=1 (stream cut/enable, no pulse) | 6.37 (unchanged) |
| B | cut, DTG P0/P1 0x32/0x62 = 0x06, 200 ms, = 0x04, 500 ms, enable (exact driver restore) | 6.37 (unchanged, measured 3 s later) |
| C | 984 reg 0x01 = 0x01 (Sync Video) | 6.38 |

A single replay done by hand did not leave the panel dark at the 3 s sample point.

### 4.2 Driver poll re-enabled with default settings, white pattern on screen (16:06:48)

Luminance sampled every ~3 s (`measure-display.sh --sensor-only=yes`), driver at
`dtg_tolerance=32`, `dtg_recover=1`:

```
16:06:48..16:07:16  1029 .. 1026 nits   steady white, dtg_wedge_count=25
16:07:19  driver: "DTG wedge without video loss (measured 2560, programmed 2816), restoring"  -> count 26
16:07:19  593   16:07:28  309   16:07:36  194   16:07:39  1024   16:07:42  0.8   16:07:46  1023
16:08:02  194   16:08:05  0.8   16:08:14  224   16:08:22  458   16:08:25  223   16:08:28  0.8
16:08:38  driver: second false wedge (measured 3071), restoring -> count 27
16:08:37..16:10:01  1018, 585, 215, 234, 302, 194, 1018, 0.8, 1017, 581, 458, 1016, 1015, 580, 215, 233, 236, 0.8, 1013, 578, 215, 233 ...
```

From the first driver restore onwards the panel no longer shows the Pi's solid white:
it cycles through a fixed sequence of levels (≈1020 / 580 / 455 / 300 / 233 / 215 / 194 /
0.8 nits, ~3 s per step). The rig owner confirmed by eye at that moment that the panel
was showing a changing pattern. That is the **panel's TDDI built-in self test**: the
panel has dropped the eDP video and fallen back to its own BIST. Sometimes the BIST
sequence parks on black, which is the "black screen" originally reported.

### 4.3 Driver stop-gapped (`dtg_tolerance=600`), manual replays, 5 samples each (16:11..16:15)

| Sequence | Result |
|---|---|
| 984 digital reset (Sync Video) | steady white 1007 nits, every time (6/6 incl. the recoveries below) |
| A: APB 0x084 = 0, 0.7 s, = 1 (stream cut/enable only), x3 | steady white 3/3 |
| B: cut, DTG P0/P1 reset 200 ms, release, 500 ms, enable (the driver's restore), x5 | **BIST 4/5** (B#1 survived, B#2..B#5 dropped into BIST within 1..3 s) |

So on this panel the guard's DTG pulse is the harmful step, not the stream cut, and it
fails about 80 % of the time. Combined with a false wedge every 40 s on average, a boot
on this profile is practically guaranteed to end up in BIST/black within a minute or
two of the driver loading, which is what the rig owner sees ("boot screen OK, goes
blank when the desktop comes up").

The single replay in 4.1 that survived is consistent with the 1-in-5 survival seen here.

## 5. Is it the same as the OTS-OLED "upstream 984 DTG wedge"?

No, although the driver code that reacts is the same and the symptom looks the same.

| | OTS-OLED wedge (ots-oled-bringup.md 2026-09-08/10) | 15.6-2k5 today |
|---|---|---|
| 984 measured H total | wanders 4415..5313 vs programmed 3440, stays there | 2811..2817 vs 2816; only the torn artefacts 2560 / 3070 are "wrong" |
| Consecutive reads | consistently wrong | wrong reads are isolated, next read is fine (restore path re-reads 2814) |
| Programmed H total | 3440 = 0x0D70, jitter never crosses a byte boundary | 2816 = 0x0B00, jitter crosses it constantly |
| Panel after DTG pulse on healthy link | latches black, power cycle only (event C) | recovers with a 984 digital reset |
| Root cause | real DTG fault upstream in 984 | software: non-atomic 16-bit read + aggressive recovery |

What *is* shared: both handover-doc findings apply here too. (a) A DTG reset pulse on a
healthy, streaming pipeline is not harmless. (b) The guard's "force_wedged" restore
skips its own sanity re-read, so one bad measurement is enough to pulse. The OLED
work already showed that the detection half must never fire on a healthy pipeline;
this bug is a case where it does.

The OLED doc's 2026-09-08 note ("a wedge with no video loss") and the 988 note in
the driver (measured 3730..4456 vs 2028) are real wedges with the value far away and
stable; they should be kept. The fix below keeps them working.

## 6. Which profiles are exposed (H total vs 256-boundary)

Measured 984 H total sits within about −5..+2 px of programmed on this rig, and
+1..+8 px on the OTS-OLED rig, so anything within ~8 px of a multiple of 256 is at risk.

| Profile | Pi/983 H total | hex | distance to boundary | risk |
|---|---|---|---|---|
| 15.6-2k5 | 2816 | 0x0B00 | 0 | **hits it (this bug)** |
| 3x-qvue (988) | 5628 | 0x15FC | 4 below 0x1600 | marginal, watch it |
| 14.6-2k5 | 2804 | 0x0AF4 | 12 | ok |
| 17.3-3k | 3084 | 0x0C0C | 12 | ok |
| 12.3 / 12.3-nq1 (988) | 2070 / 2074 | 0x816 / 0x81A | 22 / 26 | ok |
| 14.6-fhd (988) | 2028 | 0x7EC | 20 | ok |
| ots-oled-17 | 3440 (983 regenerates) | 0x0D70 | 112 | ok |
| 27 | 4248 | 0x1098 | 104 | ok |

Also: V total 1492 = 0x05D4 is not near a boundary, and `hh983_guard_wedge_snapshot()`
only logs V total, it never decides on it.

## 7. Recommended fix (driver, `br-wrapper/package/hh983-serializer/src/hh983-serializer.c`)

Ordered by importance. 1 and 2 are the actual bug fix; 3 and 4 are hardening the
OLED work already argued for.

1. **Make `hh983_read_htotals()` tear-proof.** Read MSB, LSB, MSB again and accept only
   when both MSBs agree (retry up to 3 times); or read the pair twice and require the
   two 16-bit results to agree within ±4. Apply the same to `MEAS_VTOTAL` in the
   snapshot. This is the minimum change and by itself removes the false wedges. The
   same helper is used by mode 1/2 (988) via `hh983_des988_check_dtg()`, so all modes
   benefit.

2. **Add plausibility filtering before calling anything a wedge.** A torn value is
   always exactly `prog & 0xFF00` (2560) or `(prog & 0xFF00) − 2` ± few (3070) — but
   rather than special-casing, require the *same* out-of-tolerance value class on both
   consecutive polls (e.g. both polls above +100 or both below −100, and |Δ between the
   two polls| < 64). Real wedges (4400..5400 drifting slowly, 3730..4456 on 988) pass;
   alternating 2560/3070 artefacts do not.

3. **Restore path must not trust a single measurement either.** `hh983_guard_restore_stream()`
   with `force_wedged=false` (boot / resync) pulsed on a torn 2560 at 15:30:37. Use the
   tear-proof read there too, and consider `DP_GUARD_WEDGE_POLLS` = 3.

4. **Reduce the blast radius of a restore on non-OLED 984 panels.** Experiments 4.2/4.3
   show what one restore costs on this panel: the DTG pulse throws it into BIST 4 times
   out of 5, and only a 984 digital reset (reg 0x01 = 0x01) gets it back; the stream
   cut/enable alone is harmless. Consider: (a) a per-profile / module-param way
   to run `dtg_recover=0` (log-only) on panels that are known to recover by digital
   reset instead; (b) on a mode-0 restore, try the cheaper 984 digital reset only
   when a wedge is confirmed twice and the pulse did not fix it. Keep the OLED order
   (cut, pulse, settle, enable) as the default because that panel latches otherwise.

5. **Optional, Pi side:** the `15.6-2k5` HDMI timing could be nudged so H total is not
   a multiple of 256 (e.g. HBP 222 → 218, H total 2812, pixel clock adjusted to keep
   62 Hz). That avoids the boundary but does not fix the driver; not recommended as
   the only change.

Stop-gap applied on the rig right now, **runtime only, reverts on reboot**:
`dtg_tolerance=600` (sysfs). Torn reads produce |Δ| = 256 or 254, so 600 masks them
while still catching the real wedges seen so far (Δ ≥ +900 on OLED, ≥ +1700 on 988).
`/etc/modprobe.d/hh983.conf` was **not** changed (still `options hh983-serializer
config_mode=0`); if you want the stop-gap to survive the validation power cycles, add
`dtg_tolerance=600` there until the driver fix is in.

## 8. How to reproduce / validate

- Torn reads, any time, no reboot needed (driver paused or not):
  `fpdlink-tool.sh --target=984 --timings` a few times, or the raw loop:
  `i2cset -f -y 1 0x2c 0x40 0x50; for i in $(seq 150); do i2cset -f -y 1 0x2c 0x41 0x40; h=$(i2cget -f -y 1 0x2c 0x42); i2cset -f -y 1 0x2c 0x41 0x41; l=$(i2cget -f -y 1 0x2c 0x42); echo $(( ((h&0x7f)<<8)|l )); done | sort -n | uniq -c`
  Expect a cluster at 2813..2816 plus outliers at exactly 2560 and 3066..3071.
- False wedges: `dmesg | grep "DTG wedge"` and `cat /sys/module/hh983_serializer/parameters/dtg_wedge_count`.
  With the fix, `dtg_wedge_count` must stay 0 over a 30-minute idle run on 15.6-2k5.
- Black screen: leave the rig idle after boot with default parameters; luminance
  (`measure-display.sh --sensor-only=yes` with the white pattern up) drops to 0 at a
  restore event. Note on the Pi `i2cget/i2cset` are in `/usr/sbin`, which is not on the
  PATH of a non-login ssh shell; `fpdlink-tool.sh` silently reports FAIL otherwise.
- Do not `rmmod`/`modprobe` the driver with the panel lit (OLED doc event B); install
  the new `.ko` and reboot.

## 9. Rig state

After the 16:17 power cycle (section 10) the rig is back on driver defaults: the
runtime stop-gap is gone, `dtg_tolerance=32`, the guard is producing false wedges
again and the panel was black (0.0 nits) at 16:23. Nothing persistent was changed on
the Pi in this session; `/etc/modprobe.d/hh983.conf` is still
`options hh983-serializer config_mode=0`. Scratch scripts from this session live in
`/tmp` on the Pi and are gone after the cycle. Sync Video (984 digital reset) brings
the picture back at any time, until the next false wedge.

## 10. Power cycle on 2026-09-13 16:17 (defaults, no stop-gap): reproduced

The rig owner power-cycled the whole chain through the Tasmota socket. Checked 5 min
after boot with nothing touched:

```
uptime 5 min, dtg_tolerance=32 (default), poll_interval_ms=1000
dtg_wedge_count=5        (false wedges at 16:21:30 [2560], 16:22:02 [3070], 16:22:50 [2560], ...)
running app: none (desktop)
i1Display Pro: Y = 0.000 nits   <-- panel black
```

Same signature as the first boot: torn values only, healthy re-read at 2814/2815,
panel black within a couple of minutes. Two boots out of two.

## 11. Instructions for the fixing session (Opus)

### 11.1 Fix

Fix the driver in `br-wrapper/package/hh983-serializer/src/hh983-serializer.c` as per
section 7 (tear-proof `hh983_read_htotals()` first, plausibility check second). Build
the `.ko` for `6.12.109-v8+`, install it on the rig and **reboot** (never rmmod/modprobe
with the panel lit, OLED doc event B).

### 11.2 Validation script: repeated power cycles, stop at the first black screen

Write a script (suggested location `br-wrapper/package/hh983-serializer/src/scripts/`
or the `misc-tools` repo, whichever the harness doc uses) that runs from the developer
host and loops power cycles until it either reaches N clean cycles or **stops at the
first black screen so the failed state can be inspected live**. Decide "screen on"
with the attached i1Display Pro, not with registers: section 3.2 shows the registers
look healthy while the panel is black.

Remote access: `tmp-docs/how-to-manage-remote-rig.md` (hosts, ssh/scp one-liners,
Tasmota commands, colorimeter usage, gotchas). Values for **this** rig today:

| Item | Value |
|---|---|
| Head-unit Pi | `pi@192.168.1.243` (the how-to lists other Pis; the rest of its recipe applies) |
| Tasmota socket (display + 983 + Pi) | `http://192.168.1.186/cm?cmnd=Power`, `Power%20OFF`, `Power%20ON` |
| ssh | `sshpass -p <pw> ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=6 pi@192.168.1.243` |
| i2c tools on the Pi | `/usr/sbin` (add to PATH in every remote command) |
| Colorimeter | `timeout 60 /home/pi/micropanel/share/disptool/display-test-framework/measure-display.sh --sensor-only=yes --quiet=yes \| grep ",SENSOR," \| tail -1 \| cut -d, -f6` gives Y in nits |
| White test pattern | `/home/pi/micropanel/usr/bin/play-pattern.sh --pattern=white` (allow 3-4 s) and `stop-pattern.sh` (allow ~6 s), as in the how-to; or `launcher-client --command=start-app --command-arg=pattern-generator` + `launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg=white` + `--command=stop-app`, which is what this session used |

Per cycle:

1. `sync` on the Pi, Tasmota `Power OFF`, wait 12 s, `Power ON`.
2. Wait for ssh (poll every 5 s, give up after 180 s → count as a failure and stop).
3. Wait until `uptime` ≥ 120 s so the desktop and the driver's first restores have
   happened (the false wedges came at +2 s, +20 s, +50 s after probe on both boots).
4. Start the pattern generator, set white, wait 3 s, take 5 luminance samples 3 s apart.
   Pass = all five ≥ 800 nits (white measured 995..1034 on this panel). Anything else
   (0.0, or the BIST cycle of ~1020/580/455/300/233/215/194/0.8) = black-screen event.
5. Record per cycle: timestamp, `dtg_wedge_count`, `dmesg | grep -c "DTG wedge"`,
   the five samples, and the 150-read torn histogram from section 8 (it should show
   outliers even after the fix; the point is that the driver no longer acts on them).
6. On pass: `stop-app`, loop. On fail: **do not power-cycle**, print the collected
   evidence and the kernel log, leave the rig as is, exit non-zero.
7. Optional soak: after the sample, keep the white pattern up and re-sample every
   30 s for 10 minutes before the next cycle, since the false wedges are spread out
   in time; then `stop-app`.

Pass criterion for calling the bug fixed: 10 consecutive cycles (with the 10-minute
soak, or 20 without) with zero black-screen events **and** `dtg_wedge_count == 0` at
the end of every cycle. Before the fix, expect the script to stop on cycle 1 or 2, which
is also a useful check that the script's detection works.

Notes for the script author:

- The Tasmota cycle reboots the 983 and the panel as well as the Pi; `hh983-serializer`
  loads on its own and the guard's boot-time restore runs 2 s later.
- `measure-display.sh` needs the pattern generator (port 8082) only for changing the
  pattern; `--sensor-only=yes` reads the sensor regardless of what is on screen.
- The runtime stop-gap `dtg_tolerance=600` is gone after every cycle; do not rely on it.
  If you want a control run, put it in `/etc/modprobe.d/hh983.conf` and expect zero
  wedges without any code change.
- Never `rmmod`/`modprobe hh983_serializer` with the panel lit.
