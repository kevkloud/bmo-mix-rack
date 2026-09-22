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

Per line, into the ring: `v[n] = x[n] + Σ_j g_ij·C(y_j[n])` — `y_j` line j's read,
`C(·)` the character chain (§4), `g_ij` §8's matrix. **The main loop has no input
gate.** The old `s` send gate is removed, not repurposed (§11), so there is no
mechanism by which the lane can disturb the main delay.

Stability requires `|g|·|H(e^{jω})|·|I(e^{jω})|·κ < 1` for all ω — `H` the
filter cascade, `I` the interpolator, `κ` the shaper's incremental gain.
`κ ≤ 1` (a tanh-family shaper), and `I` is unity at DC on every phase and falls
from there. **`|H|` is not ≤ 1 on every character.** §4 gives tape a +2 dB
head-bump shelf, and the bump sits at 55–65 Hz, where the interpolator's loss —
which is at the top — cannot offset it. The two **tilt** rather than cancel: a
note held at nominal unity on tape gains low end and loses top on every lap.
The earlier claim that every in-loop magnitude is ≤ 1 by construction was false
(found 2026-09-21).

The fix is **not** to take the bump out of the loop — accumulation per repeat is
what a head bump is for, and a bump applied only to the output would stop tape
getting warmer as it repeats. Instead, define the character's **reference loop
peak**

```
P_c = max over ω of |H(e^{jω}) · I(e^{jω})|
```

evaluated with the character's own mode filters and compander at the current
TIME and sample rate, the 10 Hz blocker in, and the **user stages at their
neutral limits** — LOW CUT 20 Hz, HIGH CUT at the 18 kHz cap, FX off, DRIVE 0.
That reference is also the worst case over all user settings, since every one of
those stages is `≤ 1` at every setting (§4, §11a) and can only attenuate
further. **`P_c` is computed, never hardcoded**: at `prepare`, and on any change
of character, TIME or sample rate, sweep the built coefficients on a log grid of
at least 512 points from 10 Hz to 0.45·f_s and take the maximum. It costs
microseconds off the audio thread and cannot drift from the coefficients a
CALIBRATE pass actually lands on.

FEEDBACK then maps to

```
g = (1.05 · fb^1.6) / P_c
```

(DECIDED, Frosty 2026-09-20; normalisation added 2026-09-21). The exponent puts
resolution in the 2–8-repeat region; the loop's peak magnitude reaches **1.000
at fb = 97.0 % and 1.05 at full travel, on every character** — Decided item 4
holds as written, and the panel carries **one** self-oscillation tick rather
than one per character. Expected `P_c`: clean **0.999**, tape **1.054**,
bucket-brigade **0.990–0.999** with TIME. Tape's tail is therefore slightly
shorter than clean's at the same knob position, which is what tape does.

**Unity means the loudest band neither grows nor decays**, not that every band
holds; a non-flat loop cannot do the latter and stay bounded. Every other band
still decays at `|H·I|/P_c` per lap, so a held sound darkens and colours (15).
Because `P_c` is fixed at the reference and not tracked live, the user's cuts
and FX only ever shorten the tail — on tape, LOW CUT at 200 Hz costs 0.84 dB a
lap. Tracking them live would hold the tail at any tone; rejected.

Full travel gives 1.05 — deliberate self-oscillation, the ~97–100% zone accepted
and marked on the panel (13 §4) (01: above ~100% regeneration recirculates
without decay). Above unity a **safety clip** bounds the loop: fixed tanh,
ceiling 1.0 (0 dBFS), active regardless of DRIVE. Its describing-function gain
`G(A)` falls monotonically from 1, so oscillation settles where
`g·|H|·G(A) = 1` — a limit cycle just under the ceiling, not divergence. The
clip sits **after** the filters, so howl inherits the mode's tone (02 flags the
trade).
TPT one-poles/biquads, prewarped `g = tan(π f_c/f_s)`. Since `g` depends only on
`f_c/f_s`, every coefficient is sample-rate invariant over 44.1–192 kHz. Loop order:
LOW CUT → HIGH CUT → mode filters → DC blocker → shaper → clip.

- **LOW CUT** HP 20 Hz–1 kHz log; **HIGH CUT** LP 1–20 kHz log, hard-capped at
  `min(18 kHz, 0.45·f_s)`. The cap is load-bearing: it keeps the shaper's input from
  Nyquist, where first-order ADAA is weakest.
- **tape** adds a 1-pole LP at 4.5 kHz (CALIBRATE, 01's per-pass rolloff) and a
  +2 dB low shelf at 55 Hz for head bump (01: ~50–60 Hz at 15 ips). It is the
  **only in-loop stage whose magnitude exceeds unity**, and it stays that way on
  purpose: the bump must compound per repeat. Its nameplate gain is 1.2589, but
  that asymptote lives below the 10 Hz blocker and LOW CUT's 20 Hz floor, so what
  the loop actually sees is **+0.45 dB at 63 Hz** — the chain peak is **1.054**,
  and §3's `P_c` divides exactly that out. **The shelf's corner convention is not
  yet pinned** (CALIBRATE): 55 Hz read as the pole gives 1.054 at 63 Hz, 55 Hz
  read as the +1 dB midpoint gives 1.040 at 64 Hz. Pick one at implementation;
  §3's runtime sweep takes the figure from the built coefficients either way, so
  the choice moves the sound, not the stability.
- **bucket-brigade** derives filters from a modelled clock: N = 4096 stages,
  `f_clk = N/(2T)`; anti-alias and reconstruction are each a 2-pole Butterworth at
  `f_c = 0.6·f_clk/2`, clamped to [800 Hz, 16 kHz]. At T = 205 ms that is
  f_clk ≈ 10 kHz, f_c ≈ 3 kHz — 01's datasheet pair, and its darkening with time.
  Each Butterworth is `Q = 1/√2` exactly — the no-peaking boundary — so the pair
  is monotonic with `|H| ≤ 1`, equality only at DC; with the blocker and the cuts
  in front, BBD's chain peak is **0.990–0.999 depending on TIME**, and its filters
  need no correction.

  A **2:1 compander (5/50 ms, CALIBRATE)** straddles the line, and **the expander
  does not re-detect**: the compressor's gain is written to a control ring beside
  the audio and read at the same fractional position, and the expander applies its
  exact reciprocal. The pair is then unity at every instant, transient included,
  and `P_bbd` comes from the filters alone. **This is a stability requirement, not
  a refinement.** A re-detecting pair with identical ballistics on both halves has
  net gain `Δ = 0.5·(Ê − S[Ê])` in dB — zero in steady state, but through a rising
  envelope it peaks at **0.184 dB per dB of envelope step**, one attack constant
  in: +3.7 dB on a 20 dB transient, unbounded in the step, *inside the loop*, at
  the same point in the circulating word on every lap. It sharpens attacks and
  thins decays each pass, so a percussive word parked at the lane's centre detent
  grows into the clip instead of holding. No choice of ballistics fixes it: a
  faster expander detector overshoots on attacks by up to the full step, a slower
  one moves the overshoot to releases. If the delayed-gain construction is
  rejected for authenticity, the fallback is to clamp the pair's net gain to
  `≤ 0 dB` from the two envelopes already in hand (DECISION for Frosty). The
  overshoot figure is **modelled, not measured** — it assumes log-domain one-pole
  detectors and a feed-forward pair, and a feedback RMS cell tracks better; bench
  it before quoting it.
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

SYNC, NOTE and **LANE NOTE** hold permanent schema slots now, note-list order
included; all three ship disabled until this plumbing lands (DECIDED, Frosty
2026-09-20 and 2026-09-23). **One SYNC governs both engines and each picks its
own division from the same sixteen** — §11.7 owns that, including why there is
no `lane_sync` and why the defaults are silent at 120 BPM.

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

**Those two figures are written for one wet engine** and §11 adds a second: with
the lane running at LEVEL 0 the 50 % case is dry unity plus *two* wet unities,
roughly +4.8 dB typical and +9.5 dB worst (§11.8). And while the output is still
never clipped, the lane at the top of LEVEL's travel can legitimately put about
+24 dBFS on the wet bus, which no path in Dwell could before (§11.6).

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

**That figure is per engine.** §11's lane is a second ring of the same fixed
maximum — `lane_time` shares TIME's range — so an instance is **8.0 MB at
192 kHz** and 2.0 MB at 44.1 kHz, plus ~0.2 MB of FX scratch across both engines
at the top rate. Nothing else allocates: no candidate needs a second buffer now
that Reverse is cut (§11a). Eight Dwells in a full rack at 192 kHz is ~65 MB of
rings. `modules/dwell/params.h`'s `kMaxTimeMs` comment and `11` §4k still quote
the one-engine figure and need the same correction.

## 11. The lane

**BMO Dwell is two delay engines running at once in one instance, not one engine
with modes** (DECIDED, Frosty 2026-09-21; `15`). The **main delay** is §§0–10,
unchanged and always running, ducked by the input per §6. The **lane** is a
second engine of the same construction, with its own storage, fed only when
asked, and never touched by the ducker.

This section previously specified THROW as a send gate with a three-way
`throwMode`, plus BUILD, FREEZE and VOICE. **All four are gone** — the schema
they belonged to no longer exists. What replaced them is below; what their
removal costs is §11.5.

**The lane shares the main delay's voicing** (DECIDED, Frosty 2026-09-23;
§11.3). It is a second delay *line*, not a second set of controls: one
CHARACTER, one STEREO, one pair of cuts, one modulation and one DRIVE govern
both engines. What the lane owns is its TIME, its LEVEL, its tail, its three
gates and its own FX stage.

**The main loop loses its input gate.** §3's injection is
`v[n] = x[n] + Σ_j g_ij·C(y_j[n])` — the `s` term is **removed, not
repurposed**. There is then no mechanism by which anything the lane does can
disturb the main delay, and that is provable rather than argued: the main
line's output must be **bit-identical** between a render in which SEND is held
throughout and one in which it is never touched (`11` §4e, test 1).

### 11.1 Topology

**REQUIREMENT — one delay engine, instantiated twice** (DECIDED, Frosty
2026-09-23). The DSP is built as **a single reusable engine type — ring,
interpolator, time-change law, character chain, modulation, FX stage, clip —
held twice**, not as one bespoke object that happens to contain two of
everything. The two instances differ only in the parameters handed to them and
in what feeds and reads them: the main engine takes the dry input and is ducked,
the lane takes SEND's gated dry input and is not.

**This is a structural requirement, not a style preference, and it is the reason
it is written here rather than left to the implementer.** Frosty chose it so
that Dwell can be **split into a plain delay and a throw delay later** without
redoing the expensive part — the engine is the expensive part, and an engine
that is already a standalone, self-contained unit can be lifted into a second
module as it stands. A dual-purpose object with the two paths interleaved would
have to be taken apart first, which is exactly the work this avoids. It also
pays immediately: every invariant in `11` §4 is then asserted against one piece
of code exercised twice rather than two code paths that must be kept in step.

Let `x[n]` be the module's dry input pair, `y_main` the main loop's read and
`y_lane` the lane's.

1. **The lane taps the dry input, not the main's wet**:
   `v_lane[n] = s_lane[n]·x[n] + g_lane·C_lane(y_lane[n])`, `s_lane` being
   SEND's ramp (§11.4). A send should catch the source word, not the main
   delay's already-coloured repeats; tapping the main's wet would also make the
   lane's content depend on the main's FEEDBACK, which is the bit-identity
   claim above running backwards. The alternative is recorded here so the
   choice is visible, not hidden (DECISION, Frosty's to overrule).
2. **`C_lane(·)` is the full character chain of §4 and §11a**, running the
   **shared** voicing — LOW CUT → HIGH CUT → mode filters → FX → DC blocker →
   shaper → **safety clip** — with only its FX stage on its own parameters
   (§11.3). Same code, **second instance, second state**: sharing a parameter
   is not sharing a buffer, and no ring, filter state, LFO phase, follower or
   crossfade is shared between the engines. That separation is what test 1
   below rests on; see §11a.
3. **The lane's output is gated by CHOP, scaled by LEVEL, and summed into the
   wet bus after the main's loop tap and after the ducker**:
   `wet = GR·wet_main + chop[n]·10^(lane_level/20)·y_lane`. §6's `GR` never
   reaches the lane (DECIDED; `15`) — the lane's whole job is to be heard.
4. **The sum is inside MIX.** `out = d(m)·dry + w(m)·wet` is unchanged, so §9's
   guarantees survive: the dry path is still bit-exact unity below 50 % MIX,
   and MIX 0 still silences both engines.

### 11.2 The lane's tail: one bipolar knob (`lane_gain`, id 14)

**`lane_gain` is a single bipolar control, −100…+100 %, with a sticky centre.**
Below centre the lane decays (a throw), at centre it holds (a freeze), above
centre it grows (a build). These are **three regions of one loop gain, not
three modes**, which is why one control covers them and why the caption —
THROW / FREEZE / BUILD — changes with the region while nothing in the schema
does. Default **−40**, in the throw region.

With `L = lane_gain / 100 ∈ [−1, +1]`, and `P_c` the character's reference loop
peak from **§3, which owns it and is not restated here** — one `P_c`, since
CHARACTER is shared (§11.3), though each engine evaluates it at **its own TIME**
where the character's filters depend on TIME, as bucket-brigade's do:

| region | loop gain |
|---|---|
| `L < 0` — THROW | `g_lane = (1 + L)^1.6 / P_c` |
| `L = 0` — FREEZE | `g_lane = 1 / P_c` **exactly**, i.e. loop peak magnitude **1.000** |
| `L > 0` — BUILD | `g_lane = (1 + (g_max − 1)·L²) / P_c` |

- **The value that is exact unity is `lane_gain = 0.0`, and only that.** The
  0.1 step divides the travel evenly about the centre, so the host's normalised
  lane lands on it exactly (`params.h`, id 14). At the detent the DSP uses
  `1/P_c` as a literal and **the smoother snaps to it rather than approaching
  it** — the same rule §9 already imposes on the dry gain below 50 % MIX, and
  for the same reason: a smoothed approach leaves 0.9999 circulating, which is
  a hold that quietly decays. A detent labelled FREEZE is a promise that centre
  is unity, and §3's normalisation is the other half of that promise: without
  it, unity would sit at a different knob position on every character.
- **The decay region is §3's feedback law rescaled so unity sits at the top of
  the region.** The main maps `fb ∈ [0,1]` to `1.05·fb^1.6 / P_c`; the lane maps
  `1 + L ∈ [0,1]` to `(1+L)^1.6 / P_c`. Same exponent, same feel, same
  resolution in the 2–8-repeat region. At the default `L = −0.4` the loop peak
  is 0.442, about **−7.1 dB a lap**: at the default 250 ms lane time that is
  roughly eight or nine audible repeats, gone in ~2.1 s. **That is why the
  default sits in the throw region and not at the detent** — a first SEND must
  echo and fade, not sustain forever.
- **The build region is quadratic, not linear** (DECISION), so the useful
  resolution sits just above the detent where the difference between 1.01 and
  `g_max` lives, and so the curve leaves the detent with zero slope — which is
  what makes the sticky centre feel like part of the travel rather than a notch
  cut into it.
- **`g_max`, the build ceiling, is CALIBRATE** (Frosty, 2026-09-22): it is
  settled by ear in `14` §3's listening round, not decided on paper, because how
  fast a build should swell is a musical judgement and the arithmetic below only
  bounds the search. The main loop caps at 1.05 by README Decided item 4 and the
  lane may sit higher, a violent build being the point. At 250 ms lane time:
  1.05 gives +0.42 dB a lap, ~1.7 dB/s, about 12 s from unity to the ceiling —
  slow for something called BUILD; 1.10 gives ~3.3 dB/s, about 6 s; 1.3 gives
  ~10 dB/s, a violent swell. Whatever is heard, it is a *starting* gain and not
  a bound — §11.6's clip is the bound, at every value in that range.

### 11.3 What the lane shares, and FX LINK (id 26)

**The lane shares the main delay's voicing rather than mirroring it** (DECIDED,
Frosty 2026-09-23). `character` (4), `stereo` (5), `low_cut` (6), `high_cut`
(7), `mod_rate` (8), `mod_depth` (9) and `drive` (10) **govern both engines**:
there is one set of voicing controls on this module and both delay lines read
it. There is **no LINK parameter and no voicing mirror** — the seven `lane_*`
voicing rows that used to exist are deleted, not defaulted-on.

What the lane still has of its own is what makes it a lane rather than a copy:
**TIME (21), NOTE (22), LEVEL (20), the bipolar tail (14), SEND (13), HOLD (15),
CHOP (16) and its own FX stage (23–25)**. Those are the controls a thrown word
needs to sit differently from the repeats it lands in — a different time in
either unit, a different loudness, a different tail, and an effect of its own.
`lane_note` is the lane's half of SYNC and is §11.7's subject.

Three exceptions to "both engines", each for its own reason:

- **DUCK (11) is main-engine only.** The ducker never touches the lane (`15`),
  because the lane's whole job is to be heard; ducking it would duck the
  emphasis against the source that caused it.
- **FEEDBACK (3) has no lane counterpart**: the lane's tail is `lane_gain`
  (§11.2), which is a different control with a different law and a detent.
- **MIX (12) governs both**, since both sum into the wet bus before it (§11.1).

**FX LINK (id 26, default on) ties the lane's FX trio (23–25) to the main's
(17–19).** It is the one tie left, and the lane's FX is the one part of its
voicing that stayed independent: **a thrown word can be crushed against a clean
main delay**, which is worth a parameter where a second set of cuts and
modulation was not. While it is on, the lane's three FX rows are **ignored, not
overwritten** — nothing is written to them, they keep whatever they held, and
the lane's stage reads the main's three directly.

**Nothing is seeded when it is turned off.** The old seed-on-unlink machinery
went with LINK: with no voicing mirror there is nothing to seed, and `fx_link`
needs none — the lane's own FX values are still there, untouched, and come back
when the tie releases. So there is no UI gesture to build, no automation pass
that rewrites parameters, and `11` §4e's assertion is simply that **automating
`fx_link` writes no parameters at all**.

### 11.4 SEND (13), HOLD (15) and CHOP (16)

**SEND gates the lane's input only.** Normal state is closed, `s_lane = 0`;
while SEND is true `s_lane` follows a half-cosine ramp — **5 ms opening, 15 ms
closing** (CALIBRATE, the figures the old THROW used) — click-free but tight
enough to catch one word. Gate timing quantises to the block boundary; no
lookahead. SEND is meant to be automated a word at a time.

**SEND onto an occupied lane sums** (DECIDED; `15`): the new word is added to
whatever is circulating, so words layer into a chord. **What bounds the
accumulation is the lane's own in-loop safety clip** — §3's fixed tanh at 1.0,
one instance per engine, last in `C_lane`. Every lap the circulating content
passes through it, so the steady state cannot exceed the ceiling however many
words are sent. Two consequences worth stating plainly:

1. **Layering is bounded by saturation, not by headroom.** The fifth word laid
   onto a full lane does not make it louder; it makes it more saturated, and the
   character's shaper colours it. That is the only bound there is, and it is a
   musical one.
2. **Injection is clipped one lap late.** `x[n]` enters the ring *before* the
   chain, so a full-scale word summed onto near-full content can momentarily
   write above 1.0 into the ring. The ring is float, so nothing overflows; the
   clip catches it on the next lap. The transient bound is therefore
   `ceiling + peak(x)` and the steady-state bound is the ceiling.

**HOLD gates the lane's life, and switching it off CLEARS the lane** (DECIDED;
`15`). **It must clear rather than mute**, because a muted-but-circulating
buffer would stack on the next SEND: the user would hear the old word reappear
under the new one, at whatever level HOLD had hidden it. With HOLD off the lane
accepts no input, does not circulate, and **emits exact zeros** — so SEND does
nothing without HOLD. Clearing zeroes the ring, both filter states, the DC
blocker, the shaper's state, the FX stage's buffers, the modulation phases and
the time-change crossfade state. The clear is preceded by a **1 ms mute** on the
lane's output, the same fade CHOP uses, so dropping a full lane does not click:
the zeroing happens under silence. Turning HOLD on is instant and needs no ramp
— the lane is empty.

**CHOP gates the lane's output only, and never touches its contents** (DECIDED;
`15`). It sits between the lane's read and LEVEL, so a stuttered hold keeps
circulating underneath and comes back intact — testable as a **bit-identity**
rather than merely as a level (`11` §4e, test 3).

**The fade, and the honest thing about it.** A zero-length cut clicks, and
`11` §4 requires nothing above −60 dBFS on either edge of a toggle. The
specified fade is **1 ms raised cosine on both edges** (CALIBRATE), per `15`'s
"the shortest fade that does not click".

**The acceptance is band-limited, and the fade stays at 1 ms** (DECIDED, Frosty
2026-09-22). A raised cosine is C¹, so its splatter falls as 1/f³ and spreads
over roughly 1/T = 1 kHz; on musical material 1 ms is click-free by ear, but on
a sustained bright tone the first sidelobe sits on the order of 30–40 dB below
the gated signal, not 60. A broadband −60 dBFS assertion and a 1 ms gate
therefore cannot both stand, and the gate wins: CHOP is a rhythmic gate on a
delay tail, tightness is the feature, and lengthening the fade to the ~3 ms a
broadband figure needs would cost tightness at sixteenths above ~160 BPM.

So the acceptance reads: **no click above −60 dBFS on content band-limited to
5 kHz.** That is what the control is used on and what it will be judged on.
Above 5 kHz the edge is measurable and is recorded as a known figure rather
than asserted away — `14` settles it by sweep at CALIBRATE.

### 11.5 What VOICE and FREEZE were, and what their removal costs

**VOICE is cut** (DECIDED, Frosty 2026-09-21; `15`, README item 18), on the main
delay and on the lane. LOW CUT and HIGH CUT are already continuous log sweeps;
VOICE only added *resonance* on top of them, raising Q from 0.5 to 6. **The two
cuts are plain one-poles again** — §4's cascade as written — and the
state-variable pair, the `Q = 0.5 + VOICE·5.5` law and **the closed-form peak
normalisation they required are retired with it**, along with the warning that
an unnormalised Q = 6 self-oscillates at a third of the feedback travel. §3's
`P_c` is now the only normalisation in the loop, and it is computed at run time
rather than in closed form.

**FREEZE is cut, and this supersedes README Decided item 2.** The old FREEZE
closed the send, set loop gain to exactly 1.0 and **bypassed every in-loop
stage** — filters, FX, DC blocker, shaper, clip — latching the length to a whole
sample so the interpolator was bypassed too. The loop was then a **bit-exact,
non-eroding circulating buffer**: stable indefinitely, and cheaper than running.

**What that costs, said plainly:** the lane's centre detent holds at **loop
gain** unity, but the signal still laps the character chain and the filters on
every repeat. §3 is explicit that unity means the loop's **loudest band** holds,
not every band — a non-flat loop cannot do the latter and stay bounded — so
every other band decays at `|H·I| / P_c` per lap and **a long hold darkens and
colours**: tape's rolloff and head bump, the BBD's clock-derived cuts and any FX
all compound as `|H|^k`. **There is no bit-exact, non-eroding hold in v1.** On
Clean with both cuts on their rails, DRIVE 0 and FX off the chain is close to
transparent and the hold is close to bit-exact — but "close to" is the honest
word, and on any other setting a held chord is audibly a different sound after
thirty seconds than it was at one. That capability leaves v1 deliberately, in
exchange for a hold that can be filtered, chopped, layered and levelled while it
runs — with the honest footnote that **filtering it filters the main delay too**
now that the voicing is shared (§11.3). Only CHOP, LEVEL, the tail and the
lane's FX move the held sound alone.

**Ping-pong** is fully specified in §8 and applies to each engine independently,
off its own `stereo` id. No WIDTH control in v1: with the dry bit-exact below
50 % MIX the image already reads as wide wet over centred dry; if added later, a
wet-only mid/side trim after the loop tap.

### 11.6 Stability and bounds for the lane

The lane **deliberately runs above unity in the build region**. This is the
situation §3 already accepts at the main loop's top of travel, with the same
mechanism and the same bound; §3 owns `P_c` and the clip and is not re-opened
here.

- **The bound is the in-loop safety clip**: a fixed tanh, ceiling 1.0 (0 dBFS),
  always on regardless of DRIVE, sitting **after** the filters and the FX stage
  and **last in `C_lane`**, one instance per engine. Its describing-function
  gain `G(A)` falls monotonically from 1, so oscillation settles where
  `g_lane·|H_lane|·G(A) = 1` — a limit cycle just under the ceiling, not
  divergence. A build inherits the mode's tone because the clip is after the
  filters, the trade `02` flags.
- **`g_max` (§11.2) is a starting gain, not the bound.** Whatever Frosty
  settles, the clip governs.
- **The clip governs the lane's internal state, not the module's output.**
  LEVEL is applied *after* the lane's loop tap, so a lane pinned at the clip
  ceiling with `lane_level` at +24 dB puts roughly **+24 dBFS** on the wet bus.
  Nothing clips — §9's promise that the output is never clipped for the user
  still holds — but the module can now legitimately emit far above 0 dBFS, which
  no path in Dwell could before. That is why the test that proves the clip
  bounds the lane belongs at **the top of LEVEL's travel and not at unity**
  (`15`): at unity the lane never reaches the clip, so the test would prove
  nothing.
- **`lane_level` is −24…+24 dB, default 0, step 0.01** (id 20; `15`), matching
  every other level in the suite rather than inventing a range. It sets the
  lane's **loudness** against the main delay's wet, which `lane_gain` cannot:
  that one is the **tail**. Without it, the relative volume of a thrown word
  would be fixed by construction, which is wrong for a feature whose whole job
  is emphasis. Default 0 dB is unity against the main wet, and since SEND ships
  off the module is silent at defaults either way.

**Tail reporting.** §9's `tail` figure becomes **the larger of the two
engines'**, from parameters only as `latencyForParams` is: the main's per §9;
the lane's as `T_lane·ceil(60 / −20·log10(min(g_lane, 0.97)))` when
`lane_gain < 0` **and HOLD is on**; and **the 30 s clamp whenever HOLD is on and
`lane_gain ≥ 0`**, because a hold or a build does not decay. With HOLD off the
lane contributes nothing. `11` §4j's assertion — the reported tail is never
below the measured time to −60 dBFS — then still holds (DECISION, derived here).

### 11.7 The lane and tempo: one SYNC, two divisions

**There is one `sync` (id 1) and it governs both engines** (DECIDED, Frosty
2026-09-23). The module is either on the grid or it is not; each engine then
picks its own division — `note` (2) for the main delay, **`lane_note` (22) for
the lane**, both reading §7's sixteen values at the same indices. When `sync` is
off both engines run from their millisecond times, `time` and `lane_time`; when
it is on both run from their divisions, mapped by §7 and re-targeted through
§2's time-change law exactly as a knob move would be.

**Why the lane needs a division at all.** Without one, the moment `12`'s
plumbing lands the main delay locks to the grid while the lane keeps
free-running in milliseconds — and **drifts against it**. The lane's rhythmic
point is a quarter running underneath while throws land on a dotted eighth;
that cannot survive one engine following the tempo and the other ignoring it.
The gap existed because the lane was given its own TIME and nothing else
temporal.

**Why one SYNC rather than a `lane_sync`.** Wanting the main synced while the
lane free-runs is a strange thing to want, and it is still reachable: turn SYNC
off for the module and set both times in milliseconds. A second switch would
buy that one case at the cost of a second mode to reason about in every tempo
test and every preset.

**The defaults agree at 120 BPM, and that is designed, not luck.** `time` is
375 ms and `note` is 1/8D, which is 375 ms at 120 BPM; `lane_time` is 250 ms and
`lane_note` is 1/8, which is 250 ms at 120. **Enabling SYNC at 120 BPM
therefore changes nothing audible** on either engine — the silent-toggle
property `modules/vcomp`'s COMPLEX was built around. It is also why `lane_note`
defaults to **1/8 and not to the main's 1/8D**: matching the main would look
tidier and would break the agreement with `lane_time`'s own default, making the
toggle audible. `11` §4h asserts both halves.

**`lane_note` ships disabled** with `sync` and `note`, on the one
`kSyncIsEnabled` switch, until `12`'s plumbing lands. Nothing about the schema
moves when it does.

### 11.8 What the second engine changes elsewhere in this document

Named here so they are not found by surprise; §§9 and 10 carry the corrections
in place.

- **§9's MIX 50 % arithmetic was written for one wet engine.** With the lane
  running at LEVEL 0 on comparable content the output is dry unity **plus two**
  wet unities: "about +3 dB typical, +6 dB worst" becomes roughly **+4.8 dB
  typical, +9.5 dB worst**. README Decided item 5 (no auto-gain) is unchanged;
  the figure is not.
- **§10's memory figure is per engine** — two rings, so **8.0 MB per instance at
  192 kHz**. **`modules/dwell/params.h`'s `kMaxTimeMs` comment still states
  4.0 MB per instance**: that is a code comment and is flagged rather than
  edited, this being a documentation pass.
- **CPU doubles while HOLD is on and costs a branch while it is off**, so the
  module at its defaults costs what the single-engine module cost. `11` §4k's
  heaviest-case budget is marked there as needing re-measurement rather than
  quietly changed.

## 11a. In-loop FX

**Position.** One FX stage in the character chain:
`LOW CUT → HIGH CUT → mode filters → FX → DC blocker → shaper → clip` (§4).
**One stage per engine** (§11): the main delay's is `fx`/`fx_type`/`fx_amount`,
the lane's is `lane_fx`/`lane_fx_type`/`lane_fx_amount`, and everything in this
section applies to each independently. **The two stages share no state** — no
allpass buffer, no LFO phase, no sample-and-hold counter, no quantiser state —
so neither can disturb the other's buffers, and "FX off is bit-identical to the
loop without the stage" is asserted **per path** (`11` §4l). **`fx_link` (id 26)
ties the two stages' parameter *values* and never their state** (§11.3): even
with both stages set identically they run from separate buffers, and that is
what keeps the claim above true at every setting of the flag. The same holds of
the voicing the two engines now share — one `character` value, two sets of
filter state.
Before the blocker, so an offset an FX introduces is removed rather than
compounded; before the shaper and clip, so the clip stays the last thing in the
loop and §3's bound still ends there. The stage recirculates, so every candidate
**compounds per repeat** — the point of it, and the risk.

**Bound.** Each candidate is non-expanding, `|F| ≤ 1` at every setting,
peak-normalised in closed form where it could exceed unity, so §3's `|g| < 1` is
unchanged. (That rule used to be stated as "§11's VOICE rule"; VOICE is cut, so
the requirement stands on its own here.) **FX off skips the stage** — not
"amount zero" — so the loop is bit-identical to the pre-FX loop at no CPU cost.
**Nothing bypasses FX any more**: the clause that FREEZE did so goes with FREEZE
(§11.5), and the lane's centre detent deliberately does **not** bypass the stage
— an FX that changes the buffer each lap is, in the lane, the point rather than
the failure. Nothing here reads ahead, so reported latency stays **0** (§0).

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
> **Octave up, Octave down and Reverse were CUT on 2026-09-21** (Frosty; see
> `15`). The octaves compound in a feedback loop — pitch moves ±12k semitones, so
> three repeats is three octaves and the content leaves the band — and Reverse was
> the only type needing a second buffer, +2.0 MB per channel allocated whether FX
> was on or not, with the standing requirement that it must never be allocated on
> the audio thread. Cutting them removes that allocation problem entirely.
> Choice lists are append-only after ship, so this was the last moment to remove
> them. Their specifications are deleted rather than kept as dead text; the
> reasoning is here and in `15`. (The four that remained tiled as a 2×2 grid;
> Sweep then went too, so the list is three — see below.)

- **Pan / Tremolo** — one LFO stepped at the delay period, so each repeat gets its
  own position or level rather than a wobble inside one; equal-power law, AMOUNT is
  depth. Chops rather than pans on the mono bus (§8). Negligible cost.
> **Sweep was CUT on 2026-09-22** (Frosty), leaving **three** types: Diffuse,
> Pan/Tremolo, Crush. **It was cut because VOICE was cut.** Sweep was specified
> as VOICE's resonant centre being moved by `2^(±AMOUNT·k/6)` per repeat, so
> when VOICE went (§11.5) the filter it swept went with it and the candidate had
> nothing left to act on. The alternative was to give the FX stage its own
> resonant band-pass, peak-normalised in closed form — which is the filter that
> had just been deliberately removed, reappearing one section later under
> another name. **That is the non-obvious part, and it is written down so nobody
> re-adds a sweep without re-opening the VOICE decision first.** Choice lists
> are append-only after ship, so this was the last moment to remove it; its
> specification is deleted rather than kept as dead text.

**CPU**: target ≤ 1.3× the FX-off loop for any one candidate at 192 kHz, ≤ 1.5×
heaviest, **per engine** (DECISION; bench per 11 §4k). Only Diffuse should
measure; two engines both on it is roughly 2.6× a single FX-off loop, which is
why 11 §4k's heaviest-case budget is marked for re-measurement.

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
| SEND ramp | 5 ms open / 15 ms close, half-cosine | CALIBRATE, §11.4 |
| Lane detent | `lane_gain` 0 ⇒ loop peak exactly 1.000 (`g = 1/P_c`), snapped not smoothed | DECIDED (Frosty, 2026-09-21); §11.2 |
| Lane tail law | throw `(1+L)^1.6/P_c`; build `(1 + (g_max−1)L²)/P_c` | DECISION, §11.2 |
| Lane build ceiling `g_max` | **CALIBRATE** — heard in 14 §3; 1.05 is ~12 s to the ceiling, 1.10 ~6 s, 1.3 a swell | DECIDED as CALIBRATE (Frosty, 2026-09-22) |
| CHOP / HOLD-clear fade | 1 ms raised cosine, both edges; acceptance band **OPEN** | CALIBRATE, §11.4 |
| Lane LEVEL | −24…+24 dB, step 0.01, default 0 | DECIDED (Frosty, 2026-09-21) |
| FX stage position | after mode filters, before DC blocker; skipped when off; **one stage per engine**, no shared state | DECISION |
| FX loop bound | `\|F\| ≤ 1` for every candidate, normalised in closed form | DECISION |
| Diffuse | 6-stage allpass, 7–37 ms × AMOUNT | CALIBRATE; 00 §1 |
| Crush | 16→3 bits, hold ÷1–32; exempt from the alias floor | CALIBRATE / DECISION |
| Pan / Tremolo | stepped once per repeat; AMOUNT is depth | CALIBRATE |
| FX types | Diffuse, Pan/Tremolo, Crush — **three**; Sweep cut because VOICE was | DECIDED (Frosty, 2026-09-22) |
| Voicing | **Shared**: `character`, `stereo`, the cuts, the modulation and `drive` govern both engines; DUCK is main-only | DECIDED (Frosty, 2026-09-23) |
| Lane's own | TIME, NOTE, LEVEL, `lane_gain`, SEND, HOLD, CHOP, its FX trio | DECIDED (Frosty, 2026-09-23) |
| SYNC | **one switch (id 1) for both engines**; `note` (2) and `lane_note` (22) are the two divisions; no `lane_sync` | DECIDED (Frosty, 2026-09-23); §11.7 |
| Silent SYNC toggle | 375 ms ↔ 1/8D and 250 ms ↔ 1/8 both hold at 120 BPM, so enabling SYNC there is inaudible | DECIDED (Frosty, 2026-09-23); §11.7 |
| FX LINK | id 26, default on, ties the lane's FX trio to the main's; no seeding | DECIDED (Frosty, 2026-09-23) |
| Engine structure | **one reusable engine instantiated twice**, so the module can be split later | DECIDED (Frosty, 2026-09-23); §11.1 |
| FX AMOUNT default | 35 % | DECISION |
| FX CPU ceiling | ≤ 1.3× FX-off, ≤ 1.5× heaviest | DECISION |

## Open decisions for Frosty

1. Ping-pong on a stereo source: sum to mono (specified), or keep L/R?
