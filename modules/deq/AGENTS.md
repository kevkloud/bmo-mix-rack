# modules/deq/ — BMO DEQ, the zero-latency dynamic EQ

Read `modules/AGENTS.md` first. This file is what cannot be read off the code:
the invariants, what the spec asked for that could not be delivered as written,
and what is still open.

The spec is `spec/spec-v0.1.md` (as received, unedited). `spec/review-v0.1.md`
is the review of it, with the measurements behind every disagreement.

## Status

**A product.** `BmoDeq` builds as VST3/Standalone (AU on macOS), and the rack
hosts it. Not yet heard in a DAW: `testing-notes/deq-testing-checklist.md` is
that pass, and `testing-notes/deq-topology-listening.md` the serial-vs-parallel
one before it. Every decision so far is in `spec/decisions.md`; in short:

- **Identity** (PR #8 on `main`): `deq`, `Bpar`, `com.lt3audio.bmodeq`,
  `.bmodeq`, teal `#5ecfc0`.
- **159 parameters**, 12 bands x 13 controls plus output, DEQ and AUTO
  (appended at 158). The rack's 32 lanes go to output, bands 1-6 by frequency
  / gain / Q / threshold / range, and DEQ; the rest are off the grid
  (`SlotOverflow`). `params.h` has the map, `DeqTests` the frozen table,
  `RackTests` the lanes.
- **Two widths** -- 320 compact (mockup C), 600 full (mockup A). A rack opens
  it compact, standalone full; the switch is on the host's bar. The machinery
  is in core (`ModuleDef::expandedWidth`; `core/AGENTS.md` has the rules) and
  only DEQ uses it.
- **Knobs show values**, **AUTO** is BMO EQ's static compensation
  (`dsp/AutoGain.h`), and **a shelf's Q stops at 2** (`kShelfMaxQ`).
- **Serial**, pending the listening test.

## The panel

One component, two layouts, chosen by nothing but the width it is handed
(`DeqPanel::isShowingExpanded`). **The layouts are the mockups Frosty chose,
A (600) and C (320)** -- `spec/panel-mockups.html`, the "BMO DEQ Panel
Options" page as published -- and `DeqPanel.cpp` lays each row at the
mockup's own y, written beside it. Where it departs from them,
`spec/decisions.md` says why.
Do not "tidy" a row away from the mockup without asking: the first build did,
one reasonable step at a time, and Frosty sent it back.

The curve (`ResponseView`) draws the bands' own matched-Z designs multiplied
together -- the serial topology for a centred source -- so it cannot drift
from the audio; drag a node for frequency and gain, wheel for Q, double-click
to switch on the next free band. Selecting a band (tab or node) rebinds the
controls under the curve; which band is selected is panel state, reached by
`snapshot ... ui.band=N`.

**Only a band that is on gets a node** (Frosty, 2026-09-11): "12 dots when
none are active is distracting and confusing". Twelve nodes over a flat curve
read as a control surface rather than as the EQ's state, and nothing told the
eye which of them was live. A band that is off but *selected* keeps its node,
dimmed, because the tab strip can select one and a selection with nothing on
screen is worse than a quiet dot; its shape is not shaded under the curve
either, which used to draw a low cut across the default panel that nothing
was doing. A band with no node cannot be grabbed: reaching one that was off
used to move its frequency and gain with nothing to see and nothing to hear.
Double-clicking the plot is how a band is added, and now the only way -- so
that gesture is load-bearing, not a shortcut.

Things a build got wrong that a green suite would not have shown, and what now
holds them:

| fault | caught by |
|---|---|
| THRESH, ATTACK, RELEASE overflowed their cells by 6-17 px | `ui_layout_tests` captions; the cells are now as wide as the words |
| the shape switches clipped to "BEL", "LO CU" | a **render**, not a test -- the switch-label check only looked at SwitchButtons. It now measures every toggle, and was mutation-checked. (Shape is now the stepped dial.) |
| a continuous log frequency round-tripped a 32-bit normalised value with 3e-4 Hz error | the golden schema's default check; frequency now steps 0.1 Hz |
| values overflowed 72 px compact cells ("+24.0 dB" is 76 px in the caption face, which runs ~30 % wider than the mockups' stand-in) | `captionOverflow`, which measures values too; compact shows mockup C's short form |
| the frequency axis cut "100" to "10" | a **render**; the label boxes are 48 px |
| a 50 px knob at face scale 0.62 ran its dotted track off its own edge | a **render**; `size()` in `DeqPanel.cpp` scales the cap to the side |
| band 12's node was cut by the well's edge | a **render**; the curve overhangs its well by `ResponseView::kOverhang` |

`checkDeqPanel` in `ui_layout_tests` holds Frosty's three calls: every band
knob shows its value, SHAPE is the dial and fits, AUTO is on the output row.

Render both widths before calling a panel change done:

    snapshot deq out.png ui.band=6                 # full, as standalone opens
    snapshot deq out.png view=compact ui.band=6    # compact, as a rack shows it

(A render viewed twice at the same path can come back cached from the image
viewer. Render to a new name when checking a change.)

## The invariant: zero latency, structurally

Reported latency is 0 in every mode, and the *reason* it is 0 is that nothing
in the audio path can delay: every band is a recursive filter whose output at
n depends only on input up to n. No delay line, no lookahead, no FIR, no
block-buffered stage. `DspCore::latencySamples()` is a `constexpr 0`.

The test that protects this is not the impulse test — it is **block-size
invariance**: one 4096-sample block and 4096 one-sample blocks give the same
bits, with dynamics running. Any block-level state fails it. That is why:

- **Coefficient redesign runs on an absolute sample counter** (every
  `kControlInterval` = 8 samples from `prepare()`/`reset()`), never at block
  boundaries. Redesigning "once per block" would be cheaper and would break
  this.
- **Denormal flushing runs on the same cadence.** Flushing at the end of a
  block makes the output depend on where the blocks fall.

A mutation test (a `tickPhase = 0` at the top of `process`) failed 12 checks.
Keep it that way.

## Design decisions and why

| decision | why | evidence |
|---|---|---|
| Matched-Z poles, fitted zeros | bilinear cramps near Nyquist; oversampling to fix it is latency | bells 3.7x-58x better than the cookbook in region 3 (`measure_deq gate`) |
| **TPT SVF structure, loaded from a biquad** | an SVF glides under modulation, a direct form does not; `SvfCoeffs::fromBiquad` realises *any* stable biquad | IR matches DF-I to 5e-14 |
| Bell: DC, gain at f0, zero slope at f0 | the peak is where the knob says, at the height it says | exact to 1e-5 dB at every rate |
| Shelves, high cut: least squares, DC pinned | three-point fits failed outright on some shelves and were 5 dB out on others | see review |
| **High shelf built from the low shelf** | a boosted HS's poles are at f0·√A, above Nyquist for big boosts up top, and e^(sT) aliases them (25 dB error). HS(A) = A²/LS(A) exactly | HS error = −(LS error), worst 0.87 dB at Q ≤ 2 |
| **Cut = reciprocal of the boost** | analogue H(−g) = 1/H(g); boost-then-cut nulls exactly, and cuts come out as accurate as boosts instead of ~3x worse | symmetry 1e-6 dB (spec asked 0.01) |
| Detector hears the **dry** input through **its own** sidechain filter | tapping the band's filter gives a detector whose gain moves with the band's (bell pole Q = A·Q): 0 dB at −12, +12 dB at +12 — a feedback loop | spec §3.2 "the tap is free" is wrong for a dynamic band |
| Every band filters M and S, not L and R | H(L) = H(M) + H(S), so one filter pair gives both the L/R and M/S contributions; the M/S blend is exact at every value with no warm-up | T6 blend sweep reads back continuous and monotone |
| tau time constants | BMO Opto already uses them (`coeffFor`); one suite, one convention | T5 attack within 5 % |


### The low cut's zeros sit on the circle

A low cut is fitted with its two zeros pinned together at DC, so they are a
double root **on** the unit circle, not inside it. Anything that asks "is this
minimum phase?" by computing root radii cannot answer that reliably: the
discriminant `b1^2 - 4 b0 b2` of a double root is nothing but rounding noise,
and taking its square root magnifies that noise to about 1e-8 in the radius --
larger than any sane tolerance. So the answer came down to how one expression
happened to round, and that differs between compilers.

It cost a day on 2026-09-11. `deq_dsp` passed on Windows and Linux and failed
on macOS alone, in T2's low-cut ceilings: 3.313 and 2.404 against limits of
1.89 and 1.69, where Windows measured 1.793 and 1.607. Nothing was wrong with
the design. clang contracts `b1*b1 - 4.0*b0*b2` into an fma and MSVC does not,
so on macOS the low cut's own fit was judged not minimum phase, and
`designByLeastSquares` fell back to `mapZerosToo` -- a filter with the same
zeros and poles but the gain matched at f0 instead of by least squares. That
fallback is exactly 3.313/2.404, which is how the cause was pinned down.

`Biquad::isMinimumPhase` is therefore written on the coefficients
(Schur-Cohn: `|b2/b0| <= 1` and `|b1/b0| <= 1 + b2/b0`), the same shape of
test `isStable()` makes on the poles. It is linear in the coefficients, so
nothing cancels and every platform agrees. **Do not rewrite it in terms of
roots**, and do not widen T2's ceilings to make a platform pass: a ceiling
that moves is a design that changed.

## Where the spec was changed in the tests, and why

Every one of these is argued with numbers in `spec/review-v0.1.md`.

- **T2 absolute targets** are asserted only at f0 ≤ 200 Hz. Above that, a wide
  loud band's skirt extends past Nyquist and no biquad can follow it (bells
  pass 42/60 at 5 kHz, 14/60 at 18 kHz). The rest of the grid is held by the
  spec's **comparative gate** (passes, 3.7x minimum) and by **regression
  ceilings** in `testAccuracyCeilings` — measured values +5 %.
- **T3 `a2 = e^(−w0/Q)`** is false for bells and shelves with the knob values.
  Asserted instead against the prototype's own poles, `e^(−(d1/d2)/Fs)`.
- **T3 analytic vs IR** uses an FFT as long as the filter needs
  (`irLengthFor`), and the *engine's* IR, not a direct-form stand-in. The
  spec's fixed 64k was 0.35 dB out on slow filters from truncation alone.
- **T5 overshoot** as worded (≤ 3 dB in the first 3 ms at 0.1 ms attack; none
  at ≥ 10 ms) is impossible for any causal detector: the first sample's error
  is ~10 dB whatever the code. Asserted instead: the gain never passes its
  static target, and approaches it monotonically.
- **T5 timing**: attack is measured on the linear envelope with the clock
  started one sample before the step; release is held to the two-stage
  cascade, which is what the §5.5 detector *is* (attack 100 / release 10
  measures 110 ms, correctly).
- **T6 "no zero-crossing artefact"** is not measurable as worded. The output is
  linear in the blend, so the test reads the blend back from the audio and
  requires it continuous, monotone and in [0, 1].
- **T7 denormals** are checked deterministically (no subnormal in any state),
  not by timing blocks — the harness may not depend on the wall clock (§6).

## Numbers (2026-09-10, `measure_deq`)

Accuracy, worst |dB| vs the prototype, regions 20 Hz–0.25 Fs / 0.25–0.40 / 0.40–0.45:

| shape | 44.1 kHz | 48 kHz |
|---|---|---|
| bell, all Q | 1.33 / 0.93 / 1.22 | 0.85 / 0.69 / 1.24 |
| shelves, Q ≤ 2 | 0.30 / 0.51 / 0.87 | 0.31 / 0.49 / 0.85 |
| shelves, all Q | 1.24 / 4.14 / 6.22 | 1.41 / 5.20 / 5.44 |
| low cut (−60 dB floor) | 1.79 / 1.61 / 2.88 | 0.89 / 1.52 / 2.61 |
| high cut (−60 dB floor) | 0.23 / 0.63 / 1.25 | 0.23 / 0.62 / 1.14 |

The cookbook bilinear design is 3–30 dB out on the same grid.

CPU at 48 kHz, 128-sample blocks, stereo (one machine; informative only):
24 static bands 37 µs/block (1.4 % of real time), 24 dynamic 114 µs (4.3 %).
A redesign costs 135 ns (bell) to 300 ns (shelf) with the engine's prebuilt
`DesignGrid`; without it a shelf was 2.4 µs.

## Open

**Frosty's, not yet asked or not yet decided:**

- **Presets.** Only Init ships, confirmed for now. Preset character is his
  call, and it should come after the listening test: a preset tuned on one
  topology would need re-tuning by ear on the other.
- **AUTO by ear.** Static, like BMO EQ's. Whether a dynamic EQ's users expect
  it to follow the dynamics too is a listening question; the argument against
  (it would undo a de-esser) is in `dsp/AutoGain.h`.

**Serial, confirmed by ear 2026-09-12.** Chosen from
`spec/topology-options.md`, where it is the only option whose response is its
band curves added in dB, and the only one in which a low cut still cuts under
an overlapping boost — then held to 57 blind pairs across seven sources
(`testing-notes/deq-blind-2026-09-11.md`). Serial stands, **with no
user-facing switch**: spec C4 is revised, and `decisions.md` has the reasoning.

Parallel stays in `DspCore` for `measure_deq render`, which is how any of this
was measurable. The old note here said to delete it once serial was confirmed;
that is now a separate call, because deleting it ends the ability to A/B the
question again, and `--match` is built on it. Serial has one property worth
knowing: **dynamic bands commute only while still.** Static bands are
order-independent to −300 dB. A dynamic band's moving coefficients do not
commute with its neighbours', so reversing the band order differs by −86 to
−88 dB on test material, −37 to −51 with fast overlapping bands, and −20 in
the test's deliberately extreme 24-band case (the regression bound). Any
serial dynamic EQ has this; the detectors reading the dry input keep it that
small.

**Engineering, not blocked:**

- **Resonant shelves** (Q > 2) are up to 6 dB out near Nyquist, so they are
  not offered: a shelf runs at `kShelfMaxQ` (2) at most. Raising the cap means
  fixing the design first.
- **External sidechain** is not possible through `ModuleDsp::process`, which
  takes the audio channels only. Needs a core interface change.
- **Parameters arrive once per block** (`ModuleDsp::setParams`). The spec's
  "sample-accurate automation within a block" (T8) is not available; changes
  glide from the block boundary.
- **T4 zipper metric** (≤ −80 dB excess energy) is not implemented; the
  modulation tests assert stability, boundedness and gain-step overshoot.
- **T9 SIMD across bands** not attempted. The per-sample loop is scalar.
- **Bell precision at f0 < 3e-4 Fs**: 2e-6 dB off the knob gain from
  cancellation in the zero fit. Inaudible; fixable by computing 1 ± a1 + a2
  from the pole radius and angle rather than from a1, a2.
- The spec's **C6** forbids linear-phase FIR oversampling, and
  `core/dsp/Oversampler.h` is exactly that. Irrelevant until something in this
  module wants to oversample.
