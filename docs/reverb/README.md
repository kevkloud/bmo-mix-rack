# Reverb — groundwork pack

Spec, theory, math and test direction for an algorithmic reverb module (plugin
code `Brvb`, reserved; preferred name **BMO Linger**). No implementation code
lives here; the dev team writes it from this pack. Assembled on AURORA,
2026-09-20 and 2026-09-21, from agent research; nothing in it has been built,
measured or heard.

The thesis: the sound of Reference B with the functionality of Reference A, and
an early-reflections section good enough to use on its own. Third-party
products appear under neutral labels; real names live only in each file's
sources key.

## Index

- [00-repo-conventions.md](00-repo-conventions.md) — the reverb-specific delta: reusable code, what a module does and does not get from the host, free accent hues.
- [01-reference-software.md](01-reference-software.md) — the two reference plugins: types, controls, how each handles early reflections, what to take from each.
- [02-reference-hardware.md](02-reference-hardware.md) — classic hardware reverbs, how each makes early reflections, a shortlist of characters.
- [03-early-reflections.md](03-early-reflections.md) — what mix engineers do with early reflections (sourced and unsourced kept apart) and the design requirements that follow.
- [04-design-approaches.md](04-design-approaches.md) — late-reverb architectures and early-reflection generators, with costs.
- [05-er-psychoacoustics-citations.md](05-er-psychoacoustics-citations.md) — primary-source citation dossier, with a folklore table.
- [10-dsp-spec.md](10-dsp-spec.md) — 8-line feedback delay network tail, image-source taps into an allpass-free diffuser, the density control, six v1 types, tail-length formula, budgets.
- [11-integration-and-test-plan.md](11-integration-and-test-plan.md) — identity, shared-code changes, accent, the frozen parameter order, panel split, test direction, milestones.

## Open decisions

1. **Lock the name.** BMO Linger (`com.lt3audio.bmolinger`) came back clear in
   a name-collision scan (not a trademark opinion). Module id `reverb`
   (`.bmoreverb`) is recommended; 10 assumes `rvb`. Both are permanent.
2. **Accent, and who gets violet.** Only about 298-309 degrees still passes the
   hue rule, and BMO Dwell wants the same window. Candidates: `#dd93dd`,
   `#eb8ae9`, `#e8a2e5`, `#e694e0`. One of the two modules will need another
   answer or an exception.
3. **Thirty parameters on one module.** 10 says 29 and 11 says 30; settle the
   count, then accept or cut. 11 proposes 7 controls plus a display on the main
   face and the rest in an expanded section (EARLY / TAIL / TONE & OUT). The
   order is permanent and the type list is append-only: Room, Chamber, Hall,
   Large Hall, Plate, Ambience.
4. **Three shared-code changes.** Tail-length reporting (every module reports 0
   today; the rack would sum its slots) is needed in v1. Mono-in to stereo-out
   is deferred, with a fallback. Host tempo is left out of v1 and should land
   once, byte-identically, with BMO Dwell.
5. **Era colour is not in v1.** Each type reserves the fields so a later
   voicing switch changes no ordinals. Confirm, or promote it now.

## Known soft spots

- Nothing public documents Reference B's internals; its character is fitted by ear (CALIBRATE).
- The flagship hardware's control ranges were not confirmed from a manual.
- The famous "early reflections only, tail off" depth technique is second-hand; no primary source was found.
- No CPU budget exists anywhere in the repo; 10's is an estimate, and this will be the rack's heaviest module.
- 10 and 11 ran over their word guides rather than cut math or tables.
