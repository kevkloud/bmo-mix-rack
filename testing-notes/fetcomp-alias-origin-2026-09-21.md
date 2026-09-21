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

---

# Addendum, same day: the full grid, and a correction to the record

The tables above answer *why* the floor is where it is. Running the pack's
whole grid afterwards — `measure_fetcomp alias` for both voicings, five rates,
three factors, three depths, on AURORA — turned up two things the record did
not have.

## 1. The earlier sweep was Black only

`testing-notes/fetcomp-dsp-2026-09-21.md` §2 gives the Off floor as a
"measured range" of **−72.4 to −84.0** and concludes *"Off beating −60 by 12 dB
is why the default stays Off"*. Black's grid reproduces those bounds exactly.
Blue's does not, and `11 §3` requires **both voicings**.

## 2. Blue at 30 dB GR with oversampling Off is 10 dB worse than anything in that range

**Blue**, dB relative to the tone:

| rate | factor | 10 dB GR | 20 dB GR | 30 dB GR |
|---|---|---|---|---|
| 44100 | Off | −72.5 | −71.9 | **−62.9** |
| 44100 | 2x | −74.2 | −73.7 | −74.1 |
| 44100 | 4x | −74.3 | −73.4 | −73.0 |
| 48000 | Off | −73.3 | −72.6 | **−63.0** |
| 48000 | 2x | −74.9 | −74.3 | −74.8 |
| 48000 | 4x | −73.5 | −72.4 | −72.4 |
| 88200 | Off | −80.0 | −78.0 | **−63.9** |
| 88200 | 2x | −79.3 | −78.2 | −78.3 |
| 88200 | 4x | −77.6 | −76.0 | −75.6 |
| 96000 | Off | −81.2 | −78.9 | **−64.1** |
| 96000 | 2x | −79.6 | −78.4 | −78.3 |
| 96000 | 4x | −78.3 | −76.5 | −76.2 |
| 192000 | Off | −84.4 | −78.9 | **−66.3** |
| 192000 | 2x | −85.6 | −82.8 | −82.8 |
| 192000 | 4x | −85.0 | −81.8 | −82.8 |

**Black**, for comparison — flat in depth, which is why the earlier summary
read as one clean range:

| rate | factor | 10 dB GR | 20 dB GR | 30 dB GR |
|---|---|---|---|---|
| 44100 | Off | −72.4 | −72.8 | −72.7 |
| 48000 | Off | −73.1 | −73.6 | −73.2 |
| 88200 | Off | −79.1 | −80.1 | −75.8 |
| 96000 | Off | −80.1 | −81.2 | −76.1 |
| 192000 | Off | −84.0 | −82.2 | −75.4 |
| 48000 | 2x | −75.0 | −74.6 | −75.4 |
| 48000 | 4x | −77.0 | −76.3 | −77.0 |
| 192000 | 2x | −85.5 | −83.2 | −84.2 |
| 192000 | 4x | −84.7 | −82.3 | −83.9 |

## What this changes

**The default-Off decision still holds, but on a third of the margin the record
claims.** Worst corner over both voicings at Off is **−62.9 dB** (Blue, 44.1 kHz,
30 dB GR), which clears `10 §9`'s −60 condition by **2.9 dB, not 12**. Nothing
about the decision changes — Off passes — but it is much closer than
"comfortably" and should be re-checked if the Blue constants are ever
recalibrated, since every CALIBRATE value in `Calibration.h` is still a
first-pass number awaiting an ear.

**At 30 dB GR on Blue, oversampling earns its keep.** −63.0 → −74.8 at 48 kHz
is nearly 12 dB, the opposite of the ~2 dB it buys at 20 dB GR. That is
consistent with the mechanism rather than a contradiction of it: driven that
hard the rectifier's *low-order* harmonics grow, and those are exactly what the
decimation filter removes. The flat skirt only dominates once the low harmonics
are gone. So the honest statement about 2x/4x is narrower than "they do
nothing" — they do little at moderate depth and a lot on Blue at extreme depth.

**Worst case over the whole grid, both voicings:** Off −62.9, 2x −73.7 (Blue,
44.1 kHz, 20 dB GR), 4x −72.4 (Blue, 48 kHz, 20 and 30 dB GR). The −70 dB
target adopted for 2x and 4x therefore carries **2.4 dB of margin at its worst
corner**, not the 5 dB a first pass over Black alone suggested.

Nothing here has been heard.
