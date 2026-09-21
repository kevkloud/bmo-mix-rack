# BMO FET — DSP measurement, and where it stands against the pack

**On AURORA, 2026-09-21**, `frosty-add-bmo-fetcomp`, from `build-dsp/`
(Release). Figures are `measure_fetcomp`; the protocol is
`docs/1176-comp/11-integration-and-test-plan.md` §3 and the targets are
`docs/1176-comp/10-dsp-spec.md`.

**Why this note exists.** The session that wrote the DSP on 2026-09-20 ended
without writing its own note. Four documents cite
`testing-notes/fetcomp-dsp-2026-09-20.md` — `11-integration-and-test-plan.md`,
`docs/1176-comp/README.md`, `modules/fetcomp/AGENTS.md` and
`modules/fetcomp/README.md` — and **that file was never in the tree**. Its
figures, including what AGENTS.md calls "the three places the plan turned out to
be unachievable as written", are lost. Everything below is a **fresh
measurement of the code as it stands**, not a recovery of those numbers.

**The DSP is uncommitted** at the time of writing. It builds clean and
`ctest` is 18/18 in `build/`, but see §4: passing is not the same as conforming.

## 1. What conforms

| what | the pack asks | measured |
|---|---|---|
| attack overshoot, 48 kHz | 4.70 / 3.18 / 1.92 / 0.98 / 0.39 / 0.09 / 0.008 dB, ±0.1 | **4.677 / 3.156 / 1.907 / 0.978 / 0.382 / 0.087 / 0.002** |
| position 7 overshoot | ≤ 0.05 dB | **0.002 dB** |
| the seven positions | strictly monotone | **monotone** |
| release, 63 % from 10 dB | 1100 / 234.5 / 50 ms, ±10 % | **1100.0 / 234.7 / 50.0** |
| programme dependence | ≥ 1.5x between burst and sustained | **6.2x / 7.7x / 7.7x** |
| THD, Black, 10 dB GR | < 0.5 % | **0.4399 %** |
| THD, Black, at rest | < 0.05 % | **0.0038 %** |
| H2 against H3 | H2 leads | **+23.0 dB at 10 dB GR** |
| THD in depth | monotone, finite | **monotone both voicings** |
| Blue against Black | above at every depth, gap widening | **above at all five; absolute gap 0.007 → 0.69 → 1.23 → 4.06 → 5.76** |
| LF ripple, 50 Hz, 1.1 s | ≈ 0.23 %, ±50 % | **0.194 %** |
| LF ripple, 50 Hz, 50 ms | ≈ 4.5 %, ±50 % | **3.392 %** |
| ripple against release | rises monotonically as release shortens | **0.194 → 3.392 across all seven** |
| latency | exactly 0 / 40 / 60 at Off / 2x / 4x | **0 / 40 / 60, reported = measured** |
| curve, slope floor | above 2:1 everywhere | **lowest 2.29** |
| curve, ordering | four settings ordered at every depth | **ordered 5–25 dB** (but see §2) |

`bench`, Release, 48 kHz / 512, stereo, Black: **149.3 ns/sample** at defaults,
150.2 at Off/20:1/fastest, 345.8 at 2x, **1000.8 at 4x all-buttons**.

## 2. What does not conform

> **Correction, AURORA, 2026-09-21 (same day).** The table below and the
> paragraph after it are **Black only**, though §3 asks for both voicings, and
> the summary drawn from them is wrong in a way that matters.
>
> Blue at **Off and 30 dB GR** runs about **−63 dB** at every rate — roughly
> 10 dB worse than anything in the "−72.4 to −84.0" range below, which
> reproduces exactly for Black and only for Black. So "Off beating −60 by
> 12 dB" is **−62.9 dB, beating it by 2.9**, at Blue / 44.1 kHz / 30 dB GR.
>
> The conclusion still stands — Off passes and the default does not move — but
> on a third of the stated margin, and 10 §9 hangs that decision on this
> number. Re-run `measure_fetcomp alias` for **both** voicings if the Blue
> constants in `Calibration.h` are ever recalibrated.
>
> The cause of the floor was also not what this section assumed. It is not
> "whatever sets this floor, it is not the aliasing the oversampler exists to
> fix" in the sense of something unexplained: it is the detector's rectifier
> aliasing *inside* the oversampled domain, measured and written up in
> `testing-notes/fetcomp-alias-origin-2026-09-21.md`. The −80/−90 targets were
> changed to −70 as a result, and `testAliasFloor` was rewritten to pin the
> mechanism. Full grids for both voicings are in that note.

**The alias floor misses its targets at both oversampled factors.** §3 wants
−60 dB at Off, **−80 at 2x** and **−90 at 4x**.

| factor | target | measured range | verdict |
|---|---|---|---|
| Off | −60 | −72.4 to −84.0 | **passes, comfortably** |
| 2x | −80 | −74.0 to −85.5 | **misses at 44.1, 48 and 88.2 kHz** |
| 4x | −90 | −73.5 to −84.7 | **misses at every rate** |

Oversampling barely moves the floor — at 48 kHz it runs −73.1 / −75.0 / −77.0
across Off / 2x / 4x, where the targets imply 17 dB and 27 dB of improvement.
Whatever sets this floor, it is not the aliasing the oversampler exists to fix.
Note the one thing this *does* settle: Off beating −60 by 12 dB is why the
default stays Off rather than moving to 2x, which is the decision 10 §9 hangs on
this measurement, and `params.h` already ships Off.

**The GR curve breaks two of its four shape assertions at 30 dB.** §3 wants the
local slope monotonically decreasing with depth and the four settings strictly
ordered at every depth. At the 30 dB row:

- 4:1 slope **rises**, 2.29 at 25 dB to **2.58** at 30 — not monotone.
- 4:1 at **2.58** sits above 8:1 at **2.42** — not ordered.

Everything from 5 to 25 dB is monotone and ordered. 30 dB GR on 4:1 is also past
what the source sweep reaches (+20 dBFS delivers 23.39 dB), so that row is the
`input` knob driven hard — the extreme corner, but a corner the pack names
explicitly.

## 3. The CPU budget, and it is missed by a wide margin

`measure_fetcomp` was **the only measure tool in the suite with a `bench`
mode**, so the budget could not be evaluated at all. A `bench` mode was added
to `measure_vcomp` on 2026-09-21 — the same 220 Hz tone, ten seconds, 512-sample
blocks, two channels, eight passes and the same `elapsed / (passes * samples)`,
line for line, because otherwise the ratio measures the difference between two
harnesses rather than between two compressors.

Both run in one session on AURORA, Release, 48 kHz / 512, stereo:

| | LTV Comp | BMO FET |
|---|---|---|
| defaults | **26.6** | **145.5** |
| working / Off 20:1 fastest | 61.9 | 146.9 |
| complex / 2x 20:1 fastest | 62.3 | 346.5 |
| heaviest | 70.4 | **993.0** |

| budget | allowed | measured |
|---|---|---|
| defaults | ≤ 2.0x | **5.47x** |
| heaviest | ≤ 3.0x | **14.11x** |

**The budget was reset rather than the code**, Frosty's call on these figures,
2026-09-21. `11-integration-and-test-plan.md` §3 now reads **6.5x** and **17x**,
about 20% above measured -- enough to absorb machine spread and the 2-3%
run-to-run noise, tight enough to fail on a real regression. The reasoning is
recorded there, and **it is flagged for the Ableton pass**
(`ableton-pass-handoff-2026-09-17.md` §3a): a ns/sample ratio cannot say whether
a track of these is usable, and that is the question that decides whether the
reset stands or the cost gets looked at properly.

**Both fail, and not marginally.** The most generous reading available — FET's
defaults against LTV Comp doing actual work at amount 50 rather than sitting at
its own default of zero — is still **2.35x** against a 2.0x allowance. The
heaviest figure is not close under any reading: 4x oversampling alone takes FET
from 145.5 to 993.0, which is 6.8x for the factor by itself.

Two things worth separating before anyone optimises. LTV Comp's default is
`amountPercent = 0`, a state in which it is not compressing, so the baseline the
budget names is that module at rest — 26.6 against 61.9 once it works. And BMO
FET is a per-sample implicit solve through a divider law with two saturating
amplifiers and an LF core around it; it is a heavier algorithm than LTV Comp by
construction, not by accident. **Whether 2.0x was ever the right number is a
question for the owner**, and it is a different question from whether this
implementation is slow.

One deviation from §3 while we are here: it specifies `100 x 10 s` and both
benches run **eight** passes. 80 seconds of audio per case is plenty for a
stable figure — run to run these move about 2-3% — but the pack and the code
disagree and one of them should move.

## 4. The test suite passes without asserting most of this

`fetcomp_dsp` is green, and eighteen test functions cover the curve,
all-buttons, the position law, overshoot, release detents, programme
dependence, the implicit solve, the voicing pair, stereo link, the pin, the
alias floor, pinned stability, invariance and latency. **Four of §3's required
suites are not in the file at all** — grepped, zero hits each:

- **THD / IMD.** The whole grid: level x depth x frequency x voicing, the
  0.5 % assertion, H2 > H3, and Blue exceeding Black with a widening gap. All
  of §1's THD figures above came from the manual tool; nothing fails if they
  move.
- **LF ripple against release.** Same: measured here, unasserted.
- **Level range.** `params.h` says of the ±60 dB `input` range that "the
  level-range DSP test is what fails if this is ever reverted". There is no
  such test.
- **IMD** specifically — SMPTE 60 Hz + 7 kHz, CCIF 19 + 20 kHz.

`measure_fetcomp` is also missing the **`render` and `gen`** modes §3 lists,
which are how the listening-pass WAVs would be produced.

## 5. Verdict

**Not ready to ship, and not pushed.** The character measurements are good —
timing, release, programme dependence, THD levels and the voicing split all land
where the pack wants them, several of them very close. Against the pack as it
now stands there are **two** measured failures left — the alias floor at 2x and
4x, and the curve's shape at 30 dB — plus four required test suites absent from
a file that passes.

The CPU budget was the third, and it is closed: it was the pack's mistake rather
than the code's, and resetting it was Frosty's call on the measurement. It is
not settled so much as moved to where it can actually be judged — a host, with
ears and a CPU meter, in the Ableton pass.

None of that is an argument against the DSP's voicing or its sound, which no
number here can settle and which still wants an ear.
