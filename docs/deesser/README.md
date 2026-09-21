# BMO Defang — groundwork pack

**A de-esser that takes the bite out of your recordings.** Spec, theory, math
and tests; no code — the dev team writes that from this pack. Assembled on
AURORA, 2026-09-20; nothing is built, measured or heard.

## Index

- [00-repo-conventions.md](00-repo-conventions.md) — the delta on `docs/1176-comp/00-repo-conventions.md`: what BMO DEQ supplies, what the repo lacks.
- [01-reference-behavior.md](01-reference-behavior.md) — sibilance acoustics, classic behaviour, artefacts, settings, targets; claims tagged for confidence.
- [02-design-approaches.md](02-design-approaches.md) — six approaches with artefacts, CPU, latency, aliasing.
- [10-dsp-spec.md](10-dsp-spec.md) — the topology (a dynamic-EQ cut on a level-independent prominence detector), detection math, gain computer, timings; §11 ADAPT.
- [11-integration-and-test-plan.md](11-integration-and-test-plan.md) — registration, identity, accent, **the v1 schema**, panel, tests, milestones.

## Decided

- **Name: BMO Defang** (owner, 2026-09-20), bundle id `com.lt3audio.bmodefang`;
  module id `deesser` and presets `.bmodeesser` unchanged: the id says what the
  module is, the name what it is called, as `fetcomp` is "BMO FET".
  A web scan found no audio product of the name: a check, not clearance.
- **Accent: D, muted coral `#ea9f9a`**, same day — in the red gap, clear of the
  26.8° bar both sides, contrast in band on both plates.
- **The v1 schema: five parameters**, in order — `freq` 2–10 kHz, `q` 0.7–6,
  `thresh` ±24 prominence-dB reading **"+3.0 dB over"**, `range` 1–18 dB
  default 8, `shape` **Bell / High Shelf**, Bell default. The last three were
  approved 2026-09-20; all five are in 11 §3 and freeze at first ship. Attack,
  release, mix, lookahead, stereo-link: out, appendable only.

## Potential — needs testing

- **ADAPT** (10 §11). κ, the reference blend, is **one internal constant in
  v1**, fixed mid-way, CALIBRATE. One end catches every sibilant but dulls
  constantly-bright material; the other leaves steady brightness alone but
  under-treats long sibilants. Whether one fixed value serves every source is
  the listening pass's question; only then does it earn a control, appended
  after `shape`.

## Top 2 open decisions

1. **Accept the topology.** A dynamic-EQ cut, bell or high shelf, zero latency,
   no lookahead or oversampling; wideband, crossover and mix are out.
2. **Listen is a momentary state, not a parameter; no spectrum in v1.** BMO
   DEQ's band-solo precedent: neither automatable nor saved. The panel gets a
   band sketch.

## Known soft spots

- No peer-reviewed paper on ML sibilance detection was found; the guards against bright material and cymbal bleed are reasoned, not measured, and every CALIBRATE needs ear and instrument.
- The meter reports peak band reduction, not a wideband figure (a choice, in 10). 10 and 11 were **reconciled on AURORA, 2026-09-20**; 11 §3 is the only parameter table.
