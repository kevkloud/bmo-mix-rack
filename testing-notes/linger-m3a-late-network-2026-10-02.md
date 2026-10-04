# BMO Linger M3a: the tail plays, 2026-10-02

All of this was done **on ICE QUEEN**, on branch `frosty-linger-m3a`, cut from
`origin/main` `3afbca0` after the cleanup (#37) merged. It is M3's first half,
in the plan in `docs/reverb/HANDOFF-linger-dsp.md`. **It stops at a listening
checkpoint:** M3b does not start until Frosty has heard the tail.

## Baseline, before any change

`build-dsp` 18/18 (plus `tune_hardtune_target`, disabled by design).
`build-full` 36/36 runnable: `tune_hostcheck` needs the BMO Tune RT plugin
built, which the build rules forbid. Both builds exited 0. Release, test
targets only.

## The CPU worst case, measured first

`measure_reverb bench … worst` with the late network running: DENSITY 100 %,
VARIATION 5, Release, median of five, in % of one core.

| | 48 kHz / 128 | 192 kHz / 128 | 192 kHz / 32 |
|---|---|---|---|
| 8 lines, first build | 1.05 | 4.29 | 4.36 |
| 16 lines, first build | 1.13 | 4.73 | 4.73 |
| 8 lines, as committed | 1.10 | 4.54 | — |

The budget is 1.5 / 5. **The line count is cheap** (16 costs +0.4 % at
192 kHz), because the ER generator dominates. The constraint is M3b's
headroom at 192 kHz: the EQ, DARKEN and the modulated, interpolated reads have
to fit in about 0.5 % at 8 lines. Built at 8, as planned.

## What was built

- **`afc6fd7`, the tail ceiling is 40 s** (core, rack and the TAIL page), as
  its own commit, green on its own. Only BMO Linger reports a tail.
- **`LateNetwork.h`:** pre-delay (tail only, 30 ms crossfade); four input
  diffusers, taking the output after two, or after four for Plate; 8 prime
  lines; Hadamard mixing; per-line absorbent filters derived from DECAY, the
  two multipliers and the type's knees; two orthogonal ±1 output vectors.
  SIZE and TYPE move the line lengths by crossfading the reads and the filter
  coefficients inside the loop. TYPE also dips the output by 30 ms down and
  30 ms up. No modulation yet: whole-sample reads, which is why the output is
  bit-identical across block sizes.
- **`DspCore`:** SOURCE feeds the tail (1 − d)·dry mid + d·ER mid, with the
  ER taken ahead of its fader. WIDTH (M/S on the tail only) and the REVERB
  fader are applied, all smoothed over 20 ms.

**The six departures from `10` §4** are listed, with their measurements, in
`10` §4's new "As built in M3a". In one line each: Hadamard, not
Householder; second-order shelves half an octave outside the knees; diffusers
capped by DECAY; denormals flushed, not injected; SIZE scales τ̄ and the
tail's level; filters blended through a length change.

## The late block of `11` §6

All in `reverb_dsp_tests`, at 48 kHz unless stated.

| rule | measured |
|---|---|
| T30 against DECAY 0.3 / 1 / 2 / 6 s | 0.312 / 0.999 / 1.999 / 6.000 s; T20 within 3 % of T30 |
| damping, bands two octaves outside the knees | ratio 0.27–0.28 at ×0.25, 0.52–0.53 at ×0.5, 1.96–1.97 at ×2 |
| mid band against DECAY, any multiplier | within 3.5 % |
| lines prime at 44.1 / 48 / 96 / 192 kHz | all six types, all rates |
| Σ*m*ᵢ ≥ 0.15 fs | five types pass; **Plate 0.146 s, known red** |
| pre-delay 40 / 250 ms at four rates | to one sample; ER bit-identical with and without |
| loop gain ≤ 1 − 1e−4 | every line, at three corners |
| DECAY 20 s × 2.0, 1 s of noise then 60 s | peak 0.36; no 10 s window louder than the one before |
| reported tail ≥ what rings, six types × four rates | always; Room told 2.27 s, rings 1.15 s |
| the 40 s corner | told 40 s, rings 32.4 s |
| bit-identical at blocks 1 / 16 / 32 / 64 / 127 / 2048 | yes |
| 1 kHz T60 across 44.1–192 kHz | within 0.9 % |
| ringing: flatness ≥ 0.3, 1/3-octave bands ≤ 6 dB | 0.52–0.92; ≤ 2.9 dB |
| ringing: envelope autocorrelation peak ≤ 0.2 | 0.08–0.16; **Plate 0.202, known red** |
| moves, worst 1 ms energy jump (≤ 3 dB) | SIZE 0.34, PRE-DELAY 0.23, DECAY 0.19, HIGH × 0.15 dB |
| TYPE Room → Hall, step ratio | 0.70 |

**Every new check was shown to fail** with the thing it guards removed:
- decay 20 % short fails T30, damping and the mid band
- a flat low shelf fails damping
- pre-delay off by two samples fails pre-delay
- non-prime lines fail the prime check
- loop gain ×1.05 fails the gain, peak and growth checks
- the tail report ×0.3 fails the report checks
- instant crossfades fail SIZE (3.3 dB) and TYPE (step 4.05)

One limit is recorded too: an *instant* pre-delay switch reads only 1.3 dB
on the energy metric, because the diffusers and the network smooth it first.

**Two test metrics were wrong before they were right**, and the record says
so:
- **The move test's edit never ran.** It fired on `at == 48000` with
  256-sample blocks, which is the same mistake the 2026-09-30 review found in
  `stepRatioAcross`. It now counts its edits and fails loudly if one does not
  run.
- **PRE-DELAY 0 → 120 ms tested nothing on a 1 kHz sine.** 120 ms is a whole
  number of cycles. It is 37.25 ms now.

**Known red, printed and not asserted, both Plate:** modal density (`10` §4
predicted it) and late-envelope autocorrelation (0.202 at a 2 ms lag, a true
local maximum). M3b's modulation, or M4's line count, takes them back.

## Goldens moved

`BusTests` regenerated Linger's **defaults** row only. At REVERB −6 dB the
tail now plays under the ER: RMS moved by up to 0.07 dB and peaks by up to
0.0016 against the M2 row. The **swept** row is bit-identical, because its
PRE-DELAY is 157 ms, past the end of the suite's 85 ms signal.

## Final state

`build-dsp` 18/18 and `build-full` 36/36 runnable, both builds exit 0, on
ICE QUEEN.

## What is owed

**The listening checkpoint: done the same day.** The set was rendered
outside the repository, from Frosty's clips on ICE QUEEN, and heard with the
amp in stereo; `testing-notes/linger-listening-set-2026-10-02-m3a.md` has the
set and the answers. It covered decay and damping across the six types, the
ER-to-tail handover, PRE-DELAY and Plate on a vocal. A SIZE move under a
sustained source was **not** in it, because the render tool has no
automation, and Frosty judged it not a concern. M3b is next.

## After QA's review of `de00c43`, 2026-10-03

QA blocked PR #38 on two findings, both reproduced on ICE QUEEN and fixed
with a test written first and shown failing:

- **`6b31ad0`: the loop grew at 96 and 192 kHz.** The absorbent filters'
  float coefficients realised a DC loop gain of up to 1.0071. They now run
  in double; the worst of 1,080 settings is 0.99935.
- **`0911af7`: `prepare()` or `reset()` mid-move.** Stale crossfade lengths
  were read outside the buffer. Both now rebuild from the current settings,
  exactly as a fresh instance does.
- **`e25b9a5`: exact silence.** The ER generator's one-pole filters stuck at
  denormals; they now flush below 1e-15. `reverb_dsp_tests --long` reaches
  exact zero in all 48 cases.

**Two decisions, Frosty's, 2026-10-03:**

- **Level at the extreme is intended.** Plate, DECAY 20 s, both multipliers
  at 2.0, REVERB 0 dB, noise at −18 dBFS RMS peaks at +3.4 to +4.0 dBFS. A
  40 s tail holds that energy, and REVERB is the control for it. No change.
- **CPU at 192 kHz is accepted.** On an idle machine the worst case is
  4.77–5.18 % of a core at 192 kHz / 32, SIZE 80, DENSITY 100, against a
  5 % budget. Frosty: 5.18 % is fine at 192 kHz, a high-fidelity rate where
  added cost should be expected. The budget at 48 kHz is unchanged.

## QA's second pass, on `6067809`, 2026-10-03

Both blockers held. The round had introduced one new one, and the fresh
review found gaps in the tests. All on ICE QUEEN.

- **`aa78b88`: `reset()` before `prepare()` never returned.** Since
  `0911af7` `reset()` rebuilds the line lengths, and on a never-prepared
  network the search had no candidate and no exit. Both processors call
  `reset()` from `releaseResources()`, so a host releasing a plugin it never
  prepared would hang. The search is now bounded, an unprepared network holds
  no lengths, and processing one writes zeros. The test hung on `6067809`
  (still running at 120 s) and passes now; the probe's `resetfirst` prints
  `returned`.
- **A full-range SIZE move burst 27 dB over either end.** QA measured it as
  −22.3 → −8.9 dBFS and judged it not a blocker. Writing the test showed it
  was a real overshoot: an instance held at SIZE 80, DECAY 0.1 sits at
  −91 dBFS, yet the move peaked at −5.4 from −32.3. Blending the filter
  coefficients does not keep a filter's gain-times-shelf product. A length
  move now crossfades two whole paths, each with its own filters and level.
  After: −32.1 dBFS in the test; the probe's `sizefade` reads −22.3 before
  and −24.9 after. `jump` is unchanged to rounding on every row.
- **The exact-silence check now runs in the suite CI runs**, not only under
  `--long`: DECAY 0.3 s, every type, at 48, 96 and 192 kHz, must be exactly
  0.0f within four seconds. It fails at all three rates with the ER flush
  removed.
- **The mid-move test compares the network, not just its lengths.** Each
  line's realised filter gain, and 0.25 s of tail sample for sample, against
  a fresh instance. Two flaws in it were found by trying to break it:
  - the instance had been run on silence before the move, so there was
    nothing for a faulty `reset()` to leave behind; it is run on noise now;
  - a break that changes `reset()` for both instances proves nothing, so the
    break used leaves filter state behind, which only the mid-move instance
    has. With it, all three cases differ by about 0.006 and fail.
- **Docs:** the last two "nothing has been heard" lines, the 192 kHz CPU
  figure in `10` §6 and `11` §6, and Frosty's two rulings written into `10`
  §4's as-built list, not only here.

`build-dsp` and `build-full` figures are in the commit that carries this
note.

## QA's third pass, on `6a37ffe`, 2026-10-03

The last round held. QA found a blocker that had been in the late network
since the first head they saw: **SIZE kept moving makes the tail grow without
limit.** Every earlier test, QA's and mine, moved SIZE once. All on ICE
QUEEN.

**Reproduced.** Probe `sizegrow`: 9 of 24 rows grow, by QA's figures to the
decimal. The worst is +573 dBFS at 60 s: 48 kHz Room, SIZE 12 ↔ 30 m every 64
blocks, DECAY 20 s, both multipliers 2.0.

**Cause.** A length move crossfaded the old and new reads in *read* time.
Reading at a longer delay replays samples that have already been round the
loop, so every move put energy back. Toggle faster than the loop loses it,
and the tail grows.

**Fix.** The two paths are weighted by when a sample was *written*:
- a sample written before the move is read at the old delay, in full, and
  never again
- a sample written after it is read at the new delay
- the weights cross over across the 30 ms after the move starts
- at no instant do the two paths together weigh more than one

That makes the reads of a line carry no more energy than was written to it.
The proof is exact for the reads. The step after them, two different
filters each under one, is held by measurement, and the code says so.

**Tests, written first:**
- SIZE, TYPE, and both together, alternating every 1, 64 and 2048 blocks of
  32 samples over a 30 s tail at 48 and 192 kHz, 43 rows: the 10 s window
  peaks may never rise. On `6a37ffe` 8 rows grew; now none.
- Both filter banks, live and incoming, realise under 1 while a move is in
  flight: 144 moves, worst 0.99935. Shown failing with a 1 % error in the
  incoming bank.
- `reset()` *after* the bottom of a TYPE dip, with the incoming bank
  running, lands on a fresh instance sample for sample.
- `reverb_dsp` and `reverb` have a 300 s timeout in ctest, so a hang fails
  CI instead of stalling it.

**Probe:** `sizegrow`, `sizegrow 5 1.0` and `sizegrow 10 1.0` all report 0 of
24 rows growing. `gain` 0.9993490, `resetfirst` returns, `sizefade` −22.3 /
−24.9 dBFS, and the 192 kHz `grow` case reaches −600 dBFS, as before.

**What one SIZE move now does**, on noise held through it (Room, DECAY
1.8 s, 48 kHz, 10 ms windows):

| move | deepest window | within 1 dB of settled | the move lasts |
|---|---|---|---|
| 12 → 30 m | 18 dB down | 70 ms | 111 ms |
| 30 → 12 m | 1.8 dB down | 160 ms | 111 ms |
| 12 → 80 m | silent | 340 ms | 246 ms |
| 80 → 12 m | 0.9 dB down | 300 ms | 246 ms |

Growing a room opens a gap in the tail, because nothing is replayed to fill
it. On held noise, shrinking one barely dips; on a decaying tail it costs
more than growing, for good (see the fourth pass below). A move lasts the
longest line plus 30 ms, and the next move waits for it. **Frosty accepted
this on 2026-10-03**, and named the fallback if the gap ever matters in
use: glide the line lengths, which has no gap and no replay but
pitch-bends the tail during the move. It is not built. `10` §4's as-built
list has both.

**One test criterion changed with it.** The single SIZE 12 → 30 m move is
held to the step ratio again (1.00), not the 1 ms energy jump (3.8 dB). The
gap passes through near-cancellation on a steady sine, where a smooth change
is a large one in dB. 11 §6 asks "no click" of SIZE and keeps the 3 dB rule
for coefficient changes; both figures are printed.

**CPU with a move in flight** (probe `sizecpu`, 192 kHz / 32, Room, on a
quiet machine): mean 10.4 % of the block, 99th percentile 39.9 %, against
10.5 % and 40.0 % on `6a37ffe`. Held: 4.99 % against 4.95 %. The spec now
says its figures are for held settings.

## QA's fourth pass, on `f3e91db`, 2026-10-03

Nothing grows any more: about 3,000 kept-moving rows in QA's probe, the
worst realised loop gain in flight 0.99935, no out-of-bounds read, no
allocation. Five smaller things were left, all closed on ICE QUEEN.

**What a move costs, which the third pass undersold.** "Shrinking is nearly
seamless" is true of held noise only. With signal still arriving the
network refills and settles at the new SIZE's level, so a move costs only
its dip. On a decaying tail nothing refills it, and every move leaves the
tail quieter than SIZE held at either end, for good. Probe `gap`, Room
unless named, DECAY 5 s, a 10 ms burst at −18 dBFS RMS at 0, one move at
0.3 s, 48 kHz / 32, the late output 1–2 s after the move against SIZE held
at the old size:

| move | 1–2 s after |
|---|---|
| 12 → 13 m | −1.2 dB |
| 12 → 30 m | −1.2 dB |
| 12 → 80 m | −1.2 dB (silent for 80 ms of the move) |
| 0.5 → 80 m | −5.0 dB |
| 30 → 12 m | −4.5 dB |
| 80 → 12 m | −10.8 dB |
| 80 → 0.5 m | −19.5 dB |
| Ambience 80 → 0.5 m | −18.3 dB |

Shrinking costs more than growing: a shrinking line reads its pre-move
samples at the old delay to the end, and the new path stays silent until
they are done. Under automation the losses add up (probe `autolevel`, Room,
DECAY 20 s, both multipliers 2.0): T60 39.45 s held, 21.58 s on a 12..13 m
LFO with a 10 s period, 18.44 s on 12..15 m, 6.65 s on 12..30 m every 4 s,
1.91 s toggled 12 ↔ 30 m every 64 blocks; held noise 3.22, 6.05, 8.79 and
12.68 dB under the held level. **Frosty, 2026-10-03: "a held SIZE is
untouched; automating SIZE thins the tail" is the behaviour for 0.2.6**,
with gliding the line lengths as the fallback if the listening pass
disagrees. SIZE is a set-and-leave control. `10` §4 has the tables.

**Fixed, one commit each, tests first:**
- **The suite fit its limit in Release only.** `scripts/build.sh` tests
  Debug, where `reverb_dsp_tests` took 374–404 s against a 300 s fence and
  ctest failed it as a timeout. The kept-moving test runs seven of its 43
  rows by default (the five 48 kHz rows that grew on `6a37ffe`, which still
  fail there, and two live ones) and all 43 under `--long`. Default run:
  40–67 s Release, 271–447 s Debug with other builds sharing the machine;
  fence 1350 s. Of a 387 s Debug run, 250 s is the 96 and 192 kHz "tail never
  grows" block, which was left alone.
- **reset() with a request queued behind a move** built from where the move
  was going, not from what was asked for: 0.232, 0.000704 and 0.0318 off a
  fresh instance over 2 s of noise (SIZE queued, DECAY queued, SIZE queued
  mid-dip). Now 0 in all three, and asserted.
- **DECAY and the multipliers wait for a move to end**, every 111–289 ms
  under SIZE automation against 0.7 ms with SIZE held. Left as it is: the
  two-path sum is held by measurement, and that measurement ran with the
  coefficients still. Written into `10` §4 and `modules/reverb/AGENTS.md`,
  and pinned: a request 10 ms into Room 12 → 80 m arrives 236.7 ms later.
- **The loss is pinned in both directions**: T60 39.45 s held, 21.6 s on the
  12..13 m LFO, 1.91 s toggled, and −3.22 dB on noise, ±2 % and ±0.25 dB.
  And the kept-moving test now says which rows it judged on a tail the moves
  had already taken under −120 dBFS: 5 of the default 7 (every row that grew
  on `6a37ffe`), 2 live.
- **Docs**: this section, `10` §4, `modules/reverb/AGENTS.md` and `README.md`;
  and the third pass's dates, which read 2026-10-04, are 2026-10-03.
