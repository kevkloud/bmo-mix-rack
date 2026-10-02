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
  - ~~bucket-brigade at a fractional-sample delay (44.1 kHz, TIME 1, 5 or 375
    ms) driven by a **one-sample** impulse rang up to 17 % past the first-round
    figure~~ — **not a tail exception at all** (fourth round, below): the
    impulse sat on the first sample after `prepare`, where the expander's
    division of one interpolated ring by the other put out a single sample of
    up to 9.9e5, and the "decay" measured was that spike ringing down. Fixed
    in the DSP; the rows are inside the figure;
  - ~~Crush's sample-and-hold growth~~ — gone with the block mean (third round).

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

## Third round (the same day): Frosty's two decisions

**The rule** (Frosty): "under 100% feedback should lose energy, not be
indefinite" — for every in-loop effect.

### Crush holds each block's mean (Frosty: "average instead of freeze")

Each held value is the mean of the samples since the last hold, truncated
toward zero; the 0.35-step dead zone is removed. A non-finite input counts as 0
in the running sum; prepare and reset clear it; each engine has its own, so the
lane gets it too.

Rows the review reproduced on 3e29d90 (peak, a 5 ms 300 Hz burst at 0.5, last
second of 30 s), now exact zeros:

| rate | character | AMOUNT | TIME | FEEDBACK | 3e29d90 | now |
|---|---|---|---|---|---|---|
| 44.1 kHz | bucket-brigade | 35 | 50 ms | 90 % | 0.5651 | 0 |
| 44.1 kHz | bucket-brigade | 35 | 50 ms | 93 % | 0.6650 | 0 |
| 44.1 kHz | bucket-brigade | 35 | 50 ms | 95 % | 0.7276 | 0 |
| 44.1 kHz | bucket-brigade | 80 | 20 ms | 95 % | 0.7399 | 0 |
| 48 kHz | bucket-brigade | 80 | 20 ms | 95 % | 0.6647 | 0 |
| 48 kHz | bucket-brigade | 70 | 10 ms | 95 % | 0.5415 | 0 |
| 44.1 kHz | clean | 35 | 50 ms | 95 % | 0.3585 | 0 |

The five round-1 rows stay silent. The lane's own Crush at the worst setting
(44.1 kHz, bucket-brigade, AMOUNT 35) held 0.728 on 3e29d90 and is silent now.

**The grids, on the built code** (a 5 ms burst, 30 s; a row cycles if Crush's
last second is louder than the same loop without FX, and is late if anything
after the reported tail passes −60 dB of the burst):

| set | rows | cycling | late | not zero by 30 s |
|---|---|---|---|---|
| every character, AMOUNT 35/60/100, TIME 20/50/100/375 ms, FEEDBACK 80–96.9 % (9 steps), 44.1/48/88.2/96/176.4/192 kHz | 1,944 | 0 | 0 | 41 |
| FEEDBACK 92–96.9 % in 10 steps, AMOUNT 60/100, the same rates and characters | 1,440 | 0 | 0 | 0 |
| AMOUNT 70/80/90, TIME 10/30/200/1000 ms, FEEDBACK 92–96.9 %, the same | 2,160 | 0 | 0 | 0 |

The 41 not yet zero at 30 s are all TIME 375 ms, AMOUNT 35, FEEDBACK 95 % and
above, on clean and bucket-brigade, and in every one the loop without FX is
itself still ringing. Last-second peak, with Crush / without FX:

| rate (kHz) | char | 95 % | 96 % | 96.5 % | 96.9 % |
|---|---|---|---|---|---|
| 44.1 | clean | — | 0.0120 / 0.0449 | 0.0292 / 0.0755 | 0.0507 / 0.110 |
| 44.1 | BBD | — | 7.7e-18 / 0.0491 | 0.0066 / 0.0822 | 0.0174 / 0.119 |
| 48 | clean | — | 0.0134 / 0.0453 | 0.0313 / 0.0760 | 0.0537 / 0.110 |
| 48 | BBD | — | 0.0014 / 0.0491 | 0.0106 / 0.0823 | 0.0214 / 0.119 |
| 88.2 | clean | — | 0.0127 / 0.0462 | 0.0300 / 0.0775 | 0.0531 / 0.112 |
| 88.2 | BBD | — | 0.0162 / 0.0490 | 0.0362 / 0.0822 | 0.0615 / 0.119 |
| 96 | clean | 0.0028 / 0.0147 | 0.0259 / 0.0463 | 0.0501 / 0.0775 | 0.0816 / 0.112 |
| 96 | BBD | — | 0.0176 / 0.0490 | 0.0396 / 0.0821 | 0.0662 / 0.119 |
| 176.4 | clean | 0.0021 / 0.0148 | 0.0227 / 0.0464 | 0.0466 / 0.0778 | 0.0746 / 0.113 |
| 176.4 | BBD | 0.0021 / 0.0155 | 0.0276 / 0.0490 | 0.0562 / 0.0821 | 0.0894 / 0.119 |
| 192 | clean | 0.0035 / 0.0148 | 0.0279 / 0.0465 | 0.0566 / 0.0778 | 0.0896 / 0.113 |
| 192 | BBD | 0.0027 / 0.0155 | 0.0290 / 0.0490 | 0.0575 / 0.0821 | 0.0914 / 0.119 |

**One pass, sine at amplitude 0.5, 48 kHz, output RMS against input, dB** —
the original sample-and-hold with rounding (d783f59), the frozen hold with
truncation and the dead zone (3e29d90), and the block mean with truncation (now):

| AMOUNT | version | 300 Hz | 1 kHz | 5 kHz | 10 kHz |
|---|---|---|---|---|---|
| 35 | d783f59 | +0.00 | −0.01 | −0.01 | silent |
| 35 | 3e29d90 | −0.01 | −0.01 | −0.01 | silent |
| 35 | **now** | **−0.09** | **−0.92** | **−14.82** | **−16.38** |
| 60 | d783f59 | +0.03 | +0.05 | +0.05 | +0.05 |
| 60 | 3e29d90 | −0.11 | −0.10 | −0.10 | −0.09 |
| 60 | **now** | **−0.32** | **−2.74** | **−30.64** | **−30.06** |
| 100 | d783f59 | −0.00 | +1.25 | +1.25 | +1.25 |
| 100 | 3e29d90 | −6.99 | −4.77 | −4.77 | −4.77 |
| 100 | **now** | **−3.98** | **silent** | **silent** | **silent** |

"Silent" means the pass truncates every sample to zero. At AMOUNT 35 the frozen
hold takes every 12th value, so a 10 kHz sine lands on the same two phases and
reads zero: aliasing, not level. At AMOUNT 100 the mean of 32 samples of a 0.5
sine at 1 kHz and above stays under one step (0.25), so it is gone on the
first pass; d783f59's +1.25 dB is the expanding rounding.

**Cost**, best of seven: the Crush stage alone 6.53 ns per sample per channel on
3e29d90, 7.83 now; the whole module with Crush on (FEEDBACK 80, 48 kHz,
stereo) 163.6 ns per stereo sample on 3e29d90, 165.7 now (+1.3 %). One add per
sample and one divide per hold; no allocation.

### Every in-loop effect loses energy: the property test

`testEveryInLoopEffectLosesEnergyUnderUnity`: Diffuse, Pan/Tremolo and Crush ×
three characters × AMOUNT 0/35/60/100 × 44.1/48/96 kHz × (TIME 50 ms, FEEDBACK
93 %) and (TIME 20 ms, FEEDBACK 96.9 %). Each row must end in exact zeros or be
quieter in its last second than the one before, and be under −60 dB of the
burst after the reported tail. **216 rows, 16 s, all pass**; no Diffuse or
Pan/Tremolo row fails. On 3e29d90's Crush it fails (AMOUNT 35, bucket-brigade,
44.1 kHz, TIME 50 ms, FEEDBACK 93 %: last second RMS 0.866 against 0.83).

### The tail: the first repeat is not charged the FX delay ("keep it safe")

The per-lap bound is unchanged; the FX delay is now charged to every lap but
the first, which is tapped before the loop's effects:

| Diffuse 100 | before | after | measured |
|---|---|---|---|
| FEEDBACK 80 %, TIME 100 ms | 21.993 s | 21.279 s | 6.20 s |
| FEEDBACK 35 %, TIME 20 ms | 3.685 s | 2.970 s | 1.12 s |
| FEEDBACK 35 %, TIME 375 ms | 5.460 s | 4.745 s | 2.62 s |

(The first round's 18.73 s for the first row was before the build-up term.)

Never-shorter grid on the corrected figure: every character, FEEDBACK
35–96.9 %, TIME 1–2000 ms, Diffuse 35/60/100, Crush 0/60/100 and Pan/Tremolo
100, four inputs, at 44.1, 48 and 96 kHz, plus FX off at 44.1: **14,952 rows,
12 short, every one the exception below**; none at 48 or 96 kHz, none on clean
or tape, none with a burst or a held tone. (Fourth round: 0 short.)

### The bucket-brigade impulse exception, re-measured

**Superseded by the fourth round**: these "measured" figures are the ring-down
of a one-sample output spike of up to 9.9e5 that the expander put out where the
impulse landed, not a tail. Kept as the record of what was seen.

Bucket-brigade, 44.1 kHz, a one-sample impulse, TIME at a fractional sample
count. Figure in seconds, before this round's first-lap fix / after / measured:

| FX | FEEDBACK | TIME | before | after | measured | past the figure now |
|---|---|---|---|---|---|---|
| off | 35 % | 375 ms | 1.889 | 1.889 | 1.909 | +1.1 % |
| off | 60 % | 375 ms | 3.765 | 3.765 | 3.778 | +0.3 % |
| Diffuse 60 | 35 % | 1 ms | 0.944 | 0.759 | 0.954 | +25.7 % |
| Diffuse 60 | 35 % | 5 ms | 0.964 | 0.779 | 0.980 | +25.8 % |
| Diffuse 60 | 35 % | 375 ms | 2.815 | 2.630 | 2.643 | +0.5 % |
| Diffuse 60 | 60 % | 1 ms | 1.875 | 1.689 | 1.922 | +13.8 % |
| Diffuse 60 | 60 % | 5 ms | 1.915 | 1.729 | 1.978 | +14.4 % |
| Diffuse 100 | 35 % | 1 ms | 3.590 | 2.875 | 3.803 | +32.3 % |
| Diffuse 100 | 35 % | 5 ms | 3.610 | 2.895 | 3.873 | +33.8 % |
| Diffuse 100 | 35 % | 375 ms | 5.461 | 4.747 | 5.281 | +11.3 % |
| Diffuse 100 | 60 % | 1 ms | 7.166 | 6.452 | 7.520 | +16.6 % |
| Diffuse 100 | 60 % | 5 ms | 7.206 | 6.492 | 7.622 | +17.4 % |

Ten were short before the first-lap fix (by 0.3–7.3 %); the two Diffuse rows
at 375 ms were inside (−3.3 %, −6.1 %) and the fix exposed them. Fixed in the
fourth round, at the cause.

### Also

docs/delay/15 has no 2026-09-23 date left (lane_note and stage 2b were
committed on 2026-09-22, 1d5b5bd and c4d2d33).

## Fourth round (the same day): the bucket-brigade output spike

**The defect.** The expander read `I(v·g) / I(g)`: the interpolated audio ring
divided by the interpolated gain ring. The gain ring holds exactly 1.0 wherever
nothing was companded — after `prepare`, after a reset or a HOLD clear, and
over anything written on clean or tape — beside compressor gains of 0.708 to
31.6. At a fractional read position the interpolator's negative taps carry the
step from 1.0 to 31.6 through zero (Hermite at the halfway phase:
−0.0625 + 0.5625 + 0.5625 − 0.0625 × 31.6 = −0.91), the division went to its
1e-6 clamp, and one output sample came out at up to 6.3e5 — **+115 dBFS**. It
has been there since the compander was built: d783f59 gives the same figures.

**The fix** (`DelayEngine::readExpanded`): every tap is divided by the gain
written beside it, then interpolated, `I(v·g / g)` — on the Hermite read, on
Clean's sinc (which reads a move off bucket-brigade's ring), and on both halves
of a crossfade. The read is then an interpolation of what was written, bounded
by that times the kernel's absolute sum: **1.25** for Hermite (exact, phase ½)
and **2.21** for the 24-tap sinc (measured from the built table, phase ½). The
stored gains are 1.0 or 0.708–31.6, so the clamp can no longer fire.

**Every event, output peak with noise at 0.3, MIX 100, FEEDBACK 60**
(`testABucketBrigadeBoundaryNeverSpikes`; bound `K (0.3 + 1)` = 2.87 per
engine, 5.74 with the lane summed in):

| event | rate, TIME | 05572c7 | now |
|---|---|---|---|
| signal on the first sample after prepare | 44.1 kHz, 375 | **5.5e5** | 0.366 |
| signal on the first sample after prepare | 48 kHz, 375.3 | **6.3e5** | 0.363 |
| signal through a reset | 44.1 kHz, 375 | **1.9e4** | 0.357 |
| signal through a reset | 48 kHz, 375.3 | **4.8e5** | 0.357 |
| signal starting at a reset | 44.1 kHz, 375 | **5.5e5** | 0.366 |
| clean → bucket-brigade mid-signal | 44.1 kHz, 375 | **2.5e5** | 0.479 |
| clean → bucket-brigade mid-signal | 48 kHz, 375.3 | **2.3e5** | 0.433 |
| tape → bucket-brigade mid-signal | 44.1 kHz, 375 | **8.7e4** | 0.409 |
| tape → bucket-brigade mid-signal | 48 kHz, 375.3 | **3.0e5** | 0.411 |
| bucket-brigade idle → clean, signal at the move | 44.1 kHz, 375 | **2.3e5** | 0.509 |
| bucket-brigade idle → clean, signal at the move | 48 kHz, 375.3 | **2.0e5** | 0.499 |
| bucket-brigade idle → tape, signal at the move | 44.1 kHz, 375 | **2.7e5** | 0.364 |
| bucket-brigade idle → tape, signal at the move | 48 kHz, 375 | **2.3e5** | 0.360 |
| MOD 50 on a whole-sample TIME, after prepare | 48 kHz, 375 | **3.4e5** | 0.360 |
| TIME 375 → 600 ms in flight, after prepare | 48 kHz, 375 | **6.2e5** | 0.337 |
| the lane, HOLD and SEND after prepare | 44.1 kHz, lane 375.3 | **5.5e5** | 0.683 |
| the lane, HOLD and SEND after prepare | 48 kHz, lane 375.3 | **26.6** | 0.652 |
| the lane, HOLD and SEND after a reset | 48 kHz, lane 375.3 | **12.4** | 0.639 |

Three of these were not in the report that opened the round. **A move off
bucket-brigade spikes too**, when the line was idle and the signal starts at
the move: the boundary then runs from companded silence (31.6) down to 1.0, and
Clean's sinc and Tape's Hermite both read it. Tape's modulation floor makes
every tape read fractional, so that row spikes at 48 kHz on a whole-sample TIME.
**MOD and a TIME move** make a whole-sample TIME fractional, so 48 kHz is not
safe on its own. **The lane** is the same engine and HOLD resets it. With signal
throughout, a move off bucket-brigade does not spike (gains near 1.8 against
1.0), which is why it looked clear.

**The property of record** (`testNoEventSpikesTheOutputOnAnyCharacter`): every
character after prepare and after reset and all six moves, 44.1 / 48 / 96 kHz,
TIME 375 and 375.3, noise throughout or from the event — 144 rows, 1.7 s. On
05572c7 **25 fail**, worst 6.3e5, every one a fractional read across a 1.0 /
companded boundary; clean and tape on their own pass every row there too, so
there is no new finding on them. Now 0 fail; worst peak 0.588.

**Steady state** (noise at 0.3 for 2 s from 1 s after prepare, MIX 100):

- bucket-brigade at a whole-sample delay, 48 kHz TIME 375, FEEDBACK 0 and 60,
  and with the noise from sample 0: **bit-identical** (same hashes);
- at a fractional delay, 44.1 kHz TIME 375, against the old render: from 1.5
  to 2.9 s, difference peak 0.00018 / 0.00022 and RMS −89.8 / −89.6 dBFS at
  FEEDBACK 0 / 60, 72.6 / 72.5 dB under the signal; over the whole render the
  peak is 0.0022, at the first repeat's onset (1.380 s), where the gain moves
  fastest.

The second round's character-move row still holds: a 0.05 repeat in flight
comes back at −29.05 dBFS, peak 0.0500, on bucket-brigade → clean, → tape,
clean → bucket-brigade and the renders that stayed.

**Cost**, bucket-brigade at FEEDBACK 60, best of seven, ns per stereo sample:
126.0–127.4 before, 119.7–125.6 after (44.1 and 48 kHz, TIME 375 and 375.3).
One Hermite with four divisions is cheaper than two Hermites and a division.

**The tail.** With the spike gone the bucket-brigade "impulse exception" is
gone with it. The 16 rows it covered (44.1 kHz, TIME 1 / 2 / 5 / 375 ms, FX off
and Diffuse 60 / 100, FEEDBACK 35 / 60 %) are now in
`testTheReportedTailIsNeverShorterThanTheDecay`: all 16 fail on 05572c7 (e.g.
Diffuse 100, FEEDBACK 35 %, TIME 375: 5.281 s against 4.747) and all pass now
(that row 1.039 s; FX off, FEEDBACK 60 %, TIME 375: 1.877 s against 3.765). The
same impulse given a second of silence first was inside the figure on 05572c7
as well (258 rows, worst 0.992 of the figure). **The never-shorter grid**
(14,952 rows, as in the third round) on the built code: **0 short** under the
30 s ceiling (12 on 05572c7); the worst row is 0.997 of its figure.

**Build and tests, on AURORA**: a Release build of every test target, named,
exit 0; `ctest -C Release` 39 of 39 passed (`tune_hardtune_target` is disabled
by design); `dwell_dsp_tests` 1390 checks, 0 failures. Crush is unchanged from
05572c7 in this round. Nothing has been listened to.

## Fifth round (2026-10-02): Crush matches energy (Frosty: "energy match it")

**What was replaced.** The block mean built on 2026-10-01 (third round) was
bounded but measured as a steep low-pass: one pass at 0.5, AMOUNT 60, about
−30 dB at 5 and 10 kHz, and at AMOUNT 100 silent from 1 kHz up. On Frosty's
decision it is replaced by **the energy-matched hold**: each held value has the
magnitude of the RMS of the samples since the last hold, `√(Σx²/N)`, and the
sign of the newest of them (the sample a frozen hold would have taken; 0 if it
is 0), then plain truncation toward zero, no dead zone. N copies of a block's
RMS carry exactly the block's energy. Causal by one block, as the mean was; a
non-finite sample counts as 0 in the sum and in the sign; prepare and reset
clear it; each engine has its own.

**One pass, sine from phase 0, 48 kHz, output RMS against input, dB** —
the original sample-and-hold with rounding (d783f59), the block mean (b2fda42)
and the energy-matched hold (now), rebuilt from each tree on AURORA:

| amplitude | AMOUNT | version | 300 Hz | 1 kHz | 5 kHz | 10 kHz |
|---|---|---|---|---|---|---|
| 0.5 | 35 | d783f59 | +0.00 | −0.01 | −0.01 | silent |
| 0.5 | 35 | block mean | −0.09 | −0.92 | −14.82 | −16.38 |
| 0.5 | 35 | **now** | **−0.01** | **−0.01** | **−0.01** | **−0.01** |
| 0.5 | 60 | d783f59 | +0.03 | +0.05 | +0.05 | +0.05 |
| 0.5 | 60 | block mean | −0.32 | −2.74 | −30.64 | −30.06 |
| 0.5 | 60 | **now** | **−0.07** | **−0.09** | **−0.08** | **−0.11** |
| 0.5 | 100 | d783f59 | −0.00 | +1.25 | +1.25 | +1.25 |
| 0.5 | 100 | block mean | −3.98 | silent | silent | silent |
| 0.5 | 100 | **now** | **−3.98** | **−3.01** | **−3.01** | **−3.01** |
| 0.125 | 35 | d783f59 | +0.00 | +0.01 | +0.01 | silent |
| 0.125 | 35 | block mean | −0.12 | −0.96 | −14.94 | −16.74 |
| 0.125 | 35 | **now** | **−0.04** | **−0.04** | **−0.04** | **−0.05** |
| 0.125 | 60 | d783f59 | −0.09 | −0.07 | −0.07 | +0.05 |
| 0.125 | 60 | block mean | −0.54 | −3.09 | silent | silent |
| 0.125 | 60 | **now** | **−0.34** | **−0.42** | **−0.33** | **−0.22** |
| 0.125 | 100 | all three | silent | silent | silent | silent |

At AMOUNT 100 the step is 0.25 and a 0.125 sine is under it, so every version
truncates it away; at 0.5 the hold's 0.354 RMS truncates to 0.25 (−3.01 dB).
d783f59's 10 kHz "silent" at AMOUNT 35 is its frozen hold landing on the same
two zero-crossing phases. Over 64 start phases the new hold reads −0.02 to
0.00 dB at AMOUNT 35, −0.16 to −0.01 at 60, −3.01 from 1 kHz up at 100, and
−3.98 to −5.23 at 300 Hz at 100 (exactly five holds a cycle, so it depends on
where they land).

**Tests**, each failing on b2fda42 first (16 failures there) and passing now:

- `testCrushHoldsTheBlockEnergy` replaces `testCrushHoldsTheBlockMean`: the rule
  sample for sample at AMOUNT 35 and 100; each held block's energy within one
  quantiser step of the input block's, read off the output (worst block
  0.25 % under at AMOUNT 35; on b2fda42 98.5 % and 100 % under); a zero hold
  sample holds 0; the NaN and infinity guard; reset; the lane's own Crush ends
  in zeros.
- `testCrushKeepsItsTopEnd`: the one-pass levels above are pinned — within
  0.25 dB of 0 at AMOUNT 35 and 60, −5.5 to −2.5 dB at 100. On b2fda42: −14.78
  and −19.08 dB at 5 and 10 kHz at AMOUNT 35, −30.64 and −28.67 at 60, silent
  from 1 kHz at 100.
- `testCrushCarriesNoDcOutOfTheLoop`: a 4 kHz tone locked to the 12-sample
  hold at AMOUNT 35 comes out of the stage alone as DC, 0.353 from a 0.354 RMS
  tone (0.0001 with the block mean); through the loop on clean, TIME 50 ms,
  FEEDBACK 95 %, the output's mean over the tone's last second is 7.1e-16
  against a 1e-3 bound, because the 10 Hz blocker after the stage keeps it out
  of the ring, and after the tone the loop decays to exact zeros.
- `testCrushEndsInZerosAtEveryRate` and
  `testEveryInLoopEffectLosesEnergyUnderUnity` pass unchanged.

**The grids, on the built code** (5 ms burst, 30 s; rows cycling / late / not
zero by 30 s; the harness was rebuilt after its scratch folder was lost, with
FEEDBACK 80, 85, 90, 92, 93, 95, 96, 96.5 and 96.9 % on the full grid and ten
even steps from 92 to 96.9 % on the dense and off-grid sets):

| set | rows | cycling | late | not zero by 30 s |
|---|---|---|---|---|
| every character, AMOUNT 35/60/100, TIME 20/50/100/375 ms, 9 FEEDBACK steps, 44.1/48/88.2/96/176.4/192 kHz | 1,944 | 0 | 0 | 46 |
| FEEDBACK 92–96.9 % in 10 steps, AMOUNT 60/100, the same rates and characters | 1,440 | 0 | 0 | 0 |
| AMOUNT 70/80/90, TIME 10/30/200/1000 ms, the same FEEDBACK, rates and characters | 2,160 | 0 | 0 | 0 |
| 44.1/48/96 kHz, every character, AMOUNT 35/70/80, TIME 10/20/50/100 ms, FEEDBACK 85/90/93/95 % | 432 | 0 | 0 | 0 |

The 46 are all AMOUNT 35, TIME 375 ms, FEEDBACK 95 % and above (44 on clean
and bucket-brigade, 2 on tape at 96.9 %), and every one is still decaying with
the loop without FX itself still ringing: Crush's last-second peak is 8.6e-15
to 0.82 of the FX-off loop's.

**The tail with Crush in the loop.** The per-lap charge is `divisor − 1`
samples at 44.1 kHz. The hold delays a lap by about that, measured at 48 kHz
(phase delay at 100 and 300 Hz, and a 300 Hz burst's energy centroid):

| AMOUNT | hold | frozen (d783f59) | block mean | **now** | charged |
|---|---|---|---|---|---|
| 35 | 12 | 5.5 | 11.0 | **10.0–11.0** | 11 |
| 60 | 20 | 9.5 | 18.9–19.1 | **16.8–19.1** | 19 |
| 100 | 32 | 15.5 | 21.0–31.5 | **25.8–32.5** | 31 |

— 0.21–0.23 / 0.35–0.40 / 0.54–0.68 ms a lap at 48 kHz. The first repeat never
passes through Crush and is exact. Every never-shorter row with Crush 0, 35,
60 or 100 in the loop, every character, FEEDBACK 35–96.9 %, TIME 1–2000 ms,
four inputs, at 44.1, 48 and 96 kHz: **5,664 rows under the 30 s ceiling, 0
short**; worst 0.998 of the figure (AMOUNT 35), 0.799 at AMOUNT 60 and 0.452
at 100. The block mean on the same rows: 0 short, the same worst figures to
within 0.03.

**Cost**, best of seven on AURORA, three runs each: the Crush stage 4.46–4.49 ns
per sample per channel on b2fda42, 4.64–4.73 now (one multiply-add a sample,
and a divide and a square root once a hold); the module with Crush 60 on,
FEEDBACK 80, 48 kHz, 160.9–162.5 ns per stereo sample before and 160.6–161.4
after.

**Build and tests, on AURORA**: a Release build of every test target, named,
exit 0; `ctest -C Release` 39 of 39 passed (`tune_hardtune_target` is disabled
by design); `dwell_dsp_tests` 1411 checks, 0 failures. Nothing has been
listened to.

## Sixth round (2026-10-02): an independent review of rounds 2-5

An independent review attacked rounds 2-5 (96c5b0c..73036df). Five of the six
DSP commits held: a fuzzer found nothing over the spike bound in 456 M samples,
and no Crush row holds a level. One did not, and it was the review's own
request. All figures below are on AURORA; measured means from the last
non-zero input sample to the last output sample on either channel above 1e-3
of the input's peak, MIX 100.

### de781b3 was a mistake, and is undone

On 2026-10-01 the review asked for the first lap to be spared the in-loop
effect's delay, on the premise that the first repeat is tapped before the
effect. That holds for a burst and not for a held note: when the input stops
the effect's state is full -- Diffuse's allpasses are still ringing -- and the
first lap after it carries their delay too. The premise had only been tested
at FEEDBACK 1 %. With it, a 0.1 tone held 1 s rang past the figure; the FX
delay is now charged on every lap again (1e50b61):

| row (0.1 tone held 1 s, FEEDBACK 50 %, TIME 47 ms) | rate | before: reported / measured | now: reported / measured |
|---|---|---|---|
| clean, Diffuse 100, 1 kHz | 48 kHz | 4.6341 / 4.7718 (+2.97 %) | 5.3485 / 4.7718 (−10.78 %) |
| clean, Diffuse 20, 664 Hz | 96 kHz | 0.5487 / 0.5668 (+3.30 %) | 0.5822 / 0.5668 (−2.65 %) |
| bucket-brigade, Diffuse 100, 1 kHz | 48 kHz | 4.6343 / 4.7709 (+2.95 %) | 5.3487 / 4.7709 (−10.80 %) |

The same three rows were short at the other two rates too (+2.03 to +3.32 %).

### Three settings the figure never covered

None was introduced by rounds 2-5, but the docs had come to say the figure
"holds up to the 30 s ceiling", so each is now a derived term (48 kHz below;
the same at 44.1 and 96 kHz to the fourth decimal unless noted):

| setting | before: reported / measured | now: reported / measured | term |
|---|---|---|---|
| clean, TIME 120, FEEDBACK 35, MOD 100 at 8 Hz, burst 1/8 into the wow | 0.6127 / 0.6371 (+3.98 %) | 0.6527 / 0.6371 (−2.39 %) | MOD's largest delay swing, every lap (7c2f63d) |
| the same, burst 2/8 in | 0.6127 / 0.6329 (+3.29 %) | 0.6527 / 0.6329 (−3.04 %) | |
| tape, TIME 1000, MOD 100 at 1 Hz, 1/8 in, 96 kHz | 5.0255 / 5.0640 (+0.77 %) | 5.1394 / 5.0640 (−1.47 %) | |
| main FEEDBACK 0, THROW −40 % at 250 ms, LANE LEVEL +24 dB, burst | 2.5029 / 2.7491 (+9.84 %) | 3.5066 / 2.7491 (−21.60 %) | the lane's LEVEL above 0 dB (b419424) |
| the same at +12 dB | 2.5029 / 2.4989 (−0.16 %) | 3.0111 / 2.4989 | |
| main FEEDBACK 60 % at 250 ms and a THROW lane at the same gain, 400 Hz held 1 s at 0.25, clean | 2.5118 / 2.7505 (+9.50 %) | 2.7604 / 2.7505 (−0.36 %) | 6.02 dB for each engine when both ring (5f043c0) |
| the same, bucket-brigade | 2.5137 / 2.7523 (+9.49 %) | 2.7628 / 2.7523 (−0.38 %) | |

The two-engine term is a bound, not a fit: two loops at the same gain fed the
same held tone add in phase to twice either, which is what the −0.36 % shows.
The hand-worked lane figure at the defaults with HOLD on moves from 10 laps of
250 ms to 11 (10.01 laps with the 6.02 dB).

### The grid, on the built code

The never-shorter grid of the third round (every character, FEEDBACK
35–96.9 %, TIME 1–2000 ms, Diffuse 35/60/100, Crush 0/60/100, Pan/Tremolo 100,
four inputs, 44.1/48/96 kHz, plus FX off at 44.1: 14,952 rows), plus held
notes through every FX type (1,296 rows), MOD at 50 and 100 % on four phases
of the wow (1,296), lane LEVEL 0 to +24 dB (540) and both engines ringing
together (108), at 44.1, 48 and 96 kHz: **18,192 rows, 5,688 at the 30 s
ceiling and not rendered, 12,504 rendered, 0 short; worst 0.9986 of the
figure** (clean, Crush 0, FEEDBACK 35 %, TIME 2000 ms, 1 kHz burst). Worst per
set: never-shorter 0.9986, held FX 0.974, MOD 0.997, lane 0.871, both
engines 0.998. The harness was rebuilt after the scratch folder was lost, from
the third round's definition.

### Smaller items

- **FX AMOUNT lands on its target** (6151284). 3643cfc missed it; it sat
  1.3e-5 off at 44.1 kHz, 1.4e-5 at 48 and 5.7e-5 at 192.
- **Every Crush hold obeys the energy rule** (4c3d13e). The first hold after a
  clear covered one sample and was held for N: with a 0.9 tone from the first
  sample, 25.58 times its block at AMOUNT 100, and 11.99 when AMOUNT went 35 to
  100 mid-tone. A hold now spreads its block's energy over the samples it is
  held for (the same value in steady state): 0.880 / 1.000 / 1.000. And FX
  back on picked up the hold from before it went out (the FX state of a
  tone-fed and a silent engine, the moment FX is back on, was 94.2167 against
  94.7124); Crush is now cleared whenever it comes (back) into the loop, and
  both read 32.7124.
- **The spike bound is per read path with the loop's actual gain**
  (1ca4af9). The round-4 bound, 2.87, passed every row of an engine whose
  output was deliberately doubled; the new one fails 155 of the property's 288
  rows (FEEDBACK 0 rows added) and one boundary row on that engine, and none
  on the real one.
- **The sustained tail rows run at 0.1** (e13a181): at 0.5 they built into the
  clip at FEEDBACK 80 and 90 % and ran at 0.55–0.93 of the figure; at 0.1
  clean reads 0.962 / 0.921 and bucket-brigade 0.925 / 0.907 (tape 0.808 /
  0.623). **High FEEDBACK is back under the ceiling**: FEEDBACK 93–95 % at
  TIME 5–20 ms with a held tone at 0.01, clean and bucket-brigade at 0.92–0.98
  of the figure.
- **Crush's lap charge is a bound** (5c444d5): 2 (N − 1) samples, not N − 1,
  which the measured 32.5 samples at AMOUNT 100 exceeded. No row had been
  short; 3-bit truncation ends those tails first.

### CI time

The JUCE-free Dwell suite, built the same way each time on AURORA: 57.7 s at
73036df, 64.2 s with this round's rows, **51.0 s** after the trim (228b6ce);
the CMake-built `dwell_dsp_tests` 51.9 s. What went: the energy property's
Diffuse and Pan/Tremolo rows at 44.1 and 96 kHz and at AMOUNT 0 (108 of 216
rows; 15.5 → 7.1 s), and the never-shorter test's Diffuse 100 / FEEDBACK
80 % / TIME 100 ms row at 44.1 and 96 kHz (18 renders of a 22 s figure the
decay reaches 28 % of; 24.0 → 19.4 s). Crush keeps every rate and AMOUNT, and
Diffuse's figure is held at all three rates by the held-note rows.

### What the figure covers now

For the parameters as they stand, the output on either channel at MIX 100 is
under 1e-3 of the input's peak by the reported time, up to the 30 s ceiling,
whatever the input. Not covered, by name: a loop past 30 s (by decision), and a
parameter moved while the loop rings.
