# De-esser — groundwork pack

Spec, theory, math and test direction for a de-esser module (plugin code
`Bdes`, reserved). No implementation code lives here; the dev team writes it
from this pack. Assembled on AURORA, 2026-09-20, from agent research; nothing in
it has been built, measured or heard yet.

## Index

- [00-repo-conventions.md](00-repo-conventions.md) — the de-esser-specific delta on top of `docs/1176-comp/00-repo-conventions.md`: what BMO DEQ already has to copy and adapt, what the repo lacks, the `Bdes` reservation, free accent hues.
- [01-reference-behavior.md](01-reference-behavior.md) — sibilance acoustics, how the classic designs behave, artefacts, typical settings, target figures; each claim tagged documented or folklore.
- [02-design-approaches.md](02-design-approaches.md) — six design approaches with artefacts, CPU, latency and aliasing consequences; neutral shortlist.
- [10-dsp-spec.md](10-dsp-spec.md) — chosen topology (dynamic-EQ cut driven by a level-independent prominence detector), detection math, gain computer, time-varying filter, timings, metering, target values.
- [11-integration-and-test-plan.md](11-integration-and-test-plan.md) — module layout and registration checklist, name and accent candidates, parameter table, panel verdict, how to build the test suites, milestones.

## Top 5 open decisions

1. **Display name.** Module id `deesser` and presets `.bmodeesser` are
   proposed; the bundle id derives from the display name, so the name is
   permanent. 11 lists four candidates with their bundle ids, and asks whether
   the name carries a hyphen.
2. **Accent.** Only violet-magenta and red-pink hues still pass the hue rule.
   Four passing candidates, no exception needed: `#e694e0`, `#eb9fe2`,
   `#e79de7`, `#ea9f9a`. Permanent.
3. **Accept the topology and its one mode parameter.** A dynamic-EQ cut, bell
   or high shelf, zero latency, no lookahead, no oversampling. The shelf stands
   in for split-band with no crossover error. Wideband mode, a crossover and a
   mix control are all left out of v1; modes are permanent parameters, so
   adding one later is an append, not a reshuffle.
4. **Fixed attack and release, and an ADAPT control.** 10 fixes the times
   (0.8 ms attack; 30 ms release with a 120 ms slow branch) rather than
   exposing them, and exposes ADAPT, the blend between a fullband reference
   and the band's own 500 ms average. Confirm six parameters is the v1 set;
   attack and release can be appended later, never inserted.
5. **Listen is a momentary state, not a parameter, and there is no spectrum
   display in v1.** Listen follows BMO DEQ's band-solo precedent, so it is not
   automatable and is not saved. The panel gets a static band sketch instead of
   an analyser.

## Known soft spots

- No peer-reviewed paper on ML sibilance detection was found; vendor claims are marked unverified, and one spectral de-essing paper is cited by title only.
- The detector's guards against bright non-vocal material and cymbal bleed are reasoned, not measured. Everything marked CALIBRATE needs measurement and ear.
- The meter reports peak band reduction, not a wideband-equivalent figure; that is a choice, stated in 10.
- 10 and 11 were written concurrently. Where they disagree on a parameter, settle it before the schema is frozen.
