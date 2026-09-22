# Repo conventions for a REVERB module — the delta only

General layout/registration/params/build-test harness: **PR #19 /
`docs/1176-comp/00-repo-conventions.md`** (via `git show
frosty-fetcomp-groundwork:...`) — not repeated. Render/visual tooling:
**`docs/1176-comp/11-integration-and-test-plan.md` §4** — reference only.

## 1. Reusable DSP

No shared delay-line/allpass/comb/diffuser exists in `core/dsp/` (it holds
only `ModuleDsp.h`, `GainComputer.h`, `Meter.h`, `Oversampler.h`,
`AnalyserTap.h`). Everything delay/allpass-shaped is **module-local**, per
the standing rule that reuse across modules is copy/adapt into the new
module's own `dsp/`, not `#include`-ing another module's file:

- **`modules/dim/dsp/DspCore.h`** — best source to crib from. `DetuneVoice`
  (61-139): crossfading fractional-delay pitch shifter, two taps a
  half-window apart, linear-interpolated, raised-cosine crossfade.
  `AllPassChain` (152-175): 6-stage first-order all-pass cascade,
  coefficient swept externally — doubles as decorrelator or (LFO-driven)
  phaser, "no separate phaser stage... same object with the LFO connected"
  (146-147). `Shuffler` (187-209): Gerzon bass shuffler, one-pole mid/side
  split. Mid/side generate→diffuse→image on side only (212-518), with a
  mono-bus early-return guard (390-391) rather than upmixing.
- **`modules/tune/dsp/SincTable.h`/`Pitch.h`/`DifferenceKernel.h`** —
  sinc/fractional-delay interpolation, module-local to pitch correction.
- **`modules/sat/dsp/DspCore.h:294-301`** and **`modules/eq`** — each owns a
  hand-rolled dry-path delay buffer matching its oversampler's latency so
  Mix nulls; module-local, not shared.
- **`core/dsp/Oversampler.h`** — cascaded half-band 2x/4x/8x FIR, whole-
  sample group delay (`kMaxLatency`, line 201); reusable, but Sat and EQ
  each instantiate their own.
- **Filters**: `modules/eq/dsp/Svf.h` and `modules/deq/dsp/Svf.h` are
  separate module-local SVFs (not shared); same for `Biquad.h` (deq only).
  No core SVF/biquad to subclass.
- **Saturator shaper** (`modules/sat/dsp/Shaper.h`, `DriveTables.h`):
  module-local, relevant only for a saturating tank; would be copied.

A reverb's comb/allpass/diffuser network is new, module-local code by this
pattern; nothing mandates promoting a primitive to `core/dsp/`.

## 2. `bmo::ModuleDsp` (`core/dsp/ModuleDsp.h:18-70`)

`prepare(sampleRate,maxBlockSize,numChannels)`, `reset()`, `setParams`
(cheap, targets only), `process(...)`, `latencyForParams(...)` (from param
values, not DSP state). No block-size ceiling beyond what the host passes.

- **Channel layout**: both processors *restricted* buses to mono **or** stereo
  with **no conversion** (`SingleModuleProcessor.cpp:70-79`); Dimension instead
  early-returns on mono (`DspCore.h:390-391`). **Shipped 2026-09-21 on AURORA:**
  `core/product/BusLayouts.h` carries mono-in/stereo-out, the rack widening
  once at its own input ahead of slot 1, covered by `bus_tests`. The sentence
  above is the survey this pack was written against.
- **Tempo/transport does not reach a module** — no `AudioPlayHead`/tempo
  plumbing anywhere in `core/`or `modules/`. Tempo-synced pre-delay needs
  new plumbing down to `prepare`/`setParams`; no existing module does this.
- **Tail length was hardcoded to zero** in both `SingleModuleProcessor.h:41`
  and `RackProcessor.h:124` — no module reported a tail. Reverb was the first
  case that was wrong for. **Shipped 2026-09-21 on AURORA:**
  `ModuleDsp::tailSecondsForParams` defaults to 0.0 on every module's vtable,
  the single processor returns it, and the rack **sums** over occupied slots
  and clamps the total at `bmo::kMaxTailSeconds` — the same 30 s a module
  clamps itself at. Do not write 30.0 anywhere else.
- **Bypass**: no per-slot enable/bypass flag in `RackProcessor.cpp/.h` —
  modules are present/absent from the chain, not toggled; verify before
  assuming a slot drops mid-tail.
- **Latency vs. pre-delay**: reported via `setLatencySamples`, pushed only
  on change (`SingleModuleProcessor.cpp:47-63`, `RackProcessor.cpp:412-434`).
  Dimension's precedent for an internal delay that isn't host PDC: report
  **zero** latency and let it live in the sound (`DimDsp.h:45-58`) — the
  model for pre-delay, which should not touch `latencyForParams`.
- **Denormals**: `modules/tune/dsp/Denormals.h` gives a Tune-only
  `ScopedNoDenormals` (FTZ/DAZ). Separately, `SingleModuleProcessor.cpp:83-85`
  and the rack apply `juce::ScopedNoDenormals` first thing in every
  `processBlock`, host-rate — already covers a reverb's decaying tails
  (comment: "Denormals in IIR filter tails cost roughly 100x CPU").
- **Allocation**: `Oversampler` uses fixed arrays; Dimension's
  `DetuneVoice::prepare` (`DspCore.h:64-69`) sizes its buffer from
  `sampleRate` inside `prepare()` — the pattern for large tank buffers.
- **Sample-rate range**: no min/max declared anywhere; `prepare` takes
  whatever the host reports, modules clamp internally (e.g.
  `Shuffler::setFrequency` clamps to `sampleRate*0.45`).

## 3. CPU/memory context

Rack: 8 slots x 32 host params/slot (`core/rack/SlotOverflow.h:9-14`). **No
documented CPU budget** anywhere (`WORKFLOWS.md`, `testing-notes/`,
`tools/measure` all checked). Only concrete figure: BMO Tune RT (not in the
rack, a pitch-correction module) at **0.934% CPU median, 48 kHz/128**
(`testing-notes/tune-latency-review-2026-09-11.md:272`) — not comparable, and
no other module has a measured figure to budget against. Measure a
prototype via `tools/measure/<id>/main.cpp` instead of inferring a target.

## 4. Panel precedents

- **Expandable/secondary section**: BMO DEQ, 320 px compact / 600 px full
  via the host bar (`ModuleDef::expandedWidth`, `products/AGENTS.md:142-145`),
  159 params against the 32/slot host limit, overflow kept in slot state
  (`products/AGENTS.md:136-140`).
- **Graphical display**: `modules/deq/panel/Analyser.h`, a spectrum tap off
  `core/dsp/AnalyserTap.h`, message-thread timer, documented as unable to
  affect sound or latency (`Analyser.h:16-20`) — the pattern for a decay/tail
  meter.
- **Choice/mode selectors**: `S::choiceParam`, e.g. `modules/opto/params.h:72`,
  `modules/tune/params.h:201-203`, `modules/eq/params.h:60-76`. No module
  swaps DSP topology via a "type"/"algorithm" selector; closest is EQ/Sat's
  `Oversampling` choice switching a processing mode, not a whole algorithm.

## 5. Identity

`Brvb` reserved in `products/AGENTS.md:149`, alongside `Bfet`, `Bdyn`,
`Bdes`, `Bovr`, `Bcmp`, `Bdly`. Pattern: module id, plugin code (`B`+3
letters), bundle id `com.lt3audio.bmo<name>` from display name, preset ext
`.bmo<moduleid>` (e.g. Dimension: `dim`, `com.lt3audio.bmodimension`,
`.bmodim`).

**Accents today on main**: EQ 336.0°, Saturator 31.7°, Util 128.9°,
Dimension 271.6°, DEQ 172.0°, Tune 80.1° (spent), LTV Comp periwinkle
236.1° (unsigned, allocated), plus non-accent azure at 198.8° every accent
must clear. Worst separation ever accepted: 26.8° (teal vs. azure). Only two
windows pass today: **298.4°-309.2°** (violet) and a narrow **2.8°-4.9°**
(red sliver) — matching the "still free" windows in
`docs/deesser/11-integration-and-test-plan.md` §2.

**Three unmerged claims narrow this**: BMO FET took `#5489d4` (215.2°) as an
explicit owner-approved *exception* to both rules (`products/AGENTS.md:304-320`
on `frosty-add-bmo-fetcomp`) — doesn't open/close a window, it's a stated
rule-break. BMO Defang took `#ea9f9a` (3.8°) cleanly, margins 27.8°/28.0°
(`products/AGENTS.md:305-331` on `frosty-add-bmo-defang`), **consuming the
whole 2.8°-4.9° window**. BMO Dwell has **not** locked a hue —
`docs/delay/README.md` on `frosty-delay-groundwork` lists only candidates
("gold gap (13) or the red/magenta candidates (11); settle with the
de-esser session"); since Defang has since settled the red gap, Dwell's
red/magenta option is likely foreclosed.

**Net once all three are counted**: the only surviving window is
**298.4°-309.2° (violet)**. Target that range, reconfirm it's open at
merge time, and be ready to negotiate an exception (as FET did) otherwise.

**Blocking unknown:** none.
