# BMO Vcomp — the THRU bands stop running away

Measured and built on **AURORA**, 2026-09-14, on branch `vcomp-thru-cap` off
`review-0.2.4`. **Nothing here has been heard yet.** This is the first item on
the 0.2.4 review's "needs a design, not a line" list, and the open question
left at the end of `vcomp-handoff.md`.

Green at handoff: **7 of 7 DSP suites**, Vcomp's own suite **59 checks, 0
failures** (was 55 — `testThruMakeupCannotRunAway` is the new one). The full
Release `ctest` including the plugin-level tests has **not** been run on this
branch; every figure below comes from the DSP-only build.

---

## The arithmetic, which is the whole finding

`LOW THRU` and `HIGH THRU` split bands off, pass them uncompressed, and add
them back. Frosty's spec after the ear pass was that *both* the compressed band
and the thru bands take the automatic makeup.

The makeup exists to give back what the curve took at the reference level. The
compressed band has had something taken, so it comes out near where it went in.
The thru band has had nothing taken, so it comes out **the whole makeup figure
louder** — and the makeup figure is what AMOUNT spends, so the harder the knob
is pushed the further the thru band runs.

Take the cap out and the new test prints the mechanism exactly:

| AMOUNT | thru band's lift, uncapped | makeup from `measure_vcomp curve` |
|---|---|---|
| 50% | 12.44 dB | 12.44 |
| 70% | 18.92 dB | 18.94 |
| 90% | 25.51 dB | 25.55 |

It is not approximately the makeup. It *is* the makeup, to the second decimal.
No voicing could have tuned this away.

---

## What ships

The 0.2.4 review's recommendation, unchanged: the thru path takes the makeup
**less the reduction the curve applies at body level**, body level being
`kReferenceDb - kThruBodyOffsetDb` — 6 dB under the reference, because chest
and air sit under the peaks the detector reads rather than on them.

That figure is the net gain the thru content would have come out with had it
been compressed along with everything else. So the thru band is neither pumped
(it has no dynamics at all) nor lifted past the rest of the voice. The curve
works it out for itself at every AMOUNT; there is no constant anybody picked
except the 6 dB that defines body level.

`thruMakeupDbFor` in `modules/vcomp/dsp/DspCore.h`. No latency, no dynamics,
and the `std::pow` stays out of the per-sample path when the split is bypassed.

---

## The four candidates, measured

Tilt is `measure_vcomp balance` — the 150 Hz band against the 1500 Hz band,
relative to LOW THRU off, so 0 means engaging LOW THRU left the balance alone.
Pumping is `measure_vcomp bands` — how much a 2 kHz burst modulates a steady
80 Hz tone sitting in the thru band, where 0 means the split has taken that
tone out of the compressor's reach and -8.6 is the fully-compressed case.

Both columns want to be near zero. Every figure is from a real build on AURORA.

| candidate | 30% | 50% | 70% | 90% | pumping |
|---|---|---|---|---|---|
| shipped, no cap | 4.55 | 8.78 | 12.94 | 17.67 | -1.48 |
| **1. partial compression on the thru band** | | | | | |
| — fraction 0.35 | 2.85 | 5.62 | 8.82 | 12.09 | -3.00 |
| — fraction 0.50 | 2.16 | 4.25 | 6.75 | 9.44 | -4.29 |
| — fraction 0.70 | 1.25 | 2.45 | 3.90 | 5.56 | -6.00 |
| **3'. thru gain tracking the compressor's, slowly** | | | | | |
| — 150 ms | 1.17 | 1.65 | 1.96 | 2.13 | -7.64 |
| — 300 ms | 1.63 | 2.52 | 3.27 | 3.93 | -5.66 |
| — 600 ms | 2.22 | 3.80 | 5.38 | 6.90 | -1.51 |
| — 1000 ms | 2.75 | 4.96 | 7.24 | 9.49 | **+1.56** |
| — 2000 ms | 3.43 | 6.43 | 9.47 | 12.64 | **+3.49** |
| **2. a flat cap on the thru makeup** | | | | | |
| — 3 dB | 1.58 | 0.27 | **-0.66** | **-1.24** | -0.03 |
| — 6 dB | 4.33 | 2.93 | 1.95 | 1.33 | -0.02 |
| — 9 dB | 4.55 | 5.65 | 4.63 | 3.98 | -0.02 |
| **2'. the curve's own figure — shipped** | **2.42** | **1.74** | **1.05** | **0.62** | **-0.03** |

### Reading it

- **Partial compression scales the tilt, it does not bound it.** Every fraction
  still grows with AMOUNT, and every fraction pays for what it does buy in
  pumping — the one thing the thru band exists to escape. The fraction that
  would bound the tilt is the fraction at which there is no thru feature left.
- **Slow tracking half works and adds a new fault.** Tilt improves, and then
  the pumping figure goes *positive* at 1 s and 2 s. Positive is not "better
  than zero": it means the thru band's gain arrives late, so the reduction
  lands in the quiet after the burst instead of during it, and the post-burst
  reference is the part that got ducked. A low end that dips after the singer
  stops is a worse artefact than a low end that pumps with them. The 600 ms row
  is where the lag crosses over — good tilt, apparently normal pumping, and the
  artefact hidden exactly at the crossing. That row is why this candidate was
  measured at five time constants instead of one.
- **A flat cap works and has to be argued about.** 3 dB is too tight — the tilt
  goes negative at AMOUNT 70 and 90, which is the thinning the ear rejected in
  the first place. 6 dB bounds it. 9 dB holds a flatter tilt but puts "Keep The
  Chest" back outside tolerance. Being a number rather than a consequence,
  every one of those is a position someone has to defend.
- **The curve's own figure wins on both columns and needs no defending.** It is
  the flattest tilt of any candidate, the nearest to zero at the top of the knob
  where the problem was, and it is derived rather than chosen.

---

## What it buys back

| "Keep The Chest" | delta against the +/-3 dB tolerance |
|---|---|
| shipped, at AMOUNT 70 (where it wants to be) | **+8.14** |
| shipped, at AMOUNT 35 (where it had to go) | +3.37 |
| flat 6 dB cap, at AMOUNT 70 | +1.96 |
| **shipped here, at AMOUNT 70** | **+1.36** |
| **shipped here, at AMOUNT 35** | **+1.74** |

The preset that introduces the feature can be written at the strength the
module is capable of instead of at a third of it.

**Note the +3.37 row.** On `review-0.2.4` as it stands, the measure tool puts
"Keep The Chest" outside the tolerance at the AMOUNT it currently ships at. The
plugin-level `VcompTests` level-matching check uses its own harness and passed
26 of 26 at the review handoff, so this is the tool's source and not
necessarily a failing test — but it is close enough to the line to be worth
confirming in the full build before the merge.

---

## Two things found on the way

1. **The `bands` report has been measuring the limiter as well as the split.**
   The shipped build's -1.48 dB of residual pumping on the thru tone is not the
   crossover leaking. It is the thru band, lifted by the full makeup, driving
   the limiter, which then rides the burst. Held at body level the thru band
   never reaches the limiter and the figure is -0.03. Any small number read out
   of that report at AMOUNT 80 should be read with the limiter in mind.
2. **The factory preset table exists twice** — in
   `modules/vcomp/presets/FactoryPresets.h` and again in
   `tools/measure/vcomp/main.cpp`. The 0.2.4 review already caught the tool's
   copy going stale once. Editing the module's presets and re-running the tool
   silently measures the old ones, which cost a wrong measurement here before it
   was noticed.

---

## What is not done

- **Nobody has heard it.** Everything above is measurement, and this repo has
  been wrong about what an ear would say before. The listening question is
  narrow and belongs on the same take as the rest of the Vcomp ear pass: push
  AMOUNT to 70-90 with LOW THRU around 150-300 and check that the low end still
  *arrives* — that capping it has not made LOW THRU feel like it stops working
  at the top of the knob. Then the same at the top with HIGH THRU on a sibilant
  source.
- **The two THRU presets are still at AMOUNT 35.** Raising "Keep The Chest" and
  "Keep The Air" back to 70 is inside tolerance now, but it changes what a
  preset sounds like, so it waits for the ear.
- **`kThruBodyOffsetDb` is 6 dB and that one is a picked number** — how far
  under the detector's peaks the chest and the air are taken to sit. Larger
  means more thru lift, smaller means less. Nothing was swept here; 6 dB is the
  review's figure.
- **The band-split crossfade** is untouched and still on the review's list.
