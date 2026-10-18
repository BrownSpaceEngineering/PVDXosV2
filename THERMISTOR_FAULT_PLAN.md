# Thermistor Fault Detection & Replacement Plan

Design for making the heater control robust to a single broken thermistor. **Not yet
implemented** — this is the agreed approach to wire into `thermistor_driver.c`.

## Context

- 4 NTC thermistors (10K3A1I), 2 heaters.
- Pairing: heater 1 ← thermistors 0,1; heater 2 ← thermistors 2,3.
- `heater_control_update()` currently drives each heater from the worst/coldest valid
  thermistor of its pair (temporary 2.3 V threshold variant in place at time of writing).
- **Key assumption:** all four thermistors are physically close, so they should read
  within a small spread of each other. "Broken" therefore means "disagrees with the
  group," which is a stronger signal than any fixed absolute range.

## Circuit (as-built, see `thermistor_diagram.png`)

Three-element divider:

```
Vrail --[ R_T1 ]--+-- ADC tap --[ R_T2 ]-- NTC(thermistor) -- GND
```

with `R_T1 = 15.7 kΩ`, `R_T2 = 5.78 kΩ`, and

```
k = code/FS = Vtap/Vrail = (R_T2 + Rth) / (R_T1 + R_T2 + Rth)
=>  Rth = k*R_T1/(1 - k) - R_T2
```

Hard-fault landing spots in this topology:

| Fault            | effect              | k      | code  |
|------------------|---------------------|--------|-------|
| NTC open         | Rth -> inf, tap->rail | ->1.0  | ->4095 |
| NTC shorted      | tap sees R_T2 only  | 0.269  | ~1102 |
| tap short to GND | V=0                 | 0      | 0     |
| tap short to rail| V=rail              | 1.0    | 4095  |

Note the shorted-NTC floor is **k = 0.269, not 0** — a shorted thermistor still reads a
plausible-looking voltage, so "near 0 / near FS" alone is not a sufficient test.

## (1) Detection — outlier-from-consensus

Primary method: use the four sensors as a consensus and flag the disagreeing one.

1. Read all four, convert to temperature (or work in raw ADC-code/voltage space — see
   "open choice" below).
2. Compute the **median** of the four.
3. Flag any sensor with `|value_i - median| > DELTA_FAULT` as broken.

Why median (not mean): the median of four is the average of the two middle values, so a
single wild outlier sorts to an end and cannot drag the reference off the good cluster.
A mean would be pulled toward the outlier and smear detection.

This is stronger than per-pair comparison: a pair is only 2 sensors (can detect
disagreement but not isolate which is bad); across all four there is a 3-vs-1 majority,
so a single failure is both detected and attributed.

### Keep a cheap electrical guard underneath
An open thermistor pushes `k -> 1`, driving `Rth = k*R_T1/(1-k) - R_T2` toward
divide-by-zero / negative. Keep a `k >= ~0.99` or `Rth <= 0` guard so a hard fault yields
a clean "invalid" instead of a NaN/inf that would poison the median before it can be
rejected.

## (2) Replacement — substitute the pair partner

When a sensor is flagged, do not feed its value anywhere; replace it with the **other
thermistor in its heater pair**, not the median/consensus of all four. The two sensors in
a pair sit on the same heater zone, so the partner is the closest available proxy for what
the broken one should read, and it keeps each heater's decision sourced from its own zone
rather than borrowing a cross-board average that could be colder/warmer than the zone the
heater actually controls.

- Heater 1 ← thermistors 0,1: if one is flagged, the heater runs off the other.
- Heater 2 ← thermistors 2,3: same.

In practice this collapses into the existing per-heater logic: drop the flagged sensor and
let the surviving partner drive that heater (coldest-of-pair over whichever members are
still valid). A dead sensor's slot is covered by its pair partner rather than a four-way
consensus value.

**Both sensors of a pair broken:** there is no partner to fall back on, so fall back to the
**consensus** of the remaining good sensors (the median/mean of whatever is still valid —
in the 4-sensor layout that is the other pair). This borrows the other heater's zone, which
is less accurate than a same-zone reading, but it keeps the heater running off a real
temperature estimate instead of going blind. So the replacement order is: pair partner
first, four-way consensus only when the whole pair is gone. (Degenerate case: if *every*
sensor is invalid there is no consensus left either, and that still needs a fail-safe
default — hold last-good or a fixed safe duty.)

## Things to get right

1. **Threshold vs. real gradient.** `DELTA_FAULT` must be above the largest legitimate
   spatial spread across the board (so a genuinely-coldest edge sensor isn't flagged) but
   below a fault-sized jump. If normal tracking is ~1-2 C, flagging at >5-8 C is a safe
   gap. Set from the measured real spread.
2. **Open choice — detection domain:** temperature space (physically meaningful, fixed
   DELTA_FAULT, but depends on Beta accuracy) vs. raw ADC-code/voltage space (avoids Beta
   nonlinearity, simpler, threshold in codes). Near the ~5 C operating point they are
   nearly equivalent; lean code-space for robustness (one fewer thing that can be wrong).
3. **Limits of consensus:**
   - Robustly survives **one** bad reading. Two simultaneous failures that agree-wrong can
     fool the majority (unlikely).
   - A **common-mode** shift (e.g. VDDANA reference drift) moves all four together, so
     consensus won't catch it — but that's a reference fault, not a thermistor fault.

## Scope / status

- Not implemented. When approved, wire into `thermistor_driver.c`: a per-sensor validity
  pass (electrical guard + median-outlier test), substitution of the **pair partner** for
  any flagged sensor, feeding the existing two-heater control.
- Related: the resistance->temperature conversion already matches this circuit
  (`therm_resistance_from_code` uses the 3-element model above). The Beta midrange error
  (~0.4 C near 25 C) is a separate, accepted approximation.
