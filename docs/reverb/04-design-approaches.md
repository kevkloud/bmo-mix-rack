# 04 — Design approaches (literature survey)

Survey only. No decision is made here. CPU/memory figures are order-of-magnitude
estimates from the published structures, **not measured** on any machine.

---

## 1. Late-reverberation networks

**Schroeder comb/allpass (Schroeder, JAES 1962).** Parallel combs summed into
series allpasses, or an allpass chain alone. Cheap and instructive, but echo
density grows only as the comb count; four to eight combs give an audible
flutter and strong metallic ringing on transients, and the modal distribution is
uneven. Decay is set by comb feedback gain from the comb length (g = 10^(-3L/T60·fs)),
which is accurate per comb but broadband only. No damping without extra filters.
Cheap (≈4 MACs/comb), memory ≈ sum of comb lengths (~0.1–0.3 s).

**Moorer (Computer Music Journal, 1979).** Adds (a) a tapped FIR early-reflection
front end taken from a geometric simulation of a real hall, and (b) a one-pole
lowpass inside each comb loop, giving frequency-dependent T60 — the first
published "damping" control in this lineage. The lowpass makes the tail
measurably closer to real rooms; the density problem remains.

**Gardner nested allpass (chapter in *Applications of DSP to Audio and Acoustics*,
Kluwer, 1998).** Allpasses whose delay contains further allpasses, wrapped in a
feedback loop with a damping lowpass. Very high echo density for the delay
budget and a smooth onset; the classic small-room/medium-hall topologies are
published with delay tables. Decay is set by one loop gain, so T60 accuracy is
good but the loop is easy to push into audible ringing if gains are mistuned.
Nesting makes density cheap: tens of MACs, tens of kilobytes.

**Dattorro plate loop (JAES 45(9), 1997, "Effect Design, Part 1").** The figure-
of-eight (two-halves) tank: an input diffusion allpass chain feeding a loop of
modulated and unmodulated allpasses plus damping lowpasses, with output taps
drawn from both halves for stereo. Published in full with delay lengths and
coefficients at 29.761 kHz, so it must be rescaled. Character is dense, smooth
and plate-like almost immediately, with a fast onset. Two controls do most of
the work (`decay`, `bandwidth`/`damping`). Modulation of the two loop allpasses
is explicitly part of the design and is what suppresses the metallic tone; too
much gives obvious chorus on sustained tones. The multi-tap output sum is an
effective decorrelator and is mono-safe in practice. Cheap: ~10 delay lines,
~40–60 ops/sample, <100 kB.

**FDN (Jot & Chaigne, AES 90th Conv., preprint 3030, 1991; Jot, DAFx 2000,
absorbent allpass).** N delay lines recirculated through an N×N lossless matrix,
each line followed by an *absorbent* filter whose magnitude realises a target
T60(f). This is the only approach in the list where frequency-dependent decay is
**designed** rather than tuned: the filter magnitude is derived directly from
T60(f) and the line length, so low/mid/high decay multipliers are accurate and
independent of the network. Matrix choice: Hadamard (fast, N log N via a
Walsh–Hadamard butterfly, maximally mixing), Householder (N adds + one
broadcast, very cheap), random orthogonal (dense, N² MACs). Delay lengths are
chosen mutually prime and spread logarithmically to avoid coincident modes and
to hit a target mode density (≥ ~0.15 modes/Hz is the usual rule of thumb) and
echo-density build-up; Schlecht & Habets quantify build-up and mixing time
(*IEEE/ACM TASLP*, 2017) and show that colouration is a modal-excitation problem
(DAFx-20in21) that time-varying **orthogonal** feedback matrices mitigate while
remaining provably stable (*JAES*, 2015; "Scattering in FDNs", 2020). N=8 with
Hadamard at 48 kHz: ~60–90 ops/sample, ~100–200 kB for a 3 s tail; memory scales
linearly with sample rate (×4 at 192 kHz).

**Allpass-loop "ring" reverbs (Griesinger, AES 7th Int. Conf., 1989, and
patents).** A long loop of allpasses and delays with multiple injection and
extraction points and slow random modulation. Publicly described in outline
only; the smooth, non-metallic, wandering character is widely attributed to the
modulation scheme rather than the topology. **Flagged:** no complete published
coefficient set exists, so any implementation is a reconstruction.

**Scattering delay networks (De Sena, Hacıhabiboğlu, Cvetković & Smith,
*IEEE/ACM TASLP* 23(9), 2015).** Delay lines between scattering junctions placed
at first-order reflection points, so ER and late tail come from one physically
parameterised network. Renders first-order reflections exactly. Excellent
geometric control, but the sound is a room simulation rather than a mix-friendly
effect, and per-band wall filters at each junction are costly.

**Velvet-noise / sparse-FIR late reverb (Karjalainen & Järveläinen, AES 111th
Conv., 2001; Välimäki, Holm-Rasmussen, Alary & Lehtonen, *Applied Sciences* 7(5),
2017; Välimäki & Prawda, interleaved velvet noise, *IEEE/ACM TASLP* 29, 2021).**
Sparse ±1 impulses at ~1000–3000/s, filtered in a few bands. Perceptually smooth
and completely free of modal ringing; decay shape is set per band by the
envelope, so non-exponential decays are easy. Costs only the impulse rate in
adds, but needs a long buffer and gives no cheap, continuous T60 knob — changing
decay means regenerating or re-enveloping the sequence.

**Hybrid convolution + algorithmic.** Measured or synthesised ER convolved,
algorithmic tail behind it. Best realism, but partitioned convolution adds
latency or heavy zero-latency partitions, and ER length is fixed by the IR.
Rules itself out against a zero-reported-latency requirement unless the ER block
is short enough for direct-form FIR.

---

## 2. Early-reflections generators *(priority section)*

**Tapped delay line with per-tap gain/pan/filter.** The workhorse. One shared
delay buffer, N read taps, each with gain, L/R pan and optionally a one-pole or
shelving filter. Taps are near-free (1 read + 1–2 MACs); **per-tap filters are
the cost driver** — a one-pole per tap roughly triples per-tap work, a two-band
shelf roughly quintuples it, so 24 filtered taps ≈ the cost of a small FDN.
Controllability is total: every reflection's time, level, colour and position is
a parameter.

**Image-source method (Allen & Berkley, *JASA* 65(4), 1979).** For a shoebox,
mirror the source across the six walls recursively; tap time = distance/c, gain =
1/distance × Π(wall reflection coefficients), direction = image bearing. Gives a
physically consistent tap set from room dimensions plus source/listener
positions — the natural way to derive room/hall/chamber ER presets offline and
bake them into tables. Cost is entirely offline; runtime is just a tapped line.
Order 2–3 is usually enough before handing over to the late network.

**Ray-traced or measured tap sets.** Peak-picked from a real hall's IR or from a
ray/beam tracer. Most convincing, least parametric; needs source material and
careful peak picking or it drags room colour along with it.

**Moorer's published ER tables (1979).** A 19-tap pattern derived from a
geometric simulation of a concert hall, given with times and gains. Directly
usable as a hall preset and as a sanity reference for image-source output.
**Flagged:** exact tap values were not re-read from the paper for this survey.

**Diffused ER / "early diffusion".** A short allpass chain (or a small
multichannel diffuser — a 4×4 Hadamard stage of short delays) placed after or
instead of discrete taps. Turns a sparse, flamming tap set into a smooth
pre-tail wash and is the standard cure for metallic tap combing. Costs a handful
of allpasses; the trade-off is that transient definition and per-reflection
control are lost as diffusion rises, so exposing it as an ER **diffusion/shape**
control (discrete ↔ diffuse) covers both tastes.

**Velvet-noise ER.** A short, sparse ±1 burst with a shaped envelope. Denser
than a tap set at the same cost, and inherently free of the regular combing that
evenly spaced taps produce; less controllable per reflection.

**SDN as unified ER + late.** First-order reflections exact, later orders
approximated by the same network — elegant, but it couples ER control to the
late network's geometry.

**Routing: series, parallel, crossfeed.** *Series* (ER output feeds the late
network) gives a natural build-up — the tail inherits ER timing and colour, echo
density climbs smoothly, and the late onset is automatically masked — but ER and
late level can no longer be balanced independently. *Parallel* (dry split feeds
both) gives independent ER/late mix, but the late onset can arrive as an audible
second event unless pre-delayed and diffused. *Crossfeed* (parallel plus a
scaled ER→late send) is the common compromise and is what a strong, controllable
ER section wants: an **ER→tail send** control.

**Size scaling.** Multiply all tap times by a size factor and adjust gains by
1/distance; keeping the *pattern* and scaling the *times* preserves the room's
identity. Because size changes delay lengths, the same click/glide problem as
the late network applies (see §3).

**Colour, flamming, mono.** Evenly spaced taps comb; jitter tap times (a few
percent, non-uniform) and avoid near-coincident L/R tap pairs, which flam and
smear transients. For stereo decorrelation use different tap *sets* per channel
rather than a delayed copy — an L/R time offset collapses to comb filtering in
mono. Checking the ER pattern summed to mono is the cheapest colour test
available. Mid/side: keeping the first one or two reflections near-centre and
widening later taps preserves phantom-centre stability.

**CPU (order of magnitude, per instance, 48 kHz, stereo out):** 24 unfiltered
taps ≈ 50–70 ops/sample; with one-pole per tap ≈ 150–200; plus a 4-allpass
diffuser ≈ +30. Memory: one ER buffer sized to max pre-delay + max ER span
(~250 ms ≈ 48 kB mono float at 48 kHz, ×4 at 192 kHz).

---

## 3. Supporting blocks

- **Pre-delay**, including tempo sync (ms and note values); needs the same
  glide-vs-crossfade handling as size.
- **Input bandwidth / diffusion** — one-pole lowpass and highpass on the feed
  plus 2–4 input allpasses (Dattorro 1997); the main control over onset
  softness.
- **Damping**: one-pole in-loop (simplest, 6 dB/oct, T60 accuracy degrades at
  extremes); shelving (better control of the corner); **multiband decay
  multipliers with crossovers** (low/mid/high T60 ratios) — the Jot absorbent-
  filter formulation makes these accurate rather than approximate, and is the
  approach that matches a "low decay ×, high decay ×" control set.
- **Output EQ** (tilt/shelves), **width** (M/S gain, or tap-set choice).
- **Modulation**: single-LFO delay modulation is cheap but produces periodic
  chorus on sustained tones; multiple incommensurate LFOs, or smoothed random /
  chaotic "spin & wander", give smoothness without a detectable rate. Modulating
  a **feedback matrix** instead of delay lengths (Schlecht & Habets) changes
  colouration without pitch modulation at all. Any delay-length modulation needs
  fractional interpolation — linear is cheap but lowpasses, allpass interpolation
  is flatter but transient-sensitive (both discussed in Dattorro 1997).
- **Vintage colour**: bandwidth limiting, coarse internal delay resolution,
  reduced word length with/without dither, converter-style filters, low-level
  noise. Any non-linearity or sample-rate reduction aliases; at 44.1 kHz the
  aliases fold into the audible band, so either oversample the offending stage or
  bandlimit ahead of it.
- **Freeze/infinite decay** (loop gain → 1, input muted or attenuated; watch
  matrix losslessness and denormals), **ducking** (tail gain from an input
  envelope follower), and **tail handling on bypass** (fade the tail out over
  ~50–200 ms, or keep processing the tail while muting input, rather than
  hard-cutting).

**Stability & denormals.** Losslessness requires an orthogonal/unitary matrix and
absorbent filters with |H(ω)| < 1 everywhere; time-varying matrices must stay
orthogonal at every instant. Enable FTZ/DAZ and/or inject a tiny DC or noise
floor — decaying tails are the classic denormal trap.

**Sample-rate scaling.** Delay lengths are in samples: recompute at every rate
change, re-round to mutually prime values (not just multiply), and re-derive
absorbent-filter coefficients. Memory scales linearly with rate.

**Parameter-change behaviour.** Size/pre-delay changes = delay-length changes.
Three published options: pointer glide (Doppler pitch drift — sometimes wanted),
crossfade between two read pointers (~2× read cost, no pitch artefact, brief
comb during the fade), or rebuild on silence. Whatever is chosen must be
consistent between ER and late or the two will drift apart audibly.

---

## 4. Comparison

| Approach | Density / smoothness | ER controllability | CPU | Memory | Modulation friendliness | Param-change robustness | Risk |
|---|---|---|---|---|---|---|---|
| Schroeder comb/allpass | Low; flutter, metallic | None (separate) | Very low | Low | Poor (combs pitch-shift) | Good | Sounds dated |
| Moorer | Low–med; damped | Good (built-in FIR ER) | Low | Low–med | Poor | Good | Density too low alone |
| Gardner nested allpass | High; smooth | None (separate) | Low | Low–med | Fair | Fair (nested lengths coupled) | Ringing if mistuned |
| Plate loop (Dattorro) | High; fast onset | None (separate) | Low | Low | Good (designed in) | Fair | Plate-leaning; limited hall size |
| FDN + absorbent filters | Med–high, tunable | None (separate) | Med | Med–high | Very good (matrix or delay) | Good (lengths independent) | Colouration/tuning effort |
| Allpass "ring" | Very high; wandering | None (separate) | Med | Med | Very good | Fair | Not fully published |
| SDN | Med; physical | Excellent, but coupled | Med–high | Low | Fair | Poor (geometry-tied) | Simulation, not effect |
| Velvet noise | Very high; no ringing | Good (as ER) | Very low | High | N/A (no modes to modulate) | Poor (regenerate to change T60) | No smooth decay knob |
| Hybrid convolution | Highest realism | Fixed by IR | High | Very high | None | Poor | Latency; asset burden |

---

## 5. Neutral shortlist (late network × ER generator)

**A. FDN (8–16 lines, Hadamard or Householder, absorbent filters) + independent
tapped-delay ER with per-tap pan and optional filter, crossfed.** Most accurate
multiband decay control, size-independent tuning, strongest ER controllability,
best modulation options (matrix modulation avoids pitch artefacts). Highest
tuning effort and the highest CPU of the three.

**B. Figure-of-eight allpass tank (Dattorro 1997) + tapped ER, parallel with an
ER→tail send.** Cheapest route to a dense, smooth, immediately usable tail with
few controls; strong for plate and chamber characters and for "vintage digital"
presets. Weaker at large-hall scale and at accurate per-band T60, so hall types
may need a second topology or an FDN alongside.

**C. Nested-allpass / allpass-loop late (Gardner 1998 topologies, randomly
modulated) + velvet-noise or short-diffuser ER.** Very high density for very low
CPU, good onset, cheap per instance in a many-instance rack. Least accurate
decay-time control and the least literature support for the modulated-loop
variant.

All three pair with the same supporting blocks in §3; the ER section is
independent of the late choice in A and B, which is an argument for building it
first.

---

## 6. Flagged / unverified

- Griesinger's allpass-loop topology has no complete published parameter set;
  only conference descriptions and patents exist.
- Moorer's 19-tap ER table values were not re-read from the 1979 paper.
- Late-1990s hardware "characters" are proprietary and undocumented; any such
  preset is an inference from published descriptions, not a reproduction.
- All CPU/memory figures are estimates from published structures; none measured.
- Dattorro's published coefficients are for 29.761 kHz and require rescaling —
  the scaling method (and whether it preserves the tuning) is not covered in the
  paper.

## References

Schroeder, M. R., "Natural Sounding Artificial Reverberation," *JAES* 10(3), 1962.
· Moorer, J. A., "About This Reverberation Business," *Computer Music Journal*
3(2):13–28, 1979. · Allen, J. B. & Berkley, D. A., "Image Method for Efficiently
Simulating Small-Room Acoustics," *JASA* 65(4):943–950, 1979. · Griesinger, D.,
"Practical Processors and Programs for Digital Reverberation," *AES 7th Int.
Conf.*, Toronto, 1989. · Jot, J.-M. & Chaigne, A., "Digital Delay Networks for
Designing Artificial Reverberators," *AES 90th Conv.*, preprint 3030, 1991. ·
Dattorro, J., "Effect Design, Part 1: Reverberator and Other Filters," *JAES*
45(9):660–684, 1997. · Gardner, W. G., "Reverberation Algorithms," in Kahrs &
Brandenburg (eds.), *Applications of Digital Signal Processing to Audio and
Acoustics*, Kluwer, pp. 85–131, 1998. · Jot, J.-M., "A Reverberator Based on
Absorbent All-Pass Filters," *DAFx*, Verona, 2000. · Karjalainen, M. &
Järveläinen, H., "More About This Reverberation Science," *AES 111th Conv.*,
2001. · Välimäki, V. et al., "Fifty Years of Artificial Reverberation,"
*IEEE TASLP* 20(5), 2012. · De Sena, E., Hacıhabiboğlu, H., Cvetković, Z. &
Smith, J. O., "Efficient Synthesis of Room Acoustics via Scattering Delay
Networks," *IEEE/ACM TASLP* 23(9):1478–1492, 2015. · Schlecht, S. J. & Habets,
E. A. P., "Time-Varying Feedback Matrices in Feedback Delay Networks," *JAES*,
2015; "Feedback Delay Networks: Echo Density and Mixing Time," *IEEE/ACM TASLP*,
2017; "Scattering in Feedback Delay Networks," 2020. · Välimäki, V.,
Holm-Rasmussen, B., Alary, B. & Lehtonen, H.-M., "Late Reverberation Synthesis
Using Filtered Velvet Noise," *Applied Sciences* 7(5), 2017. · Välimäki, V. &
Prawda, K., "Late-Reverberation Synthesis Using Interleaved Velvet-Noise
Sequences," *IEEE/ACM TASLP* 29:1149–1160, 2021.
