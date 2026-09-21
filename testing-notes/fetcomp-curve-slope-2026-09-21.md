# BMO FET: what adds slope above 20 dB GR, and why it is the input amplifier

**AURORA, 2026-09-21.** Closes item 2 of `docs/1176-comp/HANDOFF-dsp-fixes.md`.
Measured with `measure_fetcomp slopeorigin`, built in `build-dsp` with
`BMO_DSP_ONLY=ON` (Release), build exit 0 with no error lines. Branch
`frosty-add-bmo-fetcomp`, PR #22.

The short version: **the divider law, the solve and the bias are all
exonerated. It is `inputAmp` in `Stages.h`, and it is deliberate character.**
The pack's "at every depth" was not reachable and has changed. Nothing here
has been heard.

## The question

`measure_fetcomp curve black` gives 4:1 a local slope that falls 3.76 / 2.99 /
2.56 / 2.34 / 2.29 across 5 to 25 dB GR and then **rises to 2.58** at 30 dB.
That breaks two of 11 §3's four shape properties: the slope is not
monotonically decreasing with depth, and 4:1 at 2.58 sits above 8:1 at 2.42 so
the four are no longer strictly ordered.

The handoff's guess was "the solve or the bias behaviour at extreme drive
rather than the divider law itself." The test file already carried a different
guess in a comment — the input amplifier's soft compression — asserted but
never demonstrated.

## The measurement

`slopeorigin` prints the measured local slope beside the slope of the **static
divider law alone**: the algebra of 10 §5 solved offline, no stages, no
detector, no per-sample solve. Both columns are read at the same drive, taken
from the law rather than searched for.

**Black:**

| GR dB | 4:1 meas | law | 8:1 meas | law | 12:1 meas | law | 20:1 meas | law |
|---|---|---|---|---|---|---|---|---|
| 5 | 3.76 | 3.75 | 7.79 | 7.75 | 11.97 | 11.89 | 20.51 | 20.27 |
| 10 | 2.99 | 2.98 | 5.24 | 5.23 | 7.58 | 7.56 | 12.33 | 12.28 |
| 15 | 2.56 | 2.55 | 3.83 | 3.82 | 5.14 | 5.13 | 7.79 | 7.78 |
| 20 | 2.34 | 2.31 | 3.03 | 3.02 | 3.76 | 3.76 | 5.26 | 5.25 |
| 25 | 2.29 | 2.17 | 2.60 | 2.58 | 3.00 | 2.99 | 3.84 | 3.83 |
| 30 | **2.64** | **2.10** | 2.42 | 2.32 | 2.59 | 2.56 | 3.05 | 3.03 |

**The law never misbehaves.** Its slope falls monotonically at every ratio, and
at 30 dB it keeps the four ordered: 2.10 < 2.32 < 2.56 < 3.03. Whatever adds
slope is outside it.

And the excess is in drive order. At 30 dB GR the implementation departs from
the law by **+0.54 on 4:1, +0.10 on 8:1, +0.03 on 12:1, +0.02 on 20:1** — and
the drives those settings need are:

| GR dB | 4:1 | 8:1 | 12:1 | 20:1 |
|---|---|---|---|---|
| 20 | 30.1 | 25.1 | 23.4 | 22.1 |
| 25 | 39.1 | 32.9 | 30.6 | 28.5 |
| 30 | **48.5** | 41.4 | 38.5 | 35.6 |

dB over threshold. The setting driven hardest picks up the most slope.

## The proof

Correlation is not cause, so: **linearising the input amplifier**. One line in
`Stages.h`, `processInput` returning `cored` instead of
`inputAmp.process (cored)`. Re-measured, Black:

| GR dB | 4:1 meas | law |
|---|---|---|
| 20 | 2.32 | 2.31 |
| 25 | 2.21 | 2.17 |
| 30 | **2.19** | 2.10 |

Monotonically decreasing. And ordered at 30 dB: 2.19 < 2.33 < 2.56 < 3.03.
**Both broken assertions come back.** The mutation was reverted; `Stages.h` is
unchanged in the commit.

## Blue breaks first, at 25 dB — and was not in the record

| GR dB | 4:1 meas | law | 8:1 meas | law | 12:1 meas | law | 20:1 meas | law |
|---|---|---|---|---|---|---|---|---|
| 20 | 2.37 | 2.31 | 3.04 | 3.02 | 3.78 | 3.76 | 5.29 | 5.25 |
| 25 | **2.67** | 2.17 | 2.65 | 2.58 | 3.02 | 2.99 | 3.86 | 3.83 |
| 30 | **3.06** | 2.10 | 2.77 | 2.32 | 2.69 | 2.56 | 3.10 | 3.03 |

Blue's 4:1 rises at **25 dB** (2.37 → 2.67) and already crosses above 8:1 there
(2.67 > 2.65). By 30 dB it is +0.96 off the law. Black holds to 25 and breaks
at 30.

**This is the second time a Black-only sweep has hidden Blue behaviour**, after
the alias floor on the same day (`fetcomp-alias-origin-2026-09-21.md`). The
handoff's table for this item is Black's. 11 §3 says "every curve, THD and
alias case runs in both voicings" and it is worth treating any single-voicing
figure in the record as provisional until re-run.

20 dB is therefore the honest cutoff for both voicings, not a Black-shaped
compromise.

## What changed

**`11 §3`** — the slope and ordering properties now read "to 20 dB GR" with the
measurement, the drive table and the `inputAmp` proof beside them. "At every
depth" is gone. The other two properties are unchanged because they hold.

**`testCurveAboveTwentyDb`** is new, and it asserts what nothing was asserting:
above 20 dB the curve must stay finite, monotone in input, free of
discontinuity (no more than 2 dB of reduction per dB of drive), above 2:1, and
30 dB GR must really be deeper than 25 — for both voicings at all four ratios.
`testGainReductionCurve` stopped at 20 and took these with it.

`fetcomp_dsp` goes **1302 → 1390 checks**, 0 failures.

**Proved falsifiable:** capping `kControlCeiling` at 0.30 so the cell tops out
early produces **22 failures**, including seven settings dropping below 2:1 at
30 dB. Reverted.

## The judgement, and what is not settled

Driving the input amplifier 48.5 dB over threshold is the intended use — the
`input` range reaches +60 dB precisely so 30 dB GR is reachable at 4:1, and
`params.h` says so. The character it adds up there is the model working, not a
defect, and removing it would move the THD figures that currently conform.

So the recommendation matches §1's: **the pack was wrong, not the code.**

What that does *not* settle is whether Blue at 25 dB rising 2.37 → 2.67 sounds
like anything. It is a 13 % slope change at a depth the module is designed to
reach, on the voicing meant to be the more coloured one. That is an
Ableton-pass question and it is now written down as one. **Nothing in BMO FET
has been heard.**
