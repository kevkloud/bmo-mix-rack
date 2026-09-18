# BMO Vcomp — handoff

Built and heard on **AURORA**, 2026-09-13 to 2026-09-14, on branch
`add-bmo-vcomp`. Two commits are ahead of `integration`:

    77e5f57  vcomp: the voicing the ear asked for
    d72075c  vcomp: a limiter, instantaneous so it costs no latency

plus three already merged into `integration` at `8567765` (the module itself,
the shared gain-computer lift, and the layout-test fix).

**Green at handoff:** 16/16 plugin tests, 15/15 DSP tests, full rack build with
no errors. The Vcomp DSP suite is 55 checks.

---

## What it is

A modern feedforward vocal compressor, alongside BMO Opto rather than instead
of it. Frosty's brief: the sound of Waves RVox and RComp, the simplicity of
RVox and Klanghelm DC1A.

    in -> gate -> [band split] -> compressor on the mid band
       -> + the thru bands -> auto makeup -> OUTPUT -> limiter

Two knobs and a gate handle on the face; five more controls behind COMPLEX.
Ten parameters, zero latency at every setting.

---

## The ear pass

Done in Ableton on AURORA against real material, 2026-09-14. Frosty's verdict:
**"flying colors"**, with four changes asked for and made, then a fifth after
a second listen. All five are in `77e5f57` and `d72075c`.

| Asked for | Change | Measured result |
|---|---|---|
| LOW/HIGH THRU did the opposite of the intent | Makeup now applies to the whole sum, not the compressed band alone | tilt went from **-4.3 dB** (thinning) to **+20.5 dB** (boosting) at AMOUNT 90 — see the open question below |
| "Not sure what ARC is doing" | Slow branch 5x -> 10x RELEASE, charge 2x -> 1.2x | ARC on vs off at the default: **-26.4 dB -> -16.7 dB** (AMOUNT 40), **-20.2 -> -11.9** (AMOUNT 70) |
| Gate needs to expand deeper | 3:1/50 dB -> **6:1/60 dB** | shut depth at GATE -40: **-16.4 dB -> -40.6 dB** |
| Gate pops on the way in | Open 0.5 ms -> **3 ms** | slew **113.3 -> 19.2 dB/ms**; onset cost 0.00 dB at every usable threshold |
| Limiter missing (RVox's third stage) | Instantaneous, zero latency, last in chain | ceiling holds exactly at -0.10 dBFS at every AMOUNT/OUTPUT combination |

---

## Measurements

All reproducible with `measure_vcomp <report>`. Every report writes WAVs of the
same render it measures, so a number and a listen are never of different things.

### `curve` — AMOUNT's sweep

Output stays flat while reduction climbs to 28.8 dB. This is the "density, not
level" claim, and it is the first table to check after any revoicing.

| AMOUNT | thresh | knee | ratio | makeup | GR | out |
|---|---|---|---|---|---|---|
| 0% | -6.0 | 12.0 | 1.00 | 0.00 | 0.00 | **-7.00** |
| 30% | -16.2 | 10.2 | 3.10 | 6.23 | 6.21 | **-6.97** |
| 50% | -23.0 | 9.0 | 4.50 | 12.44 | 12.41 | **-6.96** |
| 70% | -29.8 | 7.8 | 5.90 | 18.94 | 18.89 | **-6.96** |
| 100% | -40.0 | 6.0 | 8.00 | 28.88 | 28.83 | **-6.95** |

### `presets` — level matching

Tolerance is ±3 dB. No preset sets OUTPUT or GATE; these are level-matched by
construction rather than by hand-solved figures, which is unique in the suite.

| Preset | delta |
|---|---|
| Lift | -1.90 |
| Forward | -0.52 |
| In Front | +0.01 |
| Fast Vocal | -0.54 |
| Smooth Lead | -0.47 |
| Keep The Chest | **+2.12** |
| Keep The Air | -0.23 |
| Manual | -0.79 |

The limiter *improved* several of these — In Front was +2.46 before it.

### `gate` — depth and onset cost

At 6:1 into a 60 dB floor, opening over 3 ms. The onset column is the one that
matters: a gate that cleans the silence and bites the first word is a bad trade.

| GATE | shut | onset |
|---|---|---|
| -45 | -15.8 | 0.00 |
| -40 | -40.6 | 0.00 |
| -35 | -59.9 | 0.00 |
| -25 | -59.8 | -0.15 |

### `gateopen` — the slew that predicts a click

| open ms | peak slew | fully open |
|---|---|---|
| 0.5 | **113.3 dB/ms** | 2.0 ms |
| 2.0 | 28.8 | 8.1 ms |
| **3.0 (shipped)** | **19.2** | 12.2 ms |
| 5.0 | 11.5 | 20.3 ms |

### `bands` — reconstruction and pumping

Reconstruction error is **0.00 dB at every frequency tested** (40 Hz to 14 kHz)
with the split in circuit at AMOUNT 0. The bands are not an EQ.

Pumping of a steady tone under a 2 kHz burst at AMOUNT 80:

| setting | 80 Hz | 12 kHz |
|---|---|---|
| both at rails | -11.56 | -11.57 |
| LOW THRU 300 | **-0.08** | -11.57 |
| HIGH THRU 4000 | -11.56 | **-0.08** |
| both | -0.08 | -0.08 |

### `balance` — what a THRU band does to the spectral balance

**This is the open question.** Tilt is low minus mid, relative to THRU off.

| AMOUNT | tilt |
|---|---|
| 30% | +4.63 |
| 50% | +9.24 |
| 70% | +14.64 |
| 90% | +20.52 |

### `colour` — the limiter's harmonic signature

Second harmonic is **absent** (-180 dB): the gain stage is symmetric, so it can
only make odd harmonics. What colour there is reads as edge, not warmth.

| driven in | THD | 2nd | 3rd | out |
|---|---|---|---|---|
| 0 dB | 0.015% | -180 | -77.9 | -0.23 |
| 6 dB | 0.489% | -180 | -47.1 | -0.10 |
| 18 dB | 0.801% | -180 | -42.8 | -0.10 |

---

## Faults found during the build

Five of these were found by measurement or by a test, not by reading the code.
Recorded because each has a general lesson.

1. **AMOUNT's bottom third was dead.** The first curve made AMOUNT 0 inert via
   the threshold, so at AMOUNT 20 the knee had only reached -8.6 dBFS and a
   vocal at -10 got nothing. Fixed by spending the *ratio* on being inert
   (1:1 at zero) instead. `testAmountGrabsHarder`.
2. **The makeup reference was read off an RMS meter.** Set to -10 dBFS when the
   detector is a *peak* detector and the test source peaks at -3.6. Every
   preset came out quiet, worst at the bottom of the knob. Now -7 dBFS.
3. **The band split silently stopped compressing above 10.8 kHz.** Engaging
   LOW THRU alone left the upper crossover in circuit at its clamped cutoff
   while the panel read 20 kHz. Fixed two ways: each side engages
   independently, and the clamp went from 0.45 to 0.98 of Nyquist. Found by
   `measure_vcomp bands`, not by any test — `testOneSideEngagedLeavesTheOtherAlone`
   is the test that was missing.
4. **`ui_layout` never looked at this panel.** It shipped with "LOW THRU" and
   "HIGH THRU" rendering clipped and the suite green, because the module was
   never added to `tests/ui/LayoutTests.cpp`'s product list. **`captionOverflow`
   was never at fault** — it reports the overflow correctly, and an earlier
   note in this repo claiming otherwise was wrong and has been corrected.
   That list also addressed products by hard-coded index, so adding a row in
   the middle re-pointed DEQ's assertions at the new module; it looks up by
   name now.
5. **The gate popped after being made deeper.** The open time never changed —
   the depth it opens *from* did. Same ramp over 58 dB instead of 16.

---

## Open, and deliberately not done

- **The THRU bands run away.** ~~First thing to pick up in Vcomp's own pass~~
  -- **done on 2026-09-14**, on branch `vcomp-thru-cap` off `review-0.2.4`, by
  the fourth candidate rather than any of the three listed here: the thru path
  takes the gain the curve would have given that content had it been
  compressed. Tilt 4.6/8.8/12.9/17.7 dB across the knob becomes
  2.4/1.7/1.1/0.6, pumping -1.5 becomes -0.03, and "Keep The Chest" at AMOUNT
  70 goes +8.14 dB to +1.36 so the preset can come back up to strength.
  Measured, not heard. `testing-notes/vcomp-thru-cap-2026-09-14.md`.
- **Limiter warmth.** Adding second harmonic needs deliberate asymmetry. Worth
  weighing against BMO Saturator already existing for colour.
- **No lookahead, no oversampling, no parallel MIX, no stereo LINK switch.**
  Each is a decision with a reason, all listed in `modules/vcomp/AGENTS.md`.
- **The accent `#a2a8ff` has not been signed off.** Allocated in
  `products/AGENTS.md` with its measurements; permanent once shipped.
- **Names are first drafts** — AMOUNT, LOW THRU, HIGH THRU, SC HPF, COMPLEX,
  and "BMO Vcomp" itself.

---

## For whoever pushes this

- `integration` was at `8567765` and matched `origin` exactly at handoff —
  nothing local to push there, and the merge of `add-bmo-vcomp` should be clean.
- **CI's macOS job failed on run 34818965850 for infrastructure reasons**
  (*"job was not acquired by Runner of type hosted"*), not a build fault. DSP,
  Windows and "each side alone" all passed and `BMO-Windows` uploaded fine.
- **Tune was deliberately not touched.** `integration` carries a Tune behind
  `bmo-tune-work`, and the `BMO Tune RT.vst3` installed on AURORA is still the
  2026-09-10 copy — left alone so this work could not disturb Tune's own pass.
- The eight other Release VST3s installed on AURORA came from that CI artifact;
  only Vcomp was rebuilt locally afterwards, to carry the ear-pass changes.
