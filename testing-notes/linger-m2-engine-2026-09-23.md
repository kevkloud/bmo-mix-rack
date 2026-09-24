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
sweep to 96 and 192 kHz and prints each type. The patch was kept in the
session scratchpad, which was cleared between 2026-09-23 and 2026-09-24: **it
is gone**, and rebuilding it means rewriting it (the design is described above
and in modules/reverb/AGENTS.md).

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

## M2 integration: the engine meets the tables (2026-09-24, on AURORA)

`origin/frosty-linger-er-tables` at 527bc1b merged into `frosty-linger-er`
(merge 334a0ef). Every build and figure below on AURORA.

### Hashes (`measure_reverb hash`, Release, 48 kHz)

Baseline right after the merge, on the real tables, and **identical at every
later step** (after removing the comb fields, after the span clamp, and at the
end):

| position | hash |
|---|---|
| Var 0 | 24b48c88a7d5db4e |
| Var 1 | c16665b21c68a23a |
| Var 2 | 75f6f28b62f44fcc |
| Var 3 | f1fee44df219f052 |
| Var 4 | a9a79078c4155b0e |
| Var 5 | b9440ba4fec79aa5 |
| Var 6 | 7181049501fd144b |
| Var 0-5 combined | 0a49c9a63ce51240 |

### What each step did

1. **Merge** (334a0ef): additive conflicts in reverb_dsp's includes,
   measure_reverb and AGENTS.md; both halves kept. On the real tables two
   engine checks went red at once. The tap-gain check read a tap over the
   window between its neighbours, which Plate's dark bands (a tail still half
   a pulse 25 samples on) and taps 13 samples apart at half size defeat: worst
   11.2 dB. **Fixed in the test** (149299a): the gain is now the IR summed
   from the tap to the end less every other tap's closed-form tail -- 4536
   gains read, worst 7.5e-5 dB. The other is the density bound (below).
2. **Comb fields removed** (5670b83): `combDelayMs`, `combGain` gone from
   ErTable.h, the recipes, the emitter and the tests. The re-emitted `.inc`
   files differ from the merged ones by exactly the seven removed lines; hash
   identical.
3. **Var 6 in the audit** (2c59772): gamma over Var 0-5 only; Var 6 checked as
   an exact mono null (its two channels the same set, to the bit). Every
   type's gamma margin unchanged (Room 0.0339, Chamber 0.0431, Hall 0.0162,
   Cavern 0.0155, Plate 0.1159, Ambience 0.0521); every Var 6 exact.
4. **Span clamp** (09de56f): the engine already held every span inside its
   clamp; `erSizeScale` is now the one law (engine, span, tail), and
   `tailSecondsFor` reads the per-type span. Hash identical. The latest tap
   the engine plays is at worst 1.06 ms inside its clamp, and equal to
   `erSpanMsAt` to a sample, every type at SIZE 0.5 m, default and 80 m.
5. **Density level** (ac62100, measurement only): below.
6. **Panel** (7f065ba): the EARLY scatter draws `erTableFor (type)`; the
   placeholder table is retired. core/ui untouched.
7. **Docs** (e97f42d, 424b664).

### t_ER,max, old -> new, ms

Old is the placeholder's 79.1 * S / 12 for every type, unclamped.

| type | clamp | 0.5 m | default | 80 m |
|---|---|---|---|---|
| Room | 100 | 3.296 -> 4.904 | 79.100 -> 98.078 (12 m) | 527.333 -> 98.078 |
| Chamber | 100 | 3.296 -> 4.942 | 118.650 -> 98.845 (18 m) | 527.333 -> 98.845 |
| Hall | 200 | 3.296 -> 4.851 | 224.117 -> 194.057 (34 m) | 527.333 -> 194.057 |
| Cavern | 200 | 3.296 -> 4.953 | 362.542 -> 198.119 (55 m) | 527.333 -> 198.119 |
| Plate | 200 | 3.296 -> 4.285 | 145.017 -> 38.568 (22 m) | 527.333 -> 140.247 |
| Ambience | 100 | 3.296 -> 6.183 | 52.733 -> 98.931 (8 m) | 527.333 -> 98.931 |

### Reported tail, old -> new, s

TailTests' hand-written seconds (every row TYPE Room):

| setting | old | new |
|---|---|---|
| defaults (12 m, 1.8 s, 1.20 / 0.40) | 2.2891 | 2.308078 |
| 125 ms / 4 s / 1.50x / 24 m | 6.3332 | 6.273078 |
| 3 s, damping 0.20 / 0.20, 12 m | 3.1291 | 3.148078 |
| 20 s, 1.00 / 1.00, 12 m | 20.1291 | 20.148078 |
| two slots in series (sum) | 8.6223 | 8.581156 |
| 250 ms / 20 s / 2.0x / 80 m | 40.827, reported 30 | 40.398, reported 30 |

`measure_reverb tail`: defaults 2.289 -> 2.308; shortest decay, smallest room
0.173 -> 0.175; 6 s at 0.10 6.129 -> 6.148; everything at maximum 30 -> 30.
Reported tail over the measured ER-only -60 dB time: smallest margin 110.7 ms
after the merge, 128.3 ms after the span change.

### Density level on the real tables (`measure_reverb density-level`)

Worst ER energy error over DENSITY 65-100 % against DENSITY 0, both channels,
VARIATION 0-6 (the Variation in brackets), dB. **The 0.3 dB check in
reverb_dsp is red on these tables and is left red**, threshold unchanged,
pending the owner.

Taps

| type | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|
| Room | 0.278 (V5) | 0.318 (V5) | 0.280 (V5) |
| Chamber | 0.361 (V2) | 0.375 (V5) | 0.335 (V4) |
| Hall | 0.236 (V1) | 0.234 (V1) | 0.258 (V1) |
| Cavern | 0.272 (V0) | 0.220 (V0) | 0.219 (V0) |
| Plate | 0.725 (V4) | 0.720 (V4) | 0.738 (V4) |
| Ambience | 0.312 (V4) | 0.326 (V4) | 0.275 (V4) |

Energy

| type | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|
| Room | 0.510 (V4) | 0.369 (V4) | 0.390 (V4) |
| Chamber | 0.409 (V4) | 0.463 (V4) | 0.468 (V4) |
| Hall | 0.366 (V3) | 0.242 (V3) | 0.264 (V4) |
| Cavern | 0.231 (V4) | 0.206 (V4) | 0.184 (V3) |
| Plate | 0.507 (V6) | 0.518 (V4) | 0.481 (V4) |
| Ambience | 0.714 (V1) | 0.775 (V1) | 0.583 (V1) |

Blend

| type | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|
| Room | 0.375 (V1) | 0.394 (V1) | 0.369 (V1) |
| Chamber | 0.443 (V2) | 0.340 (V2) | 0.325 (V2) |
| Hall | 0.169 (V4) | 0.219 (V4) | 0.181 (V1) |
| Cavern | 0.152 (V0) | 0.161 (V0) | 0.151 (V0) |
| Plate | 0.391 (V5) | 0.361 (V5) | 0.257 (V4) |
| Ambience | 0.408 (V5) | 0.388 (V4) | 0.272 (V4) |

Worst anywhere 0.775 dB (Ambience, Energy, Var 1, 96 kHz). The bridge
(DENSITY 0-60 %) stays exact on the real tables: 1.8e-6 dB.

### Renders (scratchpad, not the tree)

In `C:\Users\thesp\AppData\Local\Temp\claude\C--Users-thesp-OneDrive-Documents-REPO-bmo-mix-rack-333\66bf0708-6bd0-40aa-9504-7585913edb62\scratchpad\linger-renders\`:
`linger-early-Room.png`, `linger-early-Hall.png`, `linger-early-Plate.png`
(EARLY page, defaults, TYPE set) and `linger-eq-signal-18.png` (EQ page,
signal=-18), rendered with `build\tools\Debug\snapshot.exe`. Room's EARLY
readout: 35 taps, 3.7-98.1 ms; Plate's: 45 taps, 0.7-38.6 ms.

### Bench after integration (Release, block 128, 60 s, median of five)

| row, 48 / 192 kHz | pre-merge (546bb41) | after, run 1 | after, run 2 |
|---|---|---|---|
| worst case | 0.585 / 2.291 | 0.676 / 2.748 | 0.663 / 2.662 |
| Var 6 (mono null) | 0.383 / 1.535 | 0.476 / 1.885 | 0.461 / 1.876 |
| Energy mode | 0.575 / 2.301 | 0.680 / 2.758 | 0.673 / 2.718 |
| crossfading every block | 1.029 / 4.164 | 1.174 / 4.745 | 1.174 / 4.758 |
| DENSITY moving every block | 0.916 / 3.688 | 1.004 / 4.059 | 1.006 / 4.054 |

All inside 1.5 % / 5 %, but about 15 % up on the stand-in, and the crossfade
row at 192 kHz has about 0.25 % of headroom. Memory 418.5 kB at 192 kHz (was
290.5): the real tables reach 200 ms windows. The likely cost is the real
tables' pair lists (overlap terms) and the longer delay line; not profiled.

### Non-vacuity (each break one edit, rebuilt with exit 0, reverted)

reverb_dsp (counts include the standing density failure):

| break | red |
|---|---|
| engine gains lose the 1/d law | tap gains, Var 6 L = E (3) |
| engine times 1 % late | tap times, times at every rate, Var 6, gains, engine span = erSpanMsAt (6) |
| Var 6 mismatches never counted | the one-tap Var 6 break fails, and is counted (3) |
| Size law unclamped | span inside clamp (both), engine span = erSpanMsAt, termination, fuzz, taps (8) |
| erSpanMsAt 5 % long | Room's span, span inside clamp, engine span = erSpanMsAt (4) |
| TapTables.h time law off | both Size-law frame checks (3) |
| one threshold 1.68 in the data | shape [0, 1], the tables' own threshold check, the pin (4) |

Full tree:

| break | red |
|---|---|
| panel first tap by the raw law | ui_layout: twice the size held at the clamp (both panel hosts) |
| panel last tap 20 % long | ui_layout: last tap is the table's span (both) |
| tail formula back on the placeholder span | tail: 10 figures |

### Final verification

- `build-dsp` Debug, `--clean-first`, every test target and measure_reverb
  named: exit 0. ctest **17/18**, `tune_hardtune_target` disabled by design;
  **reverb_dsp red on exactly one check, the density bound**.
- `build` Debug, `reverb_dsp_tests reverb_tests tail_tests ui_layout_tests
  rack_tests snapshot` named: exit 0, no plugin product. ctest: reverb, tail,
  ui_layout and rack pass; reverb_dsp red on the same one check.
- `build-dsp-rel` Release measure_reverb: exit 0; hashes and bench above.
