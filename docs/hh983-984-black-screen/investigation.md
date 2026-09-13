# The 15.6" 2K5 black screen: torn H-total reads, false DTG wedges, and the wrong recovery

Rig: Pi 4 (Raspberry Pi OS, kernel 6.12.109-v8+) → HDMI-to-DP bridge → DS90UH983
serializer → FPD-Link IV → DS90HH984 deserializer → eDP → 15.6" 2K5 (0OD) TDDI panel.
Driver `hh983-serializer` in mode 0 (983+984), HDMI profile `15.6-2k5`
(`2560 0 10 24 222 1440 0 11 3 38 0 0 0 62 0 261888000 4`, H total 2816, V total 1492).
Investigated and fixed on 2026-09-13; this is the consolidated record. Raw evidence is
in `data/` (see `README.md` for which file backs which claim).

## 1. Symptom

After a fresh power cycle the Pi's boot screen shows, then the panel goes dark around the
time the desktop launcher comes up. The Stream Deck "Sync Video" button
(`fpdlink-tool.sh --recover --target=984`, a 984 soft digital reset: main page `0x01 = 0x01`)
brings the picture back. Not every boot; intermittent. It looked like the "upstream 984 DTG
wedge" seen on the 17" OTS-OLED, and the kernel log literally said "DTG wedged".

## 2. What was actually happening

### 2.1 The driver was pulsing the DTG every 30..120 s on a healthy pipeline

Kernel log of the first captured boot: 25 "DTG wedge without video loss ... restoring"
events in 30 minutes, the first 2 s after probe. Every "wedged" measurement was 2560 or
3066..3071 against a programmed 2816, and the re-read the restore path took a second later
was always 2814 or 2815. Every snapshot showed the 983 VP synced, FPD-Link locked, stream
enabled. `fpdlink-tool.sh --diagnose` said "Pipeline healthy" while the panel was black.

### 2.2 Torn 16-bit read of `MEAS_HTOTAL`

The 984's measured H total lives in two plain read-only bytes (SNLS726 7.6.2.16.29/30,
`MEAS_HTOTAL_MSB/LSB_P0`, DTG page offsets 0x40/0x41) with no latch or shadow. On this
profile the programmed value is 2816 = **0x0B00** and the measured value jitters 2811..2817,
i.e. it crosses the byte boundary many times per second. The driver read MSB and LSB as two
I2C transactions through the 983 pass-through, so:

- MSB 0x0A (from 28xx) + LSB 0x00 (from 2816) → **2560**
- MSB 0x0B (from 2816/2817) + LSB 0xFE.. (from 2814..) → **3070**

Measured on the rig, 150 reads: 67×2814, 47×2815, 16×2560, 4×3070, 3×3071, rest 2811..2816.
About 15 % torn (11..21 % across later runs). Reading LSB first does not help. Two torn
polls in a row (`DP_GUARD_WEDGE_POLLS = 2`) at that rate happen about every 40 s, which
matches the log.

Of all display profiles only `15.6-2k5` has an H total on a 256 boundary. The OTS-OLED
(3440 = 0x0D70), 17.3-3k (3084), 14.6-2k5 (2804), the 988 panels (2028, 2070, 2074, 4248)
are 12 px or more away; `3x-qvue` (5628 = 0x15FC) is 4 px below one and worth watching.

### 2.3 The DTG pulse is what blacks this panel

The guard's recovery is: cut the 984 main stream (APB 0x084 = 0), hold DTG P0/P1 reset
(page 0x50 offsets 0x32/0x62 = 0x06) 200 ms, release (0x04), settle 500 ms, enable. With
the i1Display Pro on the glass and a solid white pattern up:

| Sequence replayed by hand | Result |
|---|---|
| stream cut / enable only, ×3 | steady white 3/3 |
| full restore with DTG pulse, ×5 | panel drops into its own TDDI BIST 4/5 |
| 984 digital reset | picture back every time |

The panel's BIST is a 12-step colour cycle that reproduces the Pi's white, red, green and
blue to within the sensor's repeatability, and sometimes parks on black, which is the
"black screen". No single luminance or chromaticity reading can tell BIST from Pi content;
a command-response test (change the pattern, check it arrives) can, and that is what the
validation script uses.

### 2.4 Not the OLED wedge

| | OTS-OLED wedge | 15.6-2k5 |
|---|---|---|
| 984 measured H total | wanders 4415..5313 vs 3440, stays wrong | 2811..2817 vs 2816; only 2560/307x artefacts are "wrong" |
| Consecutive reads | consistently wrong | isolated, next read fine |
| Panel after a DTG pulse on a healthy link | latches black, power cycle only | BIST, recovers with 984 digital reset |
| Root cause | real DTG fault | software: non-atomic read + aggressive recovery |

Warm reboots (`sudo reboot`) do produce **genuine** wedges on this rig, every time in 10 of
10 observed (measured 4201..5110 against 2816); cold power cycles never did. That
distinction mattered for the fix, see 3.2.

## 3. The fix (`package/hh983-serializer/src/hh983-serializer.c`)

### 3.1 First attempt, and why it was not enough

Reading MSB, LSB, MSB and accepting only when both MSBs agree catches a crossing that
happened and stayed. It does not catch a counter that crosses and comes straight back
inside the ~3 ms the triple takes (both MSBs 0x0A, LSB 0x00: self-consistent, wrong by 256).
On a value that straddles the boundary continuously that is about one read in twenty, and
the bench showed it: two false wedges in 83 s with that build.

### 3.2 Second attempt, and the regression it introduced

Confirming a suspicious read with four more samples, all required to be out of tolerance
on the same side, removed the false wedges (24 clean cold cycles). But the tear-proof read
returned an error when all three attempts tore, and the confirmation treated an error as
"not confirmed". A genuinely wedged DTG wanders by hundreds of px between reads, so its
MSB differs between the first and third read almost every time. Result: the more wedged the
part, the more "unreadable" it looked, and the guard did nothing. Warm reboot with that
build: measured 4495, log "not confirmed by further reads, no pulse", still wedged 95 s
later, panel not showing the Pi.

### 3.3 What ships

- **`hh983_read_meas15()`** reads MSB, LSB, MSB; returns the pair when the MSBs agree, and
  otherwise returns the **last raw pair as a value** with a `torn` flag. Only a real I2C
  failure is an error.
- **`hh983_dtg_confirmed_bad()`**: after an out-of-tolerance read, four more **raw** samples;
  a wedge is confirmed only if all are out of tolerance and on the same side of the
  programmed value. Tearing can only move a value inside its own or the adjacent 256-block,
  so it can never make a wedged 4500 look healthy and can never push a healthy 2816 more
  than ~256 off; five same-side tears in a row is out of reach. `wedge-rule-check.py` runs
  the rule over the recorded distributions: 0 false wedges in 10⁶ healthy polls (12 % torn),
  a real wedge declared on poll 2.
- **`hh983_wedge_consistent()`**: the two consecutive out-of-tolerance polls of the debounce
  must be on the same side. No "agree within N px" rule: real wedges wander (909 px spread
  in 150 reads), and such a rule would stretch detection past the ~10 s in which the
  OTS-OLED latches black.
- The boot/resync restore path (`force_wedged=false`) goes through the same confirmation.
  Modes 1 and 2 (988) get the same read and confirmation through the shared helpers;
  code-reviewed only, no 988 rig was attached.
- **`wedge_recovery`** module parameter (0644, default 0). `0` = cut stream, DTG pulse,
  settle, enable (the OLED-validated order, unchanged). `1` = cut stream, 984 digital
  reset, wait for FPD-Link re-lock (poll 0x54 bit 0, up to 1 s), settle, re-measure,
  enable; if still out of tolerance, fall back to the pulse once. The digital reset
  preserves the 984 configuration (main page 0x00..0x5F diff: only status bits 0x09,
  0x53[7], 0x54[2] change). **`15.6-2k5` needs `wedge_recovery=1`**; `micropanel`'s
  `pi-config-txt.sh` writes it, and `hh983-config.sh` no longer clobbers it. The OLED keeps
  0 because the digital reset has never been tried on the panel that black-latches.
- `fpdlink-tool.sh` `ind_read15_be()` takes the median of three MSB/LSB/MSB samples, so
  `--timings` / `--diagnose` no longer report "DTG stuck on corrupt H_TOTAL" on a healthy
  pipeline.

Notes: `dtg_wedge_count` counts wedges found by the periodic check only; a wedge found by
the boot restore path is logged in dmesg but not counted. A DTG pulse is harmful to this
panel only when it interrupts a stream the panel is displaying, not at boot with the
stream already cut.

## 4. Validation (`package/hh983-serializer/src/scripts/power-cycle-validate.sh`)

Host-side; cold cycles through the Tasmota socket or warm reboots with `--warm`; the
screen-on decision is a command-response test with the i1Display Pro (`--verdict=sequence`
walks white + four colours in a random order + white; `--verdict=hold` holds one colour
across 10 s, then white); stops at the first failure without power-cycling again and dumps
the evidence. The driver poll is paused around the diagnostic 150-read histogram because
both use the 984's indirect-access registers.

| Run | Build | Result |
|---|---|---|
| 2 cold cycles, unfixed driver | original | stopped on cycle 1: 0.0 nits, 3 false wedges, `--diagnose` "healthy" |
| 9 + 15 cold cycles | confirmation, error-on-tear | 24/24 pass, 0 wedges; histogram still 11..18 % torn |
| 1 warm reboot | same | genuine wedge 4495 undetected, panel not on Pi content |
| 8 warm reboots | final, `wedge_recovery=1` | 8/8 wedged at boot (4239..5007), 8/8 detected and recovered by digital reset, 8/8 pass |
| 10 cold cycles | final | 10/10 pass, measured 2814/2815 at boot, 0 wedges |
| injected wedge, poll stopped at boot, both recovery values | final | detected 0.02 s after the poll was released, both recovered (stream was already cut) |
| forced recovery on a live stream (`dtg_tolerance=0`) | final | `wedge_recovery=1` kept the picture; `0` (pulse) lost it, needed a manual digital reset |

## 5. Open items

- OTS-OLED and 988 rigs have not run the new driver. Detection changes reach them; the OLED
  recovery path is unchanged (`wedge_recovery=0`).
- `3x-qvue` (988) sits 4 px below a 256 boundary; the same confirmation now guards it, untested.
- `dtg_wedge_count` does not count boot-time wedges (see 3.3 notes).
- The i1Display Pro returns `ERROR` for roughly one read in a hundred; the script re-measures.
