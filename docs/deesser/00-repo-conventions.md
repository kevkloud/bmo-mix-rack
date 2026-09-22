# Repo conventions for a new de-esser module — the delta

General module layout, `bmo::ModuleDsp`, ParamSpec/ParamSet, registration and
build/test conventions are already covered in
`docs/fet-comp/00-repo-conventions.md` — read that first, not repeated here.
Researched read-only; `modules/fetcomp`, in-flight `docs/fet-comp` edits, and
`build*/` were ignored per instructions.

## 1. Reusable DSP a de-esser needs

BMO DEQ (`modules/deq/`) is the closest existing engine — a de-esser is a
narrowband dynamic-EQ cut, and DEQ already is one:

- **Band filter**: `modules/deq/dsp/Svf.h` — a TPT state-variable filter
  (`SvfCoeffs`/`SvfTaps`/`SvfState`) that realises any stable biquad and
  glides coefficients sample-by-sample. Module-local to `deq`.
- **Filter design**: `modules/deq/dsp/Design.h`/`Design.cpp` (`DesignGrid`,
  not read in full here) turns shape/freq/Q into a `Biquad`, then
  `SvfCoeffs::fromBiquad` (`Svf.h:34-56`).
- **Detector/envelope**: `modules/deq/dsp/Dynamics.h:43-80`, `Detector` —
  decoupled peak/RMS envelope (Giannoulis/Massberg/Reiss), tau-convention
  attack (`onePoleCoeff`, line 15-19), reads a **band-limited copy of the dry
  input through its own sidechain filter**, never the band's own (moving)
  filter output (rationale in `modules/deq/dsp/DspCore.h:100-111`) — exactly
  the split-band-sidechain shape a de-esser needs.
- **Gain computer**: `modules/deq/dsp/Dynamics.h:102-126`, `GainComputer` —
  soft-knee offset toward a `rangeDb` ceiling, `Direction::above/below`.
  Module-local, DEQ-specific (uses `rangeDb`/direction, not ratio+threshold
  the way `core/dsp/GainComputer.h` does).
- **Dynamic gain applied to a filter**: `DspCore.h:206-227` (`Band` struct) —
  the detector's offset redesigns the band's target coefficients every
  `kControlInterval` (8 samples, `DspCore.h:25`), glided in between
  (`Glide`, lines 194-204); latency stays **0** always
  (`DspCore.h:131`, `latencySamples()`), since it's IIR, not a crossover/delay
  scheme.
- **Stereo/M-S**: `Placement::stereo/mid/side` + continuous `msAmount` blend
  (`DspCore.h:59-61,76-86`); one filter pair serves L/R and M/S since
  `H(L)=H(M)+H(S)` (comment, `DspCore.h:106-109`).
- **No shared crossover.** Grep found `Crossover.h` only in
  `modules/vcomp/dsp/Crossover.h` — module-local, per the conventions doc
  ("no shared compressor-detector library"). A de-esser more likely wants
  DEQ's single-band SVF approach than a multiband crossover.
- **Shared, in `core/dsp/`**: `AnalyserTap.h` (lock-free ring buffer for a
  panel display, used by DEQ's post-EQ tap, `DspCore.h:151-156`),
  `Oversampler.h` (half-band cascade, 1x/2x/4x/8x, used by saturating modules
  — no lookahead/delay line here, it's for antialiasing), `GainComputer.h`
  (generic ratio/knee reduction curve, feedforward vs feedback slope math,
  used by Opto/Vcomp — a different shape than DEQ's own), `Meter.h` (not
  inspected in depth), `ModuleDsp.h` (the base interface).
- **Reuse rule**: per `docs/fet-comp/00-repo-conventions.md` §2, anything in
  a module's own `dsp/` folder (DEQ's `Svf.h`, `Dynamics.h`, `Design.*`) is
  **module-local** — there is no precedent in this repo for one module
  `#include`-ing another's `dsp/` files. A de-esser needing DEQ's SVF/detector
  shape would copy/adapt the pattern into its own `modules/<id>/dsp/`, not
  reuse DEQ's files directly, matching how vcomp/opto each own their own
  detector rather than sharing one.

  **Settled 2026-09-21, and split rather than taken whole — Frosty's call.**
  Read at source, §2 bars a shared *compressor-detector* library: "each
  dynamics module owns its own `ReleaseStage`/`Smoother`". That is a rule
  about detectors, and BMO Defang's detector is duly its own
  (`modules/deesser/dsp/Detector.h`, adapted from `Dynamics.h`, already
  divergent — it is fed a power-summed level from both channels).

  Filter *design* is not a detector. It is arithmetic with one right answer,
  the same category as the `GainComputer` and `Oversampler` already shared in
  `core/dsp/`, and copying it would have meant two divergent copies of a
  least-squares zero fit, each needing the same correction twice. So
  `Biquad.h`, `Prototype.h`, `Design.{h,cpp}` and `Svf.h` **moved to
  `core/dsp/`** in namespace `bmo::dsp`, on the same second-caller trigger
  `core/ui/LevelBars.h` had used the day before. DEQ reaches them through
  `modules/deq/dsp/Filters.h`, so its own diff is four include lines, and its
  audio and its three renders were proved byte-identical either side of the
  move.

  One thing the move exposed rather than caused: a module's DSP is a static
  library built from its own sources and does **not** link `bmo_core`, so
  shared DSP with a compiled part needs its own library. That is `bmo_dsp`,
  and `core/CMakeLists.txt` says why.

## 2. Existing de-essing-like behaviour / listen / spectrum display

- No module currently does de-essing or offers a sidechain "listen/audition"
  toggle. What DEQ has is **solo-a-band**, `DspCore::setSolo`/`soloedBand`
  (`DspCore.h:133-149`) — momentary, panel-held, not a saved parameter — which
  auditions a band's *contribution* (`H(x)-x`), not its full output; the
  closest thing to an "audition what's being caught" control in the repo.
- **Spectrum/band display**: `modules/deq/panel/Analyser.h` — an FFT
  spectrum (4096-pt Hann, `kFftOrder=12`) drawn from the post-EQ
  `AnalyserTap`, built per-pixel-column on a log axis
  (`buildPath`, lines 173-240), five tint options, message-thread only, and
  it is emphatically **on whenever the panel is open, no switch**
  (`Analyser.h:72-82`). Paired with `modules/deq/panel/ResponseView.h/.cpp`
  for the curve itself (not read in full; same panel).

## 3. Identity facts

- `products/AGENTS.md:169` — `` `Bdyn` dynamics, `Bdes` de-esser, `Bovr` overdrive, ``
  — reserved, not built.
- Identity-row pattern (`products/AGENTS.md:13-24`): `| Product | Module id |
  Plugin code | Bundle id | Presets |`; bundle id is
  `com.lt3audio.bmo<lowercase-display-name>`; preset ext is `.bmo<moduleid>`.
  Allocate the row **before the first build** (`AGENTS.md:11`).
- Accents table (`products/AGENTS.md:293-304`), occupied hues: Saturator
  31.7°, Tune RT (lime, not in rack) 80.1°, Util 128.9°, DEQ 172.0°, utility
  azure (not an accent, but every panel shows it) 198.8°, FET 215.2° (being
  added, an explicit exception to the hue/contrast rules), LTV Comp
  periwinkle 236.1°, Dimension 271.6°, EQ 336.0°. The worst *accepted*
  separation on record is the teal's 26.8° from azure (`AGENTS.md:308,370`).
  Checking every gap against that bar, only two gaps clear it today:
  **~280°-330°** (magenta/red-violet; midpoint ~304°, which is where
  `#e694e0` at 304.4° was mocked and rejected only for "not being blue",
  `AGENTS.md:319-320` — the hue itself passes the rule) and **~350°-20°**
  wrapping through red (midpoint ~4°, ~28° from EQ's 336° and ~28° from
  Saturator's 31.7°). Every other gap (Sat-Tune, Tune-Util, Util-DEQ,
  DEQ-azure, azure-FET, FET-periwinkle, periwinkle-Dimension) is already
  tighter than 26.8° on at least one side once FET's 215.2° is counted.

## 4. Render/visual tools and test targets

Full detail is in `docs/fet-comp/11-integration-and-test-plan.md` — reference
it, not repeated here. Short list of what a new module must pass:

| tool/target | path |
|---|---|
| panel snapshot renderer | `tools/snapshot/main.cpp` (register the module + product) |
| render inspector | `tools/inspect/Inspect.exe` (`scan hist crop sheet hash ratio gaps`) |
| layout dump/test | `tests/ui/LayoutTests.cpp` |
| DSP measurement tool | `tools/measure/<id>/main.cpp` → `build/tools/Release/measure_<id>.exe` |
| DSP unit tests | `tests/dsp/<Id>DspTests.cpp` |
| plugin/schema golden test | `tests/plugin/<Id>Tests.cpp` |
| full build + ctest | `scripts/build.sh` (`--snapshots` for panel PNGs) |

**Blocking unknown: none.**
