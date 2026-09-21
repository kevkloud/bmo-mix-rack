# BMO Dwell — delay module groundwork

Groundwork pack for the rack's delay module: display name **BMO Dwell**, plugin
code `Bdly`. Spec, theory, math and test direction only — no DSP code. Written
by dispatched agents on AURORA, 2026-09-20, under `HANDOFF-groundwork.md`.

## Index

- `00-repo-conventions.md` — delay-specific repo delta: reusable DSP, no host tempo reaches modules today, tail and latency rules, free accent gaps.
- `01-reference-behavior.md` — documented behaviour of tape echo, bucket-brigade, early digital and modern delays; target figures with confidence.
- `02-design-approaches.md` — interpolation, time-change, feedback loop, stereo, ducking and character-modelling survey; neutral shortlist.
- `10-dsp-spec.md` — the chosen topology: one loop with clean / tape / bucket-brigade modes, no oversampling, zero reported latency, dry-held-to-50% MIX law, THROW / BUILD, VOICE filters, FREEZE, and the in-loop FX stage. Owns DSP meaning and fixed values.
- `11-integration-and-test-plan.md` — identity row, registration, the permanent parameter table (ids 0–19) and its order, and how to build the test suites. Owns ids, ranges and choice lists.
- `12-tempo-and-tail-plumbing.md` — host tempo and tail-length plumbing, processor → rack → module; can land as its own PR first.
- `13-panel-direction.md` — control hierarchy, layout, readouts, meter, accent. Owns layout and captions.
- `14-calibration-and-listening.md` — how every CALIBRATE and DECISION value gets settled, and what freezes at ship.
- `20-name-clearance.md` — name-clearance note for "Dwell": web search plus a USPTO registry search; not legal advice.
- `HANDOFF-groundwork.md` — the ruleset that produced this pack.

## Decided (Frosty, 2026-09-20)

1. **SYNC** — the SYNC and NOTE slots and the note-list order are reserved in the permanent schema now; the feature ships, enabled, when the tempo plumbing (`12`) lands.
2. **FREEZE** — its own button and parameter slot (row 16), enabled in v1; not folded into THROW.
3. **Maximum delay 2000 ms** (4.0 MB per instance at 192 kHz).
4. **Feedback top of travel is loop gain 1.05**; the ~97–100% self-oscillation zone is accepted and marked on the panel. BUILD depends on it.
5. **No auto-gain at MIX 50%** — the bit-exact dry path wins (about +3 dB typical, +6 dB worst).
6. **Module id `dwell`.**
7. **VOICE** — one continuous control as `10` defines it; the stepped `voicing` list is dropped.
8. **In-loop FX** — reserved in the permanent schema now (`fx`, `fxType`, `fxAmount`). **Tied but not the same** (DECIDED, Frosty 2026-09-20): `fx` (id 17) is the sound, on the main panel; a separate on-panel arrow, never automatable or in presets, opens the expanded section where `fxType` and FX AMOUNT live, and turning `fx` off never closes it. With FX off the stage is skipped at zero CPU cost. The FX types are **candidates for testing** — the list and its order stay free until ship, then are append-only forever, so anything that fails listening (`14` §3, L3) comes out first.

## Still open before the schema commit

- Name clearance (`20`): the USPTO search found no live DWELL mark in audio software or musical instruments (risk low–medium). EU, UK, WIPO and unregistered use are not checked; decide whether that is enough before the identity row ships.

## Decide during the add-bmo-dwell build (see or hear it first)

- VOICE resonance as a ring on HIGH CUT or a fourth knob row.
- Accent: gold gap (`13`) or the red / magenta candidates (`11`); settle with the de-esser session, which wants a gap too.
- THROW strictly momentary or modifier-click latch; `throwMode` order (Send open → Throw → Build).
- Ping-pong on a stereo source: sum to mono (specified) or keep L/R; dual-offset ratio fixed at 2/3.
- Whether the accumulated alias floor (target ≤ −60 dBFS at 10 repeats) forces a half-band stage into the loop.
