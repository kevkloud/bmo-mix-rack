# De-Esser DSP Spec

Math and prose only. **01** = `01-reference-behavior.md`, **02** =
`02-design-approaches.md`. `Fs` = sample rate; `τ` = one-pole time constant
(63.2 % convention, as in the existing dynamic-EQ module).

## 1. Topology

**A single dynamic-EQ cut (02 §3) driven by a relative band-to-fullband detector
(02 §4), TPT/SVF realised, zero latency, no oversampling, no lookahead.**

Wideband (02 §1) dulls by definition; split-band (02 §2) sums flat only at equal
band gains, so at 2–6 dB the crossover edges comb, and an FIR split trades that
for latency and pre-ring on the sharp onsets 01 §4 warns about. A bell or shelf
has no split to reconstruct, so reconstruction error is identically zero at
every depth; 02 rates it most surgical and least dulling, and its one cost,
modulation noise, is budgetable (§5).

**Modes.** One permanent choice parameter, `shape` — DEQ's id and its "Bell" /
"High Shelf" labels (11 §3): *bell* or *high shelf*. The shelf is the
split-band mode without a crossover, from one minimum-phase filter that cannot
mis-reconstruct. No wideband or crossover mode, and no MIX — on a minimum-phase
cut that is a shallower cut of nearly the same shape, which RANGE already
gives; if one is ever wanted it appends at the end of the list, never inserts.

**Not done:** STFT/ML detection (02 §5) — block latency breaks the zero-latency
preference, per-instance FFT or inference breaks "many instances", and 01 §5
found no citable source for a neural sibilance detector. No lookahead either
(01 §6 Low-Medium; §6's attack makes it unnecessary) — so there is no LOOKAHEAD
parameter and `latencyForParams()` reads 0 at *every* setting, not merely at
the default, which is what the test in 11 §5 asserts.

## 2. Signal flow

**(a) Detection path:** per channel a first-order 150 Hz high-pass feeds the
*fullband reference* rectifier and a *band* bandpass (high-pass in shelf mode)
at the user's f0 and width. Both come from the **dry** input, never the moving
output filter, whose poles track its own gain — tapping it would close a
feedback loop. **(b) Relative detector** → one level-independent *prominence* in
dB (§3). **(c) Gain computer**: threshold, soft knee, slope, range → an offset
≤ 0 dB (§4). **(d) Smoothing**: in the envelope (§6) and the coefficient glide
(§5). **(e) Reduction element**: one TPT SVF per channel, gain = the offset.
**(f) Listen**: momentary, panel-held, never a parameter — it outputs
`H(x) − x`, the sibilance being removed. The hook it rides on and the rule that
no listen state is ever saved are 11 §3.

## 3. Detection math

**Band filter.** Second-order constant-Q bandpass, `f0 ∈ [2, 10] kHz` (01 §6,
High; the `freq` parameter), `Q ∈ [0.7, 6]` default 2.5 (`q`) — narrow enough to exclude the vowel region,
wide enough to cover male ~3–6 kHz and female ~6–8 kHz concentrations (01 §6,
Medium) at one setting. Shelf mode detects through a second-order high-pass at
the same corner, matching what is cut.

**Level-independent detector.** Band and reference levels are power-summed
across channels — decorrelation-safe, unlike a mono sum, and it forces identical
gain on both channels so the image cannot wander:

    b[n] = sqrt( (1/N) Σ_c band_c[n]² )      w[n] = sqrt( (1/N) Σ_c ref_c[n]² )

Three envelopes: `B` = fast decoupled peak envelope of `b`; `W` = fast envelope
of `w`, same timing; `S` = slow RMS envelope of `b`, τ = 500 ms, the programme's
brightness memory. Prominence, dB:

    P[n] = 20·log10(B) − [ κ·20·log10(W) + (1−κ)·20·log10(S) ] − P_ref

**Normalisation.** Every term is a log of a level, so an input gain adds the
same constant to `B`, `W` and `S` and cancels exactly at every `κ` — the
threshold never needs re-riding across a take (01 §2, the reason this detection
style exists). `P_ref` places 0 dB prominence at typical vocal balance;
CALIBRATE.

**κ is internal in v1 — one fixed constant, not a parameter.** `κ = 1` is
classic relative detection, band against current fullband level. `κ = 0` is
self-referential, band against its own recent average — the guard against
*bright non-vocal material* and *cymbal bleed* (02 §4's named failure), since
steady brightness raises `S` and stops triggering while a short burst still
rides above it. v1 ships **one middle value, first pass 0.6, CALIBRATE**: it is
fitted by ear once the DSP exists, and nothing about the schema depends on
which value wins. Exposing it is §11.

**Floors.** Two absolute gates, both hard-zeroing the offset: reference above
**−55 dBFS** — what stops noise, room tone and silence producing huge prominence
as `W → 0` — and band envelope above **−60 dBFS**, which also catches breaths.
`S` is clamped to at most 20 dB below the κ=1 reference, so the adaptive term
cannot chase a fade-out downward. **Already-dull sources** fall out of the
detector's definition — low prominence, no action — and are bounded by RANGE.

**Peak vs RMS.** `B` and `W` are decoupled *peak*: sibilance is a short noise
burst and RMS smears the onset. `S` is RMS, a level estimate not an event
detector. **Hold/hysteresis:** 5 ms hold and 1.5 dB of threshold drop while
engaged, so a /s/–/t/ cluster is one event. All CALIBRATE.

## 4. Gain computer

    over = P − T
    knee = 0                          over ≤ −W/2
         = (over + W/2)² / (2W)        |over| < W/2
         = over                        over ≥ W/2
    offset_dB = −min( Range, (1 − 1/R) · knee )

`T` = THRESHOLD in prominence-dB (`thresh`, −24…+24, 0 at typical vocal); `W` =
6 dB fixed knee (01 §2; a hard corner snaps audibly on 60 ms events); `R` = 4:1
fixed internal slope; `Range` = RANGE (`range`), 1–18 dB. Static curve: flat to `T − 3`, a
parabola through `T` at −(1−1/R)·W/8 ≈ −0.56 dB, then 0.75 dB of cut per dB of
prominence, flattening hard at −Range. The 2–6 dB working point (01 §4) sits in
the upper knee and lower linear region, the smoothest part.

## 5. Reduction element

One TPT SVF per channel, coefficients from the matched-Z zero-latency design
proven in this repo's dynamic-EQ module — bell after Vicanek, shelf as the
reciprocal of its boost (§7: why not bilinear).

**Update law.** Every 8 samples the target `(g, k, m0, m1, m2)` is re-derived
from smoothed controls plus the current offset; between ticks each coefficient
advances by a fixed per-sample increment. **Stable by construction:** `g > 0,
k > 0` is the SVF's stability condition and holds everywhere on a straight line
between two stable sets, so no intermediate state can ring however fast the
detector moves. A direct-form biquad has no such guarantee (02 §3).

**Modulation/zipper.** The gain trajectory is bandlimited by the attack pole: at
τ = 0.8 ms it is −3 dB at 199 Hz and ≈40 dB down at 20 kHz. The glide adds a
ramp whose sidelobes fall as 1/f², and the 8-sample interval is 5.5 kHz at
44.1 kHz — 27× the envelope's bandwidth, so the piecewise-linear path adds no
stair.

**Aliasing.** Plainly: **no oversampling is needed; the shared oversampler must
not be used.** A time-varying filter is not a nonlinearity and generates no
harmonics. The only folding mechanism is convolution of the signal spectrum with
the gain spectrum, whose sum components above Nyquist wrap; with the gain's HF
content ≥40 dB down and depth ≤ 0.87 linear (18 dB), folded products sit far
below the sibilance's own noise floor. CALIBRATE: steady tone at `f0`, detector
at maximum rate and depth, bar −80 dBFS. No crossover is used, so 02 §2's
reconstruction and phase consequences do not arise; the filter is minimum-phase
and shifts phase only while reducing.

## 6. Times and programme dependence

Sibilant events run ~60–200 ms (01 §1, folklore). **Attack τ = 0.8 ms** (01 §6
allows <1–20 ms): reduction is ~90 % applied 2 ms into an onset, so no lookahead
is needed to avoid audible overshoot, yet it does not track noise peaks at audio
rate. **Release, two branches.** Fast, τ = 30 ms, is the default and covers a
normal ess tail — reduction is gone within ~90 ms, comparable to the event,
which is why it does not pump HF ambience: the tail is never held down, and
because detection is relative a decaying tail's prominence falls below threshold
anyway. Slow, τ = 120 ms, crossfades in only after >150 ms continuously over
threshold — a sustained bright passage, not a phoneme — so long sibilants do not
chatter. CALIBRATE within 01 §6's 2–60 ms band.

Both ship **fixed, not as parameters**: 01's durations are narrow enough that
the programme-dependent release covers the spread, and parameter order is
append-only, so they can be added later. If they ever are, they append at the
end of 11 §3's list as `logParam` milliseconds *ascending* — LTV Comp's idiom,
not BMO FET's knob position, since no printed knob is being modelled here.

## 7. Sample-rate independence

All time constants derive as `a = exp(−1/(τ·Fs))`, identical from 44.1 to
192 kHz. The 8-sample control interval is a *rate*, so the glide must be
specified in milliseconds and converted to a per-sample increment, never in
ticks, or modulation behaviour changes with `Fs`.

**Near-Nyquist.** Bilinear designs cramp a 10 kHz bell at 44.1 kHz — the top of
01's range — and the usual fix, oversampling the EQ, costs the latency this
module refuses. Matched-Z needs no warping correction: it maps the pole exactly
and fits the zeros to the analogue magnitude. `f0` still clamps to
`[2 kHz, min(10 kHz, 0.45·Fs)]` (19.8 kHz at 44.1 kHz, so it never bites, but
hosts send anything), and the detector skirt clamps alike. Q clamps to
`[0.1, 40]` and depth to ≤ 30 dB, so no parameter combination yields a
non-finite coefficient.

**Smoothing:** the per-parameter constants are the *smooth* column of 11 §3's
schema table and are not repeated here. Two rules belong to the DSP rather than
to the schema: `freq` and `q` glide one-pole in the *log* domain, and `shape`
crossfades the two contributions rather than jumping coefficients. The first
set after prepare/reset is snapped.

## 8. Metering

`currentGainReductionDb()` returns the **peak band reduction** — the magnitude
of the currently *applied* (glided, not target) offset, signed positive for
reduction per the interface, 0…18 dB, inside the shared 24 dB meter. Explicitly
*not* wideband-equivalent: a 6 dB bell cut at Q 2.5 removes under 1 dB of
broadband energy, so a wideband meter would read ~0.5 dB exactly when the user
is doing the 6 dB of work 01 §4 describes, and the published "2–4 dB" rules of
thumb would read wrong. RANGE caps at 18 dB, so on the shared 24 dB scale the
needle cannot pin in use; a value forced past the scale clamps rather than
wraps. `latencyForParams()` returns 0 always, at every setting.

## 9. Fixed values to target

**The five user parameters are deliberately not listed here.** `freq`, `q`,
`thresh`, `range` and `shape` — their ids, captions, ranges, defaults, skews,
units, smoothing and order — are the **v1 schema table, 11 §3**, which is the
single authoritative copy; the reasoning behind each figure is §§3–7 above.
Duplicating the table is how the two documents drifted apart in the first
place.

What follows is the internal constants only: fixed at first ship, invisible to
the host, and free to be retuned right up until it.

| Value | Target | Source / confidence |
|---|---|---|
| `P_ref` — where 0 prominence-dB sits | typical vocal balance | CALIBRATE |
| Reference blend `κ` | 0.6 first pass, internal in v1 (§11) | no figure; CALIBRATE by ear |
| Slope `R` / knee `W` | 4:1 / 6 dB | 01 §2 Medium; CALIBRATE |
| Attack / fast release τ | 0.8 / 30 ms | 01 §6 Medium |
| Slow release τ / engage | 120 / 150 ms | 01 §1; CALIBRATE |
| Slow reference `S` τ / clamp | 500 ms / 20 dB below the κ=1 reference | no figure; CALIBRATE |
| Hold / hysteresis | 5 ms / 1.5 dB | 02 §6; CALIBRATE |
| Reference HPF / gates | 150 Hz 1st order; −55, −60 dBFS | CALIBRATE |
| Engine clamps | `f0` ≤ 0.45·Fs, Q 0.1–40, depth ≤ 30 dB | robustness; hosts send anything |
| Channel link | power-sum, always linked | 01 §3 High |
| Control interval / latency | 8 / 0 samples | repo; constraint |
| Working reduction | 2–6 dB (2–4 usual) | 01 §6 Medium; target |

## 10. Open questions and risks

1. **`P_ref` and the threshold scale are the usability story.** If the default
   does not sit near the top of a typical vocal's prominence distribution the
   module feels broken. Fit it against dry takes, male and female, before any
   listening round.
2. **One fixed κ may not suit every source.** The whole bet of v1 is that a
   middle value is good enough on a solo vocal, a vocal over a bright bed and a
   full mix alike. If it is not, that is what §11 is for — and it is a
   listening finding, not a measurement.
3. **Cymbal bleed is not solved.** No level-domain detector separates a hat from
   an /s/; a lower κ reduces steady-state false triggering, not coincident hits.
   Spectral flatness (01 §5) is the honest fix, deferred — and if added it
   belongs behind the existing parameters, not as a mode.
4. **Fast release plus narrow Q may still breathe** on reverberant sources at
   maximum range. Mitigation is a release floor, not a parameter.
5. **Shelf mode risks dulling** — the one shape that takes down everything above
   the corner. It may need a lower RANGE ceiling than the bell, applied inside
   the engine: `range` is one parameter whatever the shape, exactly as DEQ
   caps a shelf's Q behind an unchanged knob.
6. **The aliasing claim in §5 is reasoned, not measured.** The one place this
   spec could be wrong at redesign cost; measure it in a DSP test first.
7. **Fixed attack/release** bets that 01's durations generalise. Appending them
   later is schema-safe; retuning the release around them is not.

## 11. Potential: ADAPT

**Not in v1 — owner's decision, 2026-09-20: the idea is liked, but it has to be
proven before it earns a control.** κ stays one internal constant (§3) until
then. What the control would be, in plain words:

- **One end compares the band with the whole signal, right now.** Every sibilant
  is caught, however the singer is riding the mic — but material that is
  *constantly* bright, a cymbal-heavy bed or an airy synth, is treated
  constantly too, and the result dulls.
- **The other end compares the band with the band's own last half second.** A
  steady bright programme stops triggering the moment the detector has learned
  it, so the module leaves bright material alone — but a long or dense run of
  sibilance teaches the detector that sibilance is normal, and gets
  under-treated.

**It needs testing before it ships as anything.** The four sources that decide
it are a solo vocal, a vocal over a bright bed, cymbal bleed, and a full mix;
the question each one answers is whether one fixed middle κ is already good
enough, and if not, whether the useful settings are a continuum or two named
places. **Knob or switch — and what the two ends are called — comes out of that
listening, not out of this document**; a switch cannot later become a knob.

**Schema consequence.** ADAPT can only ever be **appended after `shape`**,
never inserted, so shipping v1 without it costs nothing but its absence. The
DSP is written with κ as a variable regardless (§3), so exposing it later is a
parameter and a panel control, not a redesign.
