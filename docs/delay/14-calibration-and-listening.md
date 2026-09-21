# BMO Dwell — calibration and listening protocol

Written on AURORA, 2026-09-20. Procedure only, no code. Every CALIBRATE and DECISION
value in `docs/delay/10-dsp-spec.md` §12, against the rated targets in
`01-reference-behavior.md`. Method: `testing-notes/blind-listening-protocol.md`.

## 1. The open values

M = measure (§2), L = listen (§3), F = Frosty's call.

| Value, where to start, what it controls | How it settles, and the acceptance |
|---|---|
| Hermite HF loss, spec estimate, repeat darkening | M1: within 1 dB at 0.1/0.25/0.4·f_s, monotonic |
| Glide τ / cap, 120 ms / 0.25, tape pitch bend | M2, L2: pitch, never a jump; ρ within ±4 semitones |
| Clean crossfade, 20 ms, time change | M8, L2: no click, ≤0.5 dB dip, no flam |
| LOW/HIGH CUT, as spec'd, loop tone | M6: extremes stable; cap holds at every rate |
| VOICE Q, 0.5–6 (`0.5 + VOICE·5.5`), loop resonance | M6: unit peak at every Q; no growth |
| Telephone point, 300/3400 Hz, VOICE landmark | M6, L1: on the detent, clear in one repeat |
| Tape LP / bump, 4.5 kHz / +2 dB, tape voicing | L1: beats or ties two neighbours, blind |
| BBD compander, 2:1, 5/50 ms, noise vs pumping | M4, L1: floor −10 dB, no breathing on a pad |
| Flutter, 11.7 Hz, tape flutter | M2, L1: inside 10–100 Hz; not vibrato |
| Clean mod depth, 0–8 ms, chorus width | L1: chorus not detune; zero is still |
| Duck detector, 5/180 ms, −30 dB, repeats stepping aside | M9, L1: recovered before the next phrase |
| THROW ramp, 5 / 15 ms, gate edges | M8, L2: catches one word; no click |
| BUILD, 1.02, 400/800 ms, held-throw swell | M3, L2: swells inside a bar; no step |
| Dual-offset, 2/3, L/R spread | F, shown L1: he names one; recorded as a decision |
| Feedback law, `1.05·fb^1.6`, repeats, self-oscillation | M3, F: 2–8 repeats over ≥30% travel; onset ≈97%; bounded above unity |
| Alias floor, ≤ −60 dBFS, shaper quality | M5: met, or halfband added and re-measured |
| Max delay, 2000 ms (BBD 1500), memory vs reach | F: fixed before allocation is written |
| Mix law, `w = sin(πm)` / `d = cos(π(m−0.5))`, dry unity ≤50% | M7: dry bit-exact to 50%; sum stated on the panel |
| FREEZE, gain 1.0, all stages bypassed, whole-sample length | M3: no drift over 60 s; DC at the latch logged |
| THROW modes, Send open / Throw / Build, performance gate | M8, L2: Send open bit-exact; each mode measured |

Open decisions 3–5 in 10's closing list are Frosty's alone. Do not settle them by ear.

## 2. Measurements

From a tool in the tree driving the shipping path, deterministic, 48 kHz, repeated
at 44.1 and 192 kHz. One table per procedure.

- **M1 repeat HF loss.** Log sweep, or a noise burst shorter than T; magnitude of
  repeat k against k−1, per interpolator and mode. Force fractional phases; the
  whole-sample phase is flat by construction and flatters the result.
- **M2 wow/flutter.** Instantaneous pitch of a steady tone: peak deviation percent,
  rate in Hz. Verify `clamp(T/300 ms, 0.5, 2)` at three delay times.
- **M3 decay.** Impulse; RT60 per FEEDBACK setting in 10% steps, and
  repeats-to-inaudible. Above unity must settle to a limit cycle under the ceiling,
  the reported tail within one repeat of measured. Same run covers BUILD, and FREEZE
  held 60 s.
- **M4 saturation per repeat.** Level and THD of repeat k, k = 1…10, min/mid/max
  DRIVE, both modes. **M5 alias floor** is that run's non-harmonic floor at repeat
  10, max DRIVE.
- **M6 loop filters.** Sweep both cutoffs at high feedback, VOICE 0/0.6/1; log the
  resonance peak at every extreme. Nothing may grow.
- **M7 dry null.** Output against input at every MIX ≤ 50%, TIME beyond the block,
  feedback zero and maximum. Bit-exact, not −100 dB.
- **M8 gate edges.** THROW ramp times, peak sample step at both edges, crossfade
  dip. Per mode.
- **M9 ducking.** Wet bed, dry bursts: GR depth, attack, release, against 5/180 ms
  and 01's 2–4 dB figure.

A measurement reading zero for what it tests invalidates its round.

## 3. Listening

Blind, level-matched, rendered from the real DSP, **a different shuffle seed per
source**, key beside the audio, audio outside the repository, 32-bit float.

- **L1 voicing**: three candidates, **one entered twice**. The gap between the
  duplicates is the round's noise floor; a smaller separation decides nothing (the
  Opto lesson). One control pair per source, first.
- **L2 gesture**: glide, crossfade, throw, build; moving material only.
- **Sources**, all four every round: vocal throws, drums, a sustained pad, a mono
  guitar. Ping-pong and dual-offset also on the mono bus.
- **The winner**: one bold line per pair, the listener comments, the session writes
  it in; the key opens once every line is filled; decode and verdict in the same
  file, dated and named. Three files per round.
- **A result holds only for the machine and monitoring it was heard on.** Name it in
  the run record and the verdict. This pack was written on AURORA.

## 4. Order

On paper, before any DSP: identity row, accent, max delay, mix law and dry region,
feedback shape, whether SYNC ships in v1.

M7 and M8 then gate everything: a click or a leaking dry path corrupts every later
round. Then M1, M3, M5 in order — the interpolator fixes the repeat tone the filters
are voiced against, the feedback law how many repeats are heard, the alias floor
whether a halfband enters the loop. M6, BUILD and FREEZE need M3's bound; M2
follows. Listening starts once M1–M8 pass: L1, then L2, ducking last.

## 5. Freeze before ship

These re-voice saved sessions if they move: parameter ranges and curves (FEEDBACK
law, MOD DEPTH scaling, VOICE Q, TIME range, max delay), note-value multipliers, the
mode list and its order, the dual-offset ratio while fixed, the safety-clip ceiling,
the mix law and its dry region. After ship they move behind a version gate, never in
place.
