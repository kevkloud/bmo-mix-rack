# BMO Linger M2, engine half — 2026-09-23, on AURORA

Branch `frosty-linger-er`, worktree `bmo-mix-rack-333-lingerer`, built on
**AURORA** (Frosty's laptop). The ER tables are the **stand-in** in
`modules/reverb/dsp/ErTable.cpp`; every figure below that depends on a table
was measured against it and will move when the generator's tables land.
Nothing has been heard.

## CPU: `measure_reverb bench`

Release, DSP-only tree `build-dsp-rel`, whole module (`ReverbDsp`), stereo,
block 128, 60 s of fixed-seed noise, median of five, percent of one core.
The ER alone: there is no late network yet.

**Before tuning** (commit 6e26dc3, the engine as first built):

| setting | 48 kHz | 192 kHz |
|---|---|---|
| worst case: DENSITY 100 %, 48 taps a channel, 3 diffuser stages, Var 2 | 0.566 % | 2.340 % |
| the same, Variation 6 (comb) | 0.401 % | 1.698 % |
| the same, Energy mode | 0.561 % | 2.286 % |
| crossfading on every block | 1.017 % | 4.111 % |
| DENSITY moving every block | 1.228 % | 4.944 % |

**Final engine** (after 0a3b34f: pair-overlap renormalisation, new diffuser
delays). Two runs:

| setting | 48 kHz | 192 kHz |
|---|---|---|
| worst case | 0.586 / 0.595 % | 2.439 / 2.364 % |
| Variation 6 | 0.414 / 0.419 % | 1.689 / 1.708 % |
| Energy mode | 0.581 / 0.575 % | 2.332 / 2.482 % |
| crossfading on every block | 1.028 / 1.069 % | 4.293 / 4.270 % |
| DENSITY moving every block | 1.301 / 1.301 % | **5.290 / 5.282 %** |

Budget (10 section 6): 1.5 % at 48 kHz, 5 % at 192 kHz. **The worst steady
case is inside it** with the ER taking about 40 % (48 kHz) and 48 % (192 kHz)
of the whole module's budget. **The DENSITY-moving transient is over it at
192 kHz**: every tap weight and the renormalisation are recomputed on every
sample while the smoother moves. Memory: 290.5 kB at 192 kHz (the ER line and
the diffuser's 24 short lines).

## `reverb_dsp` figures (Debug, `build-dsp`, 48 kHz unless stated)

- ER taps at DENSITY 0, hi-cut open, 6 types x 6 per-channel variations x
  2 channels x 3 sizes: 4536 taps checked, 0 skipped, worst gain error
  9.1e-7 dB, worst time error 0 samples.
- Density energy, all six types, D in steps of 0.05: worst **1.3e-6 dB over
  0-60 %** (the tap bridge), worst **0.238 dB over 60-100 %** (the diffuser).
  **11 section 6 asks for 0.2 dB over the whole sweep; the diffuser range
  does not meet it.** The test holds it to 0.3 dB. See below.
- Taps brought in by the density ramp: 52, loudest on arrival 0.089 % of its
  final level.
- Density-sweep click detector (second difference on a 100 Hz sine, 1 s
  sweep): 0.019 % of the ER's peak (limit 1 %).
- Pulses at DENSITY 100 %: at least 12929/s on the worst type (limit 2000/s).
- ER HI-CUT -3 dB points: 2000.0, 4000.0, 8000.0, 16016 Hz; cross-correlation
  lag 0 at all four.
- ER-only energy after span + 5 ms: exactly zero in every case tested (types
  x Taps/Energy/Blend x DENSITY 0.5/1 x SIZE 0.5/12/80 m).
- SIZE change (12 -> 6 m): worst 1 ms step 1.15 dB; second difference 0.017 %.
- TYPE change (Room -> Chamber, size halved): worst 1 ms step at level 1.90
  dB, dip bottom -45.7 dB; second difference 0.017 %.
- Reported tail over the measured ER-only -60 dB time: smallest margin
  120 ms (DECAY 0.1 s, damping 0.1, every type, Taps/Energy, D 0/1, 3 sizes).
- Level laws, phasing null, MIX ends, block sizes 1/16/32/64/127/512/2048
  bit-identical, tap times in ms at 44.1/48/88.2/96/176.4/192 kHz, zero
  latency, diffuser paths distinct at all six rates, fuzz finite, silence
  after reset, zero allocations: all pass.
- Run time of `reverb_dsp_tests`, Debug: about 8.5 s.

## Why the diffuser range misses 0.2 dB

Three causes were found by the test; two were fixed at their cause:

1. The spec's renormalisation treats taps as independent. The stand-in has
   infill within a sample of a core tap; their filtered pulses overlap and the
   level moved 0.24 dB (Ambience) as DENSITY brought the infill in. **Fixed**:
   the renormalisation now includes each band's pulse energy and the
   closed-form overlap of nearby pairs. Bridge error 1e-6 dB.
2. Fed `[1,1,1,1]/2`, the diffuser had a DC gain of 2 with every stage in
   (the butterfly is an involution), and positive-tap ERs keep a few per cent
   of their energy near DC: +0.39 dB worst. **Fixed**: the fourth line enters
   inverted, DC gain exactly 1.
3. What is left is structural. The butterfly preserves the energy of its four
   lines together; a channel's output is one line, and only an allpass holds
   every input's energy, which a feed-forward network cannot be. The error is
   the table's tap pattern interfering with the diffuser's 64 paths. Measured
   worst: 0.238 dB (20.3 ms delay set, shipped), 0.210 dB (25.7 ms set, tried
   and not kept), both against the stand-in; a temporarily jittered stand-in
   (never committed) gave 0.25 dB. **An owner decision**: accept about 0.3 dB
   above DENSITY 0.6, or change what the diffuser is.

## Non-vacuity: every new assertion broken on purpose

Each break was one edit, rebuilt (exit 0 every time), run, and reverted with
`git checkout`. Counts are the failing assertions.

| # | break | red |
|---|---|---|
| 01 | tap gains lose the 1/d Size law | 1: tap gains |
| 02 | tap times scaled by 1.01 | 4: tap times, Var 6 sum and difference, times at every rate |
| 03 | Variation 6's R takes +g instead of -g | 2: Var 6 sum and difference |
| 04 | renormalisation on raw gains (the spec's literal formula) | 2: bridge and diffuser energy |
| 05 | diffuser fed `[1,1,1,1]/2` (DC gain 2) | 1: diffuser-range energy |
| 06 | density ramp width 1e-4 (a switch) | 2: tap appearance, density click detector |
| 07 | diffuser stages never fade in | 1: pulses per second |
| 08 | hi-cut designed at 1.3x its setting | 4: -3 dB at each corner |
| 09 | hi-cut as three poles | 7: corners (4), zero lag (2), block sizes (static state in the break) |
| 10 | `diffuserSpreadMs` understated by half | 1: ER-only termination |
| 11 | ER LEVEL law at /40 | 1: level laws |
| 12 | ER LEVEL off only below -50 | 3: -40 is off, MIX 100 % has no dry, phasing null |
| 13 | dry gain off by 0.001 | 6: MIX 0 exact (2), MIX 100 %, phasing, latency, -40 off |
| 14 | filter state dropped at each block | 2: block sizes, density click |
| 15 | tap delays rounded at 48 kHz whatever the rate | 1: times at every rate |
| 16 | two stage-3 delays equal | 1: diffuser paths distinct |
| 17 | NaN comb gain | 4: fuzz finite, Var 6 (2), block sizes |
| 18 | `reset()` keeps the delay line | 1: silence after reset (**green until the fuzz input was fixed**) |
| 19 | crossfade of 2 samples | 5: SIZE and TYPE, energy and waveform |
| 20 | TYPE falls to silence in one sample, then rises | 1: TYPE waveform (**green until the waveform check was added**) |
| 21 | allocation in `process()` | 1: zero allocation |
| 22 | a band pole at 0.99995 (ringing) | 15, including the tail report |
| 23 | TYPE swaps with no dip | 3: TYPE dip, energy step, waveform |

Breaks 18 and 20 exposed two tests that could not fail, and a third hole on
the way: the fuzz fed R = -L, so the ER's mono sum was silence and the engine
was never fuzzed. All three fixed in 3f06a3c and re-proved.

## Not done here

- The comb, flamming, mono-gamma and lateral-fraction audits: the table
  half's.
- Nothing heard: no listening pass on any setting.
- `tests/plugin/ReverbTests.cpp`'s header and its "no preset level check"
  comment still say the DSP is a pass-through. Left alone: that file holds
  the goldens this pass may not touch.
