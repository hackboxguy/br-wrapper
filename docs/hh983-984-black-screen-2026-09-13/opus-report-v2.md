# 15.6" 2K5 (0OD): wedge detection restored, recovery matched to the panel

Report for `tmp-docs/fable-prompt-v2.md`, 2026-09-13, rig `pi@192.168.1.243`,
kernel `6.12.109-v8+`, `hh983-serializer` mode 0, profile `15.6-2k5`.
Continues `tmp-docs/opus-report-v1.md` on the same branch
`fix/984-htotal-torn-read`, which was at `d44d74f`. Neither prompt document was
edited; everything that differs from what v2 expected is in section 6.

## 1. Summary

The review is accepted in full. Build `b22cb1f` did detect false wedges
correctly and did survive 24 cold power cycles, but it could not reliably detect
a **genuine** wedge, and cold cycles cannot show that because they do not
produce one on this rig. Warm reboots do, every time.

Three changes, one harness fix, and the validation that was missing in v1:

1. **`hh983_read_meas15()` no longer turns a torn read into an error.** That was
   the detection bug, and its failure mode was exactly backwards: a wedged DTG
   is the state in which the measurement moves most between reads, so the MSB
   re-read failed most often, so the guard decided the register was unreadable
   and did nothing. The more wedged the part, the blinder the guard.
2. **`hh983_dtg_confirmed_bad()` works on raw samples.** A torn sample counts
   like any other and only a real I2C error aborts. This is safe because tearing
   cannot cross the two cases: it only moves a value inside its own 256-block or
   the neighbouring one.
3. **`wedge_recovery=1` recovers by 984 digital reset** instead of the DTG
   pulse, for this panel. Default stays 0.
4. Modes 1 and 2 now confirm too (code-reviewed, no 988 rig attached).

Validation: **8 warm reboots, all 8 of which wedged the DTG at boot, all 8 detected
and recovered without intervention**, plus 10 cold cycles with zero wedges, plus
the injected-wedge test for both `wedge_recovery` values.

## 2. Commits added (on top of `d44d74f`)

| Commit | Subject |
|---|---|
| `cbde990` | hh983-serializer: a torn measurement is a measurement, not an error |
| `e381ff9` | hh983-serializer: confirm the 988 wedge check too (modes 1 and 2) |
| `35cb78f` | hh983-serializer: wedge_recovery=1, recover by 984 digital reset |
| `b8db25c` | power-cycle-validate.sh: --warm, boot facts, and a re-measure on mismatch |
| `35e2545` | power-cycle-validate.sh: sequence mode never got the retry, and a sensor error is not a black screen |
| `48c73c0` | power-cycle-validate.sh: a non-numeric reading must never pass |

Nothing squashed; one commit per change, as asked.

## 3. The detection bug, measured

`hh983_read_meas15()` returned `-EAGAIN` when all three MSB/LSB/MSB attempts
disagreed. `hh983_read_htotals()` turned that into `-EIO`;
`hh983_dtg_confirmed_bad()` treated any failed read as "not confirmed"; and
`hh983_guard_check_dtg()` treated it as a bus problem and reset its counter.

What makes this more than a latency bug is that it is self-reinforcing. A torn
read of a *healthy* value is rare-ish and harmless. A *wedged* DTG measures
4201..5110 against a programmed 2816 -- 909 px of spread, consecutive samples
hundreds of px apart -- so the MSB frequently differs between the first and
third read of a triple, all three attempts tear, and the function reports
failure. The guard therefore did least exactly when it was needed most.

Two warm reboots on 2026-09-13 with build `b22cb1f` (md5 `5c1f871...`) installed, same
build, opposite outcomes:

| | Fable, 18:48 | this session, 18:56 |
|---|---|---|
| DTG at boot | 4495 | 4751 |
| Boot restore log | `DTG measured 4495 not confirmed by further reads (programmed 2816), no pulse` | `DTG measured Htotal=4751, 983 Htotal=2816 (DTG wedged)` |
| Recovery | none | DTG pulse |
| 95 s later | still wedged (4751), `dtg_wedge_count=0`, panel not showing the Pi | measurement back to 2814, panel passing the command-response verdict |

So the old build's response to a real wedge was a coin flip on how the reads
happened to tear. R3's diagnosis is confirmed.

### 3.1 The fix, and why raw samples are safe

Tearing moves a value by exactly a multiple of 256 and only into an adjacent
256-block. It therefore cannot make a wedged value look healthy, and it cannot
make a healthy value look more than about 256 px wrong. The confirmation can
work on raw samples, and must, because demanding untorn ones is what blinded it.

`scripts/wedge-rule-check.py` (new, on the branch) implements
`hh983_dtg_confirmed_bad()` plus the two-poll debounce as a pure function and
runs it against the two distributions recorded on this bench -- the healthy
histogram from a passing cold cycle (12 % torn) and the wedged one from
`post-install-real-wedge.txt`:

```
tolerance=32 samples/poll=5 polls/wedge=2 programmed=2816

healthy (12 % torn reads)      polls=1000000  confirmed-bad polls=4     wedges declared=0   PASS
genuinely wedged (4201..5110)  polls=1000     confirmed-bad polls=1000  wedges declared=500 first at poll=2  PASS
```

0 false wedges in 1,000,000 healthy polls (~11.6 days at 1 Hz), and a real wedge
declared on poll 2, i.e. ~2 s, inside the ~10 s the OTS-OLED takes to latch
black.

## 4. Recovery by 984 digital reset (`wedge_recovery`)

On this panel the DTG pulse is the wrong recovery even when the wedge is real:

| Evidence | Source |
|---|---|
| A pulse forced on a healthy stream drops the panel into its TDDI self test 4 times in 5 | v1 experiment 4.3 |
| Pulsing a genuine mid-session wedge left the measurement correct at 2814 and the panel at 0.000 nits, four samples running | v1 `post-install-real-wedge.txt` |
| One 984 digital reset cleared a genuine wedge (4751 -> 2815) **and** brought the picture back | v2 R2, reproduced here |

`wedge_recovery` (0644, default 0):

- **0** -- unchanged: cut the 984 main stream, pulse the DTG reset, settle,
  enable. The order the OTS-OLED bring-up validated, and the only one ever
  tried on that panel.
- **1** -- cut the stream, write 984 main page `0x01 = 0x01`, wait for the
  FPD-Link to re-lock (poll `0x54` bit 0, up to 1 s) rather than a fixed delay,
  settle 200 ms, re-measure, enable. If the DTG is still out of tolerance it
  falls back to the pulse once and logs
  `984 digital reset did not clear the wedge, falling back to a DTG pulse`.

Both recovery sites reach it, because the periodic check and the boot/resync
restore both recover through `hh983_guard_restore_stream()`.

### 4.1 The digital reset preserves the 984's configuration

Verified as R4.1 asked, main page `0x00..0x5F` dumped either side of one write
with the driver's poll stopped (`digital-reset-regdiff.txt`). Three registers
moved, all of them status:

| Register | Before | After | What it is |
|---|---|---|---|
| `0x09` | 0x40 | 0x00 | `RX_BCC_STATUS`, back-channel status |
| `0x53` | 0x01 | 0x81 | `GP_STATUS_0`, bit 7 `FPD_DECODE_ERROR` latched by the reset |
| `0x54` | 0xC1 | 0xC5 | `GP_STATUS_1`, bit 2 `LOCK_STS_CHG` latched by the reset |

No configuration register changed, so the driver's GPIO, INTB and pass-through
setup survives and does not need reapplying. This is one register more than v1
section 3.4 reported (`0x09` was not in that list), and it is a status register
too, so the conclusion is unchanged.

Guard state after a recovery is consistent: `guard_stream_cut` is cleared by the
stream re-enable at the end of the restore exactly as on the pulse path, and
`guard_video_up` is untouched by the recovery itself. The warm-reboot cycles
below confirm it in practice -- every one of them recovered and then polled on
without further wedges.

### 4.2 Where the value is set

`hh983-config.sh` on the Pi writes `/etc/modprobe.d/hh983.conf`, but by
**deserializer** type (984 / 988 / 988-video), not by display profile, and
`pi-config-txt.sh` handles the display type without touching module options.
Neither is part of `br-wrapper`; both ship in the micropanel image. So, per R4.2,
the option went into the rig's `/etc/modprobe.d/hh983.conf` directly:

```
options hh983-serializer config_mode=0 wedge_recovery=1
```

The previous contents are saved at `/home/pi/hh983.conf.before-wedge-recovery`.

**Caveat worth acting on:** `hh983-config.sh --type=984` rewrites that file
wholesale with `echo "options hh983-serializer config_mode=0" > "$HH983_CONF"`,
so running it -- or anything that calls it -- silently drops `wedge_recovery=1`
and the panel goes back to being pulsed. Making `hh983-config.sh` preserve extra
options, or teaching it the display profile, belongs in the micropanel image and
is listed as follow-up in section 8.

## 5. Validation

### 5.1 Warm reboots, 8 cycles, `--verdict=sequence` -- the case v1 could not reach

`power-cycle-validate.sh --warm` reboots over ssh instead of cutting the Tasmota
socket. Cold cycles never wedge the 984 DTG on this rig; warm reboots wedge it
every time, which is why v1's 24 clean cold cycles could coexist with a guard
that could not detect a real wedge.

Module `adbe8a6...` (`35cb78f`), `wedge_recovery=1`, `dtg_tolerance=32` default,
no stop-gap.

| Cycle | DTG measured at boot | Wedged at boot | Recovery ran | `dtg_wedge_count` | Verdict |
|---|---|---|---|---|---|
| 1 | 4777 | yes | digital-reset | 0 | PASS |
| 2 | 4239 | yes | digital-reset | 0 | PASS |
| 3 | 5006 | yes | digital-reset | 0 | PASS |
| 4 | 4852 | yes | digital-reset | 0 | PASS |
| 5 | 4462 | yes | digital-reset | 0 | PASS |
| 6 | 4751 | yes | digital-reset | 0 | PASS |
| 7 | 5007 | yes | digital-reset | 0 | PASS |
| 8 | 4463 | yes | digital-reset | 0 | PASS |

**8 of 8 warm reboots wedged the DTG** (4239..5007 against a programmed 2816),
all 8 were detected, all 8 were recovered by the digital reset, and the panel
passed the command-response verdict every time with no manual intervention.
R5.1 asked for the wedge count to be recorded honestly: it was 8 of 8, not a
subset.

`dtg_wedge_count` stays 0 throughout because these wedges are found by the
boot/resync restore path, which does not increment that counter -- only
`hh983_guard_check_dtg()` does. The per-cycle `dmesg` wedge-line count is 1 on
every cycle, which is where each of these is recorded. That is worth knowing
before treating `dtg_wedge_count` as the only wedge indicator.

Two cycles needed a re-measure after a transient colorimeter read failure
(cycle 5: `red=retry(Y=ERROR) red=ok-after-8s(260.48)`, and the same for blue).
Both re-measures passed. The sensor error rate is a property of the
i1Display Pro on this rig, not of the panel -- see section 6.3.

### 5.2 Cold power cycles, 10 cycles, `--verdict=hold`

The v1 criterion, re-run to show the raw-sample confirmation has not
reintroduced false wedges. A cold boot does not wedge this rig, so any non-zero
`dtg_wedge_count` here would be a regression of R3.1.

| Cycle | DTG at boot | Wedged | Recovery | `dtg_wedge_count` | Result |
|---|---|---|---|---|---|
| 1 | 2815 | no | none | 0 | PASS |
| 2 | 2815 | no | none | 0 | PASS |
| 3 | 2815 | no | none | 0 | PASS |
| 4 | 2815 | no | none | 0 | PASS |
| 5 | 2814 | no | none | 0 | PASS |
| 6 | 2814 | no | none | 0 | PASS |
| 7 | 2814 | no | none | 0 | PASS |
| 8 | 2814 | no | none | 0 | PASS |
| 9 | 2814 | no | none | 0 | PASS |
| 10 | 2814 | no | none | 0 | PASS |

10/10 PASS, every boot measuring a healthy 2814/2815, no wedges, no recoveries.

### 5.3 Injected wedge, poll stopped at boot, both `wedge_recovery` values

`/etc/modprobe.d/zz-hh983-test.conf` with `poll_interval_ms=0`, warm reboot, so
the DTG is left wedged with the guard asleep. Then the guard is released with
`poll_interval_ms=1000`.

| | `wedge_recovery=1` | `wedge_recovery=0` |
|---|---|---|
| DTG with the guard asleep | 4207 / 4777 / 4265 | 4207 / 4852 / 4464 |
| Panel before | 0.000 nits on commanded white **and** red | 0.000 nits on both |
| Detected after release | 4778, +0.02 s | 4341, +0.02 s |
| Recovery | `984 digital reset ... DTG measured Htotal=2814` | DTG pulse |
| H total after | 2815 | 2814 |
| Panel after | white 1100, red 262, green 708, blue 129, white 1097 -- all correct | white 1100, red 262, green 708, blue 129, white 1097 -- all correct |

Detection and recovery took about 0.3 s from the guard being released, inside
the 2 x `poll_interval_ms` R5.3 asks for.

**This did not come out as R5.3 expected**, and the reason is in both logs:
`984 stream_en=0`. A wedge found at boot is found with the main stream already
cut, so the DTG pulse lands on a panel that is not being fed, and the panel
comes up cleanly when the stream is enabled afterwards. The boot-time injection
therefore cannot tell the two recovery paths apart -- both pass. It also
explains an observation from earlier in the evening: a warm reboot on the *v1*
build pulsed a genuine boot wedge and kept its picture.

### 5.3b The case that does separate them: a recovery on a live stream

The pulse's harm is specific to interrupting a stream the panel is displaying.
To exercise that, `dtg_tolerance` was dropped to 0 with the panel lit and the
pattern generator running: the healthy 2813..2815 against a programmed 2816 is
then out of tolerance on one side, so the guard confirms a wedge and runs its
recovery with `stream_en=1`.

(`dtg_tolerance=1` does not work for this -- 2815 is within a tolerance of 1 and
is ~35 % of samples, so the five-sample confirmation almost never completes.)

| | `wedge_recovery=1` | `wedge_recovery=0` |
|---|---|---|
| Guard fired at | 211.2 s, `stream_en=1` | 262.4 s, `stream_en=1` |
| What ran | digital reset, then fallback pulse | DTG pulse |
| Panel after, white held | 1090.6 OK | 1088.2 OK |
| Panel after, cmd=red | 256.4 OK | 254.7 OK |
| Panel after, cmd=green | 706.1 OK | **196.6, x=0.2076 y=0.1829 -- not green** |
| Panel after, cmd=white | 1089.5 OK | **239.1 -- not white** |
| Recovered by | nothing needed | `fpdlink-tool.sh --recover --target=984` |

With the pulse alone the panel stopped following the Pi partway through the
sequence -- the BIST signature -- and needed a manual 984 digital reset. With
`wedge_recovery=1` it followed every pattern.

Two honest caveats on this experiment:

- The forced wedge is artificial (measured 2815 against a programmed 2816 at
  `dtg_tolerance=0`), so the digital reset can never bring the measurement
  "within tolerance" and the fallback-to-pulse always fires in the
  `wedge_recovery=1` trial. That at least exercises the fallback path and shows
  it works. On a *real* wedge the reset clears it outright with no fallback --
  section 5.3, `4778 -> 2814`.
- One trial each. The pulse costs this panel its picture about four times in
  five (v1 experiment 4.3), not five times in five, so a single pulse trial that
  kept the picture would not have been evidence of anything. This one lost it,
  which is consistent, but the strong statement remains the v1 5-trial result.

### 5.4 Tearing is still there

Diagnostic only, 150 raw MSB+LSB reads with the driver poll stopped:

| Run / cycle | 2560 | 306x/307x | torn of 150 |
|---|---|---|---|
| warm / 1 | 6 | 15 | 21 (14 %) |
| warm / 5 | 17 | 14 | 31 (21 %) |
| cold / 1 | 10 | 16 | 26 (17 %) |
| cold / 6 | 7 | 10 | 17 (11 %) |

11..21 % of raw reads are torn, unchanged by any of this work. The driver reads
them, counts them as measurements, and still declares no false wedge.

## 6. What differed from what v2 expected

### 6.1 `wedge_recovery=0` did not lose the picture on the injected wedge

R5.3 expected the pulse trial to end with the picture lost and the verdict
failing. It did not: both settings recovered the injected boot-time wedge
completely. Section 5.3 has the reason -- at boot the main stream is already
cut, so the pulse is harmless -- and section 5.3b has the experiment that does
separate the two paths. The conclusion R4 draws is still supported, but by the
live-stream case rather than the boot case, and the distinction is worth
keeping: **on this panel a DTG pulse is dangerous when it interrupts a stream
the panel is displaying, not when it happens with the stream already cut.**

### 6.2 `dtg_wedge_count` does not count boot-time wedges

R5 asks for `dtg_wedge_count` per cycle and, at the end, "equal to the number of
genuine wedges of the current boot only". Note that a wedge found by the
boot/resync restore path does **not** increment it -- only
`hh983_guard_check_dtg()` does. All 8 warm-reboot wedges were found at boot, so
`dtg_wedge_count` read 0 on every one of them despite 8 real wedges being
detected and recovered. The per-cycle `dmesg` wedge-line count is the indicator
that catches those, and the harness logs both. Anyone using `dtg_wedge_count`
alone as "did we wedge" will get the wrong answer on a boot wedge.

### 6.3 The 571-nit red was not a digital-reset settling effect

R6 asked whether the panel needs longer than 3 s to settle after a digital
reset. Measured directly: a digital reset on a **healthy** pipeline produces no
transient at all -- red held 258.5, 258.4, 258.3, 258.1, 258.0, 257.9, 257.9,
257.7 across eight consecutive samples with the pattern never re-commanded.

So the 571 nits in R2 was not the reset settling; it was the panel's local
dimming still converging after recovering **from a wedge**, with the
chromaticity (0.6854) already correct. Rather than raise the settle globally,
the harness re-measures once on a mismatch (`--retry-settle`, default 8 s). That
fired twice in the warm run, both times passing on the re-measure. A BIST cannot
exploit it: it keeps stepping, so it would have to coincide with the commanded
pattern twice.

### 6.4 Three bugs in the validation harness, not the driver

All three were mine, and two of them would have made the v2 numbers untrustworthy
in opposite directions:

1. `check_sequence()` still carried an inline copy of the per-pattern check from
   before `check_one()` existed, so the re-measure-on-mismatch only ever applied
   to `--verdict=hold`. The warm run uses `sequence`.
2. A colorimeter read error was scored as a black screen. Cycle 4 of the first
   warm run stopped with `white=MISMATCH(Y=ERROR)` while every other pattern in
   the same sequence matched and the panel measured 1096 nits moments later.
3. Worse, a colorimeter read error was scored as a **pass** for black:
   `pattern_ok` compares with awk, awk coerces `ERROR` to 0, and black's band is
   0..0.3. Cycle 2 of the second warm run logged `black=ok(ERROR)`.

Fixed in `35e2545` and `48c73c0`: readings are checked for being numeric before
they are evaluated, a non-numeric reading is labelled `SENSOR-ERROR`, the whole
verdict is re-run once, and a persistent one stops the run as `FAIL-SENSOR`
saying the measurement failed rather than the panel. The i1Display Pro on this
rig returns `ERROR` for roughly one read in a hundred; it is worth knowing that
before trusting a single sample.

### 6.5 The digital reset changes one more register than v1 recorded

v1 section 3.4 listed `0x53` and `0x54`. The full `0x00..0x5F` dump also shows
`0x09` `RX_BCC_STATUS` going 0x40 -> 0x00. It is a status register like the
other two, so the conclusion (configuration preserved) is unchanged.

### 6.6 Cosmetic: "digital reset after 0 ms"

`hh983_guard_digital_reset()` logs the loop counter before its increment, so a
lock acquired on the first 50 ms poll is reported as "after 0 ms". The elapsed
time is at least 50 ms. Log text only; deliberately not fixed here so the binary
carrying the 8 warm and 10 cold cycles is the one reported. The fix is to log
`waited + 50`.

### 6.7 Host-side, not rig-side

Two validation runs were killed by the development host's low-memory heuristic
while 55 GiB were free. The runs were restarted detached (`setsid`) and completed
normally. Nothing reached the rig; noted only so the gap in the log timestamps
is not mistaken for a rig event.

## 7. Final rig state

Cold power-cycled at the end so the counters reflect a normal boot rather than
the artificial wedges of section 5.3b.

| Item | Value |
|---|---|
| Module | `/lib/modules/6.12.109-v8+/extra/hh983-serializer.ko`, md5 `adbe8a63a01e79fd426f2ed876dd427c` (commit `35cb78f`) |
| Backup of the v1 build | `/home/pi/hh983-serializer.ko.fix` is the current one; the pre-branch original is still `/home/pi/hh983-serializer.ko.before-torn-fix`, md5 `376df2961f4f2abd8e7693cd2942cd7b` |
| `/etc/modprobe.d/hh983.conf` | `options hh983-serializer config_mode=0 wedge_recovery=1` (previous contents at `/home/pi/hh983.conf.before-wedge-recovery`) |
| `/etc/modprobe.d/zz-hh983-test.conf` | removed |
| Parameters | `dtg_wedge_count=0`, `dtg_tolerance=32`, `poll_interval_ms=1000`, `dtg_recover=1`, `wedge_recovery=1`, `config_mode=0` |
| `dmesg` wedge lines this boot | 0 (cold boot, DTG measured 2814 at the boot restore) |
| Running app | none (desktop) |
| Colorimeter on the desktop | 9.27 nits (launcher draws dark at the meter spot; normal) |
| Left on the Pi | `fpdlink-tool-torn-fix.sh`, `bist-vs-pi.sh`, `injected-wedge.sh`, `midsession.sh`, `hh983-serializer.ko.fix` |

New data files under `tmp-docs/fable-prompt-v1-data/`:
`warm-before-fix.txt`, `digital-reset-regdiff.txt`, `settle-after-reset.txt`,
`injected-wr1.txt`, `injected-wr0.txt`, `midsession.txt`, `midsession2.txt`,
`final-state-v2.txt`, and the `power-cycle-validate-*.log` files for the warm
and cold runs.

## 8. Follow-up

- **`hh983-config.sh` will silently drop `wedge_recovery=1`.** It rewrites
  `/etc/modprobe.d/hh983.conf` wholesale by deserializer type. Any run of
  `hh983-config.sh --type=984` puts this panel back on the DTG pulse. The script
  ships in the micropanel image, not in `br-wrapper`; it should either preserve
  unknown options or learn the display profile.
- **Modes 1 and 2 are code-reviewed, not bench-tested.** `e381ff9` routes
  `hh983_des988_check_dtg()` through the confirmation; no 988 rig is attached.
  The 3x-qvue profile (H total 5628, four px below `0x1600`) is the one to try
  it on.
- **`wedge_recovery` on the OTS-OLED is unknown.** The digital reset has never
  been tried on the panel that black-latches. Default stays 0 for that reason.
- **The fixed `fpdlink-tool.sh` is still not the installed one.**
  `/home/pi/micropanel/bin/fpdlink-tool.sh` is the old copy; the fixed one is at
  `/home/pi/fpdlink-tool-torn-fix.sh`. It reaches the rig through the next
  micropanel image build.
- **Log nit** in section 6.6.

## 9. Full driver diff against `d44d74f`

`power-cycle-validate.sh` and `wedge-rule-check.py` are on the branch and not
reproduced here.

```diff
diff --git a/package/hh983-serializer/src/hh983-serializer.c b/package/hh983-serializer/src/hh983-serializer.c
index 91d3b36..b5f5759 100644
--- a/package/hh983-serializer/src/hh983-serializer.c
+++ b/package/hh983-serializer/src/hh983-serializer.c
@@ -121,6 +121,30 @@ static int dtg_recover = 1;
 module_param(dtg_recover, int, 0644);
 MODULE_PARM_DESC(dtg_recover, "Modes 0, 1 and 2: 1=pulse the DTG to recover a detected wedge (default), 0=detect and log only");
 
+/*
+ * Mode 0: how a confirmed wedge is recovered.
+ *
+ * 0 (default) cuts the 984 main stream, pulses the DTG reset, settles and
+ * re-enables -- the order the OTS-OLED bring-up validated with a colorimeter,
+ * and the only one tried on that panel.
+ *
+ * 1 cuts the stream, issues a 984 digital reset (main page 0x01 = 0x01, the
+ * same action as the Stream Deck "Sync Video" button), waits for the FPD-Link
+ * to re-lock, re-enables, and falls back to a pulse once if the measurement is
+ * still wrong.  That is for panels where the pulse itself is the problem: on
+ * the 15.6" 2K5 a pulse on a healthy stream dropped the panel into its TDDI
+ * self test four times out of five, and pulsing a genuine mid-session wedge on
+ * 2026-09-13 left the measurement correct and the panel dark, while one digital
+ * reset fixed the measurement and the picture together.
+ *
+ * Stays 0 by default: the digital reset has never been tried on the OTS-OLED,
+ * which is the panel that black-latches, and that part is not on this bench.
+ * Set it per display type -- see /etc/modprobe.d/hh983.conf on the 15.6-2k5 rig.
+ */
+static int wedge_recovery;
+module_param(wedge_recovery, int, 0644);
+MODULE_PARM_DESC(wedge_recovery, "Mode 0: 0=cut stream, pulse the DTG, enable (default, OLED-validated); 1=cut stream, 984 digital reset, wait for lock, enable (panels that lose the eDP stream on a DTG pulse, e.g. 15.6-2k5)");
+
 /* Mode 0 (983+984) OLED-OTS touch controller routing.
  *
  * On the OLED-OTS 17.3 board the HX8530 TDDI touch controller is on the
@@ -191,6 +215,9 @@ MODULE_PARM_DESC(ots_touch, "Mode 0 only: 1=route the OLED-OTS HX8530 touch (984
 #define DES984_GP_STATUS_1       0x54  /* [0]=LOCK [6]=FPDRX_PLL_LOCK (no SIG_DET) */
 #define DES984_INTB_VALUE        0x81
 /* 984 local display timing generator and DP TX (same indirect/APB scheme as 983) */
+#define DES984_RESET_CTL         0x01  /* [0] digital reset, self-clearing, registers preserved */
+#define DES984_DIGITAL_RESET     0x01
+#define DES984_LOCK_WAIT_MS      1000  /* how long to wait for FPD-Link re-lock after one */
 #define DES984_IND_PAGE_DTG      0x14  /* DTG page (script byte 0x50) */
 #define DES984_DTG_P0_CTL        0x32  /* Port 0 DTG control */
 #define DES984_DTG_P1_CTL        0x62  /* Port 1 DTG control */
@@ -558,7 +585,7 @@ static void hh983_guard_cut_stream(struct hh983_data *data)
  *
  * Re-reading the MSB after the LSB catches the plain case: if the counter
  * crossed a byte boundary and stayed there, the two MSB reads disagree and the
- * attempt is thrown away.  It does NOT catch a counter that crosses and comes
+ * attempt is retried.  It does NOT catch a counter that crosses and comes
  * straight back inside the ~3 ms the triple takes -- both MSB reads then see
  * 0x0A while the LSB read sees the 0x00 of 0x0B00, which is self-consistent
  * and wrong by exactly 256.  That is not a corner case on a value that
@@ -567,13 +594,32 @@ static void hh983_guard_cut_stream(struct hh983_data *data)
  * read can look at distinguishes that result from a genuine 2560, so the
  * callers that act on it confirm with hh983_dtg_confirmed_bad() instead.
  *
- * Returns the 15-bit value, or negative on an I2C failure or on three torn
- * attempts.
+ * When all three attempts tear, this used to give up and return an error, and
+ * that was a bug with the failure mode exactly backwards.  A *wedged* DTG is
+ * precisely the case where the measurement moves hundreds of px between reads,
+ * so the MSB rarely holds still, so every attempt tore, so the guard decided it
+ * could not read the register and did nothing -- the more wedged the part, the
+ * blinder the guard.  On 2026-09-13 a genuine wedge measuring 4495 was declined
+ * at boot for that reason and was still wedged, unnoticed, 95 s later.
+ *
+ * A torn read is a measurement, not a failure.  Tearing can only move a value
+ * within its own 256-block or into the neighbouring one, so it can never make a
+ * wedged value look healthy and never makes a healthy value look more than
+ * ~256 px wrong.  The last raw pair is therefore returned as a value and the
+ * caller is told, through *torn, that it may be off by a multiple of 256;
+ * a negative return is now reserved for a real I2C failure.
+ *
+ * Returns the 15-bit value, or -EIO on an I2C failure.  *torn (optional) is set
+ * when no attempt produced two matching MSB reads.
  */
-static int hh983_read_meas15(struct hh983_data *data, u8 hi_off, u8 lo_off)
+static int hh983_read_meas15(struct hh983_data *data, u8 hi_off, u8 lo_off,
+			     bool *torn)
 {
 	struct i2c_client *client = data->client;
-	int attempt, hi, lo, hi_again;
+	int attempt, hi = 0, lo = 0, hi_again;
+
+	if (torn)
+		*torn = false;
 
 	for (attempt = 0; attempt < DP_GUARD_MEAS_TRIES; attempt++) {
 		hi = hh983_deser_ind_read(client, data->deser_addr,
@@ -588,7 +634,9 @@ static int hh983_read_meas15(struct hh983_data *data, u8 hi_off, u8 lo_off)
 			return ((hi & 0x7F) << 8) | lo;
 	}
 
-	return -EAGAIN;
+	if (torn)
+		*torn = true;
+	return ((hi & 0x7F) << 8) | lo;
 }
 
 /* Do two consecutive out-of-tolerance measurements describe the same fault?
@@ -620,19 +668,21 @@ static bool hh983_wedge_consistent(int meas, int prev, int prog)
 
 /* Read the 984's measured input line length and the 983's programmed output
  * line length, the pair the guard compares to decide whether the 984 DTG has
- * wedged.  Returns 0 with both filled in, or a negative value if the measured
- * value could not be read untorn or either programmed byte failed.
+ * wedged.  Returns 0 with both filled in, or a negative value only if an I2C
+ * transfer failed; a torn measurement is still a measurement and is reported
+ * through *torn (optional) rather than as an error.
  *
  * Only the measured value needs the tear-proof read: the programmed pair is
  * static configuration in the 983's VP, not a running counter.
  */
-static int hh983_read_htotals(struct hh983_data *data, int *meas, int *prog)
+static int hh983_read_htotals(struct hh983_data *data, int *meas, int *prog,
+			      bool *torn)
 {
 	struct i2c_client *client = data->client;
 	int meas_htotal, prog_hi, prog_lo;
 
 	meas_htotal = hh983_read_meas15(data, DES984_DTG_MEAS_HTOTAL_HI,
-					DES984_DTG_MEAS_HTOTAL_LO);
+					DES984_DTG_MEAS_HTOTAL_LO, torn);
 	prog_lo = hh983_ind_read(client, SER_IND_PAGE_VP, SER_VP0_H_TOTAL_LO);
 	prog_hi = hh983_ind_read(client, SER_IND_PAGE_VP, SER_VP0_H_TOTAL_HI);
 	if (meas_htotal < 0 || prog_lo < 0 || prog_hi < 0)
@@ -656,23 +706,37 @@ static int hh983_read_htotals(struct hh983_data *data, int *meas, int *prog)
  * wedged DTG is out of tolerance on every one of them and on the same side,
  * however much it wanders (4201..5110 against 2816 on this bench after a warm
  * reboot, 4415..5313 against 3440 on the OTS-OLED, 3730..4456 against 2028 on
- * the 988).  Requiring all DP_GUARD_MEAS_SAMPLES to be out of tolerance on one
- * side raises the odds of a torn run to the fourth power of a single tear --
- * out of reach -- while a real wedge is still declared on the poll it always
- * was, which is what keeps the response inside the ~10 s the OTS-OLED takes to
- * latch black.
+ * the 988).
+ *
+ * The samples are deliberately *raw*: a torn one counts like any other, and
+ * only a real I2C failure aborts.  That is the whole point, because tearing
+ * cannot cross the two cases.  With a healthy 2816 a torn sample is 2560
+ * (below) or 3070 (above), so five samples that are all out of tolerance AND
+ * all on the same side would need five tears of the same kind in a row: at the
+ * ~15 % tear rate measured on this rig that is under 1e-4 per poll, and the
+ * two-poll debounce squares it.  With a wedged 4500 every sample is far above
+ * 2816 whatever tearing does to it, because a tear only moves a value inside
+ * its own 256-block or the neighbouring one.  Demanding untorn samples here
+ * instead is what blinded the guard to real wedges before 2026-09-13.
+ *
+ * So a real wedge is still declared on the poll it always was, which is what
+ * keeps the response inside the ~10 s the OTS-OLED takes to latch black.
  *
  * Only the suspicious path pays: a healthy pipeline returns on the first extra
  * sample, and a first read inside tolerance never gets here at all.
  */
 static bool hh983_dtg_confirmed_bad(struct hh983_data *data, int first, int prog)
 {
+	struct i2c_client *client = data->client;
 	bool above = first > prog;
 	int i, meas = 0, meas_prog = 0;
 
 	for (i = 1; i < DP_GUARD_MEAS_SAMPLES; i++) {
-		if (hh983_read_htotals(data, &meas, &meas_prog) != 0)
+		if (hh983_read_htotals(data, &meas, &meas_prog, NULL) != 0) {
+			dev_warn_ratelimited(&client->dev,
+					     "DP guard: I2C failure while confirming a DTG measurement, not deciding\n");
 			return false;
+		}
 		if (abs(meas - meas_prog) <= dtg_tolerance)
 			return false;
 		if ((meas > meas_prog) != above)
@@ -707,7 +771,7 @@ static void hh983_guard_wedge_snapshot(struct hh983_data *data, int meas, int pr
 	/* Same counter, same tearing: V total is only logged, never decided on,
 	 * but a torn one here would misdirect whoever reads the snapshot. */
 	meas_vtotal = hh983_read_meas15(data, DES984_DTG_MEAS_VTOTAL_HI,
-					DES984_DTG_MEAS_VTOTAL_LO);
+					DES984_DTG_MEAS_VTOTAL_LO, NULL);
 	if (meas_vtotal < 0)
 		meas_vtotal = -1;
 
@@ -724,6 +788,52 @@ static void hh983_guard_wedge_snapshot(struct hh983_data *data, int meas, int pr
 		   meas, meas_vtotal, prog, vp_sts, ser_sts, stream_en, des_sts0, des_sts1);
 }
 
+/* Mode 0 wedge recovery by 984 digital reset (wedge_recovery=1).
+ *
+ * Main page 0x01 bit 0 is self-clearing and leaves the configuration registers
+ * alone -- the analysis session diffed the 984's main page either side of one
+ * and only the clear-on-read status bits moved -- so the driver's GPIO, INTB and
+ * pass-through setup survives and does not need reapplying.  What it does do is
+ * re-initialise the output pipeline and re-train the eDP link to the panel,
+ * which is why the Stream Deck "Sync Video" button (the same write) brings this
+ * panel's picture back.
+ *
+ * Called with the main stream already cut.  Waits for the FPD-Link lock rather
+ * than a fixed delay, then re-measures: returns true only if the DTG is back
+ * inside tolerance, so the caller can fall back to a pulse when it is not.
+ */
+static bool hh983_guard_digital_reset(struct hh983_data *data)
+{
+	struct i2c_client *client = data->client;
+	int waited, sts1, meas = -1, prog = -1;
+
+	if (hh983_write_deser_reg(client, data->deser_addr, DES984_RESET_CTL,
+				  DES984_DIGITAL_RESET) < 0)
+		return false;
+
+	for (waited = 0; waited < DES984_LOCK_WAIT_MS; waited += 50) {
+		msleep(50);
+		sts1 = hh983_read_deser_reg(client, data->deser_addr,
+					    DES984_GP_STATUS_1);
+		if (sts1 >= 0 && (sts1 & 0x01))
+			break;
+	}
+
+	/* The DTG reads 0 for a moment after the reset while it re-locks onto
+	 * the incoming stream; measuring straight away would report a failure
+	 * that fixes itself. */
+	msleep(200);
+
+	if (hh983_read_htotals(data, &meas, &prog, NULL) != 0)
+		return false;
+
+	dev_info(&client->dev,
+		 "DP guard restore: 984 digital reset after %d ms, DTG measured Htotal=%d, 983 Htotal=%d\n",
+		 waited, meas, prog);
+
+	return abs(meas - prog) <= dtg_tolerance;
+}
+
 /* Mode 0 DP guard: bring the 984 output back after the 983 VP has resynced.
  *
  * Order matters and follows the sequence verified with a colorimeter:
@@ -759,11 +869,11 @@ static void hh983_guard_restore_stream(struct hh983_data *data, bool force_wedge
 	stream_en = hh983_deser_apb_read8(client, data->deser_addr,
 					  DES984_APB_MAIN_STREAM_EN);
 
-	if (hh983_read_htotals(data, &meas_htotal, &prog_htotal) != 0) {
+	if (hh983_read_htotals(data, &meas_htotal, &prog_htotal, NULL) != 0) {
 		/* One retry: a single failed transfer on a bus this busy is not
 		 * evidence of anything. */
 		msleep(20);
-		unreadable = hh983_read_htotals(data, &meas_htotal, &prog_htotal) != 0;
+		unreadable = hh983_read_htotals(data, &meas_htotal, &prog_htotal, NULL) != 0;
 	}
 
 	if (!force_wedged && !unreadable) {
@@ -807,15 +917,26 @@ static void hh983_guard_restore_stream(struct hh983_data *data, bool force_wedge
 					DES984_APB_MAIN_STREAM_EN, 0);
 
 	if (wedged) {
-		hh983_deser_ind_write(client, data->deser_addr, DES984_IND_PAGE_DTG,
-				      DES984_DTG_P0_CTL, DES984_DTG_HOLD_RESET);
-		hh983_deser_ind_write(client, data->deser_addr, DES984_IND_PAGE_DTG,
-				      DES984_DTG_P1_CTL, DES984_DTG_HOLD_RESET);
-		msleep(200);
-		hh983_deser_ind_write(client, data->deser_addr, DES984_IND_PAGE_DTG,
-				      DES984_DTG_P0_CTL, DES984_DTG_RELEASE);
-		hh983_deser_ind_write(client, data->deser_addr, DES984_IND_PAGE_DTG,
-				      DES984_DTG_P1_CTL, DES984_DTG_RELEASE);
+		bool fixed = false;
+
+		if (wedge_recovery == 1) {
+			fixed = hh983_guard_digital_reset(data);
+			if (!fixed)
+				dev_notice(&client->dev,
+					   "DP guard restore: 984 digital reset did not clear the wedge, falling back to a DTG pulse\n");
+		}
+
+		if (!fixed) {
+			hh983_deser_ind_write(client, data->deser_addr, DES984_IND_PAGE_DTG,
+					      DES984_DTG_P0_CTL, DES984_DTG_HOLD_RESET);
+			hh983_deser_ind_write(client, data->deser_addr, DES984_IND_PAGE_DTG,
+					      DES984_DTG_P1_CTL, DES984_DTG_HOLD_RESET);
+			msleep(200);
+			hh983_deser_ind_write(client, data->deser_addr, DES984_IND_PAGE_DTG,
+					      DES984_DTG_P0_CTL, DES984_DTG_RELEASE);
+			hh983_deser_ind_write(client, data->deser_addr, DES984_IND_PAGE_DTG,
+					      DES984_DTG_P1_CTL, DES984_DTG_RELEASE);
+		}
 	}
 	msleep(500);
 
@@ -852,9 +973,10 @@ static void hh983_guard_check_dtg(struct hh983_data *data)
 	if (!dtg_check || data->guard_stream_cut)
 		return;
 
-	if (hh983_read_htotals(data, &meas, &prog) < 0) {
-		/* A failed read is not evidence of a wedge -- a bus that has
-		 * gone away is the VP-sync guard's problem, not this one. */
+	if (hh983_read_htotals(data, &meas, &prog, NULL) < 0) {
+		/* Only a real I2C failure gets here now -- a bus that has gone
+		 * away is the VP-sync guard's problem, not this one.  A torn
+		 * measurement is no longer an error: see hh983_read_meas15(). */
 		data->guard_dtg_count = 0;
 		return;
 	}
@@ -982,8 +1104,9 @@ static void hh983_des988_check_dtg(struct hh983_data *data)
 		return;
 	}
 
-	if (hh983_read_htotals(data, &meas, &prog) < 0) {
-		/* A failed read is a bus problem, not evidence of a wedge. */
+	if (hh983_read_htotals(data, &meas, &prog, NULL) < 0) {
+		/* A real I2C failure is a bus problem, not evidence of a wedge.
+		 * A torn measurement is not a failure: see hh983_read_meas15(). */
 		data->guard_dtg_count = 0;
 		return;
 	}
@@ -1007,6 +1130,19 @@ static void hh983_des988_check_dtg(struct hh983_data *data)
 		return;
 	}
 
+	/* Same confirmation mode 0 uses: one out-of-tolerance read is not
+	 * evidence.  The 988 path is exposed to the same tearing -- the 3x-qvue
+	 * profile programs an H total of 5628, four pixels below 0x1600 -- and
+	 * the shared hh983_read_meas15() cannot remove it on its own.
+	 * Code-reviewed, not bench-tested: no 988 rig was attached. */
+	if (!hh983_dtg_confirmed_bad(data, meas, prog)) {
+		dev_dbg(&client->dev,
+			"DTG guard (mode %d): out-of-tolerance %d (programmed %d) not confirmed, torn read\n",
+			data->mode, meas, prog);
+		data->guard_dtg_count = 0;
+		return;
+	}
+
 	data->guard_dtg_count++;
 	if (data->guard_dtg_count < DP_GUARD_WEDGE_POLLS)
 		return;			/* one odd measurement is not a wedge */
@@ -1052,10 +1188,10 @@ static void hh983_des988_check_dtg(struct hh983_data *data)
 	 * "failed" recovery that in fact succeeded a moment later (seen on the
 	 * bench 2026-09-12 after an HPD drop). One retry, then report whatever it
 	 * says - including a genuine failure. */
-	if (hh983_read_htotals(data, &meas, &prog) == 0 &&
+	if (hh983_read_htotals(data, &meas, &prog, NULL) == 0 &&
 	    (meas <= 0 || abs(meas - prog) > dtg_tolerance)) {
 		msleep(700);
-		(void)hh983_read_htotals(data, &meas, &prog);
+		(void)hh983_read_htotals(data, &meas, &prog, NULL);
 	}
 
 	dev_notice(&client->dev,
```
