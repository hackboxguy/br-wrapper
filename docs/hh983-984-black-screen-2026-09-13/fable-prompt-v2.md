# TASK v2 FOR THE FIXING SESSION: real wedges are no longer detected, and the recovery does not fit this panel

Read `tmp-docs/opus-report-v1.md` (your own previous report) and `tmp-docs/fable-prompt-v1.md`
first. Everything there stands: the torn-read root cause, the confirmation approach, the
24 clean cold cycles. This document is the review of that work plus the next task. Work
on the same `br-wrapper` branch `fix/984-htotal-torn-read` (currently at `d44d74f`).
Same rig, same ground rules as v1 section T1 (Pi `192.168.1.243`, Tasmota `192.168.1.186`,
password and recipes in `tmp-docs/how-to-manage-remote-rig.md`, `/usr/sbin` on PATH,
`sync` before a power cut, never rmmod/modprobe with the panel lit, no passwords in files).

## R1. Review verdict on v1

Accepted as is: the torn-read diagnosis and its confirmation-based fix for false wedges,
the dropped 64 px window (my "drifts slowly" claim was wrong, your 4201..5110 data is
right), the command-response colorimeter verdict (my "five samples ≥ 800 nits" criterion
was unsound, your BIST characterisation proves it), the validation script, and the
report format. Keep all of it.

Not accepted: the shipped build `b22cb1f` **does not detect a genuine wedge on this rig
any more**, and the v1 validation could not see that because cold power cycles never
produce a genuine wedge here. Warm reboots do (your own section 7.2). Details below.

## R2. Warm reboot with the final build (Fable, 2026-09-13 18:48, `5c1f871...` installed)

`sudo reboot`, then at uptime 101 s (`fable-prompt-v1-data/warm-reboot-final-build.txt`):

```
[    6.846] DP video back (VP_STS=0x01), restoring 984 video stream
[    6.852] DP guard restore: DTG measured 4495 not confirmed by further reads (programmed 2816), no pulse
[    6.852] DP guard restore: 984 stream_en=0, DTG measured Htotal=4495, 983 Htotal=2816
uptime=101 dtg_wedge_count=0
fpdlink-tool --timings: 984 measured H Total 4751          <-- genuinely wedged, 95 s later, still undetected
command-response: white 1010 OK, red -> Y=355 x=0.364 (NOT red), green -> Y=270 y=0.329 (NOT green)
984 digital reset (0x01=0x01), 4 s:
command-response: red x=0.685 OK, green y=0.716 OK, white 1007 OK; measured H Total 2815
```

Three facts:

1. The DTG was genuinely wedged from boot (4495, later 4751 against 2816) and the driver
   declined to act on it at boot and for the next 95 s of polling. `dtg_wedge_count`
   stayed 0. The pre-v1 driver detected the same kind of wedge (your
   `post-install-real-wedge.txt`, measured 4853, with build `42591be`).
2. While wedged the panel does not show the Pi's picture (the command-response verdict
   catches it: commanded red and green do not arrive).
3. **One 984 digital reset both clears the wedge (measured back to 2815) and brings the
   Pi's picture back.** That is the same action as the Stream Deck "Sync Video" button.

## R3. Why detection broke: a torn-read guard that eats the wedge it should catch

`hh983_read_meas15()` returns `-EAGAIN` after three attempts in which the two MSB reads
disagree. On a healthy 2816 that is rare. On a **wedged** DTG the measurement wanders by
hundreds of px between consecutive reads (your 4201..5110 data, 0x1069..0x13F6), so the
MSB differs between the first and third read most of the time, all three attempts fail,
and the function returns `-EAGAIN`. Then:

- `hh983_read_htotals()` turns that into `-EIO`;
- `hh983_dtg_confirmed_bad()` returns **false** on any failed read, i.e. "not confirmed,
  torn read" (the boot log above: "DTG measured 4495 not confirmed by further reads");
- `hh983_guard_check_dtg()` on a failed read resets `guard_dtg_count` and returns ("a
  failed read is not evidence of a wedge").

So the more wedged the DTG, the more unreadable it looks, and the less the guard does.
This also reaches modes 1 and 2 through the shared helper: the 988 wedge in the driver's
own comment (3730..4456, wandering) would now be swallowed the same way.

The confusion is between two kinds of "bad read": a **torn** read of a healthy value
(wrong by exactly ±256, alternating sides) and a **noisy** read of a wedged value (any
value, but always far out on one side). A torn read of a wedged 0x11xx is still ≥ 0x1100 =
4352, far above 2816. Tearing can only move a value inside its own 256-block or the
adjacent one, so it can never make a wedged value look healthy, and it can never make a
healthy value look more than ~256 px off. The confirmation must therefore work on
**raw** samples and not require them to be untorn.

### R3.1 Required change (driver)

- `hh983_read_meas15()`: keep the MSB/LSB/MSB attempt loop, but when all attempts
  disagree return the **last raw pair** (as a value, not an error) and report the tear
  through an out-parameter or a separate function. `-EIO` stays reserved for a real I2C
  failure. Do not let a torn read become an error.
- `hh983_dtg_confirmed_bad()`: every sample is a raw read; the rule stays "all
  `DP_GUARD_MEAS_SAMPLES` out of tolerance and on the same side". Torn samples count
  like any other. Only a genuine I2C error aborts (return false and log it once, at
  `dev_warn`, rate-limited).
- `hh983_guard_check_dtg()` and `hh983_des988_check_dtg()`: same distinction. A torn
  read is a measurement, not a bus failure.
- Reasoning check before you build: with a healthy 2816, a torn sample is 2560 (below) or
  3070 (above); five raw samples all out of tolerance **and** all on the same side needs
  five tears of the same kind in a row, probability well under 1e-4 per poll at the
  measured 15 % tear rate. With a wedged 4500, every raw sample is above 2816 whatever
  tearing does. Write this as a comment on the function.
- Unit-style check on the host is possible: extract the decision into a pure function
  over an array of samples and test it with the recorded distributions (healthy torn
  histogram from `capture-black.txt`, wedged from `post-install-real-wedge.txt`).

## R4. Recovery on this panel must be the 984 digital reset, not the DTG pulse

From your own data: after the driver pulsed a real wedge (`post-install-real-wedge.txt`),
the panel read 0.000 nits four times. From v1 section 4.3: a pulse on a healthy stream
drops it into BIST 4 of 5 times. From R2: a digital reset fixes both the wedge and the
picture. So on this panel the pulse is the wrong recovery even for a genuine wedge, while
the OLED work established the pulse (with the stream cut) as the right one there.

### R4.1 Required change (driver, mode 0)

Add a module parameter, e.g. `wedge_recovery` (0644, default 0):

- `0` = current behaviour: cut, DTG pulse, settle, enable (OLED-validated order,
  unchanged code path).
- `1` = cut stream (APB 0x084 = 0), 984 digital reset (main page 0x01 = 0x01), wait for
  FPD-Link lock (poll 0x54 bit 0, up to ~1 s), settle, enable stream, then re-measure;
  if the measurement is still out of tolerance, fall through to the pulse path once and
  log it.

Use it in both places a wedge is recovered: the periodic check and the boot/resync
restore. Verify on the rig that the digital reset preserves the driver's 984
configuration (dump 984 main page 0x00..0x5F before and after; the analysis session's
diff in v1 section 3.4 showed only clear-on-read status bits changing, so expect the
same) and that the guard's own state (`guard_stream_cut`, `guard_video_up`) is left
consistent.

### R4.2 Where the value is set

Check `/home/pi/micropanel/usr/bin/hh983-config.sh` and `pi-config-txt.sh` on the Pi:
if module options are already written per display type, add `wedge_recovery=1` for
`15.6-2k5` there (and document it); if not, put it in `/etc/modprobe.d/hh983.conf` on
this rig (`options hh983-serializer config_mode=0 wedge_recovery=1`) and say so in the
report. Do **not** change the default to 1: the OTS-OLED is not attached, and the digital
reset has never been tried on it.

## R5. Validation (must all pass before the report)

Extend `power-cycle-validate.sh` with `--warm` (reboot through ssh `sync; sudo reboot`
instead of the Tasmota socket; everything else identical). Per-cycle log gains: measured
H total at first read after boot, `dtg_wedge_count`, and whether a recovery ran (grep
the dmesg line). Then, with the new driver installed and `wedge_recovery=1` configured:

1. **Warm reboots, 8 cycles, `--verdict=sequence`.** Pass per cycle: the panel passes the
   command-response verdict **without any manual intervention**, and if dmesg shows a
   wedge at boot then it also shows the recovery and a measured H total back within
   tolerance. Record how many of the 8 actually wedged (on 2026-09-13 both warm reboots
   observed did). If none wedges in 8, say so; do not fake it.
2. **Cold power cycles, 10 cycles, `--verdict=hold`.** Same criteria as v1: no failure,
   `dtg_wedge_count == 0` on every cycle (cold boots do not wedge; a non-zero count here
   is a false wedge and a regression of R3.1).
3. **Injected wedge with the poll paused**, once: warm reboot with `poll_interval_ms=0`
   set through a temporary `/etc/modprobe.d/zz-hh983-test.conf` (remove it afterwards),
   confirm the DTG is wedged (`--timings`), then set `poll_interval_ms=1000` and check
   that the guard detects it within `2 x poll_interval_ms`, recovers by digital reset,
   and the panel passes the verdict. Then repeat once with `wedge_recovery=0` to record
   what the pulse path does to this panel (expected: picture lost, `--verdict` fails;
   recover by hand with `fpdlink-tool.sh --recover --target=984`).
4. **Torn-read histogram still shows tearing** (diagnostic only, as in v1): 11..18 % of
   raw reads. The driver must no longer act on it.

Leave the rig lit on the desktop, fixed module installed, `wedge_recovery=1` configured,
`dtg_wedge_count` equal to the number of genuine wedges of the current boot only.

## R6. Smaller items

- The odd luminances right after the digital reset in R2 (commanded red 571 nits with
  correct red chromaticity, green 222 with correct green chromaticity, against 268 / 710
  when healthy) were taken 3 s after the pattern command. Check once whether the panel
  needs longer than 3 s to settle after a digital reset, and if so raise the settle in
  the script for the first verdict after a recovery.
- The fixed `fpdlink-tool.sh` is still not the one in `/home/pi/micropanel/bin/`. Leave
  that as it is (it ships with the micropanel image), but say so again in the report.
- Modes 1 and 2 get the R3.1 change through the shared helper; they still lack the
  confirmation step. Route `hh983_des988_check_dtg()` through `hh983_dtg_confirmed_bad()`
  as well, mark it "code-reviewed, not bench-tested (no 988 rig attached)" in the commit
  message and the report.
- Squash nothing; keep the commit history as in v1, one commit per change.

## R7. Report

Write `tmp-docs/opus-report-v2.md`: commits added, full driver diff against `d44d74f`,
the warm-reboot table (cycle, wedged at boot yes/no, measured before recovery, recovery
ran yes/no, verdict), the cold-cycle table, the injected-wedge results for both
`wedge_recovery` values, the histogram, the final rig state, and anything that differs
from what this document expects. Do not edit `fable-prompt-v1.md` or `fable-prompt-v2.md`.
