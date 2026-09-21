# De-Esser — Integration & Test Direction

Groundwork; no code. Topology, modes, latency and constants: **per
`10-dsp-spec.md`**. Tools, fonts and visual-pass step order:
`docs/1176-comp/11-integration-and-test-plan.md` §4d — follow it, not repeated.
Figures computed on **AURORA**.

## 1. Drop-in

`modules/deesser/`: `params.h`; `dsp/DspCore.{h,cpp}` (JUCE-free) with
`Band.h` (TPT/SVF), `Detector.h` and `DeesserDsp.h` (`ModuleDsp` adapter);
`panel/DeesserPanel.{h,cpp}`; `presets/FactoryPresets.h`; `Module.{h,cpp}`;
`AGENTS.md` + `README.md`, linked from `modules/AGENTS.md`. Per `00` §1 DEQ's
`Svf.h`/`Dynamics.h` are **copied in**, never included.

Code **`Bdes`** (reserved, `products/AGENTS.md:169`); id **`deesser`**, which
says what the module is, per the FET precedent. Presets are `.bmodeesser` for
every name below; only the bundle id moves, and it is the lowercased display
name with spaces and hyphens removed, as every existing row is.

| Display name | Bundle id | Fit, and risk |
|---|---|---|
| BMO Ess | `com.lt3audio.bmoess` | shortest that still says it; sits beside Opto/Util/FET; no clash found |
| BMO DES | `com.lt3audio.bmodes` | the CEQ/DEQ short-caps style and the code; but one letter from BMO DEQ in a plugin list |
| BMO Sift | `com.lt3audio.bmosift` | character; selective removal without naming the artefact; no clash found |
| BMO De-Ess | `com.lt3audio.bmodeess` | plainest; needs the hyphen rule settled |
| BMO Tame | `com.lt3audio.bmotame` | character; says the effect, not the band; no clash found |
| BMO Sizzle | `com.lt3audio.bmosizzle` | character, but names the problem rather than the cure |
| BMO Hiss | `com.lt3audio.bmohiss` | reads as a noise reducer, which this is not |
| BMO Sibilance | `com.lt3audio.bmosibilance` | **clashes with an existing commercial product name**; also the longest on a narrow strip |

**Ranked.** 1. **BMO Ess** — short, sayable, unhyphenated, and the only plain
option with no clash. 2. **BMO DES** — spends the reserved code in its own name
and matches the house short-caps, at the cost of sitting next to BMO DEQ.
3. **BMO Sift** — the character pick, if the suite wants one name that is not an
abbreviation. **Hyphens: avoid one.** No shipped display name has a hyphen, so
"De-Ess" would be the first, and the bundle id would have to strip it silently
(`bmodeess`) — a rule that has to be written down before it is relied on.

**Registration, from `c142f37`:** the `modules/`, `products/`, `tests/` and
`tools/` `CMakeLists.txt`; `products/deesser/{Product.h,main.cpp,CMakeLists.txt}`;
`products/rack/{Registry.cpp,CMakeLists.txt}`; the `rack_tests`,
`ui_layout_tests` and `snapshot` link lists; `tests/plugin/RackTests.cpp`;
`tests/ui/LayoutTests.cpp`; `tools/snapshot/main.cpp`; `scripts/build.sh`;
and identity **and accent** rows in `products/AGENTS.md` *before the first
build*.

**Permanent at first ship:** module id, code, bundle id, state tags; parameter
ids and their **order**, ranges, steps, defaults; every choice list and index
order; the accent. New controls append only.

## 2. Accent — passing candidates only

Nine hues are taken, 31.7°–336.0° (`products/AGENTS.md`), including the utility
azure at 198.8° that every panel shows. Bar = the worst *accepted* separation,
teal's **26.8°**; the windows holding it both sides are **298.4–309.2°**
(Dimension→CEQ) and **2.8–4.9°** (CEQ→Saturator, through red). Bands: dark
`#2e2e32` 5.87–7.19, pale `#efefef` 1.64–2.00.

| | hex | hue | Δ nearest two | dark | pale |
|---|---|---|---|---|---|
| **A** orchid | `#e694e0` | 304.4° | 31.6 CEQ / 32.8 Dim | 6.23 | 1.89 |
| **B** mauve | `#eb9fe2` | 307.1° | 28.9 CEQ / 35.5 Dim | 6.82 | 1.72 |
| **C** magenta | `#e79de7` | 300.0° | 28.4 Dim / 36.0 CEQ | 6.68 | 1.76 |
| **D** muted coral | `#ea9f9a` | 3.8° | 27.8 CEQ / 28.0 Sat | 6.39 | 1.84 |
| **E** saturated orchid | `#ec8bee` | 298.8° | 27.2 Dim / 37.2 CEQ | 6.11 | 1.92 |
| **F** rose | `#f99a94` | 3.6° | 27.6 CEQ / 28.1 Sat | 6.49 | 1.81 |

**All six pass both rules; no exception is proposed and no near-miss had to be
included.** A separates best and is already drawn on real plates (candidate F of
`docs/1176-comp/11` §4c, refused there for not being blue). A **colour mock of
all six was reviewed on AURORA** — swatches, a de-esser strip on both plates with
the accent used as the rack uses it, each candidate beside its two nearest
shipped accents, and a full-rack row — and it is a mock, not a `tools/snapshot`
render. Suggested: **A**, with **E** if it reads soft beside Dimension's
lavender and **D**/**F** if the module should read warm. Confirm with
`Inspect.exe ratio` on a render, both appearances, before the row lands.

## 3. Parameters and automation

GR reports through `currentGainReductionDb()` (signed, positive = gain taken
away), **never a parameter**. All rows continuous except 6 and 7.

| # | id | range | default | skew | units | smooth | auto |
|---|---|---|---|---|---|---|---|
| 0 | `sensitivity` | 0…100 | 50 | linear | % | 20 ms | yes |
| 1 | `freq` | 1500…16000 | 6500 | log | Hz | 20 ms | yes |
| 2 | `width` | 0.3…3.0 | 1.2 | log | oct | 20 ms | yes |
| 3 | `range` | 0…18 | 12 | linear | dB | 20 ms | yes |
| 4 | `attack` | 0.1…20 | 1.0 | log | ms | target | yes |
| 5 | `release` | 2…200 | 60 | log | ms | target | yes |
| 6 | `mode` | choice **per 10** | **per 10** | stepped | — | 5 ms xfade | yes |
| 7 | `lookahead` | Off/1.5/3/5 ms **per 10** | Off | stepped | — | none | **no** |

`sensitivity` is **unitless deliberately**: detection is relative (01 §2), so a
fixed-threshold mode, if 10 ships one, is the same knob mapped inside the DSP.
`width` is octaves, not Q: male and female bands sit an octave apart (01 §6).
`range` caps at 18 dB against 01's 10–15 dB. `attack`/`release` follow LTV
Comp's ascending ms, unlike BMO FET, which models a printed knob. `lookahead`
re-syncs host PDC — setup, not automation.

**Listen is NOT a parameter — decided.** Momentary panel state over the existing
`ModuleContext::setSolo` / `ModuleDsp::setSolo(int)` hook
(`core/ui/ModulePanel.h:75-80`; −1 clears), as DEQ does
(`modules/deq/dsp/DspCore.h:148-149`): a saved one could be recalled or printed
into a bounce, and this costs no schema slot. Render as `ui.listen=on|off`.

**No `mix`, stereo-link parameter or M/S in v1.** `range` already means less
de-essing, and a dry path needs delay-matching once lookahead ships; stereo is
always linked so a hard-panned sibilant cannot shift the image
(`vcomp`/`fetcomp`). All three may be appended, none inserted.

**Metering:** shared `ui::DynamicsMeter` in GR mode from
`context.gainReductionDb`; `kGrRangeDb = 24` unchanged, since `range` caps at 18
and the needle never pins in use — tested anyway. The panel owns the IN/GR/OUT
row `modules/AGENTS.md` requires.

**Owner items:** display name; accent; whether `lookahead` ships (latency is
permanent); `mode` labels and order; the 18 dB cap.

## 4. Panel — band display?

Precedent `modules/deq/panel/Analyser.h` (4096-pt Hann, message-thread) over
`core/dsp/AnalyserTap.h`. Cost: a ring write per block, an FFT and a per-column
path rebuild per repaint, plus a widget to lay out and snapshot; DEQ pays it
because its interaction *is* the curve. **No spectrum in v1** — GR plus momentary
listen already answers "is it catching the sibilance, how hard". **Fallback, and
the proposal:** a static band sketch from `freq`/`width` alone, no tap, FFT or
timer, with GR drawn as its cut.

## 5. Building the test suites

**Stimuli generated in-test from a fixed-seed LCG, never committed.** Sibilant
event = bandpass noise (4–9 kHz, 120 ms, 5 ms raised-cosine edges) over a
vowel-like carrier (f0 110 and 220 Hz, harmonics 1…40 at −6 dB/oct, formants near
700/1200 Hz), at **−30 to −6 dBFS in 6 dB steps** — that sweep proves level
independence. Negatives: vowel, breath, silence, bright non-vocal bed.

### JUCE-free DSP target — `tests/dsp/DeesserDspTests.cpp`

| case | assertion |
|---|---|
| detection | ≥ 3 dB GR on ≥ 95 % of bursts; vowel/breath ≤ 1 dB; silence 0; bright bed **characterised, not bounded** (01 §4) |
| level independence | GR within **±1.0 dB** over the span; fails if detection goes absolute |
| depth vs sensitivity | ten steps, golden array, monotone; mid-range 2–6 dB **±2 dB**; clamped by `range` **±0.5 dB** |
| timing | 01's 63 % onset/offset definitions at 6 dB depth, **±20 %**, monotone |
| HF pumping | over a steady −30 dBFS 8–16 kHz bed, band cut 6 dB: bed modulation **≤ 1 dB** |
| transparency | untriggered residual **≤ −100 dB**; a split build cannot null (02 §2) — sweep flat to **±0.1 dB** |
| modulation | fastest attack: non-harmonic sidebands **≤ −80 dB** (02 §3); 60 s leaves state finite; coefficients glide |
| aliasing | a probe folding onto bins no harmonic can occupy (`SatDspTests::testOversampling`); every rate; floor **≤ −70 dB**; no oversampler, slow the smoothing |
| listen path | soloed output = the band's contribution (DEQ's `H(x)−x`), nulls to −100 dB; −1 restores **bit-identical** output |
| stereo link | hard-panned burst ducks both channels equally, one shared GR, L−R within 0.1 dB |
| invariance | 44.1–192 kHz hold; `freq` 16 kHz at 44.1 kHz stable; blocks 1…1023 identical to −120 dB; ramps below −80 dB |
| robustness | silence → exact zeros; no denormal stall; no NaN/Inf at extremes |
| `latencyForParams` | equals impulse cross-correlation at every setting, stable across `prepare`/`reset`, **0 at the default** |
| meter pin | past 24 dB the needle clamps at 1.0, no wrap or NaN, readout true |
| regression | **golden STATE, never audio**: sensitivity array, timing table, per-block RMS/peak at 1e-4 |

Which topology a row assumes is **per 10**.

**Plugin/schema — `tests/plugin/DeesserTests.cpp` (JUCE):** golden `Expected[]`
table; `Index::count == specs().size()`; defaults match any `kStandard*`;
presets load; state XML round-trips **carrying no listen state**; rack slot fits
32 params; entry in `tests/ui/LayoutTests.cpp`.

**Manual — `tools/measure/deesser/main.cpp`:** modes
`detect | depth | timing | pump | zipper | alias | bench | render | gen`; WAVs to
`packages/deesser-listening/` (gitignored). `bench` is Release, 100 × 10 s at
48 kHz/512, run beside `measure_deq` on the same box — DEQ is the yardstick,
same SVF + detector shape; budget **≤ 1.0× DEQ** at defaults, **≤ 1.5×** at the
heaviest. **Every result names its machine (AURORA / ICE QUEEN) in
`testing-notes/`.**

**Listening pass — NOT YET HEARD.** Gain-matched, machine named: male lead vocal;
female lead vocal (band an octave higher, 01 §1); an **already-dull** vocal, for
lisping (01 §4); cymbal bleed, for false triggering; a full mix. Audition the
listen path on each.

## 6. Milestones and definition of done

Panel-first, as `c142f37` did for BMO FET. **M0** identity and accent rows,
directory, `params.h`, a marked pass-through DSP reporting latency, every
registration point, schema test green, panel rendering in both appearances.
**M1** filter, detector, gain computer → detection, level independence, depth.
**M2** timing → attack/release, pumping, transparency. **M3** modulation →
zipper, aliasing, near-Nyquist. **M4** listen path, stereo link, `lookahead` →
`latencyForParams`. **M5** presets, docs, measure tool, §4d's step order.
**M6** invariance, robustness, meter pin, CPU, listening pass.

**Done** = ctest green in all three CI jobs on both platforms; schema pinned; no
audio, renders or fonts committed; `AGENTS.md` + `README.md` linked; identity and
accent rows permanent, the accent confirmed by `Inspect.exe ratio` on a real
render rather than by formula; measure tool registered; CPU and latency inside
budget, recorded in `testing-notes/` with the machine named; listening pass done.
