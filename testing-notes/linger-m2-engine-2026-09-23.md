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

## Owner decisions, second pass (same day, on AURORA)

### 1. Variation 6 is mono null (c24707f)

L = +E, R = -E on the ER bus, E the table's variation-6 set; BMO Dimension's
mid/side convention. `combDelayMs` and `combGain` are no longer read anywhere,
buffer sizing included. `ErTable.h` untouched.

`measure_reverb hash` (3c4bb39), Release, 48 kHz, every type x 3 modes x
DENSITY 0/30/70/100 x SIZE 6/12/30 m, FNV-1a 64 over the output:

| position | before | after |
|---|---|---|
| Var 0 | 7a6c5c9cf91f143c | 7a6c5c9cf91f143c |
| Var 1 | aeeba9d578569697 | aeeba9d578569697 |
| Var 2 | eb2524248bb0a1d4 | eb2524248bb0a1d4 |
| Var 3 | 8cde6a8fff426b31 | 8cde6a8fff426b31 |
| Var 4 | a10551355686c303 | a10551355686c303 |
| Var 5 | 554970b6ae22dc82 | 554970b6ae22dc82 |
| **Var 0-5 combined** | **9d2b36f5e9af1e98** | **9d2b36f5e9af1e98** |
| Var 6 | 889a9d8f5231633c | 6bf1a65e0372ac03 |

Tests: L + R == 0.0f at every sample (6 types x 3 modes x DENSITY
0/0.3/0.7/1, impulse then noise); L not silent; L is E tap for tap (times to
a sample, gains to 0.2 dB); each side within 0.2 dB of Var 5 on the impulse
response up to DENSITY 60 % (worst 1.0e-6 dB); a mono instance is exactly
silent at MIX 100 %, exactly half the dry at 50 %, and finite.

The Var 5 comparison stops at 60 % on purpose: above it, in Energy mode, Var 5
and Var 6 are different velvet sequences and the diffuser treats them
differently by up to 0.26 dB -- the density sweep's subject, not Var 6's.
A first version measured the level over the noise half of the drive too, and
failed by 0.31 dB in Energy mode at DENSITY 0: that is noise through two
different pulse sequences, a random quantity, not a level.

Breaks (each reverted): R = -0.999 L (3 red: the null, both mono checks); the
side silent (3 red); the side 10 % loud (2 red: E tap for tap, level against
Var 5); mono taking L only (2 red); mono NaN (3 red, including finite). Two
earlier breaks, on lines shared with Var 0-5's tap loop, reddened Var 0-5's
checks as well and were redone on the side block alone.

### 2. The density level above 60 % -- not met

Built as asked: a gain computed in `prepare()` (once per rate, shared) from
the diffuser's own per-output gain -- G(D, a), the energy of one output for
a pulse smeared by a one-pole of pole a, over 49 crossfade positions and 25
poles -- dividing each set's renormalisation, table lookup on the audio
thread, no recursion. G is 1.000 at the stand-in's bright poles and swings
0.80-1.10 at a = 0.9. Worst over DENSITY 60-100 %, reverb_dsp, Debug:

| type | 48 kHz without / with | 96 kHz | 192 kHz |
|---|---|---|---|
| Room | 0.0887 / 0.0887 dB | 0.0692 / 0.0687 | 0.0923 / 0.0926 |
| Chamber, Hall, Cavern, Plate | 0.2381 / 0.2384 | 0.2857 / 0.2860 | 0.2200 / 0.2123 |
| Ambience | 0.1538 / 0.1540 | 0.1385 / 0.1387 | 0.1190 / 0.1149 |

(Chamber, Hall, Cavern and Plate are the same stand-in table at the same
clamped size, so they agree to the digit.) It moves nothing by more than
0.01 dB: the drift is the table's own taps interfering with the diffuser's
paths, which no quantity of the diffuser alone can see. **So the engine
change is not committed** and the test stays at 0.3 dB; 68a084e extends the
sweep to 96 and 192 kHz and prints each type. The patch is kept outside the
repository (the session scratchpad, `change2-diffuser-gain-table.patch`).

What would meet it: the exact correction, E_out = sum over tap pairs of
x_i x_j sum_k r_D(k) C_ij(k + n_i - n_j), is a function of the set (sizes are
continuous), so it cannot be tabulated in `prepare()`. Computed at each set
build and at each control point it is about 20 k multiply-adds at 48 kHz and
75 k at 192 kHz -- on the audio thread, which this pass was told not to do.
Or accept about 0.3 dB, or change what the diffuser is. An owner decision.

### 3. DENSITY on a 32-sample control grid (546bb41)

Bench, Release, block 128, 60 s, median of five, % of one core, 48 / 192 kHz:

| row | before (68a084e) | after, run 1 | after, run 2 |
|---|---|---|---|
| worst case: 48 taps, 3 stages | 0.570 / 2.426 | 0.585 / 2.291 | 0.579 / 2.330 |
| Variation 6 (mono null) | 0.382 / 1.536 | 0.383 / 1.535 | 0.387 / 1.553 |
| Energy mode | 0.576 / 2.346 | 0.575 / 2.301 | 0.570 / 2.289 |
| crossfading on every block | 1.024 / 4.230 | 1.029 / 4.164 | 1.018 / 4.144 |
| **DENSITY moving every block** | **1.301 / 5.325** | **0.916 / 3.688** | **0.920 / 3.646** |

Every row is now inside 1.5 % / 5 %. A new check runs DENSITY from 0 to 100 %
starting on sample 0 and asserts bit-identical output at block sizes
1/16/32/64/127/512/2048; the fixed-parameter one stays green.

Breaks: the grid counted from each block's start (1 red: the moving block-size
check); ramps that never advance (1 red); the spec's raw renormalisation and
the DC-gain-2 diffuser re-run against the three-rate sweep (2 and 1 red).
**The switched-weight break (06) no longer reddens the waveform click
detector**: on the grid a switched weight arrives as a 32-sample linear fade,
under the detector's 1 %; the per-tap "no tap appears" check still catches it.
With the switch *and* no ramp the detector reads 16.9 % of peak and goes red.

### Builds and tests after the second pass

On AURORA: `build-dsp` Debug build of `reverb_dsp_tests measure_reverb` exit 0,
ctest 18/18 passed (`tune_hardtune_target` disabled by design). `build` Debug,
targeted build of `reverb_dsp_tests reverb_tests tail_tests ui_layout_tests
rack_tests` exit 0, no plugin product built; `reverb_dsp`, `reverb`, `tail`,
`ui_layout`, `rack` 5/5 passed.
