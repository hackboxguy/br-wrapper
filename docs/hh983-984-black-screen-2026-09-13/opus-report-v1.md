# 15.6" 2K5 (0OD) black screen: fix, validation and corrections

Report for `tmp-docs/fable-prompt-v1.md`, written 2026-09-13 on the rig
`pi@192.168.1.243` (kernel `6.12.109-v8+`, `hh983-serializer` mode 0, profile
`15.6-2k5`). The analysis document was not edited; everything that turned out
differently is in section 7 below.

## 1. Summary

The root cause in sections 1 to 6 of the analysis is confirmed: the guard was
declaring a DTG wedge on a torn 16-bit read of the 984's `MEAS_HTOTAL`, and the
resulting DTG reset pulse is what threw the panel into its TDDI BIST. The fix
is in the driver and is validated by repeated cold power cycles.

Three things did not go as the analysis document expected, and all three
changed the shipped fix:

1. **A single tear-proof read is not achievable the way section 7.1 assumed.**
   Re-reading the MSB after the LSB is not sufficient, and the bench proved it:
   with that fix installed the guard still pulsed the DTG twice in 83 s. The
   counter can cross the byte boundary and come straight back inside the ~3 ms
   the triple takes. The driver now *confirms* a bad measurement with four more
   samples instead of trying to read once perfectly.
2. **Real wedges do not "drift slowly".** A genuine wedge on this rig ranged
   over 4201..5110 against a programmed 2816 -- 909 px of spread, consecutive
   samples hundreds of px apart. The "two polls must agree within 64 px" rule
   the task asked for would have stretched detection from two polls to roughly
   ten, past the ~10 s in which the OTS-OLED latches black. It was dropped in
   favour of the side test alone.
3. **The colorimeter pass criterion in section 11.2 cannot distinguish the
   panel's BIST from the Pi's picture.** The BIST reproduces the Pi's white,
   red, green and blue to within the sensor's repeatability. A five-sample
   "all >= 800 nits" verdict could have passed a panel showing BIST. The
   validation harness uses a command-response test instead.

## 2. Branch and commits

Branch `fix/984-htotal-torn-read`, from `main` at `f1b3251`, in `br-wrapper`.

| Commit | Subject |
|---|---|
| `42591be` | hh983-serializer: stop a torn H-total read from faking a DTG wedge |
| `aaf0c4a` | hh983-serializer: a real wedge wanders, so only require the same side |
| `b22cb1f` | hh983-serializer: confirm a bad DTG measurement with more samples |
| `2b52d02` | power-cycle-validate.sh: judge the panel by command and response |
| `1e4d45c` | power-cycle-validate.sh: --hist-every, and why soak length is the cheap part |
| `d44d74f` | power-cycle-validate.sh: --verdict=hold, a cheap sound verdict |

The first three are the driver; `42591be` and `aaf0c4a` are superseded in
behaviour by `b22cb1f` but kept separate because each was installed on the rig
and measured, and the report below quotes what each one did.

## 3. What the driver does now

Three changes in `hh983-serializer.c`, all in the mode-0 DP guard's wedge path:

**`hh983_read_meas15()`** reads a measured 15-bit counter as MSB, LSB, MSB and
accepts the pair only when both MSB reads agree, up to three attempts. This
removes a crossing that happened and stayed. It does not remove a crossing that
went and came straight back between the two MSB reads -- see section 7.1 -- so
it is a cheap first filter, not the guarantee section 7.1 of the analysis hoped
for. `hh983_read_htotals()` and the wedge snapshot's V total both use it, so
modes 1 and 2 get it through the shared helper.

**`hh983_dtg_confirmed_bad()`** is the real fix. After an out-of-tolerance
measurement it takes four more and requires every one of them to be out of
tolerance and on the same side of the programmed value. A tear is a minority
event, so a run of five is out of reach; a wedged DTG fails all five however
much it wanders, so a real wedge is still declared on the same poll it always
was. Only a suspicious read pays for the extra samples: a healthy pipeline
returns on the first of them, and a first read inside tolerance never gets
there.

**`hh983_wedge_consistent()`** requires the two consecutive out-of-tolerance
polls of the debounce to be on the same side of the programmed value. This is
the plausibility rule the task asked for, minus its 64 px agreement window
(section 7.2).

The boot/resync restore path (`force_wedged=false`), which pulsed 2 s after
every probe on a torn 2560, goes through the same confirmation before it
pulses. The OLED restore order -- cut, pulse only if wedged, settle, enable --
is unchanged, and modes 1 and 2 are untouched beyond the shared read helper.

`fpdlink-tool.sh` `ind_read15_be()` takes the median of three tear-proof
samples, for the same reason and with the same limitation acknowledged: it
feeds `--timings` and `--diagnose`, where a torn value produced a false
"984 DTG stuck on corrupt H_TOTAL" verdict on a healthy pipeline.

## 4. Build

The Pi has no headers for `6.12.109-v8+`, so the module is cross-built on the
development host:

```bash
K=/home/adav/pi-image-workspace/kernel-build/linux
SRC=$PWD/br-wrapper/package/hh983-serializer/src
make -C $K ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$SRC modules
make -C $K ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$SRC clean   # before committing
```

Clean build, no warnings. Vermagic as required:

```
vermagic=6.12.109-v8+ SMP preempt mod_unload modversions aarch64
```

## 5. Install

```bash
scp hh983-serializer.ko pi@192.168.1.243:/home/pi/hh983-serializer.ko.fix
ssh pi@192.168.1.243 '
  sudo cp /lib/modules/6.12.109-v8+/extra/hh983-serializer.ko \
          /home/pi/hh983-serializer.ko.before-torn-fix
  sudo cp /home/pi/hh983-serializer.ko.fix \
          /lib/modules/6.12.109-v8+/extra/hh983-serializer.ko
  sudo depmod -a && sync && sudo reboot'
```

| Item | Value |
|---|---|
| Backup of the original on the Pi | `/home/pi/hh983-serializer.ko.before-torn-fix`, md5 `376df2961f4f2abd8e7693cd2942cd7b` (matches the md5 the task quoted) |
| Installed module, final | md5 `5c1f871cee85e32a265261c67cd82541` (commit `b22cb1f`) |
| `/etc/modprobe.d/hh983.conf` | unchanged, `options hh983-serializer config_mode=0` -- no `dtg_tolerance` stop-gap, so the validation ran against the real fix |

Intermediate builds installed and measured on the way: `42591be` md5
`c3de135f168eeede057c8a613f8abcb2`, `aaf0c4a` md5
`981275df943aa0e563a82247605d22b8`.

## 6. Validation

### 6.1 The harness

`br-wrapper/package/hh983-serializer/src/scripts/power-cycle-validate.sh`, host
side, committed on the branch. It cold power-cycles the rig through the Tasmota
socket, waits for ssh and for a minimum uptime, decides whether the panel is
showing the Pi's picture using only the i1Display Pro, and **stops at the first
failure without power-cycling again**, dumping samples, `dtg_wedge_count`,
`dmesg | grep hh983`, `fpdlink-tool.sh --target=984 --diagnose` and the 150-read
H total histogram so the bad state can be inspected live. Per-cycle lines go to
`tmp-docs/fable-prompt-v1-data/power-cycle-validate-<run>.log`.

The pass decision never looks at a 983/984 register, and it is never a threshold
on a single measurement -- see section 7.3. Two verdict modes:

- `--verdict=sequence`: white, then black/red/green/blue in a freshly drawn
  order, then white. Each must arrive in luminance *and* chromaticity.
- `--verdict=hold`: one randomly chosen colour of red/green/blue, read twice
  `--hold-secs` apart, then white. The BIST is a 12-step cycle whose steps last
  a few seconds, so it cannot still be showing the held colour 10 s later; the
  closing white rules out a panel frozen on a still frame. About a third of the
  cost, and what the 15-cycle run below used.

A cycle also fails if `dtg_wedge_count != 0`. That is the more sensitive of the
two detectors: the DTG pulse only costs the picture about four times in five, so
the counter catches false wedges the colorimeter would sleep through.

### 6.2 Pre-fix run (unmodified module, md5 `376df29...`)

2 cycles requested; **stopped on cycle 1**, which is what the task predicted.

```
1,2026-09-13T16:49:49+02:00,uptime 209s,dtg_wedge_count=3,dmesg 6 lines,
  "0.000000 0.000000 0.000000 0.000000 0.000000",FAIL
```

Evidence in `power-cycle-validate-20260913-164556-FAIL-cycle1.txt`: panel at
0.000 nits, three false wedges (measured 3070, 3070, 3071 against a programmed
2816), and `fpdlink-tool.sh --diagnose` reporting **`[OK] Pipeline healthy`**
at the same moment -- the register/photometer disagreement the analysis
documented. The 150-read histogram showed the torn distribution unchanged:
11 x 2560, 13 x 3069..3071, plus one 32767 (a failed transfer) out of 150.

### 6.3 Two intermediate builds that did not pass

Reported because each changed the fix, and both are in section 7.

| Build | Result |
|---|---|
| `42591be` (MSB re-read only, as the task specified) | **FAIL on cycle 1.** Panel in BIST at 145 s; two false wedges, both on a measured 2560, at t=36.5 s and t=83.4 s. Samples 610 / 149 / 131 / 511 / 0.6 nits -- the BIST cycle. |
| `aaf0c4a` (adds same-side plausibility, drops the 64 px window) | Not separately cycle-tested; superseded within the hour by `b22cb1f` after the same 2560 wedges recurred. |

The `42591be` failure is the reason the shipped fix confirms rather than
re-reads, and it is also the run during which a **genuine** DTG wedge appeared
after the warm reboot (section 7.2).

### 6.4 Post-fix runs (`b22cb1f`, md5 `5c1f871...`, defaults, no stop-gap)

Two runs, 24 cold power cycles in total, no failures.

**Run A -- `--verdict=sequence`, `--min-uptime=100`, 2 min soak.** Stopped by
hand at 9/20 to switch to the faster verdict; all 9 passed.

| Cycle | Time | Uptime | Wedges | dmesg wedge lines | Measured sequence (Y, nits) | Result |
|---|---|---|---|---|---|---|
| 1 | 17:20:12 | 152 s | 0 | 0 | white 1097.6, blue 128.6, red 260.6, black 0.000, green 708.5, white 1096.8 | PASS |
| 2 | 17:25:33 | 157 s | 0 | 0 | white 1102.3, green 711.2, black 0.000, blue 129.2, red 261.4, white 1100.4 | PASS |
| 3 | 17:31:36 | 165 s | 0 | 0 | white 1096.2, green 707.7, blue 128.5, red 259.5, black 0.000, white 1096 | PASS |
| 4 | 17:37:43 | 167 s | 0 | 0 | white 1092.6, blue 128.1, red 256.5, black 0.000, green 707.3, white 1092 | PASS |
| 5 | 17:43:59 | 172 s | 0 | 0 | white 1090, red 254, green 708, blue 128, black 0.000, white 1092 | PASS |
| 6 | 17:49:48 | 150 s | 0 | 0 | white 1081, red 250, green 704, black 0.000, blue 127, white 1081 | PASS |
| 7 | 17:55:39 | 150 s | 0 | 0 | white 1080, red 250, blue 127, black 0.000, green 704, white 1081 | PASS |
| 8 | 18:01:43 | 155 s | 0 | 0 | white 1081, green 703, red 249, blue 127, black 0.000, white 1081 | PASS |
| 9 | 18:06:53 | 155 s | 0 | 0 | white 1081, blue 127, red 250, green 703, black 0.000, white 1081 | PASS |

**Run B -- `--verdict=hold --hold-secs=10`, `--min-uptime=75`, no soak.**
15/15 passed, about 2 min 17 s per cycle.

| Cycle | Time | Held colour | Y at t | Y at t+10 s | White | Wedges | Result |
|---|---|---|---|---|---|---|---|
| 1 | 18:13:26 | green | 703.79 | 703.42 | 1079.77 | 0 | PASS |
| 2 | 18:15:41 | blue | 127.87 | 127.72 | 1082.26 | 0 | PASS |
| 3 | 18:17:58 | blue | 127.94 | 127.87 | 1083.80 | 0 | PASS |
| 4 | 18:20:16 | blue | 128.01 | 128.01 | 1084.67 | 0 | PASS |
| 5 | 18:22:26 | green | 705.58 | 705.43 | 1086.28 | 0 | PASS |
| 6 | 18:24:37 | blue | 128.87 | 128.72 | 1091.33 | 0 | PASS |
| 7 | 18:26:52 | red | 257.29 | 256.20 | 1092.35 | 0 | PASS |
| 8 | 18:29:13 | green | 705.80 | 705.65 | 1088.26 | 0 | PASS |
| 9 | 18:31:24 | blue | 128.46 | 128.23 | 1090.30 | 0 | PASS |
| 10 | 18:33:33 | green | 707.06 | 706.40 | 1090.59 | 0 | PASS |
| 11 | 18:35:44 | green | 710.39 | 709.35 | 1095.56 | 0 | PASS |
| 12 | 18:38:05 | red | 258.23 | 257.66 | 1090.59 | 0 | PASS |
| 13 | 18:40:18 | red | 260.05 | 258.81 | 1095.92 | 0 | PASS |
| 14 | 18:42:29 | blue | 129.32 | 129.01 | 1096.58 | 0 | PASS |
| 15 | 18:44:47 | red | 260.12 | 259.32 | 1093.37 | 0 | PASS |

Every chromaticity matched its reference within 0.03 as well (omitted from the
table for width; full lines are in the run logs). The held readings agree to
0.1..0.4 % across the 10 s gap, which a BIST stepping every few seconds cannot
produce.

**The hardware has not changed, and that is the point.** The 150-read histograms
taken during the passing cycles still show the tearing in full:

| Run / cycle | 2560 | 3069..3071 | torn of 150 |
|---|---|---|---|
| A / 1 | 18 | 9 | 27 (18 %) |
| A / 5 | 10 | 7 | 17 (11 %) |
| A / 9 | 8 | 8 | 16 (11 %) |
| B / 1 | 10 | 8 | 18 (12 %) |
| B / 6 | 10 | 11 | 21 (14 %) |

The raw register still tears at 11..18 % of reads, exactly as before the fix.
The driver no longer acts on it.

### 6.5 Direct measurement of the decision rule

Independently of the power cycles, the shipped rule was run against the live
hardware from userspace with the driver poll stopped, 300 polls:

```
polls=300  first-read-out-of-tolerance=29  CONFIRMED-BAD=0
```

29 reads (9.7 %) tore past the MSB re-read; none survived the four-sample
confirmation.

## 7. Corrections to the analysis document

Put here rather than in `fable-prompt-v1.md`, as instructed. Sections 1 to 6 of
that document -- the root cause, the register evidence, the profile table -- are
confirmed and nothing below contradicts them.

### 7.1 A tear-proof read is not just "MSB, LSB, MSB again" (section 7.1, T2)

The task specified: read MSB, LSB, MSB again, accept only if both MSBs agree,
up to 3 attempts. That was implemented as `42591be` and installed. It reduced
the torn-read rate but did not remove it, and the panel still went black.
Measured on the rig with `42591be` running:

```
[   36.552] DP guard: DTG wedge without video loss (measured 2560, programmed 2816), restoring
[   83.426] DP guard: DTG wedge without video loss (measured 2560, programmed 2816), restoring
```

The reason is that the MSB re-read only catches a counter that crossed the byte
boundary **and stayed**. A counter that crosses and comes straight back inside
the ~3 ms the triple takes defeats it: both MSB reads see `0x0A`, the LSB read
in between catches the `0x00` of `0x0B00`, and the triple is internally
consistent and wrong by exactly 256. On a value that straddles `0x0AFF/0x0B00`
continuously -- which is this bug -- that is common, not rare: about one read in
twenty still came back torn.

Nothing available inside a single read distinguishes that result from a genuine
2560. The fix is to stop trying and confirm instead, which is what
`hh983_dtg_confirmed_bad()` does.

Measured directly on the rig, driver poll stopped, running exactly the shipped
rule for 300 polls:

```
polls=300  first-read-out-of-tolerance=29  CONFIRMED-BAD=0
```

So ~9.7 % of reads still tear past the MSB re-read, and none of them survives
the confirmation.

### 7.2 Real wedges do not drift slowly (section 5, section 7.2, T2)

The task asked that the two consecutive out-of-tolerance polls "must agree with
each other within 64 px and lie on the same side of the programmed value",
on the stated basis that real wedges are "thousands of px away, drifting
slowly".

The warm `sudo reboot` that installed the first build produced a genuine wedge
on this rig -- the how-to document already notes that Pi reboots wedge the
deserializer DTG where cold power cycles do not. Its measurement did not drift
slowly. 150 tear-proof reads taken during it:

```
4201 .. 5110 against a programmed 2816
909 px of spread, ~50 distinct values, consecutive samples hundreds of px apart
```

Against that, requiring two consecutive polls to agree within 64 px turns a
two-poll detection into roughly a ten-poll one. The OTS-OLED latches black at
about 10 s of distorted timing, and the existing comment in the driver records
that the two-poll budget was chosen precisely to stay inside it, so the 64 px
window is a real regression risk on the panel the guard was written for.

The agreement window was dropped in `aaf0c4a`; the same-side test was kept. The
window bought nothing anyway: it rejects an *alternating* torn pair (2560 then
3070), which the side test already rejects, and it never rejected a same-side
torn pair, since those two values are identical.

### 7.3 The colorimeter criterion in section 11.2 cannot see the difference

Section 11.2 specified: white pattern up, five samples 3 s apart, all >= 800
nits = pass. That cannot distinguish the Pi's picture from the panel's own TDDI
BIST, which is the exact failure being validated.

Measured with the i1Display Pro while the Pi sent nothing but solid white and
the panel was in BIST (`tmp-docs/fable-prompt-v1-data/bist-vs-pi-content.txt`):

| Pattern | Pi-generated | BIST step |
|---|---|---|
| white | Y=1109  x=0.3050 y=0.3301 | Y=1105  x=0.3044 y=0.3300 |
| red   | Y=268   x=0.6852 y=0.3134 | Y=267   x=0.6853 y=0.3133 |
| green | Y=710   x=0.2229 y=0.7175 | Y=709   x=0.2235 y=0.7171 |
| blue  | Y=129   x=0.1428 y=0.0856 | Y=129   x=0.1427 y=0.0854 |

The BIST walks a 12-step cycle containing all of those plus two blacks and five
greys. The sensor repeated to +-0.0007 in x and y across the whole session, so
those are the same readings: **no threshold on luminance, on chromaticity, or on
both can separate the two sources from a single measurement.** Worse, the BIST
steps at roughly the sample interval, so five samples 3 s apart could have
locked onto its white step and scored a pass on a black screen. The section 4.2
luminance sequence quoted in the analysis (1020 / 580 / 455 / ...) is that same
cycle sampled at a different phase.

What the BIST cannot do is follow the Pi. Commanded red and then green while it
was running, it answered:

```
cmd=red    0.93,  186.3, 1103.5      (red is Y=268 x=0.685)
cmd=green  128.9, 250.9,  251.0      (green is Y=710 y=0.717)
```

So the verdict is a command-response test: set a pattern, measure it, require it
to arrive in both luminance and chromaticity, over a sequence drawn fresh each
time. The order matters -- the BIST cycle runs white -> red -> green -> blue, so
a *fixed* command order is the one thing it could imitate by accident.

One convenient side effect: the Pi's black measures 0.000 where the BIST's
darkest steps measure 0.93 and 1.70, so black separates on luminance alone.

### 7.4 Smaller corrections

- **Pause the driver poll around any i2c hammering.** The 150-read histogram of
  section 8 reaches the 984 through the same indirect-access registers on the
  983 that the driver's own poll uses. Run concurrently, the two interleave, the
  driver's reads come back inconsistent and its wedge check goes blind. This
  cost about 40 s of detection latency before it was noticed. The analysis
  session paused the poll before every dump it took; the validation script now
  does the same (`1e4d45c`).
- **Soak length is the cheap thing to cut.** The failure latches -- the panel
  stays in BIST until a 984 digital reset -- and `dtg_wedge_count` is cumulative
  and catches wedges the colorimeter cannot (the pulse only costs the picture
  about four times in five). So a verdict at t=100 s sees everything that
  happened since boot, and cycles, which sample the per-boot clock relationship
  that actually varies, are worth more per minute than soak. The run below is
  20 cycles with a 2 min soak rather than 10 with 10 min.
- **`DP_GUARD_WEDGE_POLLS` was left at 2**, not raised to 3 as section 7.3
  suggested considering. With the confirmation in place each poll is already
  five agreeing measurements, and raising the debounce would cost response time
  against the OTS-OLED's ~10 s latch for no gain.
- **Section 7.4 (blast radius) was not implemented.** Out of scope for T2, and
  it needs the OLED panel present to validate the alternative recovery order.

## 8. Recommended follow-up

- **Modes 1 and 2 still decide on a single measurement.** T2 said not to touch
  them beyond the shared read helper, so they have `hh983_read_meas15()` but not
  `hh983_dtg_confirmed_bad()`. Section 6 of the analysis lists `3x-qvue` (988)
  at an H total of 5628, 4 px below `0x1600` -- the same exposure this bug had.
  Routing `hh983_des988_check_dtg()` through the confirmation is a small change
  and should be done before that profile is used in anger; it needs a 988 rig to
  validate.
- **The rig's installed `fpdlink-tool.sh` is still the old one.**
  `/home/pi/micropanel/bin/fpdlink-tool.sh` was left untouched so the
  `--diagnose` output in the failure dumps stays comparable across the runs; the
  fixed copy is at `/home/pi/fpdlink-tool-torn-fix.sh`. It reaches the rig
  properly through the next micropanel image build.

## 9. Final rig state

Left as the task asked: lit, on the desktop, fixed module, driver defaults.

| Item | Value |
|---|---|
| Module | `/lib/modules/6.12.109-v8+/extra/hh983-serializer.ko`, md5 `5c1f871cee85e32a265261c67cd82541` (commit `b22cb1f`) |
| Backup of the original | `/home/pi/hh983-serializer.ko.before-torn-fix`, md5 `376df2961f4f2abd8e7693cd2942cd7b` |
| `/etc/modprobe.d/hh983.conf` | `options hh983-serializer config_mode=0` -- unchanged, no stop-gap |
| Parameters | `dtg_tolerance=32`, `poll_interval_ms=1000`, `dtg_recover=1`, `config_mode=0` -- all defaults |
| `dtg_wedge_count` | 0 |
| `dmesg \| grep -c "DTG wedge"` | 0 |
| Running app | none (desktop / launcher) |
| Colorimeter on the desktop | 8.99 nits (the launcher draws dark at the meter spot; the how-to records 0..7 as normal there) |
| Left on the Pi | `/home/pi/fpdlink-tool-torn-fix.sh` (fixed tool, not installed over the micropanel copy), `/home/pi/bist-vs-pi.sh` (the BIST characterisation), `/home/pi/hh983-serializer.ko.fix` |

Data files added under `tmp-docs/fable-prompt-v1-data/`:
`power-cycle-validate-*.log`, `power-cycle-validate-*-FAIL-cycle1.txt`,
`bist-vs-pi-content.txt`, `post-install-real-wedge.txt`.

## 10. Full diff

Against `main` at `f1b3251`. `power-cycle-validate.sh` is a new 300-line file
and is not reproduced here; it is on the branch.

### `package/hh983-serializer/src/hh983-serializer.c`

```diff
diff --git a/package/hh983-serializer/src/hh983-serializer.c b/package/hh983-serializer/src/hh983-serializer.c
index 20e18d1..91d3b36 100644
--- a/package/hh983-serializer/src/hh983-serializer.c
+++ b/package/hh983-serializer/src/hh983-serializer.c
@@ -13,6 +13,11 @@
  * Mode 0 has done this since the OTS-OLED bring-up; modes 1 and 2 gained it
  * after the failure was reproduced on a 988 (see hh983_des988_check_dtg).
  *
+ * The measurement all three act on is a free-running counter in two registers,
+ * so it is read tear-proof (hh983_read_meas15) and a wedge is only declared on
+ * measurements that agree with each other (hh983_wedge_consistent): a torn read
+ * of it cost the 15.6" 2K5 panel its picture every 40 s until 2026-09-13.
+ *
  * Author: Albert David
  */
 
@@ -205,6 +210,8 @@ MODULE_PARM_DESC(ots_touch, "Mode 0 only: 1=route the OLED-OTS HX8530 touch (984
 #define DP_GUARD_LOSS_POLLS      2     /* consecutive unsynced polls before cutting the stream */
 #define DP_GUARD_RESYNC_POLLS    2     /* consecutive synced polls before restoring the stream */
 #define DP_GUARD_WEDGE_POLLS     2     /* consecutive out-of-tolerance polls before calling it a wedge */
+#define DP_GUARD_MEAS_TRIES      3     /* attempts at an untorn read of a measured 15-bit counter */
+#define DP_GUARD_MEAS_SAMPLES    5     /* measurements that must all be wrong before a poll counts as bad */
 
 /* 984 configuration values */
 #define DES984_ENABLE_PASSTHROUGH 0xC9  /* GENERAL_CFG default 0xC1 | bit[3] I2C_PASS_THROUGH */
@@ -260,6 +267,7 @@ struct hh983_data {
 	/* Mode 0 DTG-wedge check (video up, DTG measurement wrong) */
 	bool guard_wedged;           /* currently in the wedged state */
 	int guard_dtg_count;         /* consecutive out-of-tolerance polls */
+	int guard_dtg_first;         /* first measurement of that run, for the plausibility check */
 	unsigned long guard_wedge_at;/* jiffies of the last wedge restore */
 	bool guard_wedge_armed;      /* guard_wedge_at holds a real timestamp */
 };
@@ -533,30 +541,147 @@ static void hh983_guard_cut_stream(struct hh983_data *data)
 		data->guard_stream_cut = true;
 }
 
+/* Read one of the deserializer's measured 15-bit DTG counters without tearing.
+ *
+ * MEAS_HTOTAL and MEAS_VTOTAL are two plain read-only bytes each (SNLS726
+ * 7.6.2.16.29/30): no latch, no shadow register, and the counter keeps
+ * updating between the two I2C transactions it takes to fetch them.  When the
+ * live value happens to sit on a 256 boundary that is a real problem: on the
+ * 15.6" 2K5 profile the programmed H total is 2816 = 0x0B00 and the measured
+ * one jitters 2811..2817, so it crosses the boundary many times a second and
+ * about 15 % of plain MSB+LSB reads came back as 2560 (fresh MSB, stale LSB)
+ * or 3070 (stale MSB, fresh LSB).  Two of those in a row looked exactly like a
+ * wedged DTG, and the guard spent a DTG reset pulse on a healthy pipeline
+ * every 40 s -- which throws that panel into its own BIST four times out of
+ * five (2026-09-13 analysis).  Every other profile on the bench is 12 px or
+ * more from a boundary, which is why only this one ever showed it.
+ *
+ * Re-reading the MSB after the LSB catches the plain case: if the counter
+ * crossed a byte boundary and stayed there, the two MSB reads disagree and the
+ * attempt is thrown away.  It does NOT catch a counter that crosses and comes
+ * straight back inside the ~3 ms the triple takes -- both MSB reads then see
+ * 0x0A while the LSB read sees the 0x00 of 0x0B00, which is self-consistent
+ * and wrong by exactly 256.  That is not a corner case on a value that
+ * straddles the boundary continuously: it still left about one read in twenty
+ * torn, enough to pulse the DTG twice in 83 s on 2026-09-13.  Nothing a single
+ * read can look at distinguishes that result from a genuine 2560, so the
+ * callers that act on it confirm with hh983_dtg_confirmed_bad() instead.
+ *
+ * Returns the 15-bit value, or negative on an I2C failure or on three torn
+ * attempts.
+ */
+static int hh983_read_meas15(struct hh983_data *data, u8 hi_off, u8 lo_off)
+{
+	struct i2c_client *client = data->client;
+	int attempt, hi, lo, hi_again;
+
+	for (attempt = 0; attempt < DP_GUARD_MEAS_TRIES; attempt++) {
+		hi = hh983_deser_ind_read(client, data->deser_addr,
+					  DES984_IND_PAGE_DTG, hi_off);
+		lo = hh983_deser_ind_read(client, data->deser_addr,
+					  DES984_IND_PAGE_DTG, lo_off);
+		hi_again = hh983_deser_ind_read(client, data->deser_addr,
+						DES984_IND_PAGE_DTG, hi_off);
+		if (hi < 0 || lo < 0 || hi_again < 0)
+			return -EIO;
+		if ((hi & 0x7F) == (hi_again & 0x7F))
+			return ((hi & 0x7F) << 8) | lo;
+	}
+
+	return -EAGAIN;
+}
+
+/* Do two consecutive out-of-tolerance measurements describe the same fault?
+ *
+ * A wedged DTG holds a wrong value on one side of the programmed one: the
+ * OTS-OLED sat above a programmed 3440 at 4415..5313, the 988 above 2028 at
+ * 3730..4456, and this bench's own 984 above 2816 at 4201..5110 after a warm
+ * reboot on 2026-09-13.  A torn read lands instead on whichever side the stale
+ * byte came from -- 2560 below 2816, 3070 above it -- so it alternates, and
+ * requiring the pair to fall on the same side rejects it without the code
+ * having to know anything about byte boundaries.
+ *
+ * Deliberately no "and the two agree within N pixels" on top of that.  The
+ * measurement of a real wedge is not steady: the 150 reads taken during the
+ * 2026-09-13 one were spread over 909 px with consecutive samples hundreds of
+ * px apart, so a 64 px agreement rule stretched detection from two polls to
+ * roughly ten -- past the ~10 s at which the OTS-OLED latches black, which is
+ * the deadline this check exists to meet.  Torn reads are kept out by
+ * hh983_read_meas15(), where the problem actually is; this is only here to
+ * stop an alternating artefact from pairing up with itself.
+ *
+ * Both arguments are known to be outside dtg_tolerance, so neither equals
+ * prog and the side test is unambiguous.
+ */
+static bool hh983_wedge_consistent(int meas, int prev, int prog)
+{
+	return (meas > prog) == (prev > prog);
+}
+
 /* Read the 984's measured input line length and the 983's programmed output
  * line length, the pair the guard compares to decide whether the 984 DTG has
- * wedged.  Returns 0 with both filled in, or a negative value if any of the
- * four register reads failed.
+ * wedged.  Returns 0 with both filled in, or a negative value if the measured
+ * value could not be read untorn or either programmed byte failed.
+ *
+ * Only the measured value needs the tear-proof read: the programmed pair is
+ * static configuration in the 983's VP, not a running counter.
  */
 static int hh983_read_htotals(struct hh983_data *data, int *meas, int *prog)
 {
 	struct i2c_client *client = data->client;
-	int meas_hi, meas_lo, prog_hi, prog_lo;
+	int meas_htotal, prog_hi, prog_lo;
 
-	meas_hi = hh983_deser_ind_read(client, data->deser_addr,
-				       DES984_IND_PAGE_DTG, DES984_DTG_MEAS_HTOTAL_HI);
-	meas_lo = hh983_deser_ind_read(client, data->deser_addr,
-				       DES984_IND_PAGE_DTG, DES984_DTG_MEAS_HTOTAL_LO);
+	meas_htotal = hh983_read_meas15(data, DES984_DTG_MEAS_HTOTAL_HI,
+					DES984_DTG_MEAS_HTOTAL_LO);
 	prog_lo = hh983_ind_read(client, SER_IND_PAGE_VP, SER_VP0_H_TOTAL_LO);
 	prog_hi = hh983_ind_read(client, SER_IND_PAGE_VP, SER_VP0_H_TOTAL_HI);
-	if (meas_hi < 0 || meas_lo < 0 || prog_lo < 0 || prog_hi < 0)
+	if (meas_htotal < 0 || prog_lo < 0 || prog_hi < 0)
 		return -EIO;
 
-	*meas = ((meas_hi & 0x7F) << 8) | meas_lo;
+	*meas = meas_htotal;
 	*prog = (prog_hi << 8) | prog_lo;
 	return 0;
 }
 
+/* Confirm that the DTG measurement is really wrong before a poll acts on it.
+ *
+ * One read is not enough even after hh983_read_meas15(), and the bench proved
+ * it on 2026-09-13: two polls read exactly 2560 against a programmed 2816, the
+ * guard pulsed the DTG twice inside 83 s, and that is what put the panel into
+ * its own BIST.  See hh983_read_meas15() for why a single read cannot tell a
+ * bounced boundary crossing from a genuine 2560.
+ *
+ * Another look can.  A tear is an accident of timing that most reads do not
+ * have, so further samples land back on the true value almost at once; a
+ * wedged DTG is out of tolerance on every one of them and on the same side,
+ * however much it wanders (4201..5110 against 2816 on this bench after a warm
+ * reboot, 4415..5313 against 3440 on the OTS-OLED, 3730..4456 against 2028 on
+ * the 988).  Requiring all DP_GUARD_MEAS_SAMPLES to be out of tolerance on one
+ * side raises the odds of a torn run to the fourth power of a single tear --
+ * out of reach -- while a real wedge is still declared on the poll it always
+ * was, which is what keeps the response inside the ~10 s the OTS-OLED takes to
+ * latch black.
+ *
+ * Only the suspicious path pays: a healthy pipeline returns on the first extra
+ * sample, and a first read inside tolerance never gets here at all.
+ */
+static bool hh983_dtg_confirmed_bad(struct hh983_data *data, int first, int prog)
+{
+	bool above = first > prog;
+	int i, meas = 0, meas_prog = 0;
+
+	for (i = 1; i < DP_GUARD_MEAS_SAMPLES; i++) {
+		if (hh983_read_htotals(data, &meas, &meas_prog) != 0)
+			return false;
+		if (abs(meas - meas_prog) <= dtg_tolerance)
+			return false;
+		if ((meas > meas_prog) != above)
+			return false;
+	}
+
+	return true;
+}
+
 /*
  * One-line context dump for the moment a wedge starts, for whoever ends up
  * scoping the DTG input clocking. The root cause is upstream of this driver --
@@ -576,15 +701,15 @@ static int hh983_read_htotals(struct hh983_data *data, int *meas, int *prog)
 static void hh983_guard_wedge_snapshot(struct hh983_data *data, int meas, int prog)
 {
 	struct i2c_client *client = data->client;
-	int mv_hi, mv_lo, meas_vtotal = -1;
+	int meas_vtotal;
 	int vp_sts, ser_sts, stream_en, des_sts0, des_sts1;
 
-	mv_hi = hh983_deser_ind_read(client, data->deser_addr,
-				     DES984_IND_PAGE_DTG, DES984_DTG_MEAS_VTOTAL_HI);
-	mv_lo = hh983_deser_ind_read(client, data->deser_addr,
-				     DES984_IND_PAGE_DTG, DES984_DTG_MEAS_VTOTAL_LO);
-	if (mv_hi >= 0 && mv_lo >= 0)
-		meas_vtotal = ((mv_hi & 0x7F) << 8) | mv_lo;
+	/* Same counter, same tearing: V total is only logged, never decided on,
+	 * but a torn one here would misdirect whoever reads the snapshot. */
+	meas_vtotal = hh983_read_meas15(data, DES984_DTG_MEAS_VTOTAL_HI,
+					DES984_DTG_MEAS_VTOTAL_LO);
+	if (meas_vtotal < 0)
+		meas_vtotal = -1;
 
 	vp_sts    = hh983_ind_read(client, SER_IND_PAGE_VP, SER_VP0_STS);
 	ser_sts   = hh983_read_reg(client, SER_GENERAL_STS);
@@ -641,8 +766,20 @@ static void hh983_guard_restore_stream(struct hh983_data *data, bool force_wedge
 		unreadable = hh983_read_htotals(data, &meas_htotal, &prog_htotal) != 0;
 	}
 
-	if (!force_wedged && !unreadable)
+	if (!force_wedged && !unreadable) {
 		wedged = abs(meas_htotal - prog_htotal) > dtg_tolerance;
+		/* This path decides on one measurement, and at boot it is the
+		 * path that usually runs: on 15.6-2k5 it pulsed 2 s after every
+		 * probe, on a torn 2560.  A pulse costs that panel its picture
+		 * four times out of five, so it has to pass the same
+		 * confirmation the periodic check applies. */
+		if (wedged && !hh983_dtg_confirmed_bad(data, meas_htotal, prog_htotal)) {
+			dev_info(&client->dev,
+				 "DP guard restore: DTG measured %d not confirmed by further reads (programmed %d), no pulse\n",
+				 meas_htotal, prog_htotal);
+			wedged = false;
+		}
+	}
 
 	if (unreadable && !force_wedged) {
 		dev_info(&client->dev,
@@ -728,6 +865,35 @@ static void hh983_guard_check_dtg(struct hh983_data *data)
 		return;
 	}
 
+	/* One out-of-tolerance read is not evidence: confirm it with more samples
+	 * before this poll counts against the debounce at all. */
+	if (!hh983_dtg_confirmed_bad(data, meas, prog)) {
+		dev_dbg(&client->dev,
+			"DP guard: out-of-tolerance %d (programmed %d) not confirmed, torn read\n",
+			meas, prog);
+		data->guard_dtg_count = 0;
+		return;
+	}
+
+	/* Out of tolerance.  A run of bad polls only counts as a wedge while the
+	 * polls stay on the same side of the programmed value
+	 * (hh983_wedge_consistent): one that does not starts a new run instead of
+	 * completing the old one, so an artefact that alternates sides never
+	 * reaches DP_GUARD_WEDGE_POLLS, while a real wedge -- which sits on one
+	 * side however much it wanders -- still fires on the second poll.
+	 */
+	if (data->guard_dtg_count > 0 &&
+	    !hh983_wedge_consistent(meas, data->guard_dtg_first, prog)) {
+		dev_dbg(&client->dev,
+			"DP guard: out-of-tolerance %d does not agree with %d (programmed %d), not a wedge\n",
+			meas, data->guard_dtg_first, prog);
+		data->guard_dtg_first = meas;
+		data->guard_dtg_count = 1;
+		return;
+	}
+	if (data->guard_dtg_count == 0)
+		data->guard_dtg_first = meas;
+
 	data->guard_dtg_count++;
 	if (data->guard_dtg_count < DP_GUARD_WEDGE_POLLS)
 		return;			/* one odd measurement is not a wedge */
```

### `package/hh983-serializer/src/scripts/fpdlink-tool.sh`

```diff
diff --git a/package/hh983-serializer/src/scripts/fpdlink-tool.sh b/package/hh983-serializer/src/scripts/fpdlink-tool.sh
index 0efddf2..b80529f 100755
--- a/package/hh983-serializer/src/scripts/fpdlink-tool.sh
+++ b/package/hh983-serializer/src/scripts/fpdlink-tool.sh
@@ -170,14 +170,44 @@ ind_read16_le() {
 
 # Read 15-bit BE indirect register: ind_read15_be <dev_addr> <page> <msb_offset> <lsb_offset>
 # MSB[6:0] at msb_offset, LSB[7:0] at lsb_offset -> (MSB[6:0] << 8) | LSB
+#
+# The measured-timing registers this reads are free-running counters with no
+# latch, so a value sitting on a 256 boundary tears: on the 15.6" 2K5 profile
+# (measured H total 2811..2817 across 0x0B00) about 15 % of plain reads came
+# back as 2560 or 3070, which is enough for --diagnose to report "984 DTG stuck
+# on corrupt H_TOTAL" on a perfectly healthy pipeline.
+#
+# Two defences, because one is not enough.  Each sample re-reads the MSB after
+# the LSB and is retried while the two MSBs disagree, which throws away a
+# crossing that stayed; and three such samples are taken and the median
+# returned, which throws away a crossing that went and came straight back
+# inside one sample (both MSB reads see 0x0A, the LSB read sees the 0x00 of
+# 0x0B00 -- self-consistent and wrong by 256).  A torn sample is a minority of
+# reads, so it never wins a median of three.  Static registers read the same
+# three times and are unaffected.
+_ind_read15_sample() {
+    _s_dev=$1; _s_msb=$2; _s_lsb=$3
+    _s_try=1
+    while [ "$_s_try" -le 3 ]; do
+        i2c_write "$_s_dev" 0x41 "$_s_msb"
+        _m=$(i2c_read "$_s_dev" 0x42)
+        i2c_write "$_s_dev" 0x41 "$_s_lsb"
+        _l=$(i2c_read "$_s_dev" 0x42)
+        i2c_write "$_s_dev" 0x41 "$_s_msb"
+        _m2=$(i2c_read "$_s_dev" 0x42)
+        [ $(( _m & 0x7F )) -eq $(( _m2 & 0x7F )) ] && break
+        _s_try=$((_s_try + 1))
+    done
+    echo $(( ((_m & 0x7F) << 8) | _l ))
+}
+
 ind_read15_be() {
     _dev=$1; _page=$2; _msb_off=$3; _lsb_off=$4
     i2c_write "$_dev" 0x40 "$_page"
-    i2c_write "$_dev" 0x41 "$_msb_off"
-    _msb=$(i2c_read "$_dev" 0x42)
-    i2c_write "$_dev" 0x41 "$_lsb_off"
-    _lsb=$(i2c_read "$_dev" 0x42)
-    echo $(( ((_msb & 0x7F) << 8) | _lsb ))
+    _v1=$(_ind_read15_sample "$_dev" "$_msb_off" "$_lsb_off")
+    _v2=$(_ind_read15_sample "$_dev" "$_msb_off" "$_lsb_off")
+    _v3=$(_ind_read15_sample "$_dev" "$_msb_off" "$_lsb_off")
+    printf '%s\n%s\n%s\n' "$_v1" "$_v2" "$_v3" | sort -n | sed -n 2p
 }
 
 # Read 13-bit BE indirect register (for sync widths): ind_read13_be <dev_addr> <page> <msb_off> <lsb_off>
```
