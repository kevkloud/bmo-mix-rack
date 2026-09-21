# BMO FET — groundwork pack

Spec, theory, math and test direction for a 1176-style FET compressor module.
No implementation code lives here; the dev team writes it from this pack.
Assembled on AURORA, 2026-09-20; nothing in it has been built, measured or
heard yet.

## Index

- [00-repo-conventions.md](00-repo-conventions.md) — how a module is laid out, registered, parameterised, built and tested.
- [01-reference-behavior.md](01-reference-behavior.md) — the hardware's documented behaviour, tagged measured/documented or folklore, how engineers run it hard, and target figures.
- [02-modeling-approaches.md](02-modeling-approaches.md) — four modelling approaches, with CPU, latency and aliasing trade-offs.
- [10-dsp-spec.md](10-dsp-spec.md) — topology, the divider-law loop and its quadratic solve, ratio sag, release, nonlinearity, voicings, oversampling, heavy gain reduction, targets.
- [11-integration-and-test-plan.md](11-integration-and-test-plan.md) — layout, parameters, test suites, panel and visual verification, milestones M0–M6.

## Decided

- **Identity.** BMO line, display name **BMO FET**, module id `fetcomp`, plugin
  code `Bfet` (pre-reserved), bundle `com.lt3audio.bmofet`, presets
  `.bmofetcomp`, `ui::bmoLine()`.
- **The FET divider law is the primary design.** `g = 1/(1 + k·c)` with a
  linear feedback sidechain, solved implicitly as a quadratic per sample. The
  dB-domain loop (`a_R = R − 1`) is kept as a documented fallback.
- **Usable at 20 dB+ GR, design target 30 dB. No lookahead.**
- **Switchable Blue/Black voicing, default Black** — two constant sets over one
  topology, gain-matched, identical latency.
- **Oversampling** reuses the shared `Oversampler.h` behind an Off/2x/4x
  parameter: 0 / 40 / 60 samples, zero at the default.
- **No sidechain HPF in v1**; stereo link always on, not a parameter; **`mix`
  ships**, its dry path delay-matched to the oversampler.
- **Attack and release run backwards like the hardware**, 1–7 with 7 fastest.
- **The GR meter is not widened** — 24 dB scale, pins beyond it, BMO Opto
  untouched. The 30 dB DSP target is unchanged.
- **Accent E `#5489d4`**, the deep faceplate-stripe blue — an **explicit owner
  exception**, missing the hue rule (16.4° from the utility azure, 20.9° from
  the periwinkle) and both contrast bands (3.80 dark, 3.09 pale). No blue could
  have passed; no test fails. Consequences and the required Accents-row note
  are in 11 §4c.
- **Voicing shows as a VU border**, Blue = accent, Black = literal black. The
  meter is reused from BMO Opto (`ui::DynamicsMeter`, already shared).

## Open

1. **The VU border alpha.** The pair is decided — Blue = accent E, Black =
   literal black — but the bezel alpha is not: stock 0.7 (3.88:1 between the
   states) or full (5.13:1). **Settled on real renders**, both variants in both
   states and both appearances, owner picks; full alpha also has to leave BMO
   Opto byte-identical. 11 §4b, §4d.
2. **Attack/release parameter form** — knob position 1–7 (recommended) or ms
   ascending. Units and range are permanent. 11 §2.
3. **Parameter ranges and reference level.** 30 dB GR needs `input` to +60 dB
   and `output` to ±36 dB; the dBFS↔0 VU alignment is unmeasured with
   everything riding on it. Permanent, before M0.
4. **Accept all-buttons as shape-fitted** — judged on shape, not numbers.
5. **Voicing labels and default** are permanent once shipped.

## Known soft spots

Ratio sag at depth is derived, not measured — no source gives a real unit's GR
curve past the knee, and revision-specific figures are missing for both
voicings. No 1176-specific circuit-modelling paper was found. Everything marked
CALIBRATE needs measurement and ear.
