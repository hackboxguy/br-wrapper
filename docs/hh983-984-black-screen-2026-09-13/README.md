# 15.6" 2K5 (0OD) intermittent black screen — 2026-09-13

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
`wedge-rule-check.py` checks the decision rule against the recorded distributions. Read
`opus-report-v2.md` first; `fable-prompt-v1.md` is the original analysis, `-v2.md` the review.

`data/` holds the raw evidence (register dumps, colorimeter runs, per-cycle logs, bench scripts).
Nothing was left out for size — the largest file is 24 kB. Paths quoted inside the four documents
are as they were written, i.e. relative to the `tmp-docs/` working directory of the session.
