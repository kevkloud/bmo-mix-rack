# Repo conventions for a new 1176-style FET compressor module

Researched by walking `modules/vcomp` (LTV Comp) end to end as the reference
compressor. No files modified. Ignore `build/`, `build-dsp/`, `build-rel/`.

## 1. Module layout & registration

A module = `modules/<id>/params.h`, `dsp/` (a `DspCore` + a `ModuleDsp`
adapter), `panel/` (a `ModulePanel`), `presets/FactoryPresets.h`, `Module.cpp`
+ `Module.h` exposing `ModuleDef module()` (AGENTS.md:206-209; example
`modules/vcomp/Module.cpp:1-44`). `Module.cpp` builds a `static const ModuleDef`
with: module id, display name, `kSchemaVersion`, panel width, accent
`juce::Colour`, `specs()`, `factory()`, a DSP factory lambda, `expandedWidth`
(0 unless DEQ-style), and a `ui::Line` (`&ui::ltvLine()` vs the BMO line).

Registration:
- Build: `modules/CMakeLists.txt:77-83` — `bmo_add_module(vcomp <dsp .cpp> <panel .cpp> Module.cpp)`.
- Standalone product: `products/<id>/Product.h` (`ProductInfo`, factory) +
  `main.cpp` + `CMakeLists.txt` calling `bmo_add_plugin` with `BUNDLE_ID`/`LIBRARIES`
  (`products/vcomp/Product.h:1-27`, `products/vcomp/CMakeLists.txt:4-7`).
- Rack: add `&<id>::module()` to `products/rack/Registry.cpp:15-23`, plus the
  `#include`. That vector is the whole rack registry.
- Identity table (plugin code, bundle id, preset ext, line) lives in
  `products/AGENTS.md` ("The identity table" section) — allocate a row there
  **before the first build**.

## 2. DSP interface & shared utilities

Every module implements `bmo::ModuleDsp` (`core/dsp/ModuleDsp.h:18-70`):
`prepare(sampleRate,maxBlockSize,numChannels)`, `reset()`,
`setParams(const float* values,int count)` (cheap, stores targets),
`process(float* const* channels,int numChannels,int numSamples)`,
`latencyForParams(...) const` (computed from param values, not DSP state — see
`modules/vcomp/dsp/VcompDsp.h:44-56`), optional `currentGainReductionDb()`
(signed: **positive = gain reduced**, dynamics modules only,
`ModuleDsp.h:36-49`), optional `setSolo(int)` and `analyser()`. The pattern
is a plain `DspCore` (JUCE-free) doing the audio, wrapped by a `<Id>Dsp final :
ModuleDsp` that unpacks the flat `float*` array into a `DspCore::Params`
struct in index order (`modules/vcomp/dsp/VcompDsp.h:20-38`).

`core/dsp/` shared utilities: `ModuleDsp.h` (base), `GainComputer.h`,
`Meter.h`, `Oversampler.h`, `AnalyserTap.h`. Module-local DSP utilities
(envelope/release stages, crossovers, gates) live in the module's own `dsp/`
folder (e.g. `modules/vcomp/dsp/Detector.h`, `Gate.h`, `Crossover.h`,
`Limiter.h`) — there's no shared compressor-detector library; each dynamics
module owns its own `ReleaseStage`/`Smoother`. `core/dsp/AnalyserTap.h`
governs any spectrum/GR display tap. `core/dsp` must stay JUCE-free
(`BMO_DSP_ONLY=ON`, AGENTS.md:204-205).

## 3. Parameters & state

Declared in `modules/<id>/params.h` via `bmo::ParamSpec` factories —
`floatParam`, `logParam` (equal turns per ratio; `min>0`), `boolParam`,
`choiceParam` — each with id, display name, min/max/step/default and a
`ParamFormat` (`core/state/ParamSpec.h:52-104`). IDs are plain string
constants (e.g. `kAmount = "amount"`) plus a matching `enum Index` whose order
**must** match `specs()` (checked in tests, `modules/vcomp/params.h:14,49-54`).
`ParamSet` (`core/state/ParamSet.h`) is the single read/write path for both
the standalone APVTS and the rack's `SlotParameter`; DSP always receives
values via `ParamSet::readAll()` into a flat array in spec order. State
save/restore is XML (`<PARAMS stateVersion=".."><PARAM id=.. value=.. /></PARAMS>`,
`ParamSet.h:95-128`); presets are the same shape, filed under
`modules/<id>/presets/FactoryPresets.h` as `Setting{id,value}` lists.

**Versioning/permanence rules (AGENTS.md:113-130):** parameter IDs, their
**order** in `specs()`, ranges, steps and defaults are permanent once shipped
— add new parameters at the **end** only, never insert. Plugin code, bundle
id, module id and state tags (`PARAMS`/`RACK`/`SLOT`) are likewise frozen.
The golden schema test enforces this (`tests/plugin/<Id>Tests.cpp`, e.g.
`tests/plugin/VcompTests.cpp:20-32`). Smoothing is done in the DSP core (a
one-pole `Smoother`, not in `ParamSet`) — see `modules/vcomp/dsp/DspCore.h:23-47`.
Rack constraint: 8 slots x 32 params/slot; overflow handling is
`core/rack/SlotOverflow.h` (AGENTS.md:123-126).

## 4. Build & test harness

CMake targets per module via `bmo_add_module`; tests registered in
`tests/CMakeLists.txt` with `bmo_add_dsp_tool`/`bmo_add_tool` + `add_test`
(vcomp pattern: lines 29-31 DSP test, 97-99 plugin/schema test, plus link
into `rack_tests`/`ui_layout_tests` at lines 102/109). DSP tests
(`tests/dsp/<Id>DspTests.cpp`) are JUCE-free, run against `DspCore` directly.
Plugin/schema tests (`tests/plugin/<Id>Tests.cpp`) assert the golden
parameter table via `checkSchema`. `tests/ui/LayoutTests.cpp` covers panels.
Build/test commands: `scripts/build.sh` (full + ctest), `--snapshots` renders
every panel via `build/tools/Release/snapshot.exe`; `build/tools/Release/measure_<id>.exe`
prints DSP curves (`tools/measure/<id>/main.cpp`, e.g. `measure_vcomp curve|presets|gate|bands|render`).
CI is `.github/workflows/build.yml`: `dsp` job (Linux, DSP-only, seconds),
`independence` job (Tune-alone / rack-alone), `plugin` job (macOS+Windows,
~20-25 min). No golden-audio/null-render CI gate found beyond the DSP unit
assertions; listening/measurement artifacts go to `packages/<module>-listening/`
(gitignored working folder) and are reported in `testing-notes/`.

## 5. Reference wiring — LTV Comp end to end

`modules/vcomp/params.h` (specs+Index) → `modules/vcomp/dsp/DspCore.h`
(DSP, uses `Detector.h`/`Gate.h`/`Crossover.h`/`Limiter.h`) →
`modules/vcomp/dsp/VcompDsp.h` (`ModuleDsp` adapter) →
`modules/vcomp/panel/VcompPanel.{h,cpp}` + `LevelBars.{h,cpp}` (UI) →
`modules/vcomp/Module.cpp` (`ModuleDef`) → `modules/vcomp/presets/FactoryPresets.h`
→ registered in `modules/CMakeLists.txt:77-83`,
`products/rack/Registry.cpp:8,22`, `products/vcomp/{Product.h,main.cpp,CMakeLists.txt}`
→ identity row in `products/AGENTS.md` → tested in
`tests/dsp/VcompDspTests.cpp` + `tests/plugin/VcompTests.cpp` + `tests/CMakeLists.txt:29-31,97-99`
→ documented in `modules/vcomp/AGENTS.md` (371 lines) + `modules/vcomp/README.md`
→ listening artifacts in `packages/vcomp-listening/`.

## 6. Rules from AGENTS.md / WORKFLOWS.md

Never commit audio/renders/fonts (AGENTS.md:8-79); name the machine (AURORA/ICE
QUEEN) in any recorded build/test/listen result; param IDs/order/ranges/defaults
are permanent, append-only (AGENTS.md:113-130); plugin codes are `B`+3 letters
for BMO or 2+2 for an LTV collaboration product (`products/AGENTS.md`
"Lines" section) — **LTV Comp itself sets the precedent that a
hardware-modeled compressor may be an LTV-line product, not a BMO one**; a new
module/directory needs its own `AGENTS.md` (why + context) and `README.md`
(human-facing), linked from its parent (AGENTS.md:236-244); root is the
include root (`#include "core/..."`), namespaces `bmo::`/`bmo::<module>`/`bmo::ui`
(AGENTS.md:248-251); licensed fonts in `assets/fonts` are gitignored, never
looked up by name at runtime; do not use UA/Urei branding — refer to the new
module only as "1176-style"/FET compressor per this task's own instruction,
consistent with how LTV Comp avoids naming its hardware references directly
in code/schema (it uses reference points in prose only, `products/AGENTS.md`
"LTV Comp" section).

**Blocking unknown:** none.
