# Handoff: BMO FET, the four things the DSP still owes

For a fresh session that owns `modules/fetcomp/dsp`. Written on AURORA,
2026-09-21, by the session that owned the panel, and updated later the same day
once the DSP was committed. Nothing here is about the look; that is
`HANDOFF-render-pass.md`.

## Where things stand

- Branch `frosty-add-bmo-fetcomp`, pushed to `origin` (`kevkloud/bmo-mix-rack`)
  at `80e6221`, fifteen commits, 0 behind `origin/main`. **PR #22 is open**
  into `main` and CI has run against it. (This section previously read
  `c0fc24a`, eight commits, no PR, "CI has never seen this branch" — all four
  of those are now out of date.)
- PR #19's head `c4bea4a` is an ancestor of this branch, so #22 contains the
  groundwork pack outright. #19 has deliberately been left open; closing it is
  Frosty's call.
- **The DSP is committed**, in `80e6221`: `modules/fetcomp/dsp/DspCore.h` with
  `Calibration.h`, `Detector.h`, `FetCell.h` and `Stages.h` beside it, plus
  `tests/dsp/FetcompDspTests.cpp` and `tools/measure/fetcomp/main.cpp`. Another
  session wrote them on 2026-09-20 and ended without committing or writing up.
  They are no longer at risk, and **committing them is no longer your job** —
  the four items below are.
- Verified on AURORA immediately before that commit, in `build-dsp` with
  `BMO_DSP_ONLY=ON` (Release): build exit 0 with no error lines, ctest
  **16/16**, the seventeenth being the disabled hardtune target. The full Debug
  tree was green at 18/18 before the push.
- The measurement is `testing-notes/fetcomp-dsp-2026-09-21.md` — a fresh one,
  because the note that session was to have written never existed and four
  documents cited it. Read it before you touch anything.

**What already conforms, and must not regress:** the attack overshoot table to
within 0.023 dB at 48 kHz, the release law at 1100.0 / 234.7 / 50.0 ms,
programme dependence at 6.2-7.7x, THD at 0.4399 % for Black at 10 dB GR against
a 0.5 % ceiling, Blue above Black at every depth with the absolute gap widening,
LF ripple at 0.194 % and 3.392 %, and latency exactly 0 / 40 / 60. That is a
well-built DSP. Four things are wrong with it and none of them is its character.

---

## 1. The alias floor, and the test that lets it pass

**What the pack asks** (11 section 3): -60 dB at Off, **-80 at 2x**, **-90 at
4x**, per rate, per factor, at 10/20/30 dB GR, both voicings.

**What it measures**, 48 kHz, Black: **-73.1 / -75.0 / -77.0** across Off / 2x /
4x. Off beats its target by 13 dB. 2x misses at 44.1, 48 and 88.2 kHz. **4x
misses at every rate**, best -84.7.

**The lead, and it is a weaker one than it looks.** Oversampling is buying
**1.9 dB** here. The same shared `core/dsp/Oversampler.h` under the Saturator
is held to a different standard by that module's own test, in
`tests/dsp/SatDspTests.cpp`: *"2x oversampling drops folded images by at least
15 dB"*.

15 dB there, 1.9 dB here, same oversampler. So the floor this module is hitting
is **almost certainly not the aliasing the oversampler exists to remove**.
Before optimising anything, find out what it *is*.

> **Two hypotheses are already spent. Do not re-read the code for them**
> (AURORA, 2026-09-21).
>
> - *"The gain is computed and applied at base rate."* **It is not.**
>   `processFrame` is called once per oversampled sub-sample from the `j` loop
>   in `DspCore::process`, and the detector, the implicit solve, attack,
>   release, the clamp and the gain multiply all live inside it. The whole
>   loop is closed at the oversampled rate.
> - *"The time constants are derived from the base rate, so the envelope runs
>   factor-times fast at 4x and its extra ripple offsets the folding you
>   removed."* **It is not that either.** `applyFactor` prepares every stage at
>   `rate * currentFactor` and calls `updateCoefficients`, which is documented
>   as deriving everything from the **effective** rate.
>
> Also worth holding lightly: the Saturator is a memoryless waveshaper and this
> is a closed loop with `std::abs` and `std::max` inside it, so 15 dB may never
> have been the right yardstick. Treat the comparison as a reason to look, not
> as a target.
>
> What this leaves is a measurement job rather than a reading job. The bin
> under test is `0.4375 * fs`, which is where the tone's third harmonic folds
> — so the useful first questions are what the control signal's own spectrum
> looks like against the audio path's, and what the floor does per factor with
> the loop frozen. Measure before theorising again.

**The test is not protecting this.** `testAliasFloor` asserts Off at or under
-60, and then only that 2x and 4x are **within 0.5 dB of Off** — not -80, not
-90. It passes while the module misses the pack by 13 dB. Whatever you conclude
about the floor, that assertion has to become the pack's numbers or the pack has
to change; it cannot stay a threshold fitted to what the code already does.

**If the targets turn out to be wrong rather than the code**, say so with the
measurement and change 10 section 9 and 11 section 3, the way the CPU budget was
changed on 2026-09-21. Do not quietly loosen the test.

---

## 2. The GR curve stops behaving at 30 dB

**What the pack asks** (11 section 3): local slope **monotonically decreasing**
with depth, every setting **above 2:1** everywhere, the four settings **strictly
ordered at every depth**, and the curve well formed at 30 dB GR.

**What it measures**, Black, local slope d(in)/d(out):

| GR dB | 4:1 | 8:1 | 12:1 | 20:1 |
|---|---|---|---|---|
| 20 | 2.34 | 3.03 | 3.77 | 5.27 |
| 25 | 2.29 | 2.60 | 3.00 | 3.84 |
| 30 | **2.58** | **2.42** | 2.59 | 3.05 |

Two assertions break on that last row and nowhere else. 4:1's slope **rises**,
2.29 to 2.58, so it is not monotonically decreasing. And 4:1 at 2.58 sits
**above** 8:1 at 2.42, so the four are not ordered. Everything from 5 to 25 dB
is clean, and "above 2:1" holds throughout — the lowest figure anywhere is 2.29.

**Worth knowing before you dig:** 30 dB of reduction on 4:1 is past what the
source sweep reaches — at +20 dBFS in, 4:1 delivers 23.39 dB — so that row is
the `input` knob driven hard into the top of its range. It is a real corner the
pack names explicitly, but it is the extreme corner, and the likeliest causes
are the solve or the bias behaviour at extreme drive rather than the divider law
itself. `measure_fetcomp curve black` prints both tables.

---

## 3. Four required suites are missing from a passing test file

`fetcomp_dsp` is green and has eighteen test functions. **Four of 11 section 3's
required suites are not in the file at all** — grepped, zero hits each:

- **THD / IMD.** The whole grid: level {-20, -10, 0 dBFS} x depth {0, 6, 12, 20,
  30 dB} x f {50 Hz, 1 kHz, 15 kHz} x voicing; THD under 0.5 % and H2 above H3
  at the manual's condition; THD monotone and finite at 20 and 30; **Blue above
  Black at every depth with the gap widening**. Also SMPTE 60 Hz + 7 kHz and
  CCIF 19 + 20 kHz. Every THD figure in the note came from the manual tool, so
  **nothing fails today if the voicing split silently collapses** — which is
  exactly the failure that would be hardest to notice.
- **LF ripple against release.** 50 and 100 Hz at 20 dB GR over all seven
  detents; H3 rising monotonically as release shortens, landing near 10 section
  12's table within 50 %.
- **Level range.** `modules/fetcomp/params.h` says of the input range that "the
  level-range DSP test is what fails if this is ever reverted". **There is no
  such test.** From a -18 dBFS RMS source, `input` must reach 30 dB GR or more
  at every ratio and `output` must restore unity, neither at its rail.
- **Golden state.** 11 section 3 wants compact numeric tables in the file —
  per-voicing GR-curve dB arrays, per-voicing THD tables, control-state samples,
  per-block RMS and peak quantised to 1e-4. Check what is actually pinned.

`measure_fetcomp` is also missing the **`render` and `gen`** modes section 3
lists, which are how the listening pass gets its WAVs. `measure_vcomp` has both
and is the model.

---

## 4. CPU: already settled, do not reopen

Measured 2026-09-21 on AURORA with both tools in one session: **145.5 ns/sample
at defaults against LTV Comp's 26.6, which is 5.47x**, and **993.0 against 70.4
at the heaviest, 14.11x** — against a budget of 2.0x and 3.0x.

**Frosty reset the budget to 6.5x and 17x** rather than the code, because the
old figures predated the implementation and nobody had costed a per-sample
implicit solve through 4x oversampling: 4x alone is 6.8x of the heaviest figure.
It is **flagged for the Ableton pass**
(`testing-notes/ableton-pass-handoff-2026-09-17.md` section 3a), where a host
can answer the question a bench cannot — how many instances before it is a
problem, and whether 4x is ever worth reaching for.

**So this is not a task.** Do not optimise for it, and do not treat 5.47x as a
defect. If your work on section 1 happens to make it faster, good. If a fix
makes it materially slower, say so with the number.

---

## Do not collide

- **Other sessions are live in this repository.** At the time of writing,
  `frosty-ring-takes-module-accent` had CI in progress, and worktrees existed
  for dwell, defang, linger, reverb, delay and the de-esser. `git worktree list`
  says which. Don't touch another one.
- **The schema is permanent.** No id, order, range, default or choice-order
  change in `modules/fetcomp/params.h`, ever.
- **`core/ui` is not yours**, and if you somehow end up there: BMO Opto
  `ab3ff3b77116b7a5` / `878cca7b1a80a551` / `88a7653a82c19ae0` and BMO Saturator
  `d42e23747e1ea2fc` must not move. Re-render and prove it.
- **Never build or install the rack plugin target.** The installed 0.2.5 rack is
  mid Ableton pass on AURORA.
- **`opto_measurements/` is not properly gitignored.** Running `measure_opto`
  drops about 19 MB of WAVs there and `git status --porcelain` lists them, so
  `git add -A` will sweep them into a commit. Read `git status --short` before
  every add. Fixing the ignore rule would be a kindness.
- `docs/deesser/` and `docs/delay/` are untracked on purpose. Leave them out.

## Building and measuring

    cmake --build build --config Debug --parallel
    ctest --test-dir build -C Debug
    cmake --build build-dsp --config Release --parallel
    build-dsp/tools/Release/measure_fetcomp.exe <mode> [blue|black]

Modes are `curve | timing | thd | alias | slam | allbuttons | bench`, plus
`latency` and `positions`.

**The voicing argument is lower case.** `blue`, not `Blue` — `voicingFrom`
compares against `"blue"` and anything else silently falls through to black,
which produces two identical tables and the strong impression that the voicings
do nothing. That cost this session twenty minutes.

**A build here can lie about succeeding, twice over.** `ctest` will run a stale
`.exe` and print all-green when a target failed to compile, and a build piped
into `tail` or `grep` reports the exit status of the *pipe* rather than of
`cmake`. Redirect to a log, test `$?` directly, then grep the log. A green
counts only after a build that exited 0 with no error lines.

## Reporting

Say what was measured, what was only reasoned about, and which tool could not
run. Name the machine in every recorded figure. Record the figures in
`testing-notes/` — and unlike the session before you, **write the note**: four
documents cited one that never existed, and its numbers are gone for good.
