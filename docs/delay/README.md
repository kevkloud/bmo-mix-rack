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
- `15-lane-redesign.md` — **read before `10`, `11` and `13`**: the nine-control face, THROW rebuilt as a parallel lane, the schema changes, the tape stability bug, and what stage 2 now is.
- `20-name-clearance.md` — name-clearance note for "Dwell": web search plus a USPTO registry search; not legal advice.
- `HANDOFF-groundwork.md` — the ruleset that produced this pack.
- `HANDOFF-add-bmo-dwell.md` — the prompt that starts the build from this pack.

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

## Decided (Frosty, 2026-09-21) — see `15-lane-redesign.md`

9. **The face is nine controls**; everything else is revealed, and the split is
   visibility only — **no DSP gate**, every parameter stays live.
10. **THROW becomes a parallel lane** running simultaneously with the main
    delay: its own TIME, fed by SEND, life-gated by HOLD (off **clears**),
    output-gated by CHOP, tail set by one bipolar knob whose sticky centre is
    unity. SEND onto an occupied lane **sums**.
11. **The lane is a full mirror** of the main delay — its own character, stereo,
    filters, voice, modulation, drive and FX — with **LINK**, and unlinking
    **seeds from the main's current values**.
12. **This supersedes item 2**: the old bit-exact FREEZE is replaced by the
    lane's centre detent, which holds at unity but still laps the character and
    filters, so a long hold colours. The non-eroding hold is gone from v1.
13. **DUCK keeps id 12 but defaults to 0**, ships inert and opt-in, and its
    detector's key high-pass is **fixed at build time** with no parameter
    reserved.
14. **"Dwell" ships on the USPTO search alone** — EU, UK, WIPO and unregistered
    use unchecked, risk accepted.

15. **The lane gets its own LEVEL** (2026-09-21): `-24…+24 dB`, default 0,
    matching the suite's other level controls. `laneGain` sets the lane's tail,
    not its loudness; without a level the thrown word's volume against the main
    delay would be fixed, which defeats the point of an emphasis path.
16. **The accent is the orchid `#f094e6`** (2026-09-21), measured 6.49:1 dark
    and 1.81:1 pale off a render. It spends the last wide hue arc in the rack.

## Decided (Frosty, 2026-09-21, second pass) — the table is settled

17. **The schema is 32 parameters**, ids renumbered, every one inside a rack
    slot's 32 host lanes so all are automatable everywhere. The full table is
    in `15-lane-redesign.md` and is authoritative over `11` §3 until that is
    rewritten.
18. **VOICE and lane VOICE are cut.** LO CUT and HI CUT are already continuous
    sweeps; VOICE only added resonance on top. Cutting it also retires the
    state-variable filters and their closed-form peak normalisation.
19. **`fx_type` loses Octave up, Octave down and Reverse**, leaving Diffuse,
    Sweep, Pan/Tremolo and Crush. Octaves compound in a feedback loop; Reverse
    was the only type needing a second buffer. Choice lists are append-only
    after ship, so this was the last moment.
20. **lane DRIVE is cut, and is the one to reconsider** if the sound wants it —
    appends are permitted after ship.
21. **Both FX buttons stay.** "Amount at 0 means bypassed" was considered and
    rejected: it costs the one-click A/B, and Crush's bit depth does not read
    zero as a no-op.

22. **The tape stability bug is FIXED in the spec** (2026-09-21). `10` §3 now
    normalises the feedback law by each character's reference loop peak `P_c`,
    computed at `prepare` by sweeping the built coefficients — never hardcoded.
    Unity is 97.0 % on **every** character, so the panel carries one tick. The
    earlier "unity at 84 %" figure was wrong: that is the shelf's nameplate in
    isolation, but the 10 Hz blocker and LOW CUT eat its asymptote, so tape's
    real chain peak is **1.054 at 63 Hz, unity at 93.9 %** before normalisation.
    Normalising by the nameplate 1.2589 would have made tape *decay* 1.55 dB a
    lap.
23. **Bucket-brigade stays** (Frosty, 2026-09-21). Its filters were cleared —
    Butterworth at `Q = 1/√2`, chain peak 0.990–0.999 — but its compander is now
    specified with the expander reading the compressor's stored gain rather than
    re-detecting, so the pair is unity through transients. A re-detecting pair
    would add up to +3.7 dB on a 20 dB transient, in-loop, on every lap. That
    figure is **modelled, not measured — bench it**, and the clamp fallback
    remains Frosty's decision.
