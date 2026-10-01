# Session handoff — BMO Linger at the M2 checkpoint

Written on ICE QUEEN, 2026-09-25, for a fresh session. Two days of work
are behind this: M2 was built and made green on 2026-09-24, Frosty's two
calls (MIX law, flatness rule) were pinned the same evening, and on the 25th
the listening set was completed from his recordings and the stand-in
references were rendered. **Nothing has been heard yet.** That is the next
thing that happens, and it is Frosty's.

Read this, then `docs/reverb/HANDOFF-linger-dsp.md` (the DSP handoff, current
as of M2), then the two notes it points at:
`testing-notes/linger-m2-er-2026-09-24.md` (every figure) and
`testing-notes/linger-listening-set-2026-09-24.md` (every file in the set).

## Where the work is

- **Repo:** the `bmo-mix-rack` folder in the user folder on ICE QUEEN, a fresh clone of
  Kevin's `kevkloud/bmo-mix-rack` made on 2026-09-24. Its only remote is
  `origin` = Kevin's. The older `bmo-mix-rack-333` folder next to it is
  abandoned and must not be worked in. **Work goes to Kevin's main as PRs.**
- **Branch:** `frosty-linger-m2-er`, thirteen local commits on top of
  `aa4416d` (PR #26). **Nothing is pushed.** Frosty says when.
- **Build trees:** `build-dsp` (DSP only, Release) and `build-full` (Release,
  test targets, `snapshot`, `measure_reverb`, `bmo-tune-hostrender`). Never
  build a plugin product target; never run an untargeted build. Suites at the
  last run: `build-dsp` 18/18, `build-full` 16/16, both after builds that
  exited 0. The installed VST3 set is still 2026-09-17's; Linger is **not**
  installed here.
- **The listening set:** `packages/reverb-listening/set-2026-09-24/` on ICE
  QUEEN (gitignored): `sources/` (15 clips), `linger/` (67 renders, ER only),
  `refs/` (54 renders through the UAD units). Also
  `packages/reverb-listening/refs/` with the impulse stimulus and the
  reference IRs. **No audio is in the tree**; check `git status --short`
  before any `git add`.
- **The bounce board:** https://claude.ai/artifact/YJjvAsyiDR2cyNzkPAsexy,
  pinned. It says who bounces what and carries the headless figures. Open
  cells are Frosty's Renaissance bounces in Live.
- **Memory** for this project lists the same state under "Linger M2 state"
  and "Linger reference plugins on ICE QUEEN".

## What is real

M2, the early-reflection generator, in `modules/reverb/dsp/`:

- `ImageSource.h` — the offline generator and audit. Six tables printed
  from it into `TapTables.h`; the tests re-derive them, so a hand edit fails.
- `ErGenerator.h` — the runtime: one mono line, 21 core plus 27 signed
  velvet infill pulses, the DENSITY bridge, four order-banded poles, a
  three-stage FIR diffuser per band with an exact energy normaliser, seven
  VARIATION positions, the ER hi-cut. No allpass anywhere.
- `DspCore.h` — feeds the generator the mid, applies ER and REVERB faders,
  the MIX law and OUTPUT. The tail is silent; the Reverb EQ is not in the
  path yet.
- `tests/dsp/ReverbDspTests.cpp` — the whole of 11 §6's ER block, green.
- `tools/measure/reverb` — `ir tables bench stimulus irwav render analyse
  stats samples` on top of the older modes.
- `tools/tune/hostrender` gained `--stereo`.

CPU on ICE QUEEN, worst case, median of five: 0.87 % at 48 kHz/128,
3.5 % at 192 kHz.

## Decided since the DSP handoff was written

- **MIX law** (Frosty, 24th): dry = min(1, 2(1 − mix)), wet = min(1, 2 mix),
  default 50 %, 100 % is verb only for a send. Pinned at five points.
- **ER flatness rule** (Frosty, 24th): octave-smoothed, 250 Hz–8 kHz, within
  6 dB about the tilt at DENSITY 100 %, asserted; 4.6 dB today.
- **References** (Frosty, 25th): Valhalla is dropped. The set is Renaissance
  Reverb (Live only: the Waves shell hangs the headless host) plus the four
  UAD units licensed here: Lexicon 224, Pure Plate, RealVerb-Pro, Precision
  Reflection Engine. The PRE is early reflections only and the nearest
  direct comparison for M2. Five other UAD verbs are unlicensed
  pass-throughs. Slate VerbSuite renders two clips of four headless and is
  not relied on.
- **808s stay out** of reverb tests (Frosty, 24th).
- **44.1 kHz sources are fine** (25th): every tool runs at the file's rate.

## Still open, and whose

Frosty's, in the order they come due:

1. **The listening checkpoint.** The set note maps nine items to files.
   The verdict with a deadline is **Blend**: ER MODE's count freezes at ship,
   and this is the last cheap moment to drop it. Then whether the cluster
   reads as a room at all.
2. **The 30 s tail ceiling against a 40 s effective decay**, and
   **IN HI-CUT as a parameter or a constant.** Both gate M3.
3. **Push and PR.** Say the word; then M3 starts on a fresh branch from
   Kevin's main after the merge.

Recorded in the M2 note and not blocking:

- VARIATION 6 is built mono-flat (05 §9.3), not mono-empty as 10 §3 says;
  the panel label should follow.
- The window clamp (10 §3) is not applied: the Size law is linear, as the
  panel's sketch is.
- The panel draws Room's table for every type; the engine plays each type's
  own.
- The 150 ms wet fade in `reset()` is not done.
- ISO's early lateral fraction cannot reach 0.10 with the −15.3 dB tap
  ceiling; the test asserts the ER bus's own fraction (0.20).

## How to pick up

1. `cd` into that `bmo-mix-rack` clone, `git status --short` (clean),
   `git log --oneline -14` (the thirteen commits and `aa4416d`).
2. Build the two trees with named targets and run both suites before
   changing anything; record the counts and the machine.
3. If Frosty has listened: write his verdicts into
   `testing-notes/linger-listening-set-2026-09-24.md` under a new heading,
   naming ICE QUEEN, and update `HANDOFF-linger-dsp.md`'s open list.
4. If Renaissance bounces have landed in Live: `measure_reverb analyse` on
   them for the table in the M2 note, and turn the board's cells green.
5. Do not start M3 on this branch.

## Playback cautions for the listener

`linger/lead-vocal-03-room12-mix50.wav` peaks at +1.1 dBFS and
`refs/lead-vocal-03-realverbpro-wet.wav` at +0.9 (float files, no clipping in
the file, clipping on playback). The references have tails and Linger does
not, so the comparison is early reflections against full reverbs until M3.
