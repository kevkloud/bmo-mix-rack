# BMO Opto 0.2.1 — handoff

What this branch changes, what was measured to justify it, and what is still
open. Written for someone reading the diff cold.

Everything below was measured against real renders: a vocal ("Fuji") bounced
from Ableton through BMO and through two hardware-model references, all
gain-matched. Where a number appears, it came from a file, not from a
simulation of one — except where explicitly labelled.

---

## 1. BMO Opto: the release now gives the gain back

**The bug.** Both cells held reduction across phrase gaps and got worse as a
take went on. Measured by recovering each render's gain envelope against the
dry file and reading it across the nine phrase gaps of 250 ms or more:

| | peak GR | entering gap | leaving gap | recovered |
|---|---|---|---|---|
| Distressor (reference) | 7.77 dB | 3.60 | −0.51 | **114%** |
| Distressor + Color | 8.26 dB | 3.83 | −0.48 | **112%** |
| Competitor LA-2A (reference) | 4.15 dB | 1.47 | −0.07 | **105%** |
| BMO Tele | 5.78 dB | 3.78 | 1.39 | **63%** |
| BMO ELD | 5.04 dB | 2.87 | 2.21 | **23%** |

Both references release completely inside a 0.3–1.0 s gap. Ours did not, and
the shortfall grew across the take — the first gaps recovered 112–122%, the
last five 32–47%.

**The cause.** `chargeDb`'s forget constants were longer than the gaps they
had to forget across, so charge only ever climbed; and `depth = chargeDb / 20`
maps charge linearly onto a 15–20 s ceiling. A routine 4 dB hit was buying a
multi-second tail.

**The fix**, fitted rather than guessed. `Detector.h` was ported to C# and run
against the same renders, reproducing the measured 63% / 23% to within a
couple of points *before* anything changed, which is what made it safe to fit
constants against:

    La2aCell  kReleaseSlowMaxTauSec   15 -> 4
    Stressed  kReleaseSlowTauSec      20 -> 3
    Stressed  kChargeReleaseTauSec     4 -> 0.7

Tele now measures 4.26 / 1.76 / 0.00 / **100%** against the LA-2A's 4.15 /
1.47 / −0.07 / 105%. ELD measures 7.48 / 3.66 / 0.27 / **93%** against the
Distressor's 7.77 / 3.60 / −0.51 / 114%. (Above 100% is a method artifact —
the render-derived envelope can overshoot, the simulation floors at zero.
93–100% and 105–114% are the same behaviour.)

The dosage memory the long ceiling exists for is intact: driven hard at
CRUSH 85 the cells still only give back 70% (Tele) and 44% (ELD). Recover
between phrases, hold on when leaned on.

**A test was deleted for being relative.**
`testDistressorReleaseCeilingExceedsLa2a` compared the two modes' retained
*fractions*. It passed for the whole of 0.2.0 while Stressed sat on **26.7 dB
of reduction ten seconds into pure digital silence** — because Tele was
sitting on 9.2, so the ratio still held. `testReleaseGivesTheGainBack`
replaces it with two absolute bounds at −18 dBFS: released after 3 s, still
holding after 1 s. Neither can be satisfied alone, so "fix it by making it a
fast compressor" fails the second.

**A listening result worth recording.** The tester had reported a missing
presence around 2.5 kHz and asked for the LA-2A's 2–2.4 kHz lift to be
modelled. Fine-band analysis said there was nothing there to add — BMO tracks
the reference through 2–3.2 kHz and sits slightly *higher*. The hypothesis was
that the release bug caused it, since Tele was leaving 1.39 dB of reduction
standing at phrase onsets where the reference left −0.07, which reads as lost
attack. **After the release fix the tester confirmed the presence was back.**
No EQ was added. Recorded because the tempting fix would have baked a boost in
to chase a symptom.

---

## 2. BMO Opto: panel

Measured against `core/ui/Tokens.h`. The VU meter's hot zone was `#97ddff`
drawn on `well #d6d6d6`, which measures **1.02:1** — the one element whose job
is to be seen before you look at it was invisible against its own background.

- Meter face is dark, needle and scale white, hot zone the knob-cap lavender.
  Needle **11.4:1**, hot zone **8.2:1**.
- Scale type 8.5 → 11.5 pt, sweep 100° → 124°, and −2/−1/+1/+2 are struck but
  not numbered. That last part matters as much as the size: from −3 up the
  scale crowds into the final third of the sweep, and inking every tick there
  is half of why it was unreadable. Hardware faceplates ink only the round
  figures.
- `MAKEUP` no longer clips to `MAKEU`. `PlainKnob` lays its knob out square
  with an optional side cap, so a control can be wider than its knob; the
  radius comes from `jmin(w, h)`, so no existing knob anywhere changes size.
- Every switch is a `juce::ToggleButton` drawn by `BmoLookAndFeel` — the same
  70×26 control as BMO Util's polarity switches, rather than three different
  button classes on one panel. Engaged colour is the darker accent step the
  COMP/MAKEUP captions use; white text on the raw accent is unreadable.
- TELE/ELD is one button carrying its own state, above COMP. LINK and COLOR
  stack under MAKEUP. COLOR is disabled rather than hidden in Tele mode —
  hiding it moved LINK every time the mode changed.

---

## 3. BMO Saturator: the sheen is the voicing, not the curve

**The blocking question is answered.** Every prior measurement of this stage
was made against a render with **AUTO on**, confirmed by the person who
bounced it. AUTO is `updateAutoGain` — a broadband scalar driven by a ~1.5 s
RMS ratio, clamped ±12 dB. That voids every asymmetry figure in the 0.4.0 and
0.5.0 passes: the static-transfer extraction was reading AUTO's makeup as
curve gain, which is why extracted negative-half gain exceeded 1.0. Because
AUTO is broadband it could not distort the band *tilt*, and that is now
confirmed rather than argued — AUTO-on and AUTO-off renders agree within
**0.2 dB in every band**.

**A fresh render pair separates voicing from waveshaper.** Same preset, AUTO
off, one at TONE 100 and one at TONE 0:

> At TONE 0 the plugin is within **0.5 dB of the dry file everywhere above
> 5 kHz**. At TONE 100 it is +8.8 to +9.6 dB.

The waveshaper contributes essentially nothing up there. This stage is the
whole high end — which is `DspCore.h`'s own "97–98% is a linear filter" note
arrived at from the other direction.

**The error was shape, not level.**

| band | reference | ours | ours − reference | rate artifact |
|---|---|---|---|---|
| 5–7k | +9.68 | +8.79 | −0.89 | −0.07 |
| 7–9k | +11.13 | +9.59 | **−1.54** | +0.13 |
| 9–12k | +6.53 | +7.66 | **+1.13** | −0.18 |
| 12–16k | +2.85 | +4.67 | **+1.82** | −0.29 |
| ~~16–20k~~ | +1.84 | +0.57 | ~~−1.27~~ | **−1.29** |

The reference peaks sharply at 7–9 kHz and falls away; ours peaked lower and
spilled upward. That is a bell too wide, and not a shelf set too high: a shelf
error is monotonic above its corner, and this one **changes sign** — under the
reference at 5–9 kHz, over it at 9–16 kHz. Fitting f0/Q/gain against those
four figures leaves the centre at 7 kHz and moves the width: `bellQ`
0.90 → 1.40, `bellGainDb` 11.0 → 13.5.

**The 16–20 kHz row is struck out because it is measurement artifact.** The
reference vocals are 44.1 kHz and the renders are 48 kHz Ableton bounces;
running the *same* audio through both paths costs 1.29 dB in 16–20 kHz and at
most 0.29 dB in every band below it. Our figure there was −1.27 dB — the
artifact, and nothing else. The four bands the fit uses are 4–6× larger than
their artifacts, so they stand. The frequency axes are sound: the bounces
align with the originals to a scale factor of 1.00000, so nothing is
pitch-shifted, and each file is binned at its own rate.

`sheenTilt` is deliberately untouched despite the 0.5.0 pass naming it a
suspect. It acts only on the sheen generator's output, and TONE 0 shows that
output is too small to move a band table. Changing it would have measured as
nothing.

---

## 4. Preset levels: all thirteen re-solved

A DSP change that moves how much reduction a preset holds moves every preset's
makeup figure. The level-matching tests have a ±3 dB tolerance and a pass
prints no number, so drift stays invisible until something crosses the line.
Setting `BMO_PRINT_PRESET_LEVELS` and running `ctest -V` reports every delta,
pass or fail. It found all thirteen had drifted:

- **BMO Opto** — Gentle −1.86 dB, Vocal Glue +0.67, Crushed <3 +0.23 (after a
  first correction). Gentle is the one worth catching: 1.86 dB is audible when
  auditioning presets against each other, and it had been that way since
  0.2.0, when it was left alone for being "inside tolerance".
- **BMO Saturator** — all ten between −0.76 and −1.39 dB, a uniform droop
  across the whole product.

After correction every one of the thirteen lands within **0.065 dB**.

The Saturator droop is **not** the bell change, which is the obvious suspect.
Weighting each bell's power response by the test signal's own spectrum puts
the difference at **−0.009 dB**: `voice()` is a 75 Hz harmonic stack whose
energy sits far below 7 kHz. The droop predates the bell.

`Reference` and `Mix Bus Colour` are the weakest two of the thirteen and are
marked as such in the file: they ship with Auto Gain on, so their level is set
by a dynamic matcher and their new Output figures compensate that matcher's
residual with a static number fitted on one signal.

**`Crushed <3` is off the rail.** It wanted 26.21 dB of makeup against a
permanent +24 dB range and had been pinned there for two releases. After the
release fix it needs ~20.1 dB — 3.6 dB of clear headroom, reached as a side
effect of fixing the release rather than by anything aimed at the level. CRUSH
stays at 85; whether it should now go deeper is an ear decision, not a
measurement one, and the headroom to do it exists.

---

## 5. Tools

- **`tools/measure/opto/`** — a JUCE-free C++ harness driving `DspCore`
  directly, plus `trace_measurements.py`. Written earlier, stranded on an
  unpushed local branch, rebased onto this work. Wired into
  `tools/CMakeLists.txt` alongside `measure_eq` and `measure_sat`, so it
  builds in the DSP-only job.
- **`tools/measure/renders/`** — a C# harness that reads WAV renders:
  gain-envelope recovery and phrase-gap analysis, band tables with rows above
  9 kHz, a port of `Detector.h` for fitting release constants, and a peaking
  filter fitter. **Deliberately not wired into CMake.** CI cannot see the
  renders, the machine that can has no C++ toolchain, and a broken analysis
  script should never be able to fail a plugin build. Its README records the
  mistakes that cost wrong answers.

---

## 6. The Saturator bell: two fixes for one symptom

`main` already carries a fix for the same reported sibilance — soft-limiting
the bell's boost with `tanh` at a fitted threshold of 0.42 (`Filters.h`). This
branch changes the bell's Q and gain (`DspCore.h`). **Different files, so they
merge without conflict and both end up live**, each fitted assuming the other
absent. That is the reason this section exists.

All four combinations, measured on the real AUTO-off renders. Band columns are
dB from the reference; crest is absolute in 5–10 kHz. The model was validated
first: the modelled linear bell reproduces the *actual* Render A to within
0.04 dB in every band, so the other rows can be trusted.

| variant | 3-5k | 5-7k | 7-9k | 9-12k | 12-16k | crest |
|---|---|---|---|---|---|---|
| **reference** | 0 | 0 | 0 | 0 | 0 | **14.49** |
| real Render A (linear Q0.90 +11) | 0.30 | −0.89 | −1.55 | 1.13 | 1.83 | 14.21 |
| modelled linear Q0.90 +11 | 0.29 | −0.90 | −1.56 | 1.12 | 1.82 | 14.25 |
| soft-limit alone, Q0.90 +11 | 0.24 | −0.98 | −1.67 | 1.02 | 1.78 | 14.24 |
| Q1.40 +13.5 (this branch) | 0.32 | **0.67** | **0.12** | 1.48 | 1.20 | 14.15 |
| both stacked | 0.26 | 0.54 | −0.06 | 1.32 | 1.17 | 14.12 |

**The soft-limiter does not engage on material at track level.** Its threshold
is an absolute amplitude of 0.42. Measured on the render, the bell's own boost
`|amount * bandpass(x)|` reaches:

| | Q0.90 +11 | Q1.40 +13.5 |
|---|---|---|
| peak | 0.4333 (−7.3 dBFS) | 0.5646 |
| 99.99th percentile | 0.2087 | 0.2675 |
| 99th percentile | 0.0616 | 0.0766 |
| **samples above 0.42** | **0.0001%** | 0.0002% |

About one sample in a million. That is why it moves band energy by ≤0.11 dB
and crest factor by **0.01 dB** (14.25 → 14.24) here. A threshold fitted where
the bell was driven hard is inert where a track actually arrives — the same
class of error as reading a compressor at −6 dBFS, a mix-bus level, and
concluding something about how it behaves on a −18 dBFS track.

**Every variant sits below the reference's crest** (14.12–14.25 against
14.49), including the untouched one. The premise the soft-limit was built on
is that BMO is peakier than the reference. On the AUTO-off render it is not,
which matches this branch's finding of no transient difference and is
consistent with the earlier contrary reading having been taken from an AUTO-on
render.

**Stacked scores best on band error but for a bad reason.** Nearly all of its
edge is the top band, where `tanh` distortion products fill in a band carrying
~20 dB less energy than the others — and that band is the one struck out above
as rate artifact anyway. It is a metric flattered by distortion, not a fix.

Recommendation, for the maintainer to accept or reject: the Q/gain change does
the work, the soft-limit does not engage, and if it is meant to it needs a
level-relative threshold rather than an absolute 0.42. Both are currently in
the tree and nothing of the maintainer's has been reverted.

**Resolved 2026-09-07, by the maintainer.** Kevin's `51a263b` ("Drop the
voicing bell's soft-limit in favor of Frosty's Q/gain fit") removed the tanh
stage from `Filters.h`; `Bell::process` is `x + amount * bp` and nothing
under `modules/sat/` carries a 0.42 any more. The live response is the
Q 1.40 / +13.5 dB bell alone. The ten Saturator preset levels, re-solved on
2026-09-06 with the limiter still in, land within 0.005 dB today, which is
independent confirmation that it was inert (0.2.4 review, AURORA,
2026-09-14). `sat-voicing` is therefore a listening round and nothing else.

## 7. Open

- **`Crushed <3` depth** — affordable now, undecided.
- **~~The Saturator bell~~ — resolved 2026-09-07 by `51a263b`**, see the end
  of §6. One fix in the tree, the Q/gain fit; never heard on either machine.
- **~~Naming~~ — settled 2026-09-11 (Frosty).** TELE, ELD and COLOR are the
  names. They were working names; they are not any more. The panel's button
  labels are UI strings, so this costs nothing in the schema — the `Mode`
  parameter's choices stay `Tele` / `Stressed`, which is what a saved session
  references.
- **~~Stereo Link~~ — settled 2026-09-11 (Frosty): LINK stays a control.**
  Not hard-patched always-on. Hard-patching it would have taken a parameter
  off the panel and out of the schema, and any session that had set it off
  would open sounding different. It can be re-opened later as a deliberate
  retirement rather than a leftover question.
- **The 2–2.4 kHz LA-2A lift** is deliberately still not modelled. It was
  asked for, measured, found absent, and the release fix resolved the
  perception instead.

---

## 8. Two lessons this round, both about measurement

- **Relative assertions hide absolute failures.** The deleted release test is
  the clearest case: it compared two modes to each other and passed while both
  were broken. Prefer absolute bounds, at the level a track actually arrives
  at (−18 dBFS RMS / −12 dBFS peak, not −6, which is a mix-bus level).
- **Bin every file at its own sample rate.** The reference vocals are 44.1 kHz
  PCM and the Ableton bounces 48 kHz float. Binning everything at the
  reference's rate stretches each bounce's frequency axis by 8% and produces a
  phantom −8 dB shelf in the top band — which was very nearly reported as a
  finding.
