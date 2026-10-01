# BMO Dwell — delay module groundwork

Groundwork pack for the rack's delay module: display name **BMO Dwell**, plugin
code `Bdly`. Spec, theory, math and test direction only — no DSP code. Written
by dispatched agents on AURORA, 2026-09-20, under `HANDOFF-groundwork.md`.

## Index

- `00-repo-conventions.md` — delay-specific repo delta: reusable DSP, no host tempo reaches modules today, tail and latency rules, free accent gaps.
- `01-reference-behavior.md` — documented behaviour of tape echo, bucket-brigade, early digital and modern delays; target figures with confidence.
- `02-design-approaches.md` — interpolation, time-change, feedback loop, stereo, ducking and character-modelling survey; neutral shortlist.
- `10-dsp-spec.md` — the chosen topology: **one engine instantiated twice** (the main delay and the lane) with clean / tape / bucket-brigade modes, no oversampling, zero reported latency, dry-held-to-50% MIX law, the lane's SEND / HOLD / CHOP gates and its bipolar tail, and an in-loop FX stage per engine. Owns DSP meaning and fixed values. §11 was rewritten on 2026-09-22 (THROW, BUILD, FREEZE and VOICE are gone) and §11.3 again on 2026-09-23 (the voicing is shared; LINK is gone).
- `11-integration-and-test-plan.md` — identity row, registration, the permanent parameter table (**27 rows, ids 0–26**, cut back and then extended with `lane_note` on 2026-09-23) and its order, and how to build the test suites. Owns ids, ranges and choice lists.
- `12-tempo-and-tail-plumbing.md` — host tempo and tail-length plumbing, processor → rack → module; can land as its own PR first.
- `13-panel-direction.md` — **SUPERSEDED by `15` and by the panel as built**; kept for its account of DEQ's expansion mechanism, its caption and readout conventions, and its permanent-at-ship list. Its layout, performance buttons and accent recommendation are history; the specific wrongs are marked in place (2026-09-22).
- `14-calibration-and-listening.md` — how every CALIBRATE and DECISION value gets settled, and what freezes at ship.
- `15-lane-redesign.md` — **read before `10`, `11` and `13`**: the nine-control face, THROW rebuilt as a parallel lane, the schema changes, the tape stability bug, what stage 2 now is, and **"The module was pulled back" (2026-09-23)** — why the lane shares the main's voicing and why splitting into two modules was deferred.
- `20-name-clearance.md` — name-clearance note for "Dwell": web search plus a USPTO registry search; not legal advice.
- `HANDOFF-groundwork.md` — the ruleset that produced this pack.
- `HANDOFF-add-bmo-dwell.md` — the prompt that starts the build from this pack.

## Decided (Frosty, 2026-09-20)

1. **SYNC** — the SYNC and NOTE slots and the note-list order are reserved in the permanent schema now; the feature ships, enabled, when the tempo plumbing (`12`) lands. *(Items 31–33 add `lane_note` beside them, on the same switch.)*
2. **FREEZE** — its own button and parameter slot (row 16), enabled in v1; not folded into THROW.
3. **Maximum delay 2000 ms** (4.0 MB per instance at 192 kHz — *superseded by
   item 36: two engines and the compander's control rings make it 16 MB*).
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
11. ~~**The lane is a full mirror** of the main delay, with **LINK**, and
    unlinking **seeds from the main's current values**.~~ **REVERSED by item 28
    (2026-09-23)**: the lane **shares** the main's voicing, LINK is deleted and
    nothing is seeded anywhere. The lane keeps its TIME, LEVEL, tail, three
    gates and its own FX.
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
    **Superseded 2026-10-01: Pikachu yellow `#f8d030`**, with a charcoal
    light-ground ink and cheek-red gates, after BMO Linger merged 2.1 degrees
    from the orchid. `products/AGENTS.md` has the row.

## Decided (Frosty, 2026-09-21, second pass) — the table is settled

17. ~~**The schema is 33 parameters**, ids 0–32, with id 32 outside the rack's
    lanes.~~ **SUPERSEDED by items 28 and 31: it is 27, ids 0–26, and nothing
    sits outside the lanes.** The full table is in `15-lane-redesign.md` and in `11`
    §3.
18. **VOICE and lane VOICE are cut.** LO CUT and HI CUT are already continuous
    sweeps; VOICE only added resonance on top. Cutting it also retires the
    state-variable filters and their closed-form peak normalisation.
19. **`fx_type` loses Octave up, Octave down and Reverse**, leaving Diffuse,
    Sweep, Pan/Tremolo and Crush. Octaves compound in a feedback loop; Reverse
    was the only type needing a second buffer. Choice lists are append-only
    after ship, so this was the last moment. **Superseded in part by item 26:
    Sweep went too, and the list is three.**
20. **lane DRIVE is cut, and is the one to reconsider** if the sound wants it —
    appends are permitted after ship. *(Moot from item 28: `drive` governs both
    engines, so there is nothing to append.)*
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
    would add up to +3.67 dB on a 20 dB transient, in-loop, on every lap. That
    figure is **MEASURED** — benched on AURORA, 2026-09-23, at 0.184 dB per dB,
    landing exactly on the model — and the clamp fallback remains Frosty's
    decision. The stored-gain fix costs a **control ring as long as the audio
    ring** (item 36).

## Decided (Frosty, 2026-09-22) — answering the spec rewrite's open questions

24. ~~**LINK ties SIX parameters**, the lane's voicing (ids 23–28), with the FX
    trio deliberately outside it.~~ **SUPERSEDED by item 28: LINK is deleted
    with the six rows it tied.** The reason it was kept out of FX still stands
    and is why `fx_link` survived the cut.
25. ~~**`fx_link` is added at id 32** and is **off-lane on purpose** — *"leave
    this separate fx link off a lane in case it needs to be cut later"* — the
    one Dwell parameter that is not rack-automatable.~~ **AMENDED by item 28**:
    `fx_link` stays, now at **id 26**, and is an **ordinary on-lane parameter** —
    there is no overflow for it to sit in. It moved again to **id 26** when
    `lane_note` was added (item 31), and still ties the lane's FX trio (23–25)
    to the main's (17–19), default on.
26. **Sweep is cut**, leaving `fx_type` at **three** — Diffuse, Pan/Tremolo,
    Crush. **It was cut because VOICE was cut**: Sweep moved VOICE's resonant
    centre per repeat, and replacing that with a band-pass of its own inside the
    FX stage would reintroduce the filter item 18 had just removed. Lists are
    append-only after ship, so this was the last moment.
27. **The lane's build ceiling `g_max` is a CALIBRATE value**, heard in `14`
    §3's listening round rather than fixed on paper. The safety clip bounds the
    lane at every value in the range, so this is a musical choice, not a
    stability one.

## Decided (Frosty, 2026-09-23) — the module is pulled back

28. **The lane shares the main delay's voicing instead of mirroring it**, and
    the schema fell to 26 before item 31 took it to **27, ids 0–26**. `link`,
    `lane_character`, `lane_stereo`,
    `lane_low_cut`, `lane_high_cut`, `lane_mod_rate` and `lane_mod_depth` are
    **deleted** — seven rows — and everything after them renumbers with no
    holes. `character`, `stereo`, the cuts, the modulation and `drive` govern
    **both engines**; **DUCK is main-engine only**; MIX governs both. The lane
    keeps TIME, LEVEL, its tail, SEND / HOLD / CHOP and its own FX.
    **Why**: the module had reached 33 parameters, a 980 px three-column panel
    and one row pushed off the rack's 32 lanes, and controls were being cut **to
    fit a budget rather than on merit** — VOICE, lane DRIVE and Sweep all went
    that way. That is one module doing two modules' work. Frosty reeled it in
    rather than splitting it.
29. **`fx_link` is on-lane and nothing is seeded.** Every parameter is
    rack-automatable again and Dwell uses no `SlotOverflow`. With LINK gone
    there is nothing to seed, and `fx_link` needs no gesture — it overwrites
    nothing, so the lane's FX values are still there when the tie releases.
30. **Stage 2 builds one reusable delay engine instantiated twice**, not a
    bespoke dual engine (`10` §11.1). Chosen so Dwell **can be split into a
    plain delay and a throw delay later** without redoing the expensive part.
    **Splitting now was considered and deferred**: two modules in a rack run
    **in series**, so a separate throw module would catch the main delay's
    output rather than the dry signal, losing the parallel-from-dry topology
    the lane exists for.
31. **`lane_note` is added at id 22**, the same sixteen divisions, **default
    index 6, "1/8"**, shipping disabled with SYNC and NOTE. The schema is **27,
    ids 0–26** — `lane_fx`, `lane_fx_type`, `lane_fx_amount` and `fx_link` shift
    up one to 23, 24, 25 and 26 — with **five rack lanes spare**. **Why**: the
    lane had its own TIME and no division, so the moment `12` lands the main
    delay would lock to the grid while the lane free-ran in milliseconds and
    drifted against it, which is exactly what the lane's rhythm cannot survive.
32. **There is ONE sync switch (id 1) and it governs BOTH engines.** The module
    is either on the grid or it is not; each engine then picks its own division.
    A separate `lane_sync` was rejected: wanting the main synced while the lane
    free-runs is a strange thing to want, and turning SYNC off for the module
    and setting both times in milliseconds gets it.
33. **The millisecond and note defaults agree at 120 BPM, by design.** `time`
    375 ms ↔ `note` 1/8D, `lane_time` 250 ms ↔ `lane_note` 1/8. So **enabling
    SYNC at 120 BPM changes nothing audible** — the silent-toggle property
    `modules/vcomp`'s COMPLEX was built around, and the reason `lane_note`
    defaults to 1/8 rather than copying the main's 1/8D.

## Decided and measured (2026-09-23) — what building stage 2b turned up

Stage 2b is built and committed at **c4d2d33**; these four came out of building
it, and three of them were things this pack had wrong or hedged.

34. **The DC blocker goes AFTER the shaper** — a real bug, not a preference.
    Before it, LOW CUT has already taken the DC so the blocker does nothing,
    while the asymmetric shaper's own offset goes into the ring and compounds
    per lap. Measured **−38.7 dBFS at DRIVE 100**, which fails `11` §4c;
    **−114.3 dBFS** with it moved. `P_c` is unchanged, because it is swept at
    DRIVE 0 where the shaper branches out. `10` §4 says so now, and says why,
    so nobody restores the old order.
35. **The compander overshoot is measured, not modelled.** A re-detecting pair
    benches at **+3.67 dB on a 20 dB step = 0.184 dB per dB** (AURORA,
    2026-09-23), confirming the model exactly. **That was one of the two parked
    benches; the CPU one is now done too** — see item 39.
36. **Memory is 16 MB per instance at 192 kHz**, not 8.0. The compander's
    control ring is the same length as the audio ring, per channel per engine,
    and is allocated whichever character is selected — so two engines means two
    audio rings and two control rings. Eight Dwells in a rack is ~130 MB.
37. **TAPE gets a character floor; the other two do not** (Frosty). **0.03 % of
    speed, about half a cent**, riding MOD RATE and **summing** with MOD DEPTH,
    so tape is never dead still even at depth zero — a CALIBRATE value, settled
    by ear in `14`. **Bucket-brigade gets none**: its signature is the
    time-dependent darkening as the clock slows plus the compander's breathing,
    both present with no modulation at all. **Clean gets none.** The floor
    **modulates the read position, never the output**, so a zeroed ring reads
    zero and silence stays silent structurally rather than by a gate. It was
    built before it was specified, which is how it fell through the first time;
    it is `10` §5a now.
38. **The tape shelf's corner convention is settled: 55 Hz is the pole**, which
    gives the 1.054 already written into `10` §3's acceptance. Measured `P_c`:
    clean **0.99939**, tape **1.05361 at 63.4 Hz**, bucket-brigade **0.99611**
    (0.9946–0.9983 with TIME), 48 kHz — unity at **97.0 % on all three**.
39. **The CPU budget is an absolute ceiling plus a regression guard, not a
    ratio** (DECIDED, Frosty 2026-09-23). The old "≤ 1.5× at defaults, ≤ 3.0×
    heaviest" named a mode and a tool that do not exist and never said what its
    denominator was set to — one silence worth 2.7× — and it was breached on
    every reading (6.81× and 23.74×). It is replaced by a percentage of one
    core per instance, stereo, block 512: **≤ 1.15 % at defaults and ≤ 4.0 % at
    the heaviest at 48 kHz, ≤ 4.1 % and ≤ 15.0 % at 192 kHz**, with separate
    rows for a control automated per block, plus **no configuration more than
    1.15× its recorded baseline**. **MEASURED on AURORA, 2026-09-22** at
    720b8b7: 0.880 % at defaults, 3.062 % at the heaviest, 48 kHz. Every
    ceiling is baseline × 1.30, which is three times the method's worst noise
    and still tight enough for the guard to fire first; `11` §4k carries the
    conditions, the full baseline and the argument. **The heaviest case is
    Clean + Diffuse, not bucket-brigade** — Clean's sinc is the single
    largest cost in the module. **Its taps came down from 32 to 24 on
    2026-10-01** (`10` §1), and the baseline, taken at 32, is owed a downward
    re-base. **The
    lane costs nothing at the defaults** (HOLD ships off and the second engine
    genuinely does not run) and **84–102 % when it runs**. `11` §4l's ≤ 1.3×
    per FX type **holds** — Diffuse 1.200×, Pan/Tremolo 1.022×, Crush 0.949×.

## Still open after that pass

- **`chop`'s 1 ms fade against `11` §4's broadband −60 dBFS assertion** (`10`
  §11.4). It needs a **measurement**, not a ruling: band-limit the assertion to
  5 kHz, lengthen the fade to ~3 ms, or record the measured edge.
34. **CHOP's acceptance is band-limited to 5 kHz** (2026-09-22), and the fade
    stays at 1 ms. A broadband −60 dBFS assertion and a 1 ms raised-cosine gate
    cannot both stand; tightness is what a rhythmic gate is for, and the ~3 ms
    a broadband figure needs costs sixteenths above ~160 BPM. Above 5 kHz the
    edge is measured and recorded by `14`, not asserted away.
