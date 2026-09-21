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

**Identity — DECIDED, owner 2026-09-20.** Display name **BMO Defang**, bundle
id **`com.lt3audio.bmodefang`**, module id **`deesser`**, presets
**`.bmodeesser`**, plugin code **`Bdes`** (reserved,
`products/AGENTS.md:169`), `ui::bmoLine()`. The owner's line for it: *it takes
the bite out of your recordings*.

**The module id and the display name differ on purpose.** `deesser` says what
the module is, to anyone reading the tree, a preset extension or a test name;
"BMO Defang" is what the plugin is called. That is the FET precedent exactly —
module id `fetcomp`, display name "BMO FET" — and the bundle id follows the
display name as every existing row does. A **web name-collision scan on
2026-09-20 found no audio product called Defang**; the word is in use in
computer security and by one cloud-tooling company, so this is a collision
check, not a trademark clearance.

Considered and not chosen: **BMO Ess** (`bmoess`, shortest that still says it),
**BMO DES** (`bmodes`, house short-caps, but one letter from BMO DEQ in a
plugin list), **BMO Sift** (`bmosift`), and BMO De-Ess, Tame, Sizzle, Hiss and
Sibilance — the last clashing with an existing commercial product name.
**Hyphens: still avoid one.** No shipped display name has one, and Defang needs
none.

**Registration, from `c142f37`:** the `modules/`, `products/`, `tests/` and
`tools/` `CMakeLists.txt`; `products/deesser/{Product.h,main.cpp,CMakeLists.txt}`;
`products/rack/{Registry.cpp,CMakeLists.txt}`; the `rack_tests`,
`ui_layout_tests` and `snapshot` link lists; `tests/plugin/RackTests.cpp`;
`tests/ui/LayoutTests.cpp`; `tools/snapshot/main.cpp`; `scripts/build.sh`;
and identity **and accent** rows in `products/AGENTS.md` *before the first
build*.

**Permanent at first ship:** module id, code, bundle id, state tags; the accent;
and everything in §3's schema table. New controls append only.

## 2. Accent — DECIDED: D, muted coral `#ea9f9a`

Nine hues are taken, 31.7°–336.0° (`products/AGENTS.md`), including the utility
azure at 198.8° every panel shows. Bar = the worst *accepted* separation,
teal's **26.8°**, and only two windows hold it both sides: **298.4–309.2°** and
**2.8–4.9°** (through red). Contrast bands: dark `#2e2e32` 5.87–7.19, pale
`#efefef` 1.64–2.00.

| | hex | hue | Δ nearest two | dark | pale |
|---|---|---|---|---|---|
| A orchid | `#e694e0` | 304.4° | 31.6 CEQ / 32.8 Dim | 6.23 | 1.89 |
| B mauve | `#eb9fe2` | 307.1° | 28.9 CEQ / 35.5 Dim | 6.82 | 1.72 |
| C magenta | `#e79de7` | 300.0° | 28.4 Dim / 36.0 CEQ | 6.68 | 1.76 |
| **D muted coral — CHOSEN** | **`#ea9f9a`** | **3.8°** | **27.8 CEQ / 28.0 Sat** | **6.39** | **1.84** |
| E saturated orchid | `#ec8bee` | 298.8° | 27.2 Dim / 37.2 CEQ | 6.11 | 1.92 |
| F rose | `#f99a94` | 3.6° | 27.6 CEQ / 28.1 Sat | 6.49 | 1.81 |

**All six pass both rules; no exception, no near-miss.** A **colour mock of all
six was reviewed on AURORA** — swatches, a de-esser strip on both plates, each
candidate beside its two nearest shipped accents — a mock, not a
`tools/snapshot` render.

**The owner chose D, muted coral `#ea9f9a`, 2026-09-20** — its row's figures
are the decision: in the red gap, clear of the bar both sides, contrast inside
both bands. A warm accent, no exception needed, and the violet window stays
free for a later module. The **Accents row lands on the build branch, not in
this pack**; confirm it with `Inspect.exe ratio` on a real render, both
appearances.

## 3. The v1 schema — authoritative

**The single copy**: `10-dsp-spec.md` §9 points here rather than restating it,
and if the two ever differ again this table is the schema and 10 is the
behaviour. **Six parameters, this order.** GR reports through
`currentGainReductionDb()` (signed, positive = gain taken away), **never a
parameter**.

| # | id | caption | range / step | default | skew | units | smooth | kind | auto | permanent once shipped |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `freq` | Freq | 2000…10000, 0.1 | 6500 | log | Hz | 20 ms, log | continuous | yes | band centre in bell, corner in shelf |
| 1 | `q` | Q | 0.7…6.0, 0.01 | 2.5 | log | — (Plain) | 20 ms, log | continuous | yes | Q whatever the shape; engine clamps 0.1–40 behind it |
| 2 | `thresh` | Threshold | −24…+24, 0.1 | 0 | linear | dB | 10 ms | continuous | yes | anchored to `P_ref` (10 §3); re-anchoring moves every saved session |
| 3 | `range` | Range | 1…18, 0.1 | 8 | linear | dB | 10 ms | continuous | yes | ceiling and floor both freeze |
| 4 | `adapt` | Adapt | 0…100, 0.1 | 60 | linear | % | 20 ms | continuous | yes | 100 % = fullband reference (κ 1), 0 % = the band's own 500 ms average |
| 5 | `shape` | Shape | choice: Bell, High Shelf | Bell | stepped | — | 20 ms xfade | stepped | yes | labels and index order freeze; a third shape appends at #6 |

`ParamSpec`: `logParam` for `freq` (`F::Hertz`) and `q` (`F::Plain`),
`floatParam` for `thresh`/`range` (`F::Decibels`) and `adapt` (`F::Percent`),
`choiceParam` for `shape`. `freq`'s 0.1 Hz step is DEQ's reason — a host
carries the value as a 32-bit normalised float, through which a continuous log
law will not round-trip exactly (`modules/deq/params.h:144-148`).

### 3a. Reconciled against 10 — AURORA, 2026-09-20

This section's first draft was written before 10 was read. Against it:
`sensitivity` → **`thresh`**, in the prominence-dB 10 §§3–4 detect and
threshold on — not dBFS, and the legend must say so; `width` → **`q`** (10 §§3,
5 are constant-Q, so octaves are a legend, not a parameter); `mode` →
**`shape`** (DEQ's id and labels); `freq` **1.5–16 → 2–10 kHz** (01 §6 High;
10 §7); `range` **0–18 def 12 → 1–18 def 8** (01 §4 works at 2–6 dB, and a
floor keeps this a depth control); **`adapt` added**, 10 §3's κ. Dropped:
`attack`/`release`, fixed by 10 §6 and appendable later as `logParam` ms
ascending; and `lookahead`, refused by 10 §1 — latency is 0 everywhere, and
permanent, so that is the costly deletion to undo.

**Listen is NOT a parameter — decided, and 10 §2 agrees.** Momentary panel state
on the existing `ModuleContext::setSolo` / `ModuleDsp::setSolo(int)` hook
(`core/ui/ModulePanel.h:75-80`; −1 clears), as DEQ does
(`modules/deq/dsp/DspCore.h:148-149`): a saved one could be recalled or printed
into a bounce, and it costs no schema slot. Render as `ui.listen=on|off`.

**No `mix`, stereo-link parameter or M/S in v1.** 10 §1 refuses MIX on a
minimum-phase cut — a partial blend is a shallower cut of nearly the same shape,
which `range` gives — and stereo is always linked on a power-summed detector
(10 §3), so a hard-panned sibilant cannot shift the image. All three append,
none insert.

**Metering:** shared `ui::DynamicsMeter` in GR mode from
`context.gainReductionDb`, fed 10 §8's **peak band reduction** — the applied
(glided) offset, *not* a wideband-equivalent figure. `kGrRangeDb = 24`
unchanged; `range` caps at 18, so the needle cannot pin. The panel owns the
IN/GR/OUT row `modules/AGENTS.md` requires.

**Owner items, four — all schema now, the name and accent being settled (§§1,
2).** The `thresh` value string: plain `F::Decibels`, or a `textParam` printing
"+3.0 dB over". `adapt` continuous vs 10 §10.2's two-position switch — settle
it first, stepped can never become continuous. The 18 dB cap and 1 dB floor,
and a lower internal ceiling for the shelf (10 §10.5). `shape` labels and
index order.

## 4. Panel — band display?

Precedent `modules/deq/panel/Analyser.h` (4096-pt Hann, message-thread) over
`core/dsp/AnalyserTap.h`. Cost: a ring write per block, an FFT and a per-column
path rebuild per repaint, plus a widget to lay out and snapshot; DEQ pays it
because its interaction *is* the curve. **No spectrum in v1** — GR plus momentary
listen answers "is it catching the sibilance, how hard". **Proposal:** a static
band sketch from `freq`, `q` and `shape` alone — bell or shelf corner, no tap,
FFT or timer — with GR drawn as its cut.

## 5. Building the test suites

**Stimuli generated in-test from a fixed-seed LCG, never committed.** Sibilant
event = bandpass noise (4–9 kHz, 120 ms, 5 ms raised-cosine edges) over a
vowel-like carrier (f0 110 and 220 Hz, harmonics 1…40 at −6 dB/oct, formants near
700/1200 Hz), at **−30 to −6 dBFS in 6 dB steps** — that sweep proves level
independence. Four the reconciled spec adds: a **400 ms** sibilant (slow
branch), an **/s/–/t/ cluster** 5 ms apart (hold), a **−6 → −60 dBFS fade** (the
`S` clamp), a bed crossing the **−55/−60 dBFS gates** from below. Negatives:
vowel, breath, silence, bright non-vocal bed.

### JUCE-free DSP target — `tests/dsp/DeesserDspTests.cpp`

| case | assertion |
|---|---|
| detection | ≥ 3 dB GR on ≥ 95 % of bursts, **both shapes**; vowel/breath ≤ 1 dB; silence 0 |
| gates | reference under **−55 dBFS** or band under **−60 dBFS** → offset **exactly 0**; across a fade GR never *rises* (the `S` clamp) |
| level independence | **±1.0 dB** over the span **at `adapt` 0, 60 and 100** — it must cancel at every blend, not just the default |
| adapt | bright non-vocal bed: at 0, GR ≤ 1 dB once `S` settles (≥ 1.5 s); at 100, characterised (01 §4); monotone between; a burst **over** that bed still triggers at 0 |
| curve, depth | 10 §4's curve — flat to `T−3`, ≈ −0.56 dB at `T`, 0.75 dB/dB, clamped at `range` — golden array **±0.25 dB**; ten `thresh` steps, monotone, 2–6 dB **±2 dB** |
| timing | the **fixed** 0.8 / 30 ms at 01's 63 % definitions, 6 dB depth, **±20 %**; ~90 % applied 2 ms in; no parameter moves them |
| slow branch, hold | > 150 ms over threshold crossfades to the 120 ms release: a 400 ms sibilant does not chatter (ripple **≤ 1 dB**), a 120 ms burst still releases on 30 ms; an /s/–/t/ cluster 5 ms apart is **one** event, re-trigger 1.5 dB lower while engaged |
| HF pumping | steady −30 dBFS 8–16 kHz bed, band cut 6 dB: bed modulation **≤ 1 dB** |
| transparency | untriggered residual **≤ −100 dB**; a split build cannot null (02 §2) — sweep flat to **±0.1 dB** |
| shelf mode | detection through the high-pass at the cut's own corner; depth there tracks the bell **±0.5 dB**; HF loss at max `range` **characterised** (10 §10.5) |
| modulation | at the fixed attack, non-harmonic sidebands **≤ −80 dB** (02 §3); 60 s leaves state finite; the glide is in **ms, not ticks** (10 §7) |
| aliasing | a probe folding onto bins no harmonic can occupy (`SatDspTests::testOversampling`); every rate; floor **≤ −70 dB**; no oversampler, slow the smoothing |
| listen path | soloed output = the band's contribution (DEQ's `H(x)−x`), nulls to −100 dB; −1 restores **bit-identical** output |
| stereo link | power-summed: a hard-panned burst ducks both channels equally, one GR, L−R within 0.1 dB; a decorrelated pair does not cancel as a mono sum would |
| invariance | 44.1–192 kHz hold; `freq` 10 kHz at 44.1 kHz stable, no cramping; blocks 1…1023 identical to −120 dB; ramps below −80 dB |
| robustness | silence → exact zeros; no denormal stall; no NaN/Inf at either end of every parameter, or past them into the engine clamps |
| interface | `latencyForParams` **0 at every setting**, across `prepare`/`reset`, by impulse correlation; the meter equals the **applied** offset **±0.5 dB**, ≤ 18 dB on the 24 dB scale so it cannot pin, over-range clamps at 1.0 |
| regression | **golden STATE, never audio**: `thresh` array, timing table, per-block RMS/peak at 1e-4 |

Which topology a row assumes is **per 10**.

**Plugin/schema — `tests/plugin/DeesserTests.cpp` (JUCE):** golden `Expected[]`
table — **the six rows of §3, in that order**, with ranges, steps, defaults and
the `shape` labels written out; `Index::count == specs().size() == 6`; presets
load; state XML round-trips **carrying no listen state**; rack slot fits 32
params; entry in `tests/ui/LayoutTests.cpp`.

**Manual — `tools/measure/deesser/main.cpp`:** modes `detect | depth | timing |
adapt | gates | pump | zipper | alias | bench | render | gen`; `detect` also
prints a take's **prominence distribution**, which is how `P_ref` gets fitted
(10 §10.1) before any listening round; WAVs to `packages/deesser-listening/`
(gitignored). `bench` is Release, 100 × 10 s at 48 kHz/512, beside `measure_deq`
on the same box — same SVF + detector shape, so DEQ is the yardstick; budget
**≤ 1.0× DEQ** at defaults, **≤ 1.5×** at the heaviest. **Every result names its
machine (AURORA / ICE QUEEN) in `testing-notes/`.**

**Listening pass — NOT YET HEARD.** Gain-matched, machine named: male lead vocal;
female lead vocal (band an octave higher, 01 §1); an **already-dull** vocal, for
lisping (01 §4); cymbal bleed, for false triggering; a full mix. Audition the
listen path on each, every source at **both ends of `adapt`** — whether it has a
useful middle is 10 §10.2's question and only ears close it — and in both
shapes, since shelf dulling is the other thing only ears catch.

## 6. Milestones and definition of done

Panel-first, as `c142f37` did for BMO FET. **M0** identity and accent rows,
directory, `params.h`, a marked pass-through DSP reporting latency, every
registration point, schema test green, panel rendering in both appearances.
**M1** filter, detector, gates, gain computer → detection, gates, static curve,
depth, level independence **across `adapt`**. **M2** timing → the fixed
attack/release, the slow branch, hold, pumping, transparency. **M3** modulation →
zipper, aliasing, near-Nyquist, **shelf mode**. **M4** listen path, stereo link,
meter → `latencyForParams` **0 everywhere**. **M5** presets, docs, measure tool,
§4d's step order. **M6** invariance, robustness, CPU, `P_ref` fitted, listening
pass.

**Done** = ctest green in all three CI jobs on both platforms; schema pinned; no
audio, renders or fonts committed; `AGENTS.md` + `README.md` linked; identity and
accent rows permanent, the accent confirmed by `Inspect.exe ratio` on a real
render rather than by formula; measure tool registered; CPU and latency inside
budget, recorded in `testing-notes/` with the machine named; listening pass
done.
