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
