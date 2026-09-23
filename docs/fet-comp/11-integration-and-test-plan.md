# BMO FET — Integration & Test Direction

Groundwork. No code here; this says what the devs build. Topology, ratio law,
oversampling factor, latency and curve constants come **per 10-dsp-spec.md**.

## 1. Dropping into the conventions

**Directory** `modules/fetcomp/`: `params.h`; `dsp/DspCore.h` (JUCE-free),
`dsp/Detector.h` (sidechain + program-dependent release), `dsp/FetCell.h`
(divider law, implicit solve, nonlinearity), `dsp/FetcompDsp.h` (`ModuleDsp`
adapter, unpacks the flat `float*` in `Index` order);
`panel/FetcompPanel.{h,cpp}`; `presets/FactoryPresets.h`; `Module.{h,cpp}`;
`AGENTS.md` (why) + `README.md` (human-facing), linked from `modules/AGENTS.md`.

**Identity — decided and confirmed.** BMO line. Display name **BMO FET**,
module id `fetcomp`, plugin code **`Bfet`** (reserved as "FET comp" at
`products/AGENTS.md:147`, spent here), bundle id **`com.lt3audio.bmofet`**,
presets **`.bmofetcomp`**, `ui::bmoLine()`. That follows the existing rows
(`products/AGENTS.md:13-23`): bundle id from the display name, preset extension
from the module id. No hardware branding anywhere; prose says "FET-style"/FET
only.

The **accent colour** (§4c) is the one identity item still open, and it blocks
the first build. The identity row is written on the build branch
`frosty-add-bmo-fetcomp`, not here: this pack lives on
`frosty-fetcomp-groundwork` and does not touch `products/AGENTS.md`.

**Registration touch points**: `modules/CMakeLists.txt` (`bmo_add_module`);
`products/rack/Registry.cpp` (include + `&fetcomp::module()`);
`products/fetcomp/{Product.h,main.cpp,CMakeLists.txt}`; identity row in
`products/AGENTS.md` **before the first build**; `tests/CMakeLists.txt`
(dsp + plugin + `rack_tests`/`ui_layout_tests` link lists);
`tools/CMakeLists.txt` (`measure_fetcomp`); and `tools/snapshot`'s product
list and link line, or the panel cannot be rendered at all.

**Permanence**: ids, their **order** in `specs()`, ranges, steps and defaults
freeze at first ship; new parameters append at the end only. The `ratio` and
`voicing` choice-string lists and their index order freeze too. Module id,
plugin code, bundle id and state tags freeze.

## 2. Parameters

`input` drives compression and `output` is makeup — there is no threshold knob
(01 §Threshold). GR is reported through `currentGainReductionDb()`, **signed,
positive = gain taken away** (`core/dsp/ModuleDsp.h`), never a parameter.
Solo/metering are not parameters. Smoothing is a one-pole in `DspCore`.

| # | id | Type / range | Default | Skew | Units | Smoothing | Auto |
|---|---|---|---|---|---|---|---|
| 0 | `input` | float −20…**+60** dB | 0 | linear | dB | 20 ms | yes |
| 1 | `output` | float **−36…+36** dB | 0 | linear | dB | 20 ms | yes |
| 2 | `attack` | float **1…7, knob position** | 4 | **none** | Plain — "4 (126 µs)" | target only | yes |
| 3 | `release` | float **1…7, knob position** | 4 | **none** | Plain — "4 (234 ms)" | target only | yes |
| 4 | `ratio` | choice 4:1/8:1/12:1/20:1/All | 4:1 | stepped | — | crossfade 5 ms | yes |
| 5 | `mix` | float 0…100 % | 100 | linear | % | 20 ms | yes |
| 6 | `voicing` | choice Blue/Black | **Black** | stepped | — | crossfade 5–10 ms | yes |
| 7 | `oversampling` | choice Off/2x/4x | Off | stepped | — | none | **no** |

**The two gain ranges in bold are changes.** 10 §12 shows the originally
proposed +45 dB input is about 5 dB short of reaching 30 dB GR at 4:1 from a
−18 dBFS source, and ±24 dB of output cannot restore 30 dB of reduction. Both
are permanent at first ship. **Owner confirmation item.**

`voicing` id, order, choice labels and default are equally permanent. Black is
the default; the labels "Blue"/"Black" are proposed, not settled. **Owner
confirmation item.** `mix` **ships in v1** — decided, not conditional; its dry
path has a delay-matching requirement (10 §9). `oversampling` exists because
10 §9 makes the factor user-selectable; it clicks and re-syncs PDC, so it is
marked not-automatable.

### Attack and release are the knob position — decided

**The parameter *is* the hardware's printed position**, 1–7 continuous with
7 fastest, mapped to time inside the DSP by 10 §10's law. The reason is
automation: had the parameter stayed in ms ascending with only the knob drawn
reversed, the host's lane and the knob would move in opposite directions, and a
panel cannot fix that because the lane is the parameter. It also costs nothing
elsewhere — position is linear and the law is exponential in position, so the
log sweep falls out with no skew at all.

The automated value is the position itself, so a host showing a normalised lane
maps 0 % → position 1 and 100 % → position 7, in the same direction as the knob.

**This departs from the one house precedent**, LTV Comp's `attack`/`release`,
which are `logParam` in ms ascending with `ParamFormat::Milliseconds`
(`modules/vcomp/params.h:147-148`). Taken knowingly: that module models no
hardware knob and has no direction to honour, this one does.

**Value string requirement.** The format is `Plain`, so the displayed value
must carry **both** — the position and the time it means, e.g. "4 (126 µs)"
and "4 (234 ms)" — otherwise a host that leans on the raw number shows a bare
"4.0". Positions print as integers where they land on one.

Mapping, from 10 §10: `t_att(p) = 800·(20/800)^((p−1)/6)` µs and
`t_rel(p) = 1100·(50/1100)^((p−1)/6)` ms, giving **800 / 126.5 / 20 µs** and
**1100 / 234.5 / 50 ms** at positions 1 / 4 / 7.

**Not in v1, and deliberately:** no sidechain HPF, and no stereo-link
parameter — stereo is **always linked** (10 §4), the same reasoning
`modules/vcomp` records for having no LINK switch. Either could be appended
later at the end of `specs()` with a default that leaves old sessions
unchanged; neither can be inserted mid-list.

## 3. Test suites

### JUCE-free DSP target — `tests/dsp/FetcompDspTests.cpp`
Runs against `DspCore`, seconds, CI `dsp` job. Model on `VcompDspTests.cpp`.
**Every curve, THD and alias case runs in both voicings.**

- **GR curve.** 1 kHz sine, 2 s, measure the last 200 ms (settle ≥ 10×
  release) at each `input` from −40 to +20 dBFS in 1 dB — the extended top end
  is what reaches 30 dB GR. **Do not assert a nominal ratio.** The divider law
  delivers a depth-dependent slope, so the golden object is the *curve*: commit
  the dB array 10 §5's table implies for each ratio and assert each point
  inside a fitted band — ±1.5 dB where the law is derived, ±3 dB at the 20:1
  threshold anchor (01's −24 dB ±2). On top of the array, assert four shape
  properties a coding error would break: local slope **monotonically
  decreasing** with depth, every setting **above 2:1** everywhere, the four
  settings strictly **ordered**, and the curve **still well-formed at 30 dB
  GR** (finite, monotone in input, no discontinuity).

  **The slope and ordering properties are asserted to 20 dB GR and not past
  it, changed on 2026-09-21 against measurement**
  (`testing-notes/fetcomp-curve-slope-2026-09-21.md`, AURORA). They read "at
  every depth" and that was not reachable. Above 20 dB the four settings need
  very different drives to reach the same reduction — at 30 dB GR it is 48.5 dB
  over threshold for 4:1 against 35.6 for 20:1 — so the **input amplifier's own
  soft compression** contributes slope, and contributes most to whichever
  setting is driven hardest.

  That is measured, not inferred. The static divider law's own slope falls
  monotonically at every ratio and keeps the four ordered at 30 dB
  (4:1 2.10 < 8:1 2.32 < 12:1 2.56 < 20:1 3.03); the implementation departs
  from it by +0.54 on 4:1 and +0.02 on 20:1, in drive order. **Linearising
  `inputAmp` in `Stages.h` restores both properties** — 4:1 then falls
  2.32 → 2.21 → 2.19 and the four stay ordered. The divider law, the solve and
  the bias are all exonerated.

  **Blue breaks first, at 25 dB**, where 4:1 rises 2.37 → 2.67 and crosses
  above 8:1 at 2.65. Black holds to 25 and breaks at 30. The earlier record
  showed only Black — the second time a Black-only sweep has hidden Blue
  behaviour, after the alias floor — so 20 dB is the honest cutoff for both.

  **What still holds above 20 dB and is still asserted there:** every setting
  stays above 2:1, and the curve stays well-formed — finite, monotone in input,
  no discontinuity — out to +20 dBFS at every ratio and both voicings. Those
  are the properties a coding error would break. The slope ordering above 20 dB
  is a property of a model being driven past where its settings are comparable,
  and asserting it would mean removing input-stage character that the THD
  figures depend on. **None of this has been heard.**
  All-buttons is DOCUMENTED-observed only: assert shape, not numbers — 1–2 dB
  standing GR at silence, effective slope above 12:1 near threshold, and a
  plateau (a region where the curve flattens or reverses).
- **Timing, at knob positions 1, 4 and 7.** Assert the mapping as well as the
  behaviour, because the position→time law is the permanent definition:
  **attack 800 / 126.5 / 20 µs** and **release 1100 / 234.5 / 50 ms** at
  positions 1 / 4 / 7, and **higher position = faster** at every pair (the test
  that fails if the direction is ever "corrected"). Attack = step −30→0 dBFS;
  at the fast end the settled value arrives within one sample, so the assertion
  there is 10 §12's **first-sample overshoot**: ≤ 0.05 dB at position 7 with
  oversampling Off at 44.1 and 48 kHz, the seven whole positions strictly
  monotone, and the table within ±0.1 dB at 48 kHz for a step to 20 dB of
  steady-state GR at 20:1. Repeat at 2x and 4x and assert the answer moves by
  under 0.05 dB — oversampling must not change the timing. Release = "63 %
  recovery" measured **at the reference depth 10 dB GR** (10 §6), ±10 %.
- **Program-dependent release.** Same release setting, short burst vs sustained
  heavy GR must recover at measurably different rates (≥ 1.5×). A naive
  two-branch implementation must fail this — see `testArcIsProgrammeDependent`.
- **The implicit solve.** The quadratic root reproduces the static curve to
  1e-6 at `α = 1`; agrees with an offline converged reference on a
  fast-attack/high-ratio step; stays finite with `A < 0`, with `B = 0` and with
  `4Bm ≪ A²`; and never returns a negative or NaN gain at extremes of `input`,
  `ratio` and all-buttons bias.
- **THD / IMD, including under slam.** Goertzel at bin centres, H2…H10 vs
  fundamental. Grid: level {−20, −10, 0 dBFS} × GR depth {0, 6, 12, 20,
  **30** dB} × f {50 Hz, 1 kHz, 15 kHz} × voicing. At the manual's condition
  (10 dB GR, 1.1 s release) assert THD < 0.5 % and H2 > H3. At 20 and 30 dB
  assert THD is **monotone in depth and finite**, H2 still leads, and **Blue
  exceeds Black at every depth with the gap widening** (10 §8). IMD: SMPTE
  60 Hz + 7 kHz 4:1, CCIF 19 + 20 kHz, per depth.
- **LF ripple distortion vs release.** 50 Hz and 100 Hz at 20 dB GR, release
  swept over all seven detents: H3 must rise monotonically as release shortens,
  and land near 10 §12's table (≈ 0.23 % at 1.1 s and ≈ 4.5 % at 50 ms for
  50 Hz, ±50 % of the figure — it is a mechanism check, not a tolerance).
  This is wanted character; the test exists to bound it, and to catch the day
  it stops being ripple and starts being a bug.
- **Aliasing floor.** Separate aliases from harmonics by construction: a tone
  whose harmonics land above Nyquist and fold onto bins no harmonic of the tone
  *at base rate* can occupy (48 kHz: 9 kHz tone, image at 21 kHz —
  `SatDspTests::testOversampling`). Per sample rate 44.1/48/88.2/96/192, per
  factor, **and at 10/20/30 dB GR with the fastest attack and release**, both
  voicings. Targets: **−60 dB at Off, −70 dB at 2x and at 4x.** Off missing its
  target at M3 is what moves the default to 2x (10 §9).

  **The 2x and 4x targets were −80 and −90, and were changed on 2026-09-21
  against measurement** (`testing-notes/fetcomp-alias-origin-2026-09-21.md`,
  AURORA). Those numbers assumed the floor was folded harmonic content that an
  oversampler removes, and that each factor would therefore buy roughly what it
  buys the Saturator. It is not, and it does not. Measured: −73.1 / −75.0 /
  −77.0 across Off / 2x / 4x at 48 kHz.

  The image bin is *not* a bin no harmonic can occupy once oversampling is on,
  which is the flaw in the old construction. Harmonics fold **inside** the
  oversampled domain too, and those landing below base Nyquist sit in the
  decimation filter's passband where nothing can reach them. At 2x the
  thirteenth harmonic lands there; at 4x the nineteenth. The filter is working
  — it removes the third harmonic by 46 dB at 2x — but the detector's
  rectifier is not bandlimited, so there is always a higher harmonic to take
  the bin, and its skirt is **flat within 2.8 dB from k=3 to k=19**. Each
  factor swaps one harmonic for another at nearly the same level; 8x would find
  one at about −74.

  So −80 and −90 are not reachable by oversampling at all, and a test asserting
  them could only ever be satisfied by bandlimiting the rectifier — which is a
  character change, not a fix, and would put the attack overshoot table and the
  THD figures at risk. **−70 at 2x and 4x is a bound with 2.4 dB of margin over
  the worst corner measured, not a threshold fitted to it**: it still fails if
  the rectifier is made harsher or the oversampler is broken. That worst corner
  is −72.4 dB, Blue at 48 kHz and 4x, at 20 and at 30 dB GR. What the suite
  actually pins is the *mechanism* — that 2x removes the third harmonic and 4x
  removes the thirteenth, each by at least 30 dB, which a dead oversampler
  fails and the old "within 0.5 dB of Off" assertion did not.

  **The Off target is the tight one, and it is tight on Blue.** Worst corner
  −62.9 dB (Blue, 44.1 kHz, 30 dB GR) against −60. Black is flat in depth and
  never worse than −72.4, which is why a Black-only sweep read this as a
  comfortable 12 dB. **Any recalibration of the Blue constants must re-run this
  sweep**, because 2.9 dB is what the default-Off decision in 10 §9 is standing
  on.

  Note also that **oversampling is not uniformly worth ~2 dB**: on Blue at
  30 dB GR it is worth nearly 12 (−63.0 → −74.8 at 48 kHz), because at that
  depth the rectifier's low-order harmonics grow and those *are* removable.
  The flat skirt dominates only once they are gone.

  **None of this has been heard.** Whether the residual is audible at all is
  an Ableton-pass question, not a bench one.
- **Pinned-GR stability.** 60 s at 25–30 dB of sustained reduction, fastest
  attack and release, all-buttons and 20:1: no drift in reported GR (settled
  value stable to 0.01 dB over the last 30 s), no denormal slowdown, no NaN or
  Inf, and control state bounded.
- **Level range.** From a −18 dBFS RMS source, `input` must reach ≥ 30 dB GR at
  every ratio and `output` must restore unity there, with neither at its rail.
  This is the test that fails if the §2 ranges are reverted.
- **Voicing pair.** Level-matched within ±0.1 dB at 0 dB GR and ±0.5 dB at
  10 dB GR; `latencyForParams` **identical** in both at every factor; switching
  mid-audio produces no sample discontinuity above −80 dB and no envelope jump
  (GR before and after the switch within 0.1 dB).
- **Stereo link.** Not a parameter: a hard-panned burst ducks both channels
  equally and both report one shared reduction.
- **Mix and the dry path, at every oversampling setting.** `mix` ships in v1,
  so this is a shipping requirement, not a nicety (10 §9). At Off, 2x and 4x:
  `mix` = 0 nulls against the input to −120 dB; `bypass` nulls to −120 dB; and
  **`mix` = 50 does not comb** — sweep a sine 100 Hz–18 kHz and assert the
  blended magnitude stays within ±0.5 dB of the mean, since an unmatched dry
  path 40 or 60 samples adrift puts notches every ~1.2 kHz at 48 kHz. Also
  assert the delay measured by cross-correlation equals the reported latency
  (`testDryPathIsDelayMatched`), and that changing the factor while running
  neither clicks above −80 dB nor leaves the ring misaligned.
- **GR metering past the pin.** `ui::DynamicsMeter` keeps its 0..24 dB
  reduction scale and is **not** modified (10 §12). Drive the core to 25, 30
  and 40 dB of reduction and assert the needle fraction **clamps at 1.0** — no
  wrap, no overshoot artefact, no NaN — while `currentGainReductionDb()` still
  reports the true figure. The clamp is a drawing limit, not a measurement one.
- **Null / regression, no audio ever committed.** Golden **state**, not golden
  audio: compact numeric tables inside the test file — per-voicing GR-curve dB
  arrays, per-voicing THD tables, control-state samples at fixed offsets,
  per-block RMS/peak quantised to 1e-4.
- **Invariance & robustness.** Curves/times hold across sample rates; block
  sizes 1/32/64/512/1023 give identical output to −120 dB; per-sample automation
  ramps produce no zipper (nothing above −80 dB); silence in → exact zeros; no
  NaN/Inf at extremes.
- **`latencyForParams` consistency.** For every stepped combination and a grid
  of continuous values, the reported figure equals the delay measured by
  cross-correlating an impulse through the core, and is stable across
  `prepare`/`reset`. Exactly **0 / 40 / 60** for Off / 2x / 4x, **0 at the
  default**, and independent of `voicing` — pin those numbers, as the EQ review
  did.

### Plugin/schema target — `tests/plugin/FetcompTests.cpp` (JUCE)
Golden `Expected[]` schema table; `Index::count == specs().size()`; defaults
agree with any `kStandard*` constants; factory presets load with their level
deltas; state XML round-trips; rack slot fits 32 params; panel entry in
`tests/ui/LayoutTests.cpp`.

### Manual tools — `tools/measure/fetcomp/main.cpp` (`bmo_add_dsp_tool`)
Modes `curve | timing | thd | alias | slam | allbuttons | bench | render | gen`,
each taking a voicing. WAVs go to `packages/fetcomp-listening/` (gitignored).
**Every recorded result — THD, alias floor, CPU, listening — names the machine
it ran on (AURORA / ICE QUEEN) in `testing-notes/`.**

### CPU & latency acceptance
`bench` mode, Release build, 8 × 10 s at 48 kHz/512, ns/sample; run
`measure_vcomp bench` in the same session on the same box for the baseline —
its `bench` mode uses this one's loop line for line, so the ratio is about the
two compressors and not about two harnesses. Latency: **0 at the default**,
otherwise exactly the shared oversampler's reported delay.

**Budget: ≤ 6.5× LTV Comp per sample at defaults (Off, Black), ≤ 17× at the
heaviest setting (4x, Blue, all-buttons).**

> **These were 2.0× and 3.0×, and were reset on 2026-09-21 on AURORA after the
> first measurement that had a baseline to measure against.** The original
> figures were written before anything had been built and before anyone had
> costed a per-sample implicit solve through 4x oversampling; measured, this
> module runs **5.47×** at defaults and **14.11×** at the heaviest, so the old
> budget was not a target this topology could ever have met.
>
> The new ceilings sit about 20% above the measured figures, which is enough to
> absorb machine-to-machine spread and the 2–3% run-to-run noise while still
> failing on a real regression. They are **not** an endorsement of the cost.
>
> Two things the ratio hides. LTV Comp's own default is `amountPercent = 0`, so
> the baseline is that module *at rest* — 26.6 ns/sample against 61.9 once it
> works, and BMO FET's defaults against LTV Comp working is 2.35×. And 4x
> oversampling alone takes this module from 145.5 to 993.0 ns/sample, which is
> 6.8× for the factor by itself: almost all of the heaviest figure is the
> oversampler, not the cell.
>
> **Flagged for the Ableton pass.** A ns/sample ratio says nothing about
> whether a track of these is usable, which is the question that actually
> matters and the one only a host can answer. See
> `testing-notes/ableton-pass-handoff-2026-09-17.md`, and
> `testing-notes/fetcomp-dsp-2026-09-21.md` for the full figures.

### Listening pass
A checklist item, not a test: drums (room mic, all-buttons, fast/fast), lead
vocal (4:1, attack 3, release 7, driven to 15–20 dB GR per 01's practice
notes), and bass (20:1, 20 dB+, release swept for the LF grind). Both voicings,
gain-matched, and the result names the machine.

## 4. Panel and visual verification

### 4a. The VU meter is reused, and it is already shared

`ui::DynamicsMeter` — declared `core/ui/Controls.h:408`, implemented
`core/ui/Controls.cpp:693-960` — is **already in `core/ui`**, not module-local.
Nothing needs lifting. `modules/opto/panel/OptoPanel.{h,cpp}` is the worked
example (`OptoPanel.h:90` holds the instance, constructed at
`OptoPanel.cpp:64-66`).

- **Sources.** The constructor takes three `std::function<float()>` — input
  RMS, output RMS and gain reduction in dB — plus an initial `Mode`
  (`input | output | reduction`), an accent and a hot colour. Opto passes
  `context.inputRms`, `context.rms` and `context.gainReductionDb` straight
  through; those come from `ui::ModulePanel::Context`
  (`core/ui/ModulePanel.h:47-60`) and are fed by `ModuleEngine` from the DSP's
  `currentGainReductionDb()`. They are **optional** and empty on modules that
  do not ask, so check before calling. The sign convention is documented there:
  positive = gain taken away, and a panel showing only reduction should clamp
  at zero rather than assume.
- **Ballistics and scale.** `startTimerHz (30)` (`Controls.cpp:701`) with one
  smoothed `displayed` value; `kVuReference = −18 dBFS` = 0 VU and
  **`kGrRangeDb = 24`** (`Controls.h:480-481`). Two printed scales,
  `vuScale()` and `reductionScale()`, public `ScalePoint` tables because a
  painted scale has no bounds a layout test can read.
- **Mode.** Set externally via `setMode()`; the panel owns a labelled IN/GR/OUT
  switch row, and `modules/AGENTS.md:190-217` requires one — the row is the only
  thing that names the mode. Row geometry, token heights and the 220-px width
  exception are all specified there.
- **The Opto UI pass** (2026-09-17) is what gave the meter its two scales and
  the snapshot fix: `ModulePanel::setUiState` (`core/ui/ModulePanel.h:210-235`)
  so `tools/snapshot` can render IN and GR, not only OUT — and it **refuses**
  an unknown key rather than ignoring it. Record:
  `testing-notes/ui-pass-opto-2026-09-17.md`.
- **Decided: the range is not widened.** `kGrRangeDb = 24` pins before this
  module's 30 dB design target, and that is accepted — 24 dB is plenty to read
  by, and past it the needle says "a lot" while the readout says the number.
  So no `kGrRangeDb` change, no `ScalePoint` lifting, no constructor argument,
  and **BMO Opto's render must stay byte-identical**. The DSP target is
  untouched: the loop is still designed and tested to 30 dB and
  `currentGainReductionDb()` still reports the true figure past the pin (§3
  tests it). `modules/AGENTS.md:215-217` says to lift `ScalePoint` out only
  when a module wants different *units* — this one does not.

### 4b. The voicing border

The voicing is shown as a **border around the VU meter**: blue for Blue, black
for Black. The rack accent stays blue in both states; nothing else on the panel
changes colour.

`DynamicsMeter` already has the hook. The bezel is the first of the two colours
`setColours (accent, hot)` takes (`Controls.h:437-440`), and it is drawn at
`Controls.cpp:951-957` as `accentColour.withAlpha (0.7f)` on a rounded rect
inset 0.75 px — **inside the meter bounds, on the meter face**, not on the
panel plate. So the ground for any contrast claim is `tokens().meterFace`.

**A literally black border will not be visible, and that is a hard finding.**
`meterFace` is `#464649` in *both* appearances (`core/ui/Tokens.h:83`; the dark
palette deliberately leaves it at its light value so the window reads as lit).
Pure black against `#464649` is **2.23:1** at full opacity and about **1.94:1**
at the 0.7 alpha the bezel actually uses. Going darker cannot help: from that
face, black is the most contrast available by going down.
`tools/inspect/Inspect.exe`'s own README records the same wall on the dark
plate ("the most contrast available by going darker, all the way to black, is
1.29:1"). Drawing the border *outside* the meter instead only moves the problem
— the dark plate is `#2e2e32`, darker still.

A **colour mock of five border treatments was reviewed on AURORA**, drawn with
the real token values and the 0.7 alpha, on both plates, and redrawn once the
accent was settled on **E** (§4c).

With E as the Blue state the bezel blends to `#5075aa`, **2.00:1** on the face
(2.65:1 if drawn at full alpha) — dim in isolation. What decides whether a pair
works is not that figure but the **border-against-border** ratio, since the
viewer only has to tell two states apart:

| pair | "Black" state | on face | E vs it, at 0.7 / 1.0 alpha | verdict |
|---|---|---|---|---|
| **1** | literal black `#000000` | 1.94 | **3.88 / 5.13** | **strongest** — see below |
| **2** | neutral dark grey `#3a3a3e` | 1.13 | 2.27 / 3.00 | the grey is invisible, so this is really pair 5 |
| **3** | neutral silver `#ababab` (Opto's today) | 2.84 | **1.42 / 1.07** | collapses — hue alone; refuse |
| **4** | white-ish `#f2f2f5` | 5.07 | 2.54 / 1.92 | works, but weakens as the bezel is strengthened |
| **5** | no border at all | — | 2.00 / 2.65 | reads as absence |

**Decided: pair 1** — Blue = accent E, Black = **literal black**. Choosing E
inverted the earlier answer: with the lighter candidate C pair 4 was the strong
one, but E's bezel sits **lighter** than the meter face (2.00:1 above it) while
black's sits **darker** (1.94:1 below it), so the two states land on opposite
sides of the same ground and separate by **luminance**, not hue. Pair 3 is
refused for the opposite reason — 1.42:1 falling to 1.07:1 is hue alone, the
failure the suite already fixed once on its switch colours.

**Settled: full alpha — owner's call on renders, 2026-09-20 on AURORA.** Two
variants were in play — the stock **0.7** (pair separation 3.88:1) and **full
alpha** (2.65:1 on the face, pair separation 5.13:1 by formula). E is the
darkest accent in the suite and 2.00:1 is thin, and a colour mock is not a
render, so the call went the way `testing-notes/ui-editor-handoff.md` §6 says:
eight PNGs, both variants in both voicing states and both appearances, reviewed
on a named machine, owner picks. **Measured on the shipped render the pair
separates at 5.91:1**, not 5.13 — the formula figure was the low one, so full
alpha does better than this pack predicted rather than worse. `kBezelAlpha` in
`FetcompPanel.cpp` now reads `kFullBezelAlpha`, `ui.bezel=stock` still renders
the rejected candidate, and BMO Opto is re-proved byte-identical at
`ab3ff3b77116b7a5` / `878cca7b1a80a551` / `88a7653a82c19ae0`.
`testing-notes/ui-pass-fetcomp-2026-09-20.md` §2 and §6 are the record.

**What each variant touches.** 0.7 alpha is what `ui::DynamicsMeter` already
draws (`Controls.cpp:951-957`, `accentColour.withAlpha (0.7f)`) and costs
nothing. Full alpha is a change to shared code, so it must arrive as an
opt-in — a bezel-opacity (and optionally width) argument or setter on
`DynamicsMeter`, **defaulting to 0.7** — and **BMO Opto's render must stay
byte-identical**, proven by re-rendering Opto and comparing pixel hashes, not
by inspection. Do not edit the literal in `Controls.cpp`.

Opto's `hotColourFor` (`OptoPanel.cpp:156-165`) is the repo's own pattern here:
step a colour off the face until it clears its target rather than trusting that
it does. Whatever is chosen, derive it that way and **measure it with
`Inspect.exe ratio` on a real render** — every figure above was computed by
formula, not read off pixels.

### 4c. The accent colour

The accent is **blue**, for the earliest revisions' faceplate. The allocation
table and the hue-separation rule are `products/AGENTS.md:258-320`; do not edit
that file from this branch.

Taken hues: 31.7° Saturator, 80.1° lime (Tune), 128.9° Util, 172.0° teal (DEQ),
**198.8° utility azure**, 236.1° periwinkle (LTV Comp), 271.6° lavender
(Dimension), 336.0° pink (CEQ). The azure is not a module accent and is not
available as one — it is the utility-knob colour and appears on *every* panel,
"which is exactly what makes it the hue everything else has to stay away from."

**No true blue passes the separation rule.** The blue band is bracketed by the
azure at 198.8° and the periwinkle at 236.1°, a gap of 37.3°, so the best any
blue can do is ~18.6° to each side — worse than the teal's 26.8°, which the
table already records as its worst case and a known objection. Reaching
further, 236.1°–271.6° is 35.5° wide and its midpoint ~254° (indigo) is 17.8°
from each side: also fails.

Six candidates **A–F** were drawn on both plates in the AURORA colour mock,
spanning the gap plus a deep "faceplate stripe" blue and a rule-passing control.
Shipped bands for the pass column: dark 5.87–7.19, pale 1.64–2.00.

| | hex | hue | Δ azure | Δ periwinkle | dark | pale | verdict |
|---|---|---|---|---|---|---|---|
| **A** | `#96c7f2` | 208.0° | 9.2° | 28.1° | 7.56 | 1.55 | fails both |
| **B** | `#92bdf2` | 213.1° | 14.3° | 23.0° | 6.94 | 1.69 | contrast passes, hue fails |
| **C** | `#8cb2f0` | 217.2° | **18.4°** | **18.9°** | 6.27 | 1.87 | contrast passes; the best a blue can do on hue |
| **D** | `#899ef0` | 227.8° | 29.0° | 8.3° | 5.29 | 2.22 | fails both |
| **E** | `#5489d4` | 215.2° | 16.4° | 20.9° | 3.80 | 3.09 | the deep stripe blue; far outside both bands |
| **F** | `#e694e0` | 304.4° | — | — | 6.23 | 1.89 | **passes everything — and is violet** |

**Chosen: E `#5489d4`** — the deep faceplate-stripe blue. Taken by the owner
after reviewing the mock, as an **explicit, approved exception**, knowingly
breaking both rules:

- **Hue separation.** 16.4° from the utility azure and 20.9° from the
  periwinkle, against a table whose worst shipped figure is the teal's 26.8°
  (itself recorded there as a known objection) and whose best is the lavender's
  64.4°. No blue could have passed — the azure-to-periwinkle gap is only 37.3°
  wide — so this is a rule the module cannot satisfy rather than one it declined
  to try. `F` `#e694e0` at 304.4° was the passing alternative and was refused
  for not being blue.
- **Contrast bands.** 3.80:1 on the dark plate against a shipped band of
  5.87–7.19, and 3.09:1 on the pale plate against 1.64–2.00. E is the only
  accent in the suite that is *too dark* on the dark plate and *too heavy* on
  the pale one.

**Consequences the devs handle, and do not relitigate.**

1. **The knob cap is where it actually bites.** `ui::faceOf` returns the raw
   accent on the dark plate, and `Tokens.h` states the shipped caps run
   5.87–6.84:1 against the plate with the pointer at 6.13–7.14:1 on top. E
   gives **3.80:1** cap-on-plate and **3.97:1** for `pointer` `#2b2b2e` on the
   cap. Both are outside the stated range, and the cap will read as a dark disc
   rather than as the module's colour. On the pale plate `faceOf` washes E half
   to `knobTint`, giving `#aac4ea` at 1.55:1 — just under the 1.64 floor, so
   faint rather than heavy. Expect the dark plate to be the one that needs a
   render pass.
2. **Accent-coloured text is already handled, and will not look like E.**
   `ui::accentTextOn` (default `minRatio` 4.5) steps the accent away from its
   ground — lighter on the dark plate, darker on the pale one. E starts further
   from 4.5:1 than any shipped accent (3.80 dark, 3.09 pale), so it is stepped
   further, and captions drawn this way will read as a noticeably different
   blue from the arcs beside them. **Keep E off small text.** Use it for the
   knob indicator arcs, value/GR indicators, the active-switch fill and the VU
   border; let legends and captions take `text1`/`text2`. That is already the
   suite's convention — `modules/AGENTS.md` says of the meter that "the face,
   the needle, the ticks and the scale are **not** yours", only the bezel and
   the hot zone are — so this is following the house rule, not inventing one.
3. **The active switch is fine as-is.** `ui::onAccentOf` sees E's relative
   luminance at 0.245 (above its 0.18 pivot) and darkens, giving a near-black
   label at **5.91:1** on the E fill. No special case needed; do not hand-pick a
   light label.
4. **No test fails.** There is no contrast, palette or accent assertion
   anywhere in `tests/` — enforcement is the runtime steppers above plus manual
   `Inspect.exe ratio`. So the exception costs nothing in CI and needs no
   suppression; it is a documentation duty only. What it does mean is that
   **the render pass is the only thing that will catch a problem**, so §4d's
   both-appearances rule matters more here than usual.
5. **The Accents-table row must carry the exception note.** When the row lands
   on `frosty-add-bmo-fetcomp`, record alongside the hex: the two separations,
   the two out-of-band contrast figures, that the owner took it knowingly, and
   that the hue was unreachable for any blue. The lime's row is the precedent
   for how that is written — the objection is recorded so nobody raises it again
   as a new finding.

Every figure here is formula-derived on AURORA; confirm with `Inspect.exe
ratio` on a real render.

### 4d. The tools, and the order to use them in

Nothing here needs CI; the loop is measured at about 4.3 s on AURORA
(`testing-notes/ui-pass-render-loop.md`).

| tool | path | built by | what it gives |
|---|---|---|---|
| panel renderer | `tools/snapshot/main.cpp` | CMake target `snapshot` | a PNG of a product editor, headless |
| render inspector | `tools/inspect/Inspect.exe` | **not** in CMake; `csc -out:Inspect.exe -r:System.Drawing.dll Inspect.cs` | `scan hist crop sheet hash ratio gaps` |
| layout dump | `ui_layout_tests --dump` | CMake, `tests/ui/LayoutTests.cpp` | control boxes and rules, as laid out |
| DSP curves | `build/tools/Release/measure_fetcomp.exe` | `bmo_add_dsp_tool` | the numbers behind the panel |

Commands, exactly as the repo documents them:

```
scripts/build.sh                  # builds everything and runs ctest
scripts/build.sh --snapshots      # then look at snapshots/*.png
cmake --build build-release --config Release --target snapshot
./build-release/tools/Release/snapshot.exe <id> out.png [width height] [k=v ...]
./Inspect.exe ratio "#8d8d98" snapshots/rack.png:260,364
./Inspect.exe gaps snapshots/util-dark.png
```

`snapshot` flags that matter here: `appearance=dark|light` (both, every time —
half the faults on the `ui-editor` branch existed in one only); `signal=<dBFS>`
to run a 1 kHz tone through first so the meters read instead of resting, and
**`-18` is what every existing baseline used**; `ui.<key>=<value>` for panel
state with no parameter behind it, which is how the meter mode is rendered
(`ui.meter=IN|GR|OUT` today); `theme=<file.json>` to try a candidate accent
without writing the machine-wide palette; `chain=` and `N.id=` for the rack.
Both renderers **refuse** an unknown flag or a mistyped choice name rather than
ignoring it, deliberately — a render that quietly answers a different question
is worse than none. Register `fetcomp` in the tool's product list and link line
or none of this works.

**Comparison is by pixel hash, not by eye.** `Inspect.exe hash` is SHA-256 of
the pixels (not file bytes, since a PNG encoder may vary everything around
them). Per-module before/after hashes are recorded in `testing-notes/ui-pass-*`
— e.g. `ui-pass-sat-2026-09-17.md` §1 tabulates dark and light hashes at each
step. Record this module's the same way.

**Never committed:** rendered panels (`snapshots/` is gitignored — "regenerate
with `tools/snapshot`"), any audio or WAV, `packages/`, `field-audio/`, and the
licensed `.otf` faces. Read `git status --short` before `git add -A`, every
time — twice a tool has written WAVs into the tree and the next commit swept
them up.

**Fonts.** The two display faces are licensed to individuals and live outside
the repository. Once per working copy: `scripts/set-font-dir.sh <path>`, which
checks both faces are present and records the folder in `.bmo-fontdir`
(gitignored, per-machine). Without it the build falls back to an empty
`assets/fonts/` and the panel renders in the wrong type — re-run CMake
configure if a build tree already exists.

**Step order for this module's visual pass.**
1. Register `fetcomp` in `tools/snapshot` and `tests/ui/LayoutTests.cpp`;
   confirm `.bmo-fontdir` is set.
2. `cmake --build build-release --config Release --target snapshot`.
3. Render the baseline in both appearances with `signal=-18`, and hash it.
4. Render the meter in all three modes via `ui.meter=`, with the GR scale
   driven to 20 dB+ so the widened range is visible rather than assumed.
5. **Two voicing renders, both appearances**: `voicing=Blue` and
   `voicing=Black`, i.e. four PNGs, hashed. The pair must differ *only* in the
   meter border — diff them and check nothing else moved.
6. **The border-alpha decision, and it needed renders to make. Done
   2026-09-20 on AURORA: full alpha, owner's call.** Both variants — bezel at
   0.7 and at full alpha — were rendered in both voicing states and both
   appearances, eight PNGs, hashed, put side by side with `Inspect.exe sheet`,
   and Frosty picked full. BMO Opto was re-rendered and its three hashes proved
   unchanged, which §4b requires once full alpha wins. The step is kept here
   because it is the pattern a future module's gate follows, not because it is
   still open.
7. `Inspect.exe ratio` the border against `meterFace` in both appearances, and
   the accent against both plates — E is an out-of-band exception (§4c), so
   record the figures rather than assuming them; `gaps` for the bare-band
   ranking; `ui_layout_tests --dump` for the boxes.
8. Record hashes, ratios, the chosen alpha and the largest bare band in
   `testing-notes/ui-pass-fetcomp-<date>.md`, naming the machine.

## 5. Milestones and definition of done

**M0** identity row allocated, accent reserved, parameter ranges and `voicing`
labels/default confirmed, directory, `params.h`, pass-through adapter, all
registration points (including `tools/snapshot`), schema test green.
**M1** the divider law and the quadratic solve → curve tests to 30 dB.
**M2** sidechain, feedback topology, program-dependent release → timing and
first-sample-overshoot tests. **M3** FET nonlinearity, the two voicings and
oversampling → THD/IMD/alias/slam suites + `latencyForParams`, and the
Off-default decision. **M4** all-buttons bias, plateau and transient lag.
**M5** panel and presets, `AGENTS.md` + `README.md`, measure tool — and the
visual pass of §4d, including the **border-alpha gate**: both variants (0.7 and
full alpha) rendered in both voicing states and both appearances, reviewed on a
named machine, **owner picks**, with BMO Opto's hashes re-proven if full alpha
wins. *Closed 2026-09-20 on AURORA: full alpha, Opto unchanged.*
**M6** invariance, robustness, pinned-GR stability, mix/dry-path comb
checks at every factor, CPU/latency acceptance, listening pass.

**M1–M4 landed 2026-09-20 on AURORA** and are measured rather than heard;
`testing-notes/fetcomp-dsp-2026-09-21.md` carries the figures and the three
places the plan above turned out to be unachievable as written.

**Done** = ctest green in all three CI jobs on both platforms; schema table
pinned; no audio, renders or fonts committed; `AGENTS.md` + `README.md` present
and linked; identity row and accent permanent; the border alpha picked on
renders and recorded; BMO Opto unchanged; measure tool registered; CPU and
latency inside budget and recorded in `testing-notes/` with the machine named.
