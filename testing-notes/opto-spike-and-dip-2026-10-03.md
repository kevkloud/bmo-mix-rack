# BMO Opto — the spike that got through and the dip after it

> **Outcome, 2026-10-03: adopted.** The release now gives back what a spike
> adds, in both modes; Stressed gets a two-stage attack; Tele keeps its
> 10 ms attack. Chosen blind, with the control this repository learned to use
> in September. The three preset levels were re-solved. Nothing here changes
> a parameter, a range, a default or a state tag, and latency is still 0.

Measured and heard on **ICE QUEEN**, 2026-10-03, on `main` at
`6f6b8c3`. No audio is in the repository: the listening set and the tools
that made it are kept outside it.

## What was reported

At high gain reduction Opto let a lot through on a loud spike, and then the
level dipped audibly after it. The September attack work and the 0.2.1
release work had both been near this and neither had reached it.

## What it measured, on main

Programme: 220 Hz at -18 dBFS RMS, running throughout. Crush 100. One burst
18 dB hotter for 20 ms. 48 kHz. Tele / Stressed.

| | main |
|---|---|
| reduction standing before the burst | 15.3 / 20.6 dB |
| let through: output peak in the burst above where a held level settles | 7.84 / 13.63 dB |
| extra reduction at the end of the burst | 11.4 / 13.0 dB |
| still there after 1 s | 7.5 / 10.8 dB |
| time until back within 0.5 dB | 3.46 / 7.18 s |
| ten bursts a second apart: how far the level has sunk before the tenth | 7.7 / 12.2 dB |

**Why the release work had not reached it.** The release rate was read off
the charge's absolute size, `depth = charge / 20`. The 0.2.1 fit measured
recovery across phrase gaps *into silence*, where the charge is free to fall.
With programme continuing underneath and 15 to 20 dB standing, the charge sits
at its ceiling, so everything a spike added inherited the slowest release the
cell has. And the charge was fed the reduction itself, so it went on counting
while a spike's reduction came back down.

## What was built

Four commits in `modules/opto/dsp/Detector.h`, each with its test shown
failing first.

1. **What a spike adds comes back at the fast rate.** Reduction standing above
   the charge releases at the cell's existing 60 ms, once the envelope stands
   clear of what the signal has reached in the last 30 ms.
2. **A spike adds nothing to the charge.** The charge counts reduction only up
   to what the level the signal has kept up for 60 ms would earn.
3. **A two-stage attack.** 10 ms as before within 6 dB of what the level asks
   of the static curve; quick by 12 dB short.
4. **Tele keeps its 10 ms attack.** The quick stage is Stressed's alone, at
   0.5 ms. This is the listening result, below.

A first form of rule 2 was dropped before it was committed: it stopped the
charge counting whenever nothing was pushing the envelope up. Notes that decay
do not push the envelope, so the charge never built, mean reduction on 143 ms
notes fell from 14.9 to 5.8 dB at crush 85, and preset loudness moved 2.4 dB.
The check that caught it is in the cost table below and is worth running on
any future change to the release.

## The figures

Same stimulus. Tele / Stressed.

| | main | 1 | 1 + 2 | adopted |
|---|---|---|---|---|
| let through | 7.84 / 13.63 dB | same | same | 7.84 / 8.04 |
| held step, time to within 1 dB of settled | 14.8 / 55.5 ms | same | same | 14.8 / 46.9 |
| extra reduction at 100 ms | 10.9 / 12.8 dB | 1.9 / 5.6 | 0.7 / 5.6 | 0.7 / 6.2 |
| at 250 ms | 10.3 / 12.4 | 1.3 / 1.9 | 0.05 / 0.59 | 0.05 / 0.62 |
| at 1 s | 7.5 / 10.8 | 0.0 / 0.9 | 0.0 / 0.0 | 0.0 / 0.0 |
| back within 0.5 dB | 3.46 / 7.18 s | 0.61 / 1.42 | 0.11 / 0.28 | 0.11 / 0.29 |
| same, 100 ms burst | 3.63 / 8.00 s | 1.50 / 3.58 | 0.90 / 2.39 | 0.90 / 2.40 |
| ten 20 ms bursts, level sunk before the tenth | 7.7 / 12.2 dB | 2.6 / 6.1 | 0.0 / 0.0 | 0.0 / 0.0 |
| ten 100 ms bursts | 8.5 / 13.8 dB | 6.7 / 12.1 | 5.0 / 10.4 | 5.0 / 10.4 |

Stressed keeps 8 dB of the spike because that is mostly the first crest, and
nothing without lookahead reaches the first crest. A 100 ms burst still sinks
the level because a hit that long is meant to count: that is the charge's own
0.3 s, and none of this changes it.

## What it costs

| | main | adopted |
|---|---|---|
| static curve, 84 cells of level x crush x mode | reference | within 0.0001 dB |
| phrase-gap release tests | pass | pass |
| notes decaying at 143 ms, crush 85, mean reduction | 14.94 / 21.59 dB | 9.47 / 17.17 |
| same, 400 ms | 16.08 / 22.72 | 14.40 / 21.41 |
| gain wobble on the 143 ms notes, largest | 0.003 / 0.002 dB | 0.29 / 0.15 |
| preset level before the re-solve, out minus in | -0.06 / +0.03 / +0.01 dB | +0.16 / +1.05 / +2.89 |
| CPU, 48 kHz / 512, share of a block, with the switch fixes | 0.29 / 0.26 % | 0.51 / 0.56 % |
| CPU, 192 kHz / 32 | 1.23 / 1.10 % | 1.99 / 2.18 % |

The decaying-note rows are the real cost. The top of every short note is
reduction the charge has not backed, so it comes back at the fast rate too,
and programme made of short notes is held less than it was. The preset rows
are the same thing seen on the level-matching signal. About half of the CPU
rise belongs to the switch fixes on the same branch, which run both cells all
the time.

**The quick attack is not only for spikes, and noise shows it.** The static
curve is read on a sine and does not move. A signal with noise in it is
different: its crests keep asking Stressed for more than 6 dB above what the
10 ms attack is holding, so the quick stage is working most of the time and
the cell settles lower. The `bus` suite's stimulus is a sine plus noise at
-18 dBFS RMS, and its swept row puts Opto in Stressed at crush 63:

| | RMS | peak |
|---|---|---|
| main | -18.482 dB | 0.428 |
| after rule 1 | -18.482 | 0.428 |
| after rule 2 | -18.328 | 0.428 |
| after the quick attack (rules 3 and 4) | -20.095 | 0.337 |

Rule 1 moves nothing on that stimulus, rule 2 lets 0.15 dB more through, and
the quick attack takes 1.77 dB off: 1.61 dB lower in all. Tele's rows do not
move with the attack at all. The four rows of `tests/plugin/BusTests.cpp`
that hold Opto (its own two and the rack's two) were regenerated for it, with
no tolerance touched. This was found in review: the suite had not been run
when the branch was first pushed.

## Heard, 2026-10-03

Source: a dry vocal take, 20 s, at its own level, crush 100, LINK on, COLOR
off, every file RMS-matched to the dry within 0.005 dB. Four groups of four
letters. **Each group held one build twice**, the control September arrived
at: the gap between two identical files is the noise floor of the comparison.
Frosty's answers were written down before the key was opened.

| group | builds in it | result |
|---|---|---|
| Tele, after a spike | main, main, 1, 1+2 | both copies of main marked for audible compression; 1 and 1+2 tied |
| Tele, the spike itself | 1+2 twice, 1+2+3 twice | both copies of 1+2 first and second; both copies with the quick attack below them |
| Stressed, after a spike | main, main, 1, 1+2 | 1 and 1+2 ahead and close; both copies of main behind |
| Stressed, the spike itself | 1+2 twice, 1+2+3 twice | both copies with the quick attack first and second; both copies without drew the same remark |

Every group separated by its duplicated pair. In September no group did, in
three rounds, and the attack stayed where it was. Here all four did.

**Decided on that:**

- The release fix goes in, in both modes. Rules 1 and 2 were not told apart by
  ear in either mode; both are in because rule 2 is what stops repeated spikes
  walking the level down, which measures.
- The quick attack goes in for Stressed and not for Tele. The adopted build is
  not a fifth thing nobody heard: on the vocal take its gain trace is
  identical, sample for sample, to the 1+2 build in Tele and to the 1+2+3
  build in Stressed.
- The reduction meter stays pinned at 24 dB (decided separately the same day).

## Presets

Re-solved from `opto_tests` with `BMO_PRINT_PRESET_LEVELS`, Release, two
passes. CRUSH untouched.

| | old LEVEL | new LEVEL | residual |
|---|---|---|---|
| Gentle | 5.06 | 4.90 | +0.008 dB |
| Vocal Glue | 10.86 | 9.77 | +0.005 dB |
| Crushed <3 | 20.13 | 17.13 | +0.001 dB |

## Also fixed in the same work

Separate commits, in `modules/opto/dsp/DspCore.h`, independent of the above:

- **A mode round trip left the output low.** The cell not in use was frozen.
  Leave a mode after a loud passage and come back three seconds later, and
  the output sat 11.3 dB (Tele) and 6.7 dB (Stressed) under a run that never
  left. Both cells now listen all the time; the figure is 0.00.
- **Mode, Link and Color stepped the output.** 45 to 156, 18 to 85 and 8 times
  the signal's own largest step. Each is now a 10 ms crossfade; the ratios are
  0.4 to 1.0. Unlinking no longer leaves the quieter channel 15 to 21 dB
  louder in one sample. With nothing switching, the output was bit-identical
  to main in 32 combinations before the detector changes went on top.
- **Tests** now pin the attack, the three switches, a mode round trip and
  three other sample rates. None of those was pinned before.
- `modules/opto` has its `AGENTS.md` and `README.md`.

## Not done, and why

- **The longer loud hit.** Left as designed; see the table.
- **Nothing here has been heard in a host.** The listening set was offline
  renders. The installed plugin on ICE QUEEN is not this build.
- **The presets are not ear-tuned.** They are level-matched and nothing more.
- **The shared fade helper** is a copy of the one the EQ switch fix adds. When
  that lands, this branch is rebased onto it and the copy dropped.

## Reproducing it

Two JUCE-free programs built against the tree root, kept outside the
repository beside the handoff: the review probe (`verify`, `spike`,
`ratchet`, `rates`, `params`, `bench`) and a check tool (`static`, `presets`,
`decay`, `vox`, `cmp`). Every figure above came from one of them or from the
suite. Run `verify` first: it compares the probe's own copy of the gain path
against `DspCore` and must report 0 mismatches before any other figure means
anything.
