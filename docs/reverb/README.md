# BMO Linger — groundwork pack

Spec, theory, math and test direction for an algorithmic reverb module. No
implementation code here; the dev team writes it from this pack. Assembled on
AURORA, 2026-09-20/21; nothing in it has been built, measured or heard.

## Decided

Display name **BMO Linger** · module id **`reverb`** · bundle
**`com.lt3audio.bmolinger`** · presets **`.bmoreverb`** · plugin code **`Brvb`**
· accent **`#e694e0`** · BMO line. Permanent from first ship. The id and the name
differ deliberately, as `deesser` is to BMO Defang.

The thesis: Reference B's sound with Reference A's functionality, and an ER
section good enough to use alone. Third-party products appear only under neutral
labels.

## Index

- [00-repo-conventions.md](00-repo-conventions.md) — reverb-specific delta: reusable code, what the host gives a module, free accent hues.
- [01-reference-software.md](01-reference-software.md) — the two reference plugins, and what to take.
- [02-reference-hardware.md](02-reference-hardware.md) — classic hardware reverbs; a shortlist of characters.
- [03-early-reflections.md](03-early-reflections.md) — what engineers do with early reflections, and the requirements.
- [04-design-approaches.md](04-design-approaches.md) — late-reverb architectures and ER generators, with costs.
- [05-er-psychoacoustics-citations.md](05-er-psychoacoustics-citations.md) — citation dossier and folklore table.
- [10-dsp-spec.md](10-dsp-spec.md) — 8-line FDN tail, image-source taps into an allpass-free diffuser, density, six types, budgets.
- [11-integration-and-test-plan.md](11-integration-and-test-plan.md) — identity, shared-code changes, accent, parameter order, panel split, tests, milestones.

## Open decisions

1. ~~**Accent, and who gets violet.**~~ **Settled 2026-09-21 on AURORA: Linger
   takes `#e694e0`** (V4, the window centre), chosen by Frosty from a rendered
   proof sheet rather than from hex. Linger spends the only admissible arc, so
   BMO Dwell needs a different hue — see 11 §3 for the arithmetic and for the
   Tune RT omission in Dwell's own conventions doc.
2. **Thirty parameters on one module.** Both packs now say 30, one permanent
   order; the count settled when `inhicut` (IN HI-CUT) proved to be the
   difference and was kept, marked "owner confirm" — accept or cut it before
   first ship. Main face takes 7 controls plus a display. Type list append-only:
   Room, Chamber, Hall, Cavern, Plate, Ambience.
3. **Three shared-code changes.** Tail-length reporting (every module reports 0
   today; the rack would sum its slots) is in v1, as decided. **Mono-in to
   stereo-out is also in v1** — Frosty's call on 2026-09-21, reversing the
   deferral: it is a bus-contract change in both processors, not a DSP one, and
   BMO Dimension's mono path cannot be reused for it (Dimension early-returns on
   a mono bus by design). Host tempo stays out of v1 and should land once,
   byte-identically, with BMO Dwell.
4. **Era colour is not in v1.** Each type reserves the fields so a later
   voicing switch changes no ordinals.

## Known soft spots

- Nothing public documents Reference B's internals; its character is fitted by ear (CALIBRATE).
- The flagship hardware's control ranges were not confirmed from a manual.
- The "ER only, tail off" depth technique is second-hand; no primary source found.
- No CPU budget exists in the repo; 10's is an estimate, and this is the rack's heaviest module.
