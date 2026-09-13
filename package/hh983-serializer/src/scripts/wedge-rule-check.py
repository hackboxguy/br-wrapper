#!/usr/bin/env python3
"""Host-side check of the DTG wedge decision rule in hh983-serializer.c.

Reimplements hh983_dtg_confirmed_bad() plus the two-poll debounce exactly as
the driver has them, and runs it against measurement distributions actually
recorded on the bench.  The point is the asymmetry the rule depends on:

  * on a healthy pipeline a torn sample is wrong by exactly +-256, so it lands
    on 2560 (below the programmed 2816) or 3070 (above it) - to fool the rule,
    five raw samples in a row must be torn AND all torn the same way;
  * on a wedged DTG every sample is far above the programmed value whatever
    tearing does, because a tear only moves a value inside its own 256-block or
    into the neighbouring one - so the rule fires on the first poll.

Run: python3 wedge-rule-check.py
"""
import random

TOL = 32
SAMPLES = 5          # DP_GUARD_MEAS_SAMPLES
WEDGE_POLLS = 2      # DP_GUARD_WEDGE_POLLS
PROG = 2816

# 150 raw MSB+LSB reads on a healthy pipeline, cycle 1 of the 2026-09-13 cold
# run (power-cycle-validate-20260913-1811*.log).  18 of 150 torn = 12 %.
HEALTHY = ([2814]*71 + [2815]*46 + [2813]*12 + [2560]*10 +
           [3070]*4 + [3071]*2 + [3069]*2 + [2810]*2 + [2812]*1)

# 150 reads taken during the genuine wedge of 2026-09-13 (post-install-real-
# wedge.txt): 4201..5110 against the same programmed 2816.
WEDGED = ([4597]*10 + [4751]*8 + [4495]*8 + [4521]*7 + [4778]*6 + [4522]*6 +
          [5109]*5 + [5007]*5 + [4777]*5 + [4494]*5 + [5034]*4 + [4853]*4 +
          [4493]*4 + [4341]*4 + [4238]*4 + [5110]*3 + [5033]*3 + [4976]*3 +
          [4718]*3 + [4596]*3 + [4520]*3 + [4340]*3 + [4266]*3 + [4206]*3 +
          [5005]*2 + [4776]*2 + [4749]*2 + [4719]*2 + [4594]*2 + [4463]*2 +
          [4462]*2 + [4265]*2 + [4239]*2 + [5108, 5008, 5006, 5004, 4975,
          4974, 4852, 4752, 4750, 4721, 4720, 4627, 4595, 4491, 4464, 4342,
          4263, 4240, 4209, 4207])


def confirmed_bad(samples, prog=PROG):
    """hh983_dtg_confirmed_bad(): every sample out of tolerance, same side."""
    first = samples[0]
    if abs(first - prog) <= TOL:
        return False
    above = first > prog
    for meas in samples[1:SAMPLES]:
        if abs(meas - prog) <= TOL:
            return False
        if (meas > prog) != above:
            return False
    return True


def poll(draw, prog=PROG):
    return confirmed_bad([draw() for _ in range(SAMPLES)], prog)


def run(name, population, polls, expect_wedge):
    rng = random.Random(20260913)
    draw = lambda: rng.choice(population)
    bad = streak = wedges = 0
    first_at = None
    for i in range(polls):
        if poll(draw):
            bad += 1
            streak += 1
            if streak >= WEDGE_POLLS:
                wedges += 1
                streak = 0
                if first_at is None:
                    first_at = i + 1
        else:
            streak = 0
    verdict = "PASS" if (wedges > 0) == expect_wedge else "**FAIL**"
    print(f"{name:<34} polls={polls:<7} confirmed-bad polls={bad:<7} "
          f"wedges declared={wedges:<7} first at poll={first_at}  {verdict}")
    return wedges


print(f"tolerance={TOL} samples/poll={SAMPLES} polls/wedge={WEDGE_POLLS} "
      f"programmed={PROG}\n")
print("A healthy pipeline must never reach a wedge; a wedged one must reach it")
print("on the first opportunity, i.e. poll 2.\n")

h = run("healthy (12 % torn reads)", HEALTHY, 1_000_000, expect_wedge=False)
w = run("genuinely wedged (4201..5110)", WEDGED, 1_000, expect_wedge=True)

print()
print(f"healthy: {h} false wedges in 1,000,000 polls "
      f"(~11.6 days of polling at 1 Hz)")
print("wedged : detected on poll 2, i.e. ~2 s - inside the ~10 s the OTS-OLED")
print("         takes to latch black.")
raise SystemExit(0 if h == 0 and w > 0 else 1)
