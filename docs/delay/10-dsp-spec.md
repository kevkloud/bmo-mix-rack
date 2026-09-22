# BMO Dwell (`Bdly`) — DSP specification

Written on AURORA, 2026-09-20. Evidence base: `docs/delay/00-repo-conventions.md`,
`01-reference-behavior.md`, `02-design-approaches.md` (cited 00/01/02).

## 0. Topology choice

**One base-rate ring per channel, fractional read, a 2x2 feedback matrix, and a
character stage in the loop (filters → DC blocker → ADAA shaper → safety clip). No
oversampling anywhere.** 02's shortlist row 1, extended so rows 2 and 3 become
*modes* of one loop: tape and bucket-brigade differ only in in-loop filters,
time-change law and modulation.

Why: 02 makes shaper oversampling the main CPU lever, and lookahead is forbidden.
Dropping it makes `latencyForParams` return **0** at every setting and — unlike
`eq`/`sat` (00 §1) — removes the dry compensation ring. Cost per channel per sample
is one interpolated read, two filters and one ADAA evaluation: viable for many
instances at 192 kHz. Alias control moves to ADAA and the in-loop low-pass (§4).

## 1. Delay line and interpolation

Ring length a power of two, `mask`-indexed, at base rate (02: an oversampled buffer
is the expensive option). Read position `w − D`, real-valued.

- **clean**: 32-tap polyphase Kaiser sinc, reusing `modules/tune/dsp/SincTable.h`
  (00 §1) at `cutoff = 1.0`. Unity gain at every phase, exact delay at whole
  samples, so the repeat chain accumulates no phase-dependent HF loss — the only
  place ~32 MACs is spent. Its `kHalf = 16` samples of headroom hold whenever
  `D ≥ 16`; below that, fall back to Hermite.
- **tape / bucket-brigade**: 4-point, 3rd-order Hermite (02: near Lagrange-3,
  smoother phase, good under modulation, low CPU). Worst case is the half-sample
  phase — roughly −0.1 dB at 0.1·f_s, −1 dB at 0.25·f_s, several dB at 0.4·f_s, flat
  at integer phases (estimate, verify by sweep — CALIBRATE). Coefficients are a
  closed form of the fraction, so modulation cannot zipper, and per-repeat HF loss
  is *wanted* here (01 tape rolloff, BBD darkening). No Thiran (02: it clicks).

## 2. Time-change law

`Dtgt` is the requested delay in samples, `D` the running value.

- **tape / BBD — pitch glide.** Rate-limited exponential:
  `D += clamp((Dtgt − D)·(1 − e^(−1/(τ·fs))), −r, +r)`, τ = 120 ms, r = 0.25
  samples/sample. Read rate `ρ = 1 − dD/dn`, so the cap bounds pitch to
  ρ ∈ [0.75, 1.25] (+3.9 / −5.0 semitones) — 01's glide without runaway transposition.
- **clean — crossfade.** Two taps: the old freezes, a new one starts at `Dtgt`,
  equal-power raised-cosine crossfade over 20 ms, reusing the shape in
  `modules/dim/dsp/DspCore.h` (00 §1). No pitch bend; cost is momentary doubling
  (02). §5's modulation follows this law, never limited by it.

## 3. Feedback loop and stability

Per line, into the ring: `v[n] = s·x[n] + Σ_j g_ij·C(y_j[n])` — `y_j` line j's read,
`C(·)` the character chain (§4), `g_ij` §8's matrix, `s` §11's send gate.

Stability requires `|g|·|H(e^{jω})|·|I(e^{jω})|·κ < 1` for all ω — `H` the filter
cascade, `I` the interpolator, `κ` the shaper's incremental gain. All are ≤ 1 by
construction (TPT filters, peak-normalised per §11; interpolators unity-or-less at
every phase; a tanh-family shaper), so the bound is **|g| < 1**.

FEEDBACK maps to `g = 1.05·fb^1.6` (DECIDED, Frosty 2026-09-20): the exponent puts
resolution in the 2–8-repeat region, `g = 1` lands at ~97%, full travel gives 1.05 —
deliberate self-oscillation, the ~97–100% zone accepted and marked on the panel
(13 §4) (01: above ~100% regeneration recirculates without decay). Above
unity a **safety clip** bounds the loop: fixed tanh, ceiling 1.0 (0 dBFS), active
regardless of DRIVE. Its describing-function gain `G(A)` falls monotonically from 1,
so oscillation settles where `g·|H|·G(A) = 1` — a limit cycle just under the
ceiling, not divergence. The clip sits **after** the filters, so howl inherits the
mode's tone (02 flags the trade).

## 4. In-loop filters, saturation, DC

TPT one-poles/biquads, prewarped `g = tan(π f_c/f_s)`. Since `g` depends only on
`f_c/f_s`, every coefficient is sample-rate invariant over 44.1–192 kHz. Loop order:
LOW CUT → HIGH CUT → mode filters → DC blocker → shaper → clip.

- **LOW CUT** HP 20 Hz–1 kHz log; **HIGH CUT** LP 1–20 kHz log, hard-capped at
  `min(18 kHz, 0.45·f_s)`. The cap is load-bearing: it keeps the shaper's input from
  Nyquist, where first-order ADAA is weakest.
- **tape** adds a 1-pole LP at 4.5 kHz (CALIBRATE, 01's per-pass rolloff) and a
  +2 dB shelf at 55 Hz for head bump (01: ~50–60 Hz at 15 ips).
- **bucket-brigade** derives filters from a modelled clock: N = 4096 stages,
  `f_clk = N/(2T)`; anti-alias and reconstruction are each a 2-pole Butterworth at
  `f_c = 0.6·f_clk/2`, clamped to [800 Hz, 16 kHz]. At T = 205 ms that is
  f_clk ≈ 10 kHz, f_c ≈ 3 kHz — 01's datasheet pair, and its darkening with time. A
  2:1 compander (5/50 ms, CALIBRATE) straddles the line.
- **Saturation**: the repo's ADAA residual shaper (`modules/sat/dsp/Shaper.h`,
  00 §1), driven by DRIVE, returning `shape(x) − x` anti-aliased by the first-order
  antiderivative quotient at zero latency. 02 warns aliases accumulate *per repeat*,
  growing roughly as k, so the acceptance test is the alias floor after **10 repeats
  at max DRIVE**, ≤ −60 dBFS (DECISION). If it fails, add
  `Halfband2x` (00 §1) around the shaper only: its group delay is whole-sample, so
  subtracting it from `D` keeps latency 0 and the time exact.
- **DC**: a 10 Hz blocker before the shaper, which is asymmetric (02) — offset
  would compound per repeat.

## 5. Modulation

`D_mod = D·(1 + m[n])` — deviation as a *percentage of speed*, matching 01's units
(0.1% ≈ 1.7 cents).

- Wow: sine at MOD RATE, 0.1–8 Hz (01: 0.5–6 Hz). Flutter (tape only): 11.7 Hz at
  0.25 × wow depth (01: 10–100 Hz, low end is the musical one — CALIBRATE). Noise
  (tape, BBD): white through two cascaded 1.5 Hz one-poles, 0.3 × wow — 01's wear.
- MOD DEPTH 0–100% → peak `|m|` 0–0.5%. Clean mode uses the wow sine alone, 0–8 ms
  absolute: chorus, not pitch wobble.
- Tape/BBD scale rate and depth by `clamp(T/300 ms, 0.5, 2)` (01: both scale with
  time).

## 6. Ducking

Peak follower on the **dry input only** (`(|L|+|R|)/2`), 5 ms attack / 180 ms release
(CALIBRATE), in dB as `E`: `GR_dB = −DUCK·clamp((E − T_d)/W, 0, 1)`, `T_d = −30 dBFS`,
`W = 20 dB` (both CALIBRATE). DUCK spans 0–24 dB, **default 0 dB** (DECIDED, Frosty
2026-09-21). 01 measures 2–4 dB on the units it surveys, and that remains the useful
range — but `params.h`'s rule that a freshly inserted instance does nothing it was not
asked to wins over a helpful starting value, so ducking ships inert and opt-in. `GR`
applies to the **wet output after the loop tap**, never inside the feedback path, so
ducking never shortens the tail.

**The detector's key high-pass is fixed at build time** (DECIDED, Frosty 2026-09-21):
no parameter is reserved for it, and its corner is settled with the other CALIBRATE
figures. The detector is keyed structurally rather than by routing — the module is an
insert, so the dry input *is* the track being sent to the delay — and it can never be
keyed from a different source, because `ModuleDsp::process` takes audio channels only
and both host processors declare stereo in/out with no sidechain bus. That is the
stated limit of this control: what it buys over a compressor on a return is that it
travels in presets and rack state, which routing does not.

## 7. Tempo sync

00 §2: no host tempo reaches `ModuleDsp` today. The DSP needs one new call,
`setTempo(double bpm, bool valid, bool playing) noexcept`, once per block before `process`,
plumbed through the processor → `ModuleEngine` → `ModuleDsp`. No PPQ position: this
module is free-running, not beat-anchored.

Mapping (01): `ms = (60000/BPM)·M`, M = 4, 2, 1, 0.5, 0.25, 0.125 from whole to
thirty-second; **dotted ×1.5**, **triplet ×2/3**. BPM clamped [20, 999].

- `valid == false`: hold the last valid BPM; if none was ever seen, fall back to the
  TIME control.
- Tempo change or jump: re-target `Dtgt`, let §2's law run — tape glides, clean
  crossfades. No special case for jumps.
- Transport stopped: freeze BPM; audio flows, the loop decays, never mute or flush.
- Mapped time over the §10 maximum: halve until it fits.

SYNC and NOTE hold permanent schema slots now, note-list order included; SYNC
itself ships disabled until this plumbing lands (DECIDED, Frosty 2026-09-20).

## 8. Stereo

Per-channel rings, fixed `std::array<Line,2>` (the `eq` pattern, 00 §5); `u` the
input pair, `c = C(y)` the post-character loop signals.

- **stereo**: `v = u + g·I·c`, identity matrix, both lines at T.
- **ping-pong**: input summed to mono `u' = (L+R)/2`, injected into line L only; the
  matrix is the swap `v_L = u' + g·c_R`, `v_R = g·c_L`, both at T, so repeats
  alternate sides every T. The mono sum is stated on the panel.
- **dual-offset**: identity matrix, `D_L = T`, `D_R = (2/3)·T` (CALIBRATE).

Mono bus (00 §5, `dim`'s precedent): stereo and dual-offset collapse to one line;
ping-pong collapses to plain stereo, there being no second output to alternate into.

## 9. Mix, dry path, tail, smoothing

`out = d(m)·dry + w(m)·wet`, with **`w = sin(π m)` for m ≤ 0.5, else 1** and
**`d = 1` for m ≤ 0.5, else `cos(π(m − 0.5))`**. One sine family, both halves with
zero slope at the 50% hinge, so the control has no kink there: the wet's near-linear
start gives fine grip on the first hint of echo, and the dry's cosine fade is the
equal-power taper for the only region where the two trade.

Below 50% the dry is multiplied by exactly 1.0 — **bit-exact, not merely unity
gain** — and MIX moves the wet alone; above it the wet holds full while the dry
fades to a wet-only end stop. At 50% the output is dry unity **plus** wet unity:
sparse displaced repeats sum around +3 dB, coincident in-phase content up to +6 dB.
No makeup or auto-gain (DECIDED, Frosty 2026-09-20): any trim would multiply the dry and break the
guarantee, and the safety clip is in-loop only, so the output is never clipped for
the user. Latency is 0 and nothing is oversampled, so **no dry ring is needed**.

The null test asserts bit-exactness for **every m ≤ 0.5**, not just m = 0: with TIME
beyond the test block so no repeat arrives, output must equal input
sample-for-sample at m = 0, 0.1, 0.25, 0.4 and 0.5. The dry gain must therefore snap
to exactly 1.0, not approach it.

Bypass mutes input injection but keeps processing, so the tail decays rather than
cuts; MIX ramps over 30 ms. Tail to report (00 §3, hardcoded 0):
`tail = T·ceil(60 / −20·log10(min(g, 0.97)))`, clamped to [0.5 s, 30 s]; at `g ≥ 1`
report the clamp, from parameters only like `latencyForParams`.

Smoothing: 20 ms one-pole on wet gain, DRIVE, cutoffs, DUCK; 30 ms on feedback gain;
snap-to-target inside epsilon. Below 50% the dry gain is constant, so it is never
smoothed and cannot zipper; above the hinge it smooths at 20 ms and snaps exactly on
the way back. TIME is not smoothed — §2 owns it.

## 10. Maximum time and memory

Maximum delay **2000 ms** (DECIDED, Frosty 2026-09-20; 01 has no measured class figure — chained BBD
reaches ~1.5 s, and 2 s covers a quarter note at 30 BPM). BBD caps at 1500 ms. At
192 kHz: 2.0 s × 192 000 = 384 000 samples/channel, rounded up to 524 288 × 4 bytes
= **2.0 MB per channel, 4.0 MB per instance** (+ ~4 KB of state); 0.5 MB per channel
at 44.1 kHz. Allocate at `prepare` from the fixed maximum, never from a parameter.

## 11. Creative processing

**THROW** is a send gate `s` on the input injection only; recirculation and the dry
path are untouched, so a throw never disturbs an existing tail. Normal state is send
open, `s = 1`. Armed, `s = 0` until THROW is held, and `s` follows a half-cosine
ramp — 5 ms opening, 15 ms closing (CALIBRATE) — click-free but tight enough to
catch one word. Gate timing quantises to the block boundary; no lookahead.

**BUILD**: while held, loop gain rises as `g(t) = g + (g_thr − g)(1 − e^(−t/τ_b))`,
τ_b = 400 ms, `g_thr = max(g, 1.02)` capped at FEEDBACK's 1.05; on release it relaxes
with τ_r = 800 ms (CALIBRATE), so the build decays rather than cuts. BUILD moves only
`g`, so §3's bound and safety clip govern it unchanged — a held throw reaches the
same bounded limit cycle, never divergence. Cost: one scalar ramp.

**VOICE** (the loop voicing). Replace the two one-pole cuts with one TPT state-variable filter each,
damping `R = 1/(2Q)`, `Q = 0.5 + VOICE·5.5` (CALIBRATE). At VOICE 0 the pair is §4's
neutral cascade; with resonance up and the cutoffs closed together it is a band-pass
— the telephone voicing near 300 Hz / 3.4 kHz at VOICE ≈ 0.6 (CALIBRATE). **Each
stage is peak-normalised to unit magnitude in closed form**: a resonant 2-pole peaks
at `Q/√(1 − 1/(4Q²))` for Q > 1/√2, so divide by that. `|H| ≤ 1` then holds and §3's
bound survives; unnormalised, Q = 6 self-oscillates at a third of the feedback travel
and grows as `Q^k` per repeat. Filtering compounds as `|H|^k`: the peak holds while
the skirts fall, so selectivity grows roughly as `Q√k` and the repeats narrow into a
pitched ring by the sixth. Sample-rate invariant as in §4; two SVFs per channel,
replacing the one-poles.

**Ping-pong** is fully specified in §8. No WIDTH control in v1: with the dry
bit-exact below 50% MIX the image already reads as wide wet over centred dry; if
added later, a wet-only mid/side trim after the loop tap.

**FREEZE**: own button, own parameter slot (11 §3 row 17), shipped enabled in v1 —
not folded into THROW's travel/`throwMode` (DECIDED, Frosty 2026-09-20). Send
closed, loop gain exactly 1.0, **every in-loop stage bypassed** —
filters, shaper, DC blocker, clip. The loop is then a bit-exact circulating buffer,
stable indefinitely and cheaper than running; anything less erodes the held sound.
**The latched length rounds to a whole sample so the interpolator is bypassed too** —
at a fractional phase Hermite is below unity and the hold would darken and decay.
Any DC present at the latch circulates undamped (blocker bypassed): bounded, but it
offsets the held sound, so M3 logs it. Length is latched at freeze, and TIME and
modulation are ignored while held
(DECISION), since a length change tears the loop.

## 11a. In-loop FX

**Position.** One FX stage in the character chain:
`LOW CUT → HIGH CUT → mode filters → FX → DC blocker → shaper → clip` (§4).
Before the blocker, so an offset an FX introduces is removed rather than
compounded; before the shaper and clip, so the clip stays the last thing in the
loop and §3's bound still ends there. The stage recirculates, so every candidate
**compounds per repeat** — the point of it, and the risk.

**Bound.** Each candidate is non-expanding, `|F| ≤ 1` at every setting,
peak-normalised in closed form where it could exceed unity (§11's VOICE rule), so
§3's `|g| < 1` is unchanged. **FX off skips the stage** — not "amount zero" — so
the loop is bit-identical to the pre-FX loop at no CPU cost. **FREEZE bypasses FX**
with every other in-loop stage (§11); an FX that changed the buffer each lap would
not be a hold. Nothing here reads ahead, so reported latency stays **0** (§0).

**FX AMOUNT**: one continuous control, its meaning per type, zero always inaudible.

Candidates — list and order free until ship (11 §3):

- **Diffuse** — `AllPassChain` (6-stage, `modules/dim/dsp/DspCore.h`, 00 §1),
  delays 7–37 ms scaled by AMOUNT. Unit magnitude at every ω, so `|F| = 1`
  exactly; smear accumulates over k passes into a pseudo-reverb. ~12
  MACs/sample/channel, ≤ 32 kB per channel at 192 kHz.
- **Crush** — quantise to `b = 16 − AMOUNT·13` bits and sample-and-hold at
  `f_s/⌈1 + AMOUNT·31⌉`; both ≤ unity, both compounding each lap. **Deliberate
  aliasing.** The hold's images are made *inside* the loop and meet §4's 18 kHz cap
  on the *next* lap, so the cap tames them one repeat late instead of preventing
  them — musical and bounded, and always ahead of the shaper. Crush is therefore
  **exempt from the −60 dBFS alias floor** (§4), which is measured FX-off; its own
  acceptance is only that the non-harmonic floor stops growing by repeat 10.
  Negligible cost and memory.
- **Octave up / Octave down** — ±12 semitones, two-grain overlap-add, equal-power
  crossfade, grain 60 ms (CALIBRATE). The grain's read offset is **absorbed into
  `D`** (the ring is read one grain earlier), so the repeat still lands at T and
  reported latency stays 0. AMOUNT is the shifted/unshifted blend inside the stage.
  Pitch compounds as ±12k semitones, so content leaves the band within a few laps —
  self-limiting, with both cuts in front of it. ~2 reads and a window per sample;
  ≤ 24 kB per channel at 192 kHz.
- **Reverse** — a second buffer of one delay length per channel, filled forward
  while the previous fill is read backward, swapping at the delay period, 5 ms
  raised-cosine seam (CALIBRATE). `|F| = 1`. **Memory doubles**: +2.0 MB per
  channel, +4.0 MB per instance at 192 kHz (§10), allocated at `prepare` from the
  fixed maximum whether FX is on or not.
- **Pan / Tremolo** — one LFO stepped at the delay period, so each repeat gets its
  own position or level rather than a wobble inside one; equal-power law, AMOUNT is
  depth. Chops rather than pans on the mono bus (§8). Negligible cost.
- **Sweep** — §11's VOICE centre multiplied by `2^(±AMOUNT·k/6)` per repeat (a
  free-running LFO variant is the alternative), keeping §11's peak normalisation so
  `|H| ≤ 1` still holds. Negligible cost above VOICE.

**CPU**: target ≤ 1.3× the FX-off loop for any one candidate at 192 kHz, ≤ 1.5×
heaviest (DECISION; bench per 11 §4k). Only Diffuse and the octaves should measure.

## 12. Fixed values to target

| Quantity | Value | Trace |
|---|---|---|
| Interpolators | 32-tap sinc (clean) / 4-point Hermite | 02; 00 §1 |
| Glide τ / rate cap | 120 ms / 0.25 | CALIBRATE, 01 glide |
| Clean crossfade | 20 ms raised cosine | CALIBRATE, 00 §1 |
| Feedback law | `g = 1.05·fb^1.6`, unity at fb ≈ 97% | DECIDED (Frosty, 2026-09-20); 01 |
| Safety clip, DC | tanh at 1.0 always on; 10 Hz blocker | 02 |
| LOW/HIGH CUT | 20 Hz–1 kHz / 1–20 kHz, cap 18 kHz | CALIBRATE |
| Tape LP / head bump | 4.5 kHz / +2 dB at 55 Hz | CALIBRATE / 01 |
| BBD clock, cutoff | 4096 stages, `N/(2T)`; `0.3·f_clk` | 01 |
| BBD compander | 2:1, 5 / 50 ms | CALIBRATE |
| Alias floor | ≤ −60 dBFS, 10 repeats | DECISION |
| Wow / flutter | 0.1–8 Hz / 11.7 Hz at 0.25× | 01 |
| Mod depth | 0–0.5% speed; clean 0–8 ms | 01 / CALIBRATE |
| Duck range, detector | 0–24 dB (**0 dB**); 5/180 ms, −30 dBFS, W 20; key HP fixed | DECIDED 2026-09-21 / CALIBRATE |
| Dotted / triplet | ×1.5 / ×2/3 | 01 |
| Dual-offset ratio | 2/3 | CALIBRATE |
| Max delay / latency | 2000 ms (BBD 1500) / 0 | DECIDED (Frosty, 2026-09-20) / 00 §4 |
| Mix law | `w = sin(πm)`, `d = cos(π(m−0.5))` | DECIDED (Frosty, 2026-09-20) |
| Dry bit-exact region | m ≤ 0.5, gain exactly 1.0 | DECIDED (Frosty, 2026-09-20) |
| THROW ramp, BUILD | 5/15 ms; `g_thr = max(g,1.02)`, 400/800 ms | CALIBRATE, §3 cap |
| VOICE Q, telephone | 0.5–6 peak-normalised; 300 Hz/3.4 kHz at 0.6 | CALIBRATE |
| FREEZE | loop gain 1.0, all bypassed, whole-sample length, own button/slot | DECIDED (Frosty, 2026-09-20) |
| FX stage position | after mode filters, before DC blocker; skipped when off, bypassed in FREEZE | DECISION |
| FX loop bound | `\|F\| ≤ 1` for every candidate, normalised in closed form | DECISION |
| Diffuse | 6-stage allpass, 7–37 ms × AMOUNT | CALIBRATE; 00 §1 |
| Crush | 16→3 bits, hold ÷1–32; exempt from the alias floor | CALIBRATE / DECISION |
| Octave grain | 60 ms, ±12 st, read offset absorbed into `D` | CALIBRATE |
| Reverse buffer | one delay length extra per channel, 5 ms seam | CALIBRATE |
| Pan / Sweep | stepped once per repeat; AMOUNT is depth | CALIBRATE |
| FX AMOUNT default | 35 % | DECISION |
| FX CPU ceiling | ≤ 1.3× FX-off, ≤ 1.5× heaviest | DECISION |

## Open decisions for Frosty

1. Ping-pong on a stereo source: sum to mono (specified), or keep L/R?
