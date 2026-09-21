# BMO Defang — groundwork pack

**A de-esser that takes the bite out of your recordings.** Spec, theory, math,
test direction. No code here — the dev team writes it from this pack. Assembled
on AURORA, 2026-09-20; nothing here has been built, measured or heard.

## Index

- [00-repo-conventions.md](00-repo-conventions.md) — the de-esser delta on `docs/1176-comp/00-repo-conventions.md`: what BMO DEQ supplies, what the repo lacks, hues.
- [01-reference-behavior.md](01-reference-behavior.md) — sibilance acoustics, classic behaviour, artefacts, settings, targets; claims tagged documented or folklore.
- [02-design-approaches.md](02-design-approaches.md) — six approaches with artefacts, CPU, latency, aliasing.
- [10-dsp-spec.md](10-dsp-spec.md) — the topology (a dynamic-EQ cut on a level-independent prominence detector), detection math, gain computer, timings.
- [11-integration-and-test-plan.md](11-integration-and-test-plan.md) — registration checklist, identity, accent, **the authoritative v1 schema**, panel verdict, tests, milestones.

## Decided

- **Name: BMO Defang** (owner, 2026-09-20), bundle id `com.lt3audio.bmodefang`;
  module id `deesser` and presets `.bmodeesser` unchanged — the id says what
  the module is, the name what the plugin is called, as `fetcomp` is "BMO FET".
  A web scan that day found no audio product of the name — a check, not
  clearance.
- **Accent: D, muted coral `#ea9f9a`** (owner, 2026-09-20) — in the red gap,
  clear of the 26.8° bar both sides, contrast in band on both plates. Its
  Accents row lands on the build branch.
- **Six parameters**, in order: `freq`, `q`, `thresh`, `range`, `adapt`,
  `shape` — tabulated in 11 §3, which 10 §9 points at rather than copying.
  Attack, release, mix, lookahead and stereo-link are out of v1, appendable.

## Top 3 open decisions

1. **Accept the topology and its one shape parameter.** A dynamic-EQ cut, bell
   or high shelf, zero latency, no lookahead, no oversampling. Wideband,
   crossover and mix are out of v1 — appendable, never inserted.
2. **The four schema questions 11 §3 leaves open.** How `thresh` prints;
   `adapt` continuous or two-position (stepping is permanent); the 18 dB cap
   and 1 dB floor; `shape`'s labels and order.
3. **Listen is a momentary state, not a parameter; no spectrum in v1.** It
   follows BMO DEQ's band-solo precedent — neither automatable nor saved; the
   panel gets a band sketch.

## Known soft spots

- No peer-reviewed paper on ML sibilance detection was found (vendor claims unverified, one spectral paper cited by title); the guards against bright material and cymbal bleed are reasoned, not measured, and every CALIBRATE needs ear and instrument.
- The meter reports peak band reduction, not a wideband figure — a choice, in 10.
- 10 and 11 were **reconciled on AURORA, 2026-09-20**: 11 §3 holds the only parameter table.
