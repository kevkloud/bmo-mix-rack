# BMO FET: where the alias floor actually comes from

**AURORA, 2026-09-21.** Measured with `measure_fetcomp aliasorigin`, built in
`build-dsp` with `BMO_DSP_ONLY=ON` (Release), build exit 0 with no error lines.
Branch `frosty-add-bmo-fetcomp`, PR #22.

This settles item 1 of `docs/1176-comp/HANDOFF-dsp-fixes.md`. The short version:
**the DSP is behaving correctly and the pack's oversampled alias targets are
not reachable by oversampling.** Nothing here was heard; it is all measurement.

## The question

`testing-notes/fetcomp-dsp-2026-09-21.md` recorded an alias floor of
**-73.1 / -75.0 / -77.0** across Off / 2x / 4x at 48 kHz, Black, against pack
targets of -60 / -80 / -90 (`10 §9`, `11 §3`). Oversampling was buying 1.9 dB.
The same shared `core/dsp/Oversampler.h` under the Saturator is held to *"2x
oversampling drops folded images by at least 15 dB"* in `SatDspTests.cpp`, so
the handoff read 1.9 dB as a defect and asked what the floor really was.

## What was ruled out first, by inspection

Two hypotheses from the handoff, both wrong, both cheap to eliminate:

- **"The gain is computed and applied at base rate."** It is not. `processFrame`
  runs once per oversampled sub-sample from the `j` loop in `DspCore::process`,
  and the detector, the implicit solve, attack, release, the clamp and the gain
  multiply all live inside it.
- **"The coefficients follow the base rate, so the envelope runs factor-times
  fast."** It does not. `applyFactor` prepares every stage at
  `rate * currentFactor` and calls `updateCoefficients`.

A third, mine, also wrong and worth recording so nobody re-runs it: the image
bin sits at 1.125x base Nyquist, inside the halfband's transition band, where
the filter gives only **-41.7 dB** (computed directly from `Halfband2x::design()`
coefficients; the stopband proper does not begin until 1.182x). That is a true
fact about the filter and it is *not* the cause — the measurement below shows
the filter removing the third harmonic by 46 dB, which is ample.

## The measurement

`alias` reads one bin and cannot say what put energy in it. `aliasorigin`
detunes the tone by **delta = fs/512** so that a harmonic which folds arrives
displaced by `k * delta`, separating the candidates onto their own whole
Goertzel bins over a 16384-sample window:

| harmonic | where it lands | why |
|---|---|---|
| k=3 | `0.4375*fs - 3d` | above base Nyquist, the decimation filter's job |
| k=13 | `0.4375*fs + 13d` | aliases inside the 2x domain, lands **below** base Nyquist |
| k=19 | `0.4375*fs - 19d` | the same trap one octave up; survives 4x |

20:1, fastest attack and release, 20 dB GR, 48 kHz. dB relative to the
fundamental.

**Black:**

| factor | nominal bin | H3 @ -3d | H13 @ +13d | H19 @ -19d |
|---|---|---|---|---|
| Off | -155.1 | **-73.8** | **-75.7** | **-76.6** |
| 2x | -118.0 | -120.4 | **-76.8** | **-75.7** |
| 4x | -132.5 | -121.5 | -132.9 | **-74.3** |

**Blue:**

| factor | nominal bin | H3 @ -3d | H13 @ +13d | H19 @ -19d |
|---|---|---|---|---|
| 2x | -117.8 | -120.0 | **-76.5** | **-75.2** |
| 4x | -131.4 | -127.3 | -130.6 | **-73.4** |

The `nominal bin` column is the undetuned `0.4375*fs`, which now reads empty.
That is the control: it says these are real readings and not window leakage.

## What it shows

Read the diagonal.

- **At Off** everything folds and all three harmonics are in the bin.
- **At 2x** the decimation filter annihilates the third harmonic, -73.8 to
  **-120.4**, 46 dB. It is doing its job. What is left standing is H13 and H19.
- **At 4x** it also kills H13 (-132.9), and H19 is still there at -74.3.

So the mechanism is **aliasing inside the oversampled domain**. The detector's
`std::abs`, `std::max` and clamp are not bandlimited; they throw harmonics past
the *oversampled* Nyquist; those fold within that domain; and the ones landing
below base Nyquist sit in the decimation filter's **passband**, where no filter
can reach them. The previous session's own comment on `testAliasFloor` guessed
this — "the cell's own gain modulation, which aliases inside whatever rate it
runs at" — and was right. It is now measured rather than assumed.

**Oversampling cannot fix it, and the numbers say why.** Each factor removes
one harmonic and hands the bin to a higher one. The skirt is essentially flat:
-73.8, -75.7, -76.6 across k = 3, 13, 19, which is **2.8 dB over 2.7 octaves**
of harmonic index. 8x would find another at about -74.

## Consequence for the pack — owner's call, not made

The pack's **-80 at 2x and -90 at 4x** assume a skirt that decays with harmonic
index. The measured skirt does not decay. No oversampling factor reaches those
numbers, so they are not achievable as written.

Two honest options:

1. **Change `10 §9` and `11 §3`** against this measurement, the way the CPU
   budget was changed on 2026-09-21. The Off target of -60 dB is met with 13 dB
   to spare and is the one that decides the default.
2. **Bandlimit the detector nonlinearity** — smooth the rectifier so it stops
   generating the flat skirt. This is a real DSP change with a real character
   risk: that rectifier is part of what produces the attack overshoot table that
   currently conforms to within 0.023 dB, and the THD figures that conform at
   0.4399 %. It should not be done to chase a number nobody has heard.

Recommendation is (1). **Not actioned** — the targets are untouched pending
Frosty's decision.

## What changed in the test

`testAliasFloor` previously asserted Off at or under -60, and then only that 2x
and 4x were **within 0.5 dB of Off** — a threshold fitted to what the code
already did, which could not fail. It has been replaced by assertions on the
**mechanism**, using the detuned tone:

- Off leaves the third harmonic in the bin (guards against the rest being vacuous).
- 2x removes the third harmonic, by at least 30 dB.
- 4x removes the third harmonic too.
- At 2x the thirteenth is the one in the bin.
- 4x removes the thirteenth, by at least 30 dB.

Each is a structural fact about the oversampler working, falsifiable and not
fitted to a level.

**Proved falsifiable by mutation on AURORA:** forcing `p.oversampling = 1` in
the test — an oversampler that does nothing — produces **6 failures**. The old
assertion passed that mutant exactly, because a dead oversampler is trivially
"within 0.5 dB of Off".

The pack's -80/-90 are **deliberately not asserted**, and the test says so in
its own comment with a pointer to this note. That is a stated gap, not a quiet
loosening; if the targets stand, the fix is option 2 above and the numbers go
back into the test.

## Suite state

`build-dsp`, `BMO_DSP_ONLY=ON`, Release, on AURORA: build exit 0 with no error
lines, `ctest` **16/16** (the seventeenth is the disabled hardtune target).
`fetcomp_dsp_tests` alone: **859 checks, 0 failures**.
