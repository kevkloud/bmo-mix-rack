# BMO Dwell — delay module groundwork

Groundwork pack for the rack's delay module: display name **BMO Dwell**, plugin
code `Bdly`. Spec, theory, math and test direction only — no DSP code. Written
by dispatched agents on AURORA, 2026-09-20, under `HANDOFF-groundwork.md`.

## Index

- `00-repo-conventions.md` — delay-specific repo delta: reusable DSP, no host tempo reaches modules today, tail and latency rules, free accent gaps.
- `01-reference-behavior.md` — documented behaviour of tape echo, bucket-brigade, early digital and modern delays; target figures with confidence.
- `02-design-approaches.md` — interpolation, time-change, feedback loop, stereo, ducking and character-modelling survey; neutral shortlist.
- `10-dsp-spec.md` — the chosen topology: one loop with clean / tape / bucket-brigade modes, no oversampling, zero reported latency, dry-held-to-50% MIX law, THROW / BUILD, VOICE filters, FREEZE. Owns DSP meaning and fixed values.
- `11-integration-and-test-plan.md` — identity row, registration, the permanent parameter table (ids 0–17) and its order, and how to build the test suites. Owns ids, ranges and choice lists.
- `12-tempo-and-tail-plumbing.md` — host tempo and tail-length plumbing, processor → rack → module; can land as its own PR first.
- `13-panel-direction.md` — control hierarchy, layout, readouts, meter, accent. Owns layout and captions.
- `14-calibration-and-listening.md` — how every CALIBRATE and DECISION value gets settled, and what freezes at ship.
- `HANDOFF-groundwork.md` — the ruleset that produced this pack.

## Decide before the schema commit (permanent once shipped)

1. **SYNC in v1?** Tempo plumbing (`12`) does not exist yet. Recommended: reserve the SYNC and NOTE slots and the note-list order now regardless; ship SYNC when the plumbing lands.
2. **FREEZE.** Its own button and parameter slot, or the top of THROW's travel? In v1, or slot reserved and shipped disabled?
3. **Frozen sound values.** Maximum delay 2000 ms (4.0 MB per instance at 192 kHz) or 1000 ms; feedback top of travel at loop gain 1.05 (self-oscillation reachable) or clamped below 1. No measured class figure exists for either — they are our call.
4. **MIX at 50% sums full dry and full wet with no makeup** (about +3 dB typical, +6 dB worst). Any auto-trim would break the bit-exact dry path. Confirm no auto-gain.
5. **Identity.** Module id `dwell` or `delay`; and "Dwell" has not been trademark-searched — do that before the identity row ships.

## Decide during the add-bmo-dwell build (see or hear it first)

- VOICE resonance as a ring on HIGH CUT or a fourth knob row; and whether the `voicing` choice list in `11` stays (`10` defines only a continuous VOICE) — that one is schema, settle it with decision 2.
- Accent: gold gap (`13`) or the red / magenta candidates (`11`); settle with the de-esser session, which wants a gap too.
- THROW strictly momentary or modifier-click latch; `throwMode` order (Send open → Throw → Build).
- Ping-pong on a stereo source: sum to mono (specified) or keep L/R; dual-offset ratio fixed at 2/3.
- Whether the accumulated alias floor (target ≤ −60 dBFS at 10 repeats) forces a half-band stage into the loop.
