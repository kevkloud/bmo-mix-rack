# BMO Dwell — calibration and listening protocol

Written on AURORA, 2026-09-20; **the open-values table and the FX round were
brought in line with the lane redesign on 2026-09-22**. Procedure only, no code.
Every CALIBRATE and DECISION value in `docs/delay/10-dsp-spec.md` §12, against
the rated targets in `01-reference-behavior.md`. Method:
`testing-notes/blind-listening-protocol.md`.

**What the redesign took out of this document**: VOICE and its telephone
landmark, THROW's ramp and modes, BUILD's constants and FREEZE's drift check —
all four controls are cut (`15`, `10` §11). Octave up, Octave down and Reverse
left the FX round on 2026-09-21 and **Sweep on 2026-09-22**, so **L3 hears
three candidates, not seven**. What it put in is at the foot of §1.

## 1. The open values

M = measure (§2), L = listen (§3), F = Frosty's call.

| Value, where to start, what it controls | How it settles, and the acceptance |
|---|---|
| Hermite HF loss, spec estimate, repeat darkening | M1: within 1 dB at 0.1/0.25/0.4·f_s, monotonic |
| Glide τ / cap, 120 ms / 0.25, tape pitch bend | M2, L2: pitch, never a jump; ρ within ±4 semitones |
| Clean crossfade, 20 ms, time change | M8, L2: no click, ≤0.5 dB dip, no flam |
| LOW/HIGH CUT, as spec'd, loop tone | M6: extremes stable; cap holds at every rate |
| Tape LP / bump, 4.5 kHz / +2 dB, tape voicing | L1: beats or ties two neighbours, blind |
| **Tape shelf corner convention**, 55 Hz as the pole or as the +1 dB midpoint, tape's low end | M6, L1: **it moves `P_c` between 1.054 and 1.040** (`10` §4), so it moves the sound and not the stability — `10` §3 sweeps the built coefficients either way. Pick one by ear, then log which |
| **BBD compander ballistics**, 2:1 at 5/50 ms as the starting pair, noise vs pumping | M4, L1: floor −10 dB, no breathing on a pad. The expander reads the compressor's **stored** gain rather than re-detecting (`10` §4), so these constants voice the pair; they no longer decide whether it is bounded |
| Flutter, 11.7 Hz, tape flutter | M2, L1: inside 10–100 Hz; not vibrato |
| Clean mod depth, 0–8 ms, chorus width | L1: chorus not detune; zero is still |
| Duck detector, 5/180 ms, −30 dB, repeats stepping aside | M9, L1: recovered before the next phrase |
| **SEND ramp**, 5 / 15 ms, the lane's input gate | M8, L2: catches one word; no click |
| **Lane build ceiling `g_max`**, above the detent, how fast a build swells | **L2, and it is heard rather than decided** (Frosty, 2026-09-22): 1.05 is ~12 s from unity to the ceiling at 250 ms lane time, 1.10 ~6 s, 1.3 a violent swell. The safety clip bounds every one of them (`10` §11.6), so this is a musical choice, not a stability one |
| **CHOP fade length**, 1 ms raised cosine to start, stutter tightness | **M8 and L2 together, and they can disagree**: 1 ms is click-free by ear but does not meet a broadband −60 dBFS assertion on bright sustained content (`10` §11.4). Either band-limit the assertion to 5 kHz, lengthen to ~3 ms and lose tightness at sixteenths above ~160 BPM, or record the measured edge. **Needs the measurement first; it is not a preference** |
| Dual-offset, 2/3, L/R spread | F, shown L1: he names one; recorded as a decision |
| Feedback law, `1.05·fb^1.6`, repeats, self-oscillation | M3 — DECIDED (Frosty, 2026-09-20): 2–8 repeats over ≥30% travel; onset ≈97%; bounded above unity |
| Alias floor, ≤ −60 dBFS, shaper quality | M5: met, or halfband added and re-measured |
| Max delay, 2000 ms (BBD 1500), memory vs reach | DECIDED (Frosty, 2026-09-20): fixed before allocation is written |
| Mix law, `w = sin(πm)` / `d = cos(π(m−0.5))`, dry unity ≤50% | M7: dry bit-exact to 50%; sum stated on the panel |
| **Lane detent hold**, `lane_gain` 0, the hold that replaced FREEZE | M3: per-lap magnitude ≤ 1 and level monotone on every character; drift ≤ 0.1 dB over 60 s on Clean with the chain neutral. **It colours by design on Tape and Bucket** (`10` §11.5), so the assertion is level, never spectrum |
| Diffuse spread, 7–37 ms × AMOUNT, repeat smear | M6, L3: blurs without ringing; `\|F\| = 1` measured |
| Crush bits / hold, 16→3, ÷1–32, lo-fi repeats | M5, L3: floor non-increasing repeat 10→32 |
| Pan / Tremolo depth, per repeat, motion | L3: moves per repeat, not a wobble |
| FX AMOUNT default, 35 %, arrival point | L3, F: audible but not the loudest thing |

**Struck on 2026-09-21 and 2026-09-22, with their controls**: VOICE Q and the
telephone landmark (VOICE is cut, and the cuts are plain one-poles again);
THROW's ramp under that name, THROW's three modes and BUILD's 1.02 / 400 / 800 ms
(the lane's bipolar gain replaced all of them); FREEZE's 60 s drift and latch DC
(there is no bit-exact hold in v1 — the row above is what took its place);
Octave grain and Reverse seam (both cut 2026-09-21); and **Sweep, cut 2026-09-22
because VOICE was cut** — it swept VOICE's resonant centre and had nothing left
to move.

**One figure in `10` is modelled and must not be quoted until it is benched**:
the re-detecting compander's **+0.184 dB per dB of envelope step**, +3.7 dB on a
20 dB transient (`10` §4, README item 23). It assumes log-domain one-pole
detectors and a feed-forward pair; a feedback RMS cell tracks better. **M4 owes
a measured figure**, and until it has one the number stays labelled as modelled
wherever it appears.

Open decision 1 in 10's closing list (ping-pong routing) is Frosty's alone. Do not settle it by ear.

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
  the reported tail within one repeat of measured. **Same run covers the lane**:
  its detent held 60 s per character, and its build region at each candidate
  `g_max`, at the top of LANE LEVEL's travel rather than at unity (`15`).
- **M4 saturation per repeat.** Level and THD of repeat k, k = 1…10, min/mid/max
  DRIVE, both modes. **M5 alias floor** is that run's non-harmonic floor at repeat
  10, max DRIVE. **M4 also owes the compander's measured net gain through a rising
  envelope**, against `10` §4's modelled +0.184 dB per dB.
- **M6 loop filters.** Sweep both cutoffs at high feedback, each engine, both
  tape shelf conventions; log the magnitude at every extreme and the resulting
  `P_c`. Nothing may grow. *(The VOICE resonance sweep this asked for is struck
  with VOICE.)*
- **M7 dry null.** Output against input at every MIX ≤ 50%, TIME beyond the block,
  feedback zero and maximum. Bit-exact, not −100 dB.
- **M8 gate edges.** SEND's ramp times, CHOP's fade, the HOLD-off clear, peak
  sample step at both edges of each, crossfade dip. **CHOP is measured broadband
  and band-limited to 5 kHz, both figures logged**, because which one the
  assertion uses is exactly what the measurement decides (§1).
- **M9 ducking.** Wet bed, dry bursts: GR depth, attack, release, against 5/180 ms
  and 01's 2–4 dB figure.

A measurement reading zero for what it tests invalidates its round.

## 3. Listening

Blind, level-matched, rendered from the real DSP, **a different shuffle seed per
source**, key beside the audio, audio outside the repository, 32-bit float.

- **L1 voicing**: three candidates, **one entered twice**. The gap between the
  duplicates is the round's noise floor; a smaller separation decides nothing (the
  Opto lesson). One control pair per source, first.
- **L2 gesture**: glide, crossfade, and the lane — SEND catching one word, the
  detent holding, the build region swelling at each candidate `g_max`, CHOP at a
  sixteenth-note rate. Moving material only.
- **L3 FX candidates** (10 §11a): **three now — Diffuse, Pan/Tremolo, Crush**,
  each heard **against FX off** at `fx_amount` mid and max, on **vocal throws,
  drums and a sustained pad**, one candidate per pair, blind and level-matched
  as L1. **Run per engine**: the lane's stage is its own (`10` §11a), and a
  candidate that earns its index on the main delay's repeats has not thereby
  earned it on a single thrown word. Pass is "earns its index":
  it beats or ties Off on at least one source and harms none. **Record pass or
  fail per candidate, with the machine name**, in the round's verdict file.
  **A candidate that fails is removed from `fx_type` before ship** — the list is
  append-only afterwards, so a dead index is permanent (11 §3). Four have come
  out that way already. L3 runs after L1.
- **Sources**, all four every round: vocal throws, drums, a sustained pad, a mono
  guitar. Ping-pong and dual-offset also on the mono bus.
- **The winner**: one bold line per pair, the listener comments, the session writes
  it in; the key opens once every line is filled; decode and verdict in the same
  file, dated and named. Three files per round.
- **A result holds only for the machine and monitoring it was heard on.** Name it in
  the run record and the verdict. This pack was written on AURORA.

## 4. Order

On paper, before any DSP: identity row, accent. Max delay, mix law/dry region,
feedback shape and SYNC's shipped-disabled status are DECIDED (Frosty, 2026-09-20).

M7 and M8 then gate everything: a click or a leaking dry path corrupts every later
round. Then M1, M3, M5 in order — the interpolator fixes the repeat tone the filters
are voiced against, the feedback law how many repeats are heard, the alias floor
whether a halfband enters the loop. M6 and the lane's detent and build region
need M3's bound; M2 follows. Listening starts once M1–M8 pass: L1, then L2,
ducking last. **`g_max` is settled in L2 and CHOP's fade in M8 before L2**, so
the lane is heard with a fade that is already known to be clean.

## 5. Freeze before ship

These re-voice saved sessions if they move: parameter ranges and curves (FEEDBACK
law, MOD DEPTH scaling, TIME range, max delay, **LANE GAIN's law about its
detent**), note-value multipliers, the mode list and its order, **`fx_type`'s
three entries and their index order**, the dual-offset ratio while fixed, the
safety-clip ceiling, the mix law and its dry region. After ship they move behind
a version gate, never in place. *(VOICE Q is struck: there is no VOICE.)*
