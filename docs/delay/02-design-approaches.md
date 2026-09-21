# Delay Module: Design Approach Survey

Survey conducted on AURORA, 2026-09-20. Research-only, no implementation choices.

## Fractional-delay interpolation

| Method | HF loss | Under modulation | CPU |
|---|---|---|---|
| Linear | Significant, rises with fractional offset | Smooth, no discontinuities; safe default for pitch-shifting | Lowest |
| Lagrange 3rd–5th order | Moderate, improves with order near Nyquist | Smooth; higher orders reduce zipper/HF smear during sweeps | Low–moderate |
| Thiran allpass | Near-flat magnitude, group-delay error instead | Per-sample coefficient recompute under modulation can click/ring; poor for fast sweeps | Moderate |
| Windowed sinc | Best, tunable via kernel length | Large kernels costly to recompute; usually fixed-ratio use | High (scales with taps) |
| Hermite/cubic | Near Lagrange 3rd order, smoother phase | Good modulation behaviour, common in tape-style delays | Low |

Linear/Hermite dominate modulated ("tape") delays for CPU/artefact balance; Thiran suits fixed or slowly-varying taps where phase accuracy outweighs glide smoothness (Välimäki & Laakso 1996).

## Time-change behaviour

- **Pitch-glide ("tape")**: varies the delay-line read rate, producing pitch bend during time changes — the expected tape-echo character. Needs smooth interpolation (linear/Hermite) to avoid zipper noise.
- **Crossfade ("digital")**: two read pointers cross-fade old/new delay time; avoids pitch bend but costs a second tap and transient smear during the fade.
- **Slew-limited**: rate-limits the delay-time parameter itself; a middle ground, bounded by a settable slew time; simplest, least "characterful."

Trade-off is artefact type (pitch bend vs momentary doubling vs perceived lag), not a clear winner.

## Feedback-loop design

- **In-loop filters** (tone, tilt, resonant) shape repeats cumulatively; small filter errors compound over iterations.
- **Saturation in the loop**: each pass feeds the shaper's harmonics back in, so aliasing accumulates per repeat, not per pass. Options: oversample the shaper (2–4x typical), antiderivative antialiasing (ADAA) for cheaper click-free alias suppression, or band-limit before the shaper (cheapest, dulls character).
- **DC handling**: asymmetric saturators need a DC blocker in- or pre-loop to stop offset growth over repeats.
- **Stability at ≥100% feedback**: needs an explicit gain ceiling, soft clip, or limiter in the loop, or unity-plus feedback diverges; the clipper can double as the "runaway" character control.
- Limiter/soft-clip placement (pre- vs post-filter) trades tone against safety.

## Tempo sync and host-tempo smoothing

Synced delay time must track host tempo without zipper noise: smooth the target delay-time parameter (not the read pointer) via the same slew/crossfade approaches above. Tempo ramps need continuous re-targeting; tempo jumps and transport stop/start need either a fast crossfade or a deliberate glide, a per-product character choice.

## Ping-pong / cross-feedback / dual topologies

Ping-pong alternates repeats hard left/right; general cross-feedback uses a 2x2 matrix between L/R lines with independent self- and cross-feedback coefficients. Mono input needs an explicit split/pan-in stage before the matrix, or ping-pong collapses to one side.

## Ducking detector design

An envelope follower on the dry input (fast attack, slower release, typically) attenuates the wet signal so repeats duck under live input and re-emerge in gaps — standard technique, independent of the delay-line implementation.

## Modelling tape/bucket-brigade character

Bucket-brigade character comes from clock-derived anti-alias/reconstruction filtering and companding: Raffel & Smith, "Practical Modeling of Bucket-Brigade Device Circuits," DAFx-2010 [PDF](https://www.dafx.de/paper-archive/2010/DAFx10/RaffelSmith_DAFx10_P42.pdf); Holters & Parker, "A Combined Model for a Bucket Brigade Device and its Input and Output Filters," DAFx-2018 [PDF](https://www.dafx.de/paper-archive/2018/papers/DAFx2018_paper_12.pdf). Tape character combines wow/flutter, noise-modulated speed, head/gap loss, and compander/bias effects: Chowdhury, "Real-Time Physical Modelling for Analog Tape Machines," DAFx-2019 [PDF](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_3.pdf); Arnardottir, Abel & Smith, "A Digital Model of the Echoplex Tape Delay," AES 125 (2008). Fractional-delay foundations: Välimäki & Laakso, "Splitting the Unit Delay," IEEE Signal Processing Magazine, 1996 [link](https://research.aalto.fi/en/publications/splitting-the-unit-delay-tools-for-fractional-delay-filter-design). VA/nonlinear-loop background: Zavalishin, "The Art of VA Filter Design" [PDF](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_1.1.1.pdf).

## CPU / memory / latency / oversampling

Given zero reported latency, many rack instances, and 44.1–192 kHz: interpolation and in-loop saturation must stay lookahead-free, so shaper oversampling is the main CPU lever, traded against instance count at high rates. Memory scales with max delay time x sample rate x channels; storing the buffer at base rate and oversampling only around the shaper is cheaper than an oversampled buffer. At 192 kHz, per-sample costs roughly double vs 96 kHz, multiplied across rack instances.

## Comparison and shortlist

| Architecture | Character strength | CPU/latency profile | Complexity |
|---|---|---|---|
| Digital delay, Lagrange/Hermite interpolation, in-loop filter + soft clip | Clean, flexible, mild vintage flavor optional | Low, zero-latency | Low |
| Tape-style: pitch-glide interpolation + wow/flutter LFO/noise + compander-like loop shaping | Strong tape character | Moderate (oversampled shaper) | Moderate–high |
| Bucket-brigade-style: clock-modelled anti-alias/reconstruction filtering + companding + saturation | Strong BBD character, distinct from tape | Moderate–high (filter modelling + shaper oversampling) | High |

No architecture is preferred here; presented neutrally for the design team's decision.
