# BMO Linger M3b: ATTACK, the onset bloom (2026-10-07, on ICE QUEEN)

Branch `frosty-linger-m3b`, on top of `5fa2db5`. Built and measured on **ICE
QUEEN**, Release, Visual Studio 17 2022, LTO off, test targets only. The
machine was busy all day, which matters for the CPU figures below. **Not
heard yet.**

## What was built

ATTACK is a per-type constant with no host lane (Room 30 %, Chamber 35, Hall
50, Cavern 65, Plate 0, Ambience 10). In `LateNetwork.h` each of the eight
tail lines is now fed the diffused input at its own delay and its own level:
the shortest line at once and quietly, the last ATTACK x 120 ms later and
loudest, the levels normalised so the tail's energy does not change. At
ATTACK 0 every line is fed at once at unity, as before. `10` section 4, "As
built in M3b", has the argument.

## The first build was wrong, and why

The first version summed eight rising taps on the pre-delay line into the
network's one input. That is a sparse filter in front of everything, and the
late tail's spectral flatness showed it:

| type (its own ATTACK) | no bloom | eight taps into one input | one tap a line |
|---|---|---|---|
| Room (30 %) | 0.775 | 0.528 | 0.768 |
| Chamber (35 %) | 0.864 | 0.628 | 0.873 |
| Hall (50 %) | 0.924 | 0.718 | 0.925 |
| Cavern (65 %) | 0.931 | 0.770 | 0.927 |
| Plate (0) | 0.759 | 0.759 | 0.759 |
| Ambience (10 %) | 0.529 | 0.382 | 0.522 |

The floor in the suite is 0.3. Only the third column was kept.

## Figures, as built

Room at 12 m, DECAY 1.8 s, tail only, fed directly, modulation off:

| ATTACK | whole-tail energy | half of the first 400 ms in by | first sample |
|---|---|---|---|
| 0 % | -4.946 dB | 87.0 ms | 919 |
| 10 % | -4.889 dB | 97.0 ms | 919 |
| 30 % | -4.886 dB | 115.3 ms | 919 |
| 65 % | -4.895 dB | 150.1 ms | 919 |
| 100 % | -4.865 dB | 186.0 ms | 919 |

- A change of ATTACK, 30 to 100 %, under a held 440 Hz tail: largest sample
  step 0.00357 during the move against 0.00356 settled before.
- The reported tail gains ATTACK x 0.12 s. `TailTests`' hand figures each
  moved by Room's 0.036 s (the defaults: 2.270849 to 2.306849 s).
- `BusTests`, Linger's defaults row regenerated; the 0.63 row did not move.

## What to know before listening

- **The energy is the same and the peak is not.** The lines fed last are fed
  harder (up to 4.9 dB over the no-bloom level), so on a transient the tail's
  first loud arrival is peakier: a snare through Hall, tail only, peaks at
  -30.4 dBFS at Hall's own 50 % against -33.7 with the bloom off.
- **A bloom reads as a longer decay in a short band.** With Room's 36 ms the
  50 Hz band at LOW x 0.25 fits 0.631 s where the filters alone give 0.557.
  The damping test runs with ATTACK off for that reason.
- The span (120 ms), the first line's level (0.12), the curve
  (position^1.5) and the order the lines are fed in are all to calibrate by
  ear.

## CPU

`measure_reverb bench <rate> <block> worst`, median of five:

| | bloom forced off | bloom in |
|---|---|---|
| 48 kHz / 128 | 1.357 % | 1.370 % (budget 1.5) |
| 192 kHz / 32 | not usable today | best single runs 5.23 %, medians 5.3 to 5.6 % |

The 192 kHz figure moved by more than a point between runs on the day. The
day before, on a quieter machine and without the bloom, it was 5.126 %.
Frosty accepted 5.18 % on 2026-10-03 and said it is not a budget, so this
needs measuring again on an idle machine and his word.

## Tests

New, in `reverb_dsp_tests`, "ATTACK: the tail blooms, and Plate's does not":
the table above as assertions (energy within 0.5 dB, each step later, 60 to
120 ms held back at 100 %, same first sample), the bloom rising 15 ms at a
time, 15 dB down at the start at 100 %, ATTACK 0 feeding every line at once,
the last line fed ATTACK x 120 ms after the first, and the move not clicking.

Shown failing in two broken builds: the bloom forced off (six rows fail),
and the normalisation removed with the crossfade made instant (the energy
row, at -4.85 dB, and the click row, a step of 0.0116 against 0.0020).

A click test on noise was written first and thrown away: steady noise's own
1 ms energy wanders by 4.7 dB, so it could not see a click.

Four existing tests changed, each saying why in place: the ringing test
gives each type its own ATTACK (it had been giving all six Room's), the
damping test runs with ATTACK off, and the two tail-formula rows carry the
new term.

`measure_reverb render` takes `attack=<per cent>`, the tool's own and not a
parameter, so a listening set can hear a type at another bloom.

On ICE QUEEN: build-dsp 24/24, build-full 48/48 runnable,
`reverb_dsp_tests --long` passes, all builds exit 0.

## Listening set

`set-2026-10-07-m3b-attack`, 25 files, outside the repository, every file at
OUTPUT -4 dB. Each type on a snare with the bloom off and at its own
setting, a 0 / 25 / 50 / 75 / 100 % sweep on Hall's tail alone, and a vocal
on Room, Hall and Cavern both ways.
