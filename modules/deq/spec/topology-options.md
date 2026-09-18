# How BMO Parametric's bands combine: parallel, serial, or hybrid

**Status: serial chosen 2026-09-10, pending the listening test in
`testing-notes/deq-topology-listening.md`.** Every number here is from
`measure_deq topology` (48 kHz, centred source, the shipping matched-Z
designs). Re-run it to reproduce any row. (Written while the product was still
called BMO Parametric; it is BMO DEQ.)

## The three options

| | formula | where it comes from |
|---|---|---|
| **Parallel** | `out = x + Σ (H_k·x − x)` | the spec's C4; TDR Nova describes itself as a parallel dynamic EQ |
| **Serial** | `out = H_n(… H_2(H_1(x)))` | the conventional parametric EQ |
| **Hybrid** | cut filters in series, gain bands in parallel | not in the spec; evaluated here because it fixes parallel's worst failure |

All three are zero latency and cost the same: 24 bands measured 42 vs 50 µs
per block static, 82 vs 80 dynamic — noise, since the filters are identical.
**Latency and CPU do not decide this.** Only the sound, and what the curves
on screen mean, do. Parallel and serial are both implemented in `DspCore`;
hybrid would be a few lines.

## What changes, measured

### 1. Stacking bands on one frequency

Serial adds in dB. Parallel does not, and for cuts it is not monotonic:

| two bells at 1 kHz, Q 1, each | serial | parallel |
|---|---|---|
| −24 dB | −48.0 | **−1.2, polarity inverted** |
| −12 dB | −24.0 | **−6.1, inverted** |
| −9 dB  | −18.0 | −10.7, inverted |
| −6 dB  | −12.0 | **−51.8** (a near-perfect notch) |
| −3 dB  | −6.0  | −7.6 |
| +6 dB  | +12.0 | +9.5 |
| +12 dB | +24.0 | +16.9 |
| +24 dB | +48.0 | +29.7 |

In parallel, each band contributes `G − 1`. Two cuts of G < 0.5 sum past −1,
so the result flips polarity and *rises* as the cuts deepen. At exactly −6 dB
each (G ≈ 0.5), the sum lands on zero and carves a −52 dB notch. A user
pulling a second cut down through that range would hear it vanish, then turn
into a notch, then come back. Boosts behave gently: stacking compresses rather
than adds. Some people hear that as a feature.

Wherever the table says "inverted", the parallel sum is also **not minimum
phase**: its zeros leave the unit circle. That means excess phase shift
around the band. A serial chain of minimum-phase bands is always minimum phase.

### 2. How far apart before it stops mattering

Worst |parallel − serial| across the band for two bells an interval apart, dB:

| pair | Q | unison | 1/3 oct | 2/3 | 1 oct | 2 oct | 3 oct |
|---|---|---|---|---|---|---|---|
| +6 / +6 | 1 | 2.5 | 2.4 | 2.2 | 1.9 | 1.1 | 0.6 |
| +6 / −6 | 1 | 3.5 | 3.4 | 3.0 | 2.6 | 1.4 | 0.7 |
| −6 / −6 | 1 | 37.7 | 19.6 | 10.2 | 6.0 | 1.8 | 0.7 |
| +12 / −12 | 1 | 10.2 | 10.0 | 9.5 | 8.8 | 5.9 | 3.3 |
| −12 / −12 | 1 | 17.9 | 16.0 | 11.7 | 8.6 | 20.3 | 4.5 |
| −6 / −6 | 4 | 32.7 | 4.0 | 1.2 | 0.7 | 0.2 | 0.1 |

Narrow bands separate within about an octave. Broad cuts interact across two
or more octaves. The −12/−12 pair at two octaves is 20 dB different because
the two contributions cancel between the bands.

### 3. Does a cut filter still cut?

This is parallel's most practical problem. A low cut is `x + (HP − 1)x`, and a
boost overlapping it adds `(G − 1)x` back:

| setting | probe | cut alone | serial | parallel | hybrid |
|---|---|---|---|---|---|
| low cut 80 Hz + low shelf +6 @ 100 | 20 Hz | −24.1 | −18.1 | **−0.5** | −18.1 |
| low cut 80 Hz + bell +6 @ 60, Q 1 | 25 Hz | −20.3 | −19.0 | −8.9 | −19.0 |
| high cut 12 k + high shelf +4 @ 10 k | 20 kHz | −10.0 | −6.2 | −1.8 | −6.2 |

Low cut plus low shelf is one of the commonest moves on any EQ. In parallel,
the rumble the low cut is there to remove comes straight back. The hybrid
fixes this exactly, and only this.

### 4. Settings people actually use

| setting | worst difference |
|---|---|
| vocal: LS −3 @ 120, bell −2.5 @ 350, bell +2 @ 3 k, HS +3 @ 10 k | 0.25 dB |
| kick: bell +4 @ 60, −5 @ 400, +3 @ 4 k | 0.12 dB |
| bus: LS +1 @ 80, HS +1.5 @ 12 k | 0.00 dB |
| broad tilt: +3 @ 200 Q 0.5, −3 @ 2 k Q 0.5 | 0.34 dB |
| surgical: −12 @ 2.5 k Q 8, −9 @ 3.1 k Q 8 | **6.84 dB** |

For well-separated, moderate bands, the choice is nearly inaudible. It
matters when bands overlap, when cuts are deep, and in surgical work: exactly
what a parametric EQ gets used for when it is being relied on.

### 5. Dynamics make it worse, and move it in time

A dynamic band sweeps its gain, so it passes through the regions above:

| gesture, at full excursion | worst difference |
|---|---|
| de-ess: dynamic bell → −10 @ 7 k over static HS +4 @ 8 k | 3.98 dB |
| resonance tamer: dynamic → −12 @ 2.5 k Q 4 over static +3 @ 2 k | 5.10 dB |
| low end: dynamic LS → −6 @ 100 over static bell +4 @ 80 | 2.36 dB |

In parallel, how much a dynamic band actually cuts depends on what the static
bands around it are doing. The same threshold and range give a different
result depending on the neighbours. In serial, a band's gain reduction is its
own, and it adds.

(In both topologies every detector listens to the dry input, so detection is
the same and order-independent. Only what the gain does to the audio differs.)

### 6. The curve on screen

- **Serial:** the total curve *is* the band curves added in dB. What you see
  is what you get, and a band's drawn curve is exactly what it contributes.
- **Parallel:** the total has to be computed as a complex sum. The per-band
  curves drawn on screen do not add up to it, and in the cases above they
  disagree by tens of dB.

### 7. What parallel does better

- **Boosts are self-limiting.** Stacking boosts compresses (+12 and +12 gives
  +17, not +24). It's hard to over-boost by accident.
- **"Band solo" is exact:** each band's contribution `H_k·x − x` is
  independent of the others. In serial, a soloed band's contribution also
  includes whatever the bands before it did (negligible unless they overlap).
- **It is what the spec's competitive target (Nova) does**, so it is the
  closer match for a "Nova parity" claim.

## Recommendation

**Serial**, for three reasons:

1. `products/AGENTS.md` defines BMO Parametric as the suite's *general-purpose*
   EQ: "continuous frequency, continuous Q, and clean." A general-purpose
   parametric has to do what its curves show. §1, §3 and §6 are all cases of
   it not doing so.
2. §3 is a correctness problem in ordinary use, not a matter of character.
3. Serial costs nothing: same latency, same CPU, and every other decision
   (M/S, dynamics, smoothing, zero latency) is already built for both.

If parallel's character is wanted, the **hybrid** is the defensible form of
it: it keeps the soft stacking of boosts and fixes the cut filters. It still
carries §1's non-monotonic stacked cuts, §5's neighbour dependence and §6's
curves that don't add. A **per-instance switch** is possible too (both
engines exist), but it doubles what every preset and test has to mean, and a
preset built in one mode would sound different in the other.

If the choice is serial, spec C4 is revised in the same commit, and the
`Topology` default in `DspCore.h` flips.
