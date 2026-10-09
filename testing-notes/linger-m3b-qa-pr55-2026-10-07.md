# BMO Linger M3b: answering QA's review of PR #55 (2026-10-07, on ICE QUEEN)

QA reviewed head `5fa2db5` on 2026-10-07: no blocker in the audio, one
decision for Frosty (DARKEN's range, which he has made), a false statement in
the PR body, and a list of fixes. This is what was done about each, on **ICE
QUEEN**, on top of the ATTACK commit `c4d6d18`.

## QA's items

| item | what QA found | what was done |
|---|---|---|
| 1 | The PR body said DARKEN's range was not changed; `5fa2db5` changed it | Body corrected. `modules/reverb/AGENTS.md` now has the entry. |
| 2a | The CPU table left modulation's cost out | QA's whole-PR figures are in `10` section 6 and the PR body. The 192 kHz figure is with Frosty. |
| 2b | Modulation shortens the top of the tail; no test above 6.4 kHz | Pinned as the price of modulation, with five rows at 8 and 12 kHz. Not compensated. With Frosty as a voicing question. |
| 2c | The 3-cent test passes with the slope guard removed | The test moves both knobs. The bound is built at 2.94 cents. |
| 2c | The flat-EQ test passes with the identity shortcut removed | The node's mix is read directly and asserted to the bit. |
| 2c | The energy test's 5 s windows hide 0.17 to 0.45 dB a second | Energy per second; floor 1.65 dB a second against a slowest row of 1.72. |
| 2c | Seven tests with no "shown failing" record | The table below. |
| 2d | Plate's envelope "asserted again" was not | It is asserted. |
| 2e | Stale notes after `5fa2db5` | Corrected. |
| nit | "Never past 3 cents" only with the knobs still | See 2c: now true with them moving. |
| nit | The reported tail ignores the input stage's ringing | Recorded in `10` section 4. Not charged. |
| nit | A 20 Hz to 20 kHz jump of a Q 40 node steps 2.38x | Recorded as open. With Frosty. |
| nit | One NaN latches the stage | Recorded. Not a regression, not changed. |
| nit | A test hook in a shipping header; MOD DEPTH 0 in three tests | Recorded as deliberate. |
| nit | The stage-in pins allowed 0.05 dB against 5e-7 | 0.001 dB. |

## Each test, shown failing

Five broken builds of `reverb_dsp_tests`, sources restored after each and the
suite re-run green.

| test | how it was broken | what failed |
|---|---|---|
| no line strays further than MOD DEPTH | every new target 1.5x the depth | that row |
| two lines' paths are not the same path | one seed and one rate for all eight lines | that row |
| every line is modulated | depth forced to zero | that row (and "not the same path": eight still lines are one path) |
| 3 cents with a knob moved | the slope guard's `forSlope` removed | both moved-knob rows, at 4.83 cents; the fixed-corner row still passes at 2.48, which is QA's point |
| the modulated tail falls at least 1.65 dB a second | every loop read x 1.0005 | that row, at 1.15 |
| no second of the modulated tail is louder than the last | every loop read x 1.01 | that row, 8 of 12, with the two older growth tests |
| the input stage is bit-identical across block sizes | DARKEN's glide counted once more per call | that row |
| MIX 50 %, faders off: the output is the input | one sample a block of the stage's output written to the left channel | that row |
| DARKEN is 3 dB down at the knob's frequency, 1 kHz included | the coefficient as 1 - exp (-w) | that row |
| a flat node's mix is (1, 0, 0) to the bit | the identity shortcut's condition as `false` | that row, and nothing else |
| Plate's envelope under 0.2 | depth forced to zero | that row: without modulation Plate is red again, which is the claim |

Two things the first of those builds also broke, as it should have: the
top-of-the-tail rows (three of five, the loss being smaller with every line on
one path) and four older rows that pin the tail's level and decay to 2 %.

## What the new rows measure

- Modulation, steepest detune: 2.476 cents over the 16 fixed corners; 2.943
  with MOD DEPTH or MOD RATE moved across its range, at 48 and 192 kHz.
- The modulated tail at DECAY 20 s x 2.0, 12 rows: falls 1.72 to 2.05 dB a
  second, no second louder than the one before.
- T60 at the default modulation over T60 unmodulated, Room, DECAY 2 s,
  48 kHz, ATTACK off: 0.954 (12 m, 8 kHz), 0.815 (12 m, 12 kHz), 0.690
  (0.5 m, 12 kHz), 0.811 (0.5 m, HIGH x 2.0, 8 kHz), 0.634 (0.5 m,
  HIGH x 2.0, 12 kHz).
- Plate's late-envelope autocorrelation: 0.189 against 0.2.

## Open with Frosty

1. CPU at 192 kHz / 32: the whole PR is about one point of a core over
   `main` (QA: 5.58-5.82 % to 6.64-6.88 % on the day, `main` itself reading a
   point high). He accepted 5.18 % for M3a.
2. The top of the tail under modulation: leave it, or look at holding it up
   in M4.
3. The frequency jump on a sharp node: glide the frequency, or accept.
4. The 1 kHz end of DARKEN has not been heard.
