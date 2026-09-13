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
it interrupts a live stream. **The `15.6-2k5` profile needs `wedge_recovery=1`**; `micropanel`'s
`pi-config-txt.sh` writes it. Default is 0 — the digital reset has never been tried on the OTS-OLED.

**Validation.** `../../package/hh983-serializer/src/scripts/power-cycle-validate.sh` (host-side):
8 warm reboots, all of which wedged, all detected and recovered; 10 cold cycles, no false wedges.
`wedge-rule-check.py` checks the decision rule against the recorded distributions.
**Full record: `investigation.md`** (symptom, root cause, the two fix attempts and the shipped
one, validation, open items).

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
