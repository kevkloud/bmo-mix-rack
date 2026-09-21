# BMO FET: the four missing suites, and the two missing tool modes

**AURORA, 2026-09-21.** Closes item 3 of `docs/1176-comp/HANDOFF-dsp-fixes.md`.
Built in `build-dsp` with `BMO_DSP_ONLY=ON` (Release), build exit 0 with no
error lines. Branch `frosty-add-bmo-fetcomp`, PR #22.

`fetcomp_dsp` goes from **863 checks to 1302**, still 0 failures. Runtime goes
from about 4 s to **23.3 s**, which is the cost of the THD grid's drive
searches; the CI job's ceiling is 10 minutes.

**Nothing in BMO FET has been heard.** These suites bound and pin behaviour so
that a listening pass has something trustworthy underneath it; they do not
establish that any of it sounds right.

## What was added

| suite | what it pins |
|---|---|
| `testThdGrid` | 11 §3's level x depth x frequency x voicing grid: finite, bounded, monotone at 20 and 30 dB, H2 leading H3 at 1 kHz |
| `testThdAtTheManualCondition` | the one real bound — Black under 0.5 % at 10 dB GR, measured 0.4399 % |
| `testVoicingThdSplit` | Blue above Black at every depth with the gap widening |
| `testIntermodulation` | SMPTE 60 Hz + 7 kHz and CCIF 19 + 20 kHz, finite and monotone in depth |
| `testLfRippleAgainstRelease` | H3 rising as release shortens, both ends near 10 §12 within ±50 % |
| `testLevelRange` | `input` reaches 30 dB GR and `output` restores unity, neither at its rail |
| `testGoldenState` | per-voicing GR arrays and per-block RMS/peak, quantised to 1e-4 |

`measure_fetcomp` gains **`gen`** (`voice | transients | low | sine`) and
**`render`** (arbitrary WAV in, every parameter a flag). WAVs go to
`packages/fetcomp-listening/`, which `.gitignore:15` already covers — verified
with `git check-ignore`, so no audio can reach a commit.

## Three things the measurements settled that the plan assumed otherwise

Writing assertions first and measuring second would have produced three tests
that pass by being wrong. Each of these was a failing assertion before it was
a corrected one.

### 1. "No drive" is not "no reduction"

The grid's 0 dB GR row originally set `input = 0`. At a 0 dBFS source that
still sits about 16 dB over the 4:1 threshold, so the row was measuring a
heavily compressed signal and calling it the clean reference — Blue read
2.03 % THD at 1 kHz where it should read near zero. Every row now gets its own
bisected drive, including depth 0.

### 2. THD is not monotone in depth below 20 dB, at 50 Hz

Measured, Black at 50 Hz: **1.82 / 1.57 / 1.26 %** at 0 / 6 / 12 dB GR. It
falls. At that frequency the figure is dominated by the envelope's own ripple
rather than by the cell, and the ripple shrinks relative to the tone as drive
comes up.

11 §3 asks for monotonicity **"at 20 and 30 dB"**, and that holds. The
assertion is scoped to exactly that rather than to the whole column. An earlier
draft asserted it from 0 and failed at three levels — correctly.

### 3. H3 leads H2 at 50 Hz, and is supposed to

Measured, Black at 50 Hz and 30 dB GR: **H2 −30.7 dB, H3 −26.8 dB**. The
control ripples at 2f and a gain modulated at 2f puts its product on the third
harmonic, so H3 runs above H2 there. That is the mechanism 10 §12 derives and
`testLfRippleAgainstRelease` bounds — wanted character, not a defect. The
"H2 still leads" assertion is scoped to 1 kHz, which is where the manual's
condition lives.

## And one thing the test plan's own wording gets wrong

11 §3 says `output` must "restore unity". The obvious reading — apply makeup
equal to the reduction — is wrong for this module, and an earlier draft of
`testLevelRange` failed at **every ratio** because of it.

`input` drives the cell, so the tone leaves at `source + input − reduction`.
Unity needs `reduction − input`, which at 4:1 from −18 dBFS is a **cut of about
19 dB**, not a boost of 30. Measured: the drive to reach 30 dB GR at 4:1 is
+49.4 dB, and the output that restores −18 dBFS is −19.4 dB.

Both halves of the range still matter and both are now checked: the cut that
restores unity from a driven source, and the +30 dB boost `params.h` says ±24
could not supply. The parameter comment in `params.h` — "the level-range DSP
test is what fails if this is ever reverted" — is now true for the first time.

## LF ripple, measured

50 Hz at 20 dB GR and 20:1, H3 as a percentage, across release positions 1..7:

| voicing | 1 (1.1 s) | 2 | 3 | 4 | 5 | 6 | 7 (50 ms) |
|---|---|---|---|---|---|---|---|
| Black | 0.194 | 0.316 | 0.515 | 0.843 | 1.367 | 2.180 | 3.392 |
| Blue | 0.226 | 0.355 | 0.561 | 0.884 | 1.373 | 2.156 | 3.375 |

100 Hz runs at about half those figures in both, which is the mechanism. 10 §12
derives ≈ 0.23 % at 1.1 s and ≈ 4.5 % at 50 ms for 50 Hz; both ends land inside
±50 % of the derivation, which is a mechanism check and not a tolerance — the
figures come from an algebraic estimate, not from a measurement of hardware.

## THD, measured

4:1, release position 1, 1 kHz, oversampling Off, from −18 dBFS:

| GR dB | Black THD % | Blue THD % | gap |
|---|---|---|---|
| 0 | 0.0038 | 0.0112 | 0.007 |
| 6 | 0.2593 | 0.9451 | 0.686 |
| 10 | 0.4399 | 1.6651 | 1.225 |
| 20 | 1.5228 | 5.5849 | 4.062 |
| 30 | 5.1075 | 10.8683 | 5.761 |

Blue above Black at every depth, gap widening at every step, as 10 §8 wants.

**The grid asserts the shape of this, not the values.** A level is a
calibration and every CALIBRATE constant in `Calibration.h` is a first-pass
number awaiting an ear. The single exception is the manual's condition, which
is a published bound.

## Golden state

Recorded on AURORA from this build. GR delivered at 1 kHz from −18 dBFS, 20:1,
no makeup, at INPUT drives of 0 / 10 / 20 / 30 / 40 dB:

- **Black** 5.7698, 14.9065, 23.1152, 30.2677, 36.6219
- **Blue** 5.7843, 14.9363, 23.1570, 30.3392, 36.9033

Per-block RMS and peak of a fixed 220 Hz render at 20:1, +20 dB drive, fastest
release, four 4096-sample blocks after settle: RMS 0.0629 / 0.0628 / 0.0631 /
0.0627, peak 0.0884 throughout.

**These pin behaviour, not correctness.** Every one moves the day a CALIBRATE
constant is tuned by ear, and that is the point — they make a change to the
sound visible in a diff instead of silent. If a calibration pass changes them,
re-record them in the same commit and say so; do not widen the tolerance.

## Proved falsifiable

The handoff's worry about this item was specific: *"nothing fails today if the
voicing split silently collapses — which is exactly the failure that would be
hardest to notice."*

Mutating `constantsFor` in `Calibration.h` to return `kBlack` for both voicings
— the collapse itself — now produces **12 failures**: six from
`testVoicingThdSplit` (the two voicings read identically at every depth and
every gap is 0.000000) and five from the Blue golden array, plus the gap-widening
check. Before this change that mutation was silent.

## What is still open

- **§2**, the GR curve above 25 dB: two assertions on one row.
- The **Ableton pass** itself. `packages/fetcomp-listening/` now has a starting
  set — `voice`, `transients`, `low`, and Black/Blue renders at release
  positions 1 and 7 — and the voicings verified as producing different files.
- Nothing has been heard.
