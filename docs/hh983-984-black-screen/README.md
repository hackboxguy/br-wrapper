# 15.6" 2K5 (0OD) intermittent black screen

**What happened.** On the `15.6-2k5` profile the 983 programs an H total of 2816 = 0x0B00,
the 984's measured H total jitters across that byte boundary, and `MEAS_HTOTAL` is two plain
registers with no latch — so ~15 % of reads tear to 2560 or 3070. The DP guard read that as a
DTG wedge and pulsed the 984 DTG, which drops this panel into its TDDI self test.

**The fix** (`hh983-serializer.c`). A wedge is declared only when five raw measurements are all
out of tolerance and all on the same side of the programmed value; tearing can only move a value
within its own 256-block, so it can never make a wedged value look healthy — and, importantly, a
torn read is never treated as an I2C error, because a genuinely wedged DTG tears constantly and
that once blinded the guard entirely. `fpdlink-tool.sh`'s `ind_read15_be()` takes a median of three.

**Recovery.** `wedge_recovery=1` recovers by 984 digital reset (main page `0x01=0x01`, the
Stream Deck "Sync Video" write) instead of the DTG pulse, which costs this panel its picture when
it interrupts a live stream. **`15.6-2k5` and `ots-oled-17` both need `wedge_recovery=1`**;
`micropanel`'s `pi-config-txt.sh` writes it for each. The driver default stays 0, and `12.3-nq1`
— the last mode-0 panel tested with neither problem — keeps the pulse.

**Status, 2026-09-14: image 01.29 is the release on both rigs.** Module md5
`41b42c7078bce73e1cc879eb23e97aab` (01.28 was `602642d00a6b3430f3253d7f8c455e32`); 01.29
adds only the wedge-snapshot diagnostics and a boot-clock fix for their "since boot"
field, no behaviour change. Confirmed on both rigs: 5 cold + 3 warm cycles each, every
warm boot wedge recovered by a single 984 digital reset, zero fall-backs to a DTG pulse,
zero black screens. The 15.6" 2K5 had earlier passed 30 cold and 10 warm on 01.27; the
OTS-OLED ran 28 boots on 01.28 with 3 spontaneous wedges, 15 recoveries and a clean
45-minute soak.

**One thing this does not cover.** On 2026-09-14 the OLED was found latched black with a
completely healthy DTG (measured 3441..3443 against a programmed 3440), `--diagnose`
reporting the pipeline healthy, the main stream on, and **no wedge ever detected**. The
only trace was latched event flags — `FPD_DECODE_ERROR`, `LOCK_STS_CHG`, and both
`HACTIVE_CHNG` and `VTOTAL_CHNG`. That is a second path to a black OLED which the DP
guard cannot see, because the guard watches H total and H total was correct by the time
it looked. A digital reset cleared it. See `investigation.md` section 8.

`data/` holds the evidence the reports cite, pruned to what backs a claim:

| File | Backs (section of `investigation.md`) |
|---|---|
| `capture-black.txt`, `capture-after-sync.txt` | register state while black, and after the 984 digital reset (2.1, 2.2) |
| `experiment-2.txt`, `experiment-3.txt` | the driver's restore blanks this panel, 4 of 5 (2.3) |
| `bist-vs-pi-content.txt` | the panel's BIST reproduces the Pi's colours; why a single reading cannot detect it (2.3) |
| `post-install-real-wedge.txt`, `warm-reboot-final-build.txt` | genuine wedges after warm reboots, and the build that failed to detect one (2.4, 3.2) |
| `digital-reset-regdiff.txt`, `injected-wr0.txt`, `injected-wr1.txt` | the digital reset preserves configuration and clears a real wedge (3.3, 4) |
| `power-cycle-validate-20260913-164556-FAIL-cycle1.txt` | the pre-fix run stopping on cycle 1 (4) |
| `power-cycle-validate-20260913-193212.log`, `...-195614.log` | one passing warm-reboot run and one passing cold-cycle run (4) |

The bench scratch scripts and the remaining per-run logs of the session were not kept;
`power-cycle-validate.sh` supersedes the scripts and section 4 of `investigation.md` tabulates the runs.
