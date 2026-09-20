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

**Identity — decided.** BMO line. Display name **BMO FET**, module id
`fetcomp`, plugin code **`Bfet`**, which `products/AGENTS.md:147` already
reserves as "FET comp" and which this module now spends. Following the existing
rows (`products/AGENTS.md:13-23`) — bundle id from the display name, preset
extension from the module id — that gives bundle id `com.lt3audio.bmofet` and
presets `.bmofetcomp`, with `ui::bmoLine()`. No hardware branding anywhere;
prose says "1176-style"/FET only.

Two things remain blockers for the first build: the **accent colour** (§4), and
confirming the display name is "BMO FET" and not "BMO FET Comp" — the bundle id
derives from it and is permanent. The identity row is written on the build
branch `frosty-add-bmo-fetcomp`, not here: this pack lives on
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
| 2 | `attack` | log 0.02…0.8 ms | 0.2 | log | ms | target only | yes |
| 3 | `release` | log 50…1100 ms | 400 | log | ms | target only | yes |
| 4 | `ratio` | choice 4:1/8:1/12:1/20:1/All | 4:1 | stepped | — | crossfade 5 ms | yes |
| 5 | `mix` | float 0…100 % | 100 | linear | % | 20 ms | yes |
| 6 | `voicing` | choice Blue/Black | **Black** | stepped | — | crossfade 5–10 ms | yes |
| 7 | `oversampling` | choice Off/2x/4x | Off | stepped | — | none | **no** |

**The two ranges in bold are changes, and they are why.** 10 §12 shows the
originally proposed +45 dB input is about 5 dB short of reaching 30 dB GR at
4:1 from a −18 dBFS source, and ±24 dB of output cannot restore 30 dB of
reduction. Both are permanent at first ship. **Owner confirmation item.**

`voicing` id, order, choice labels and default are equally permanent. Black is
the default because the owner wants the sound to lean that way; the labels
"Blue"/"Black" are proposed, not settled. **Owner confirmation item.**

The panel may print the hardware's reversed 1–7 captions for attack and
release, but the **parameters stay in ms ascending** — captions are free, ids
are not. `oversampling` exists because 10 §9 makes the factor user-selectable;
it clicks and re-syncs PDC, so it is marked not-automatable.

**Not in v1, and deliberately:** no sidechain HPF, and no stereo-link
parameter — stereo is **always linked** (10 §4), the same reasoning
`modules/vcomp` records for having no LINK switch. Either could be appended
later at the end of `specs()` with a default that leaves old sessions
unchanged; neither can be inserted mid-list. `mix` is justified by rack use
rather than by the hardware and is the one extra still open (README).

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
  settings strictly **ordered** at every depth, and the curve **still
  well-formed at 30 dB GR** (finite, monotone in input, no discontinuity).
  All-buttons is DOCUMENTED-observed only: assert shape, not numbers — 1–2 dB
  standing GR at silence, effective slope above 12:1 near threshold, and a
  plateau (a region where the curve flattens or reverses).
- **Timing, and the slam step.** Attack = step −30→0 dBFS. At the fastest
  detents the settled value arrives within one sample, so the assertion is
  10 §12's **first-sample overshoot**: ≤ 0.05 dB at detent 7 with oversampling
  Off at 44.1 and 48 kHz, the seven detents strictly monotone, and the whole
  table within ±0.1 dB at 48 kHz for a step to 20 dB of steady-state GR at
  20:1. Repeat at 2x and 4x and assert the answer moves by under 0.05 dB —
  oversampling must not change the timing. Release = "63 % recovery" measured
  **at the reference depth 10 dB GR** (10 §6); endpoints 50 ms and 1100 ms,
  ±10 %, plus monotonicity.
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
  whose harmonics land above Nyquist and fold onto bins no harmonic can occupy
  (48 kHz: 9 kHz tone, image at 21 kHz — `SatDspTests::testOversampling`).
  Per sample rate 44.1/48/88.2/96/192, per factor, **and at 10/20/30 dB GR with
  the fastest attack and release**, both voicings. Targets: −60 dB at Off,
  −80 dB at 2x, −90 dB at 4x. Off missing its target at M3 is what moves the
  default to 2x (10 §9).
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
- **Null / regression, no audio ever committed.** Golden **state**, not golden
  audio: compact numeric tables inside the test file — per-voicing GR-curve dB
  arrays, per-voicing THD tables, control-state samples at fixed offsets,
  per-block RMS/peak quantised to 1e-4. Plus `bypass` nulls to −120 dB and
  `mix` = 0 nulls against a delay-matched dry path at every factor
  (`testDryPathIsDelayMatched`).
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
`bench` mode, Release build, 100 × 10 s at 48 kHz/512, ns/sample; run
`measure_ltvcomp`/`measure_opto` in the same session on the same box for the
baseline. Budget: ≤ 2.0× LTV Comp per sample at defaults (Off, Black), ≤ 3.0×
at the heaviest setting (4x, Blue, all-buttons). Latency: **0 at the default**,
otherwise exactly the shared oversampler's reported delay.

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
- **Needs widening.** `kGrRangeDb = 24` pins before this module's 30 dB design
  target (10 §12). `modules/AGENTS.md:215-217` names exactly this situation:
  "the scale is still Opto's … a module wanting different units is the point at
  which to lift `ScalePoint` out into the caller — not before." **This is that
  module.** Widening `kGrRangeDb` in place would rescale BMO Opto's meter too,
  so the change is to take the range (and ideally the scale table) as a
  constructor argument, defaulting to today's values, and leave Opto's render
  byte-identical. `ui_layout_tests` and Opto's snapshot hashes are what prove
  that.

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

Two ways out, an owner decision:
1. **Keep the semantics, drop the literal black.** Blue = the module's accent;
   "Black" = a light neutral, for which `tokens().neutral` `#ababab` is the
   precedent Opto's own bezel already uses. Measured here by the WCAG formula
   on AURORA against `#464649`: `#ababab` **4.10:1**, and a candidate blue
   `#8cb2f0` **4.36:1** — both in family with what ships, and both legible in
   either appearance. The switch then reads as blue vs grey, with the state
   named in text.
2. **Accept a black border that only works on the pale plate** and specify a
   substitute for the dark one. This breaks the repo's standing rule that a
   change measured in one appearance has not been checked.

Opto's `hotColourFor` (`OptoPanel.cpp:156-165`) is the repo's own pattern for
this: step a colour off the face until it clears 4.5:1 rather than trusting
that it does. Whatever is chosen, derive it that way and **measure it with
`Inspect.exe ratio` on a real render** — the figures above were computed by
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
azure at 198.8° and the periwinkle at 236.1°, a gap of 37.3°. The best blue
sits in the middle of it, ~217°, **18.4° from the azure and 18.9° from the
periwinkle** — worse than the teal's 26.8°, which the table already records as
its worst case and a known objection. Reaching further, 236.1°–271.6° is 35.5°
wide and its midpoint ~254° (indigo) is 17.8° from each side: also fails.

A concrete candidate, if blue is taken anyway as a deliberate exception the way
the lime was: **`#8cb2f0`**, hue 217.2°, **6.27:1** on `#2e2e32` and **1.88:1**
on `#efefef` — both inside the shipped bands (dark 5.87–7.19, pale 1.64–2.00).
Computed by the WCAG formula on AURORA; confirm with `Inspect.exe ratio`.

The nearest *passing* hue is **~304°** (violet-magenta), 32.2° clear on both
sides in the widest unclaimed gap — but it is not blue. Red near 4° has 27.9°
of hue headroom and should still be refused: it lands 2.3° from BMO Opto's
engaged-red state colour `#e0685a`.

So: blue is available only as an explicitly accepted exception, on the record,
with the numbers above. **Owner decision, and permanent.**

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
6. `Inspect.exe ratio` the border against `meterFace` in both appearances, and
   the accent against both plates; `gaps` for the bare-band ranking;
   `ui_layout_tests --dump` for the boxes.
7. Record hashes, ratios and the largest bare band in
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
**M5** panel (meter range widened, voicing border), presets, snapshots,
`AGENTS.md` + `README.md`, measure tool. **M6** invariance, robustness,
pinned-GR stability, CPU/latency acceptance, listening pass.

**Done** = ctest green in all three CI jobs on both platforms; schema table
pinned; no audio, renders or fonts committed; `AGENTS.md` + `README.md` present
and linked; identity row and accent permanent; measure tool registered; CPU and
latency inside budget and recorded in `testing-notes/` with the machine named.
