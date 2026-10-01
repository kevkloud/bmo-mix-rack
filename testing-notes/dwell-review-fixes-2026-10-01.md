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
  - an input longer than one lap at high FEEDBACK builds the loop up above its
    own level and then takes longer to fall 60 dB below that input: tape, TIME
    1 ms, FEEDBACK 96.9 %: 2.95 s after one sample, 4.57 s after 50 ms of noise
    (the figure is 9.31 s, so this row is inside it, but the effect is general);
  - bucket-brigade at a fractional-sample delay (44.1 kHz, TIME 1, 5 or 375 ms)
    driven by a **one-sample** impulse rang up to 17 % past the figure (Diffuse
    100, FEEDBACK 60 %, TIME 1 ms: 7.52 s against 6.45); bursts stayed inside.
    The compander's audio and gain rings are interpolated separately, and a
    one-sample transient does not hold the gain constant across the taps;
  - Crush's one-step limit cycle, below.

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

**A one-step limit cycle survives truncation.** At FEEDBACK 95 % and above with
AMOUNT 100, a held ±0.25 step comes back through the lap's filters with a few
per cent of overshoot and re-crosses the step it left. Twenty seconds after a
5 ms burst, the last second still held a peak of 0.236–0.245:

- 44.1 kHz, bucket-brigade, FEEDBACK 95 / 96 / 96.9 %, TIME 50 and 100 ms;
- 44.1 kHz, clean, FEEDBACK 96.9 %, TIME 50 ms;
- 96 kHz, bucket-brigade, FEEDBACK 96.9 %, AMOUNT 60, TIME 100 ms (peak 0.041);
- none at 48 kHz.

A **half-step dead zone** (`|q| ≤ |x| − step/2`) ended every one of these below
unity in the same check, at the per-pass cost in the table. Not built.

## S1, output level — reported, not changed

A −18 dBFS RMS 1 kHz sine, 20 s, unchanged by any of the fixes:

| settings | peak | RMS, last 2 s |
|---|---|---|
| defaults | −8.5 dBFS | −11.5 dBFS |
| FEEDBACK 100, MIX 50 | +1.1 dBFS | −0.7 dBFS |
| HOLD and SEND, LANE GAIN +100, LANE LEVEL 0, FEEDBACK 100, MIX 50 | **+6.7 dBFS** | +5.0 dBFS |
| the same at LANE LEVEL +24 | +24.9 dBFS | +23.3 dBFS |

The +6.7 is three signals each bounded near unity by their own in-loop clip
(the dry, the main delay self-oscillating, the lane building) added together.

## Smaller items

`scripts/build.sh --snapshots` now renders `dwell`; stale comments counting
twenty-six parameters are twenty-seven; `lane_note` is in the wiring test and
the state round-trip; Dwell has a rack-slot case in `tempo_tests`; the sinc A/B
note's header says it was answered.
