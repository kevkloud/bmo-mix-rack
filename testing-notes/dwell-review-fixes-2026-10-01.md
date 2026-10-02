# BMO Dwell — the review's DSP fixes, measured

**On AURORA, 2026-10-01**, branch `review/dwell-fixes` on top of PR #35's head
`d783f59`. Release builds (MSVC 2022), 48 kHz and 512-sample blocks unless a row
says otherwise, through `DwellDsp` as a host drives it: `setParams`, then
`setTempo`, then `process`, every block. Numbers only. **Nothing here has been
listened to**, and nothing in it says how anything sounds.

The "before" figures come from the review's JUCE-free probe (kept outside the
repository), rebuilt against the unfixed tree on AURORA and identical to the
review's own run; "after" is the same probe against the finished branch. Every
new test was run against the unfixed code first and failed there.

## What changed

| | defect | test | before | after |
|---|---|---|---|---|
| B5 | one NaN or infinity in the input with DUCK up silenced the module for good | `testANonFiniteInputWithDuckUpRecovers` | output RMS over the last second **0.000000** at DUCK 6 (0.127813 without the NaN) | 0.127813 — equal to the clean render within 0.01 dB |
| B2 | CHARACTER bucket-brigade → Clean or Tape replayed the repeat in flight companded | `testACharacterMoveReplaysTheRingAtItsOwnLevel` | repeat RMS **−14.1 dBFS** (to Clean), −14.0 (to Tape), against −29.0; peak **0.558** from a 0.05 tone | −29.0 / −29.0; peak 0.0500 |
| B4 | MIX crossing 50 % inside one block stepped the output | `testMixAcrossTheHingeIsSmoothed` | largest step **0.4330** (100 → 35), **0.4351** (0 → 100); the sine's own 0.0033 | 0.0028 and 0.0033; control (100 → 60) 0.0027 before and after |
| B3 | with SYNC on, the first tempo after prepare glided from the TIME knob | `testTheFirstTempoLandsTheSyncedTime` | read delay at block 1 / 0.5 / 1 / 2 / 3 s on Tape and Bucket-brigade: **18128 / 24144 / 30160 / 42064 / 47992** samples (want 48000); Clean 18000 at block 1 | 48000 at every point on all three |
| B1 | Crush in the loop never decayed | `testCrushInTheLoopDecaysToSilence` | AMOUNT 100 / FEEDBACK 80 / TIME 100: still **−7.1 dB** under the first repeat at 39–40 s; AMOUNT 0: **−80.8 dB** for good | −60 dB at 0.30 s, exact zeros; AMOUNT 0 exact zeros |
| B6 (a) | the tail's laps were counted at `min(g, 0.97)` | `testTheReportedTailIsNeverShorterThanTheDecay` | FEEDBACK 96.9 % / TIME 20 ms reported **4.54 s**, ringing past 40 s | reports the 30 s ceiling |
| B6 (b) | a lap is longer than TIME by the loop filters' own delay | same test | bucket-brigade FEEDBACK 60 %: reported 3.375 / 9.000 s, rang **3.376 / 9.004 s** (TIME 375 / 1000 ms) | 3.393 / 9.022 s, inside |
| B6 (c) | an in-loop FX's delay per lap was not counted | same test | Diffuse 100, FEEDBACK 80, TIME 100: reported **2.30 s**, rang 6.20 s | reports 18.73 s |
| S2 | DUCK automated made the audio depend on block size | `testDuckAutomationIsBlockSizeInvariant` | blocks of 1024 and 3072 differed from 64 by **0.0125** | 0 |
| S3 | the JUCE-free schema table never ran (`!= 26` with 27 rows) | `testSchemaIsWhatItWillAlwaysBe` | 0 of its row checks ran | all 27 rows run and pass; a deliberately wrong row fails |

**(b) is not what the review described.** Measured from the end of the input,
which is what a host's tail means, the old figure at the defaults was exact
with no margin — the last sample above −60 dB came 1.873 s after a 5 ms burst
against 1.875 s reported. The review's 2.25 s was measured from the start of
the burst in whole-period windows. Where it really was short, by 1–4 ms, was
bucket-brigade at long TIME, through the filters' own delay, and that is what
was fixed.

## The tail, checked against renders

`tailSecondsFor` was rendered against the real decay — a burst, then the time
from its last sample to the last output sample above −60 dB of its peak, either
channel, MIX 100 — over every character, FEEDBACK 35 / 60 / 80 / 90 / 95 / 96 /
96.9 %, TIME 1–2000 ms, four inputs (one sample, 5 ms of 300 Hz, 50 ms of noise,
5 ms of 1 kHz), the cuts and stereo modes, the lane, and every FX type at
AMOUNT 0, 35, 60 and 100 where it applies: about 8 400 renders at 48 kHz, and
the FX-off and FX sets again at 44.1 kHz and the FX-off set at 96 kHz.

- **At 48 kHz the figure was never short below the 30 s ceiling.**
- **How conservative it is with Diffuse** (B6 (c), Frosty's call): the rendered
  decay ran between 8 % and 88 % of the figure — on average 27 % at AMOUNT 100,
  41 % at 60, 53 % at 35. The bound is each allpass's peak group delay summed,
  0.71 s a lap at AMOUNT 100, which every multiple of 1 kHz reaches at that
  setting because the six lengths are whole milliseconds. Crush and Pan/Tremolo
  rows ran at most 80 % of their figures (Crush at AMOUNT 0, which is exact
  with no margin, reached 100 %).
- **The 30 s ceiling stands**; loops longer than that ring past it.
- **Exceptions, recorded at `tailSecondsFor` and not covered by the figure:**
  - ~~an input longer than one lap at high FEEDBACK~~ — **covered from the
    second round**: the figure now counts the build-up a held input leaves
    (below);
  - bucket-brigade at a fractional-sample delay (44.1 kHz, TIME 1, 5 or 375 ms)
    driven by a **one-sample** impulse rang up to 17 % past the first-round
    figure (Diffuse 100, FEEDBACK 60 %, TIME 1 ms: 7.52 s against 6.45); bursts
    stayed inside. The compander's audio and gain rings are interpolated
    separately, and a one-sample transient does not hold the gain constant
    across the taps. Not re-measured against the second-round figure;
  - Crush's sample-and-hold growth, below.

## Crush: the decision for Frosty (B1)

Built: **truncate toward zero**, the closest quantiser on the same grid that
never makes a sample larger. One pass on a 300 Hz sine, output RMS against
input, dB:

| AMOUNT | sine | round (before) | **truncate (built)** | round, else pass the input | half-step dead zone |
|---|---|---|---|---|---|
| 0 | 0.5 | 0.00 | 0.00 | 0.00 | 0.00 |
| 35 | 0.03 | −0.01 | −0.17 | −0.03 | −0.27 |
| 35 | 0.005 | +0.09 | −0.94 | −0.10 | −1.58 |
| 60 | 0.5 | +0.03 | −0.11 | 0.00 | −0.11 |
| 60 | 0.125 | −0.09 | −0.43 | −0.09 | −0.69 |
| 60 | 0.03 | −0.59 | −0.59 | −0.59 | −3.60 |
| 60 | 0.005 | **+4.43** | silent | 0.00 | silent |
| 100 | 0.5 | 0.00 | −3.98 | −0.35 | −6.99 |

In the loop (AMOUNT / FEEDBACK / TIME, time to −60 dB of the first repeat):

| | round (before) | truncate (built) | round, else pass |
|---|---|---|---|
| 100 / 80 / 100 | never; −7.1 dB at 40 s | 0.30 s | 0.50 s |
| 60 / 80 / 100 | never; −34.3 dB at 40 s | 1.20 s | 1.50 s |
| 35 / 80 / 100 | never; −53.1 dB at 40 s | 1.90 s | 2.10 s |
| 0 / 80 / 100 | 2.30 s, then −80.8 dB for good | 2.30 s, then exact zeros | 2.30 s, then exact zeros |

"Round, else pass the input" rounds as before but hands the input on unchanged
whenever rounding would make it larger. It is also non-expanding, keeps more
level, and leaves part of the signal uncrushed.

**A one-step limit cycle survived truncation** — corrected: it starts at
**FEEDBACK 92 %**, not 95 % as this note first said (the review reproduced it;
the first-round check had no row between 90 and 95). A held ±0.25 step comes
back through the lap's filters with a few per cent of overshoot and re-crosses
the step it left. 44.1 kHz, bucket-brigade, AMOUNT 100, TIME 50 ms: FEEDBACK 92 %
held a 0.2245 peak from 9 s to 30 s against a 4.16 s reported tail; 95 % 0.2364;
clean at 96.9 % 0.2440; 96 kHz bucket-brigade AMOUNT 60 0.0410; none at 48 kHz.
**It is fixed in the second round** by a dead zone (below).

## Second round (the same day, after an independent review of the first)

### Crush's dead zone (Frosty's decision)

A held value now keeps `floor(|x| / step − 0.35)` steps. The size was measured
over every character, AMOUNT 35/60/100, TIME 20/50/100/375 ms, FEEDBACK
80/85/90/92/94/95/96/96.5/96.9 %, at 44.1, 48, 88.2, 96, 176.4 and 192 kHz
(1,944 rows, a 5 ms burst then 30 s), then FEEDBACK 92–96.9 % in ten steps at
AMOUNT 60/100 (1,440 rows). A row fails if anything after the reported tail
passes −60 dB of the burst, or if Crush's last second is louder than the same
loop without FX (Crush can only remove energy, so anything louder is Crush
holding it).

| shift (steps) | 0 (truncate) | 0.10 | 0.25 | 0.30 | **0.35** | 0.40 | 0.50 |
|---|---|---|---|---|---|---|---|
| AMOUNT 60/100 rows still cycling | 23 | 6 | 1 | 1 | **0** | 0 | 0 |

0.30 left one, at 176.4 kHz on bucket-brigade (AMOUNT 60, TIME 20 ms, 96.9 %).

One pass on a 300 Hz sine, output RMS against input, dB:

| AMOUNT | sine | truncate (round 1) | **dead zone 0.35 (built)** |
|---|---|---|---|
| 35 | 0.5 | −0.01 | −0.01 |
| 35 | 0.125 | −0.04 | −0.05 |
| 60 | 0.5 | −0.11 | −0.11 |
| 60 | 0.125 | −0.43 | −0.43 |
| 100 | 0.5 | −3.98 | −6.99 |
| 100 | 0.125 | silent | silent |

### What the dead zone does not reach: the hold (for Frosty)

The required grid includes AMOUNT 35, and there, and off the grid at AMOUNT 70
and 80, loops still do not end — **and no dead zone changes that** (truncation,
0.10, 0.25, 0.5 and zeroing below 1.5 or 2 steps were all tried). These loops
**grow above their input**: peaks of 0.046 to 0.767 from a 0.5 burst, from
FEEDBACK 90 %. The cause is the sample-and-hold. Phase-locked to a tone it
turns a sine into a square whose fundamental is up to 4/π of the sine's, so
the stage is not energy-bounded. With the dead zone built, 56 rows cycle (29 at
AMOUNT 35, 21 at 70, 6 at 80; 44.1 kHz most, also 88.2, 96, 176.4, 192), and
one more is late (44.1 kHz, bucket-brigade, AMOUNT 35, TIME 50 ms, FEEDBACK 85 %:
−49 dB after the 1.66 s tail, zero by 2.48 s).

**A non-expanding hold ends all of them.** Holding the mean of the last N
samples instead of the last sample cannot add energy (`N·mean² ≤ Σx²`). In the
same checks, with plain truncation and **no dead zone**, it left 0 cycles and 0
late rows on the 1,944-row grid and on 2,160 off-grid rows (AMOUNT 70/80/90,
TIME 10/30/200/1000 ms, FEEDBACK 92–96.9 %), at all six rates; with the 0.35
dead zone as well, also 0. One pass, dB, at 0.5 / 0.125: AMOUNT 35 −0.09 /
−0.12; AMOUNT 60 −0.32 / −0.54; AMOUNT 100 −3.98 / silent (without the dead
zone). It averages away some top end, so it changes what Crush does. Not built.

### The tail counts the build-up of a held note (Frosty's decision)

`lapsToSixtyDb` counts `ceil((60 + B) / −20 log10 g)`, `B = −20 log10 (1 − g)`,
per frequency, bounded at 120 dB. One second of a tone in phase with the loop,
clean, 48 kHz — reported → measured, before → after:

| TIME / FEEDBACK | before | after |
|---|---|---|
| 375 ms / 60 % | 3.391 → rang 3.750 | 3.762 |
| 250 ms / 80 % | 5.753 → rang 6.250 | 6.755 |
| 100 ms / 90 % | 5.803 → rang 6.101 | 7.604 |

The same rows at 44.1 and 96 kHz and on bucket-brigade failed before and pass
now; tape passed both times. Figures that moved: the defaults 1.884 → 1.888 s,
TIME 2000 and SYNC on 10.009 → 10.013 s, the held THROW at −40 % 2.258 →
2.503 s (9 laps → 10).

### The other items

- FEEDBACK, DRIVE (both smoothers) and LANE LEVEL land exactly on their targets:
  they stalled up to 3.9e-5 off at 44.1 kHz, 4.3e-5 at 48 and 1.7e-4 at 192
  (LANE LEVEL up to 2.29e-4). 21 renders where no parameter moves hash the same
  before and after.
- After `reset`, MIX 100 → 0 gave out[0] 0.00052 and out[511] 0.20668 on a 0.5
  input; now the block is the input.
- **Correction:** the first round said every steady-state row was unchanged by
  the MIX fix. That holds **only where MIX never moves**; the review measured
  that after a MIX move the settled output differs by up to 1.45e-5, because
  the gains now land exactly where they used to stall.
- Test gaps closed: the clean later-tempo check is exact; no tail row sits at
  the 30 s ceiling; the adapter and engine tail checks compare against figures;
  the tail rows also run at 44.1 and 96 kHz; the state round-trip checks each
  id once; a rack case fails without the first-tempo landing.
- docs/delay/15 dates the voicing pull-back 2026-09-22.
- The output level (below) is reviewed and left, by decision.

## S1, output level — reviewed and left (Frosty, 2026-10-01)

A −18 dBFS RMS 1 kHz sine, 20 s (probe, after the first round):

| settings | peak | RMS, last 2 s |
|---|---|---|
| defaults | −8.5 dBFS | −11.5 dBFS |
| FEEDBACK 100, MIX 50 | +1.1 dBFS | −0.7 dBFS |
| HOLD and SEND, LANE GAIN +100, LANE LEVEL 0, FEEDBACK 100, MIX 50 | **+6.7 dBFS** | +5.0 dBFS |
| the same at LANE LEVEL +24 | +24.9 dBFS | +23.3 dBFS |

The +6.7 is three signals each bounded near unity by their own in-loop clip
(the dry, the main delay self-oscillating, the lane building) added together,
and nothing limits their sum. No gain or limiter change, by decision.

## Smaller items

`scripts/build.sh --snapshots` now renders `dwell`; stale comments counting
twenty-six parameters are twenty-seven; `lane_note` is in the wiring test and
the state round-trip; Dwell has a rack-slot case in `tempo_tests`; the sinc A/B
note's header says it was answered.
