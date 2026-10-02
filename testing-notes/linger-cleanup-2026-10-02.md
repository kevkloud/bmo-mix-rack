# BMO Linger: the cleanup before M3, 2026-10-02

All of this was done **on ICE QUEEN**, on branch `frosty-linger-cleanup`, cut from
`origin/main` `a0e5ca2` after M2 (#27) and the small-room fix (#30) had merged.
It clears the decks so that M3's PR is about the tail and nothing else.
`docs/reverb/HANDOFF-linger-dsp.md` has the M3 plan this leads into.

## Baseline, before any change

Release, test targets only, never a plugin product. Targets were taken from the
generated `*_tests.vcxproj` files: in a fresh Visual Studio tree, `ctest -N`
cannot name an executable that has not been built yet.

- `build-dsp` (`BMO_DSP_ONLY=ON`): build exit 0, **18/18**, plus
  `tune_hardtune_target`, disabled by design.
- `build-full`: build exit 0, **36/36 runnable**. `tune_hostcheck` is
  *Not Run*, because it depends on the BMO Tune RT VST3 being built and the
  build rules forbid building a plugin product. `tune_hardtune_target` is
  disabled. *Caveat:* this tree started building before the panel edits below
  and finished after some of them. The DSP baseline is clean.

## What changed

**1. A control a mode makes inert is dimmed (house rule, Frosty).** Written
for every module in `modules/AGENTS.md`, with its precedents and DEQ's DYN as
the one recorded exception. In Linger, **ER SPREAD dims in Taps mode**, which
closes 11 §7's open question. One function, `erSpreadIsLive` in
`ErGenerator.h`, answers it for both the panel and the engine.

**2. Taps stopped rebuilding on a SPREAD move.** Moving ER SPREAD in Taps
rebuilt an identical table and ran a 30 ms crossfade between two copies of it,
holding off any SIZE move meanwhile. It was inaudible, since both sides were
the same, but it was wasted work. `DspCore` now hands the generator a fixed
SPREAD in Taps. Test: *"a SPREAD move in Taps starts no crossfade"*, with
Energy as the control case.

**3. IN HI-CUT is DARKEN.** The panel caption (Frosty, 2026-09-29) and the
host-facing name, "In Hi-Cut" → "Darken" (Frosty, 2026-10-02). No tester holds
a Linger session, so the rename costs nothing. The id stays `inhicut`, which is
what a session stores. Docs, the layout test's caption list and the schema
golden in `ReverbTests.cpp` follow.

**4. Automating DENSITY or ER HI-CUT no longer re-runs the diffuser's
normaliser on every block.** It now runs when DENSITY has moved 0.25 %, or a
filter coefficient 2 % (relative), since the last run, and once more, exactly,
the block the controls settle. Below DENSITY 60 % no diffuser stage is
engaged, so the normaliser is exactly 1 and its computation is skipped
entirely.

`measure_reverb bench <rate> <block> density|hicut` is new: it starts from the
worst case and moves one control as a 2 s triangle across its range. All
figures are % of one core, median of five, Release, 192 kHz / 32, on ICE
QUEEN:

| | before | after |
|---|---|---|
| worst case, held | 3.73 | 3.60 |
| DENSITY automated | 13.03 | 2.92 |
| ER HI-CUT automated | **36.58** | 4.31 |

The 2026-09-30 review flagged DENSITY at ~48 %. Measured today on this ramp,
it is 13 %. **ER HI-CUT, which nobody had flagged, was the worse of the two.**
At 48 kHz / 128 the held worst case is 0.88 % (budget 1.5 %). With a 1 %
coefficient step, ER HI-CUT measured 5.00 %, exactly on the budget, so the
step is 2 %. The hi-cut's whole effect on the normaliser is a few tenths of a
dB across its entire range.

Tests: across 12 000 blocks at 192 kHz / 32, the normaliser now runs 846
times under the DENSITY ramp and 388 under ER HI-CUT. The assertion is under
10 % of blocks. After either ramp stops, the output matches a fresh instance
that never moved, within 0.01 dB; measured 0 and −1.2e-6 dB.

**Every new test was proved non-vacuous:** with the steps at zero (the old
behaviour) both counts read 12 000/12 000 and fail, and with `DspCore` handing
SPREAD through, the Taps crossfade test fails.

**5. One bus golden regenerated:** Linger's *swept* row in `BusTests.cpp`.
At 0.63 normalised, DENSITY sits just past the first stage. The glide from an
instance's starting 50 % moved the stereo peak by 1.1e-5, one tick past the
1e-5 tolerance (0.00015 dB). Every other row printed within tolerance and was
left alone.

## Owed

- Frosty's look at the renders: ER SPREAD dimmed in Taps and live in Energy,
  and DARKEN on the EQ page. These are in gitignored `snapshots/`.
- *Answered 2026-10-02:* the host-facing name becomes "Darken" (above), and
  DEQ's DYN stays undimmed. Frosty: it is the switch that unlocks the
  dynamics section, and he overrode the dim on purpose.

## After QA's review, same day

QA reviewed `9817ef6` and found two defects in point 4, both in
`ErGenerator.h`. Both were fixed with a test first that failed on the
unfixed code. The figures below are from QA's review probe, rebuilt and run
on ICE QUEEN.

- **A slow DENSITY ramp ran the normaliser every block** (`8794ec5`). Under
  1e-5 a block, the smoother snaps to its target, so the control read as
  settled while it was moving. 0.6 → 1.0 over 20 s ran 119 999 of 120 000
  blocks at 192 kHz / 32. Now 159 in every rate and block size tried.
- **Each normaliser update was an unramped gain step** (`91bc9e0`). It
  measured 3.5–16× `main` on the second difference under a slow ramp. Each
  update now ramps over up to 2 ms. A stage that fades out keeps applying its
  ramping normaliser until it lands on one, because dropping it at the
  threshold was itself a step of up to 2.5 %. Result: 0.34–1.06× `main`
  across all sixteen cells, and 0.62× on the full-range triangle. The cost is
  level: during fast DENSITY moves it differs from `main` by up to 0.35 dB, for
  the duration of a jump. All figures are in the commit body.
- Point 5's bus row was regenerated a second time. Against `main`, its peaks
  are now up to 0.008 dB apart and its RMS up to 0.0014 dB. The figure above,
  "0.00015 dB", was wrong: 1.1e-5 on 0.065 is 0.0015 dB.
- The ER SPREAD dim is now asserted in `ui_layout`, standalone and in a rack
  slot (`0560b0b`).

CPU at 192 kHz / 32, final head: held 3.62 %, DENSITY ramp 2.99 %, ER
HI-CUT ramp 4.31 %. 48 kHz / 128 held: 0.87 %. `build-dsp` 18/18 and
`build-full` 36/36 runnable, both builds exit 0.
