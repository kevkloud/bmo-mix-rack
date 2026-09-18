# BMO-DEQ — Zero-Latency Dynamic EQ

**Engineering Spec: Test Environment, Architecture Contract & Acceptance Targets**

Derived from *Zero-Latency Dynamic EQ — Competitive DSP Teardown* (Sept 10, 2026).
This document defines **what to build, what "correct" means numerically, and how to prove it**. It contains no implementation code by design — topology, targets, math and test method only. Implementation choices below the contract line are the dev team's.

- **Module codename:** `BMO-DEQ` *(placeholder — rename before repo merge)*
- **Target:** feature/latency parity with Slate Infinity EQ; filter accuracy parity with TDR Nova **without** Nova's 187-sample PDC
- **Status:** spec for review, not yet accepted

---

## 0. Assumptions to confirm before work starts

These are placeholders inferred from the existing codebase conventions. Correct them in the first review pass; several test targets depend on them.

| # | Assumption | If wrong, affects |
|---|---|---|
| A1 | Framework is JUCE; module ships as a `juce::dsp`-style processor with `prepare / process / reset` | §6 harness wiring, §10 CI |
| A2 | Module lands as a self-contained subdirectory in the existing working repo, sibling to the saturator module, with no dependency on saturator internals | §10 layout |
| A3 | Supported rates: 44.1, 48, 88.2, 96, 176.4, 192 kHz. Supported block sizes: 1–4096, **including non-power-of-two and 1** | §7 T1, T8 |
| A4 | Channel configs: stereo in / stereo out first; mono and >2ch deferred | §4 |
| A5 | Internal processing precision is `double` for filter state and coefficients, `float` accepted only at the host boundary | §7 T7 |
| A6 | Band budget target: 24 simultaneous bands minimum, architecture imposes no hard ceiling | §7 T9 |

---

## 1. Scope

**In scope:** the EQ module itself — per-band filters, per-band dynamics, per-band L/R↔M/S placement, band summing, parameter smoothing, and the offline test harness that validates all of it.

**Out of scope (explicitly):** GUI, analyzer/FFT display, preset system, saturation stages, any linear-phase mode (deferred to Phase 4, §11).

**The one non-negotiable:** **reported plugin delay compensation must be exactly 0 samples at every supported sample rate, in every mode shipped by default, with dynamics actively processing.** Any design decision that adds a delay line, a lookahead window, a block-buffered stage, or an FIR of any length is out of contract unless it lives behind an explicitly-latent opt-in mode.

---

## 2. Architecture contract

These decisions come from the brief and are **locked** — changing one requires re-opening this spec, not a PR comment.

**C1 — Every band is a recursive minimum-phase filter.** Direct-Form-I biquad or TPT/SVF. Output sample *n* depends only on inputs and outputs up to *n*. No delay lines anywhere in the audio path. This is the entire reason the module can be zero-latency.

**C2 — Coefficients are derived by matched Z-transform (Vicanek), not by the bilinear transform.** This is the decision that separates us from Nova. Bilinear + oversampling-decramping buys Nyquist accuracy at 187 samples of latency; matched-Z buys the same accuracy at zero. The cookbook bilinear formulas are retained **only as a reference implementation inside the test harness**, never as the shipping path.

**C3 — The dynamics detector is a causal one-pole envelope follower with no lookahead.** The known cost (Giannoulis/Massberg/Reiss) is transient overshoot before the envelope catches up. That overshoot is accepted, and it is measured, budgeted and regression-tested (§7 T5) rather than hidden. Do not "fix" it with lookahead.

**C4 — ~~Bands sum in parallel~~; there is no crossover network.** Each band computes its contribution and adds into a running sum. Band count is a throughput budget, never a latency budget.

> **REVISED 2026-09-12 (Frosty): bands run in series.** The second half of C4
> stands — there is no crossover network, and band count is still a throughput
> budget and never a latency one. The first half does not: serial is the only
> topology whose response is its band curves added in dB, and the only one in
> which a low cut still cuts under an overlapping boost
> (`topology-options.md`). It costs the same latency and the same CPU.
>
> Settled by ear as well as by measurement, over 57 blind pairs on seven
> sources: `testing-notes/deq-blind-2026-09-11.md`. Every audible difference
> ran the way the measurements predicted, nothing was reported as a fault on
> either side, and where the two were matched for *amount* so that only the
> shape of the dynamic catch differed, four of six pairs were
> indistinguishable and the other two were preferred as serial.
>
> **No user-facing topology switch** (Frosty, 2026-09-12). Everything parallel
> was preferred for in round one was a case of it doing less, and that is
> reachable in serial by asking for less: within 0.76 dB for stacked cuts,
> 0.63 dB for stacked boosts, and exactly for the amount of dynamic
> reduction. Nothing serial does is reachable in parallel at all. A switch
> would double what every preset and test has to mean in exchange for a knob
> position.

**C5 — Mid-side is a per-band arithmetic matrix, not a filter.** Zero state, zero latency, applied inside the band, not globally.

**C6 — No oversampling in the EQ path.** If a later stage in the wider plugin needs oversampling, it uses minimum-phase polyphase-IIR halfband filters, never linear-phase FIR (JUCE's own module documents this tradeoff; default to IIR).

**C7 — Parameter changes are smoothed per-sample by causal means only** — coefficient interpolation or a TPT one-pole on the control signal. Never a crossfade between two filter states across a lookahead window.

---

## 3. Signal flow

Per band *k*, per sample *n*:

1. **Domain placement.** Take L/R input. Compute the band's contribution in the L/R domain and in the M/S domain; blend the two *contributions* by the band's L/R↔M/S parameter β (see §5.4 for why the blend must happen on contributions, not on the matrix).
2. **Sidechain tap.** Detector input = the band's own bandpass response of the signal (internal sidechain) or an external sidechain bus. With an SVF core, the bandpass tap is free — it falls out of the same state as the bell output.
3. **Detector.** Rectify → one-pole ballistics → envelope in dB.
4. **Gain computer.** Static curve (threshold, ratio, knee, range) maps envelope dB → dynamic gain offset in dB.
5. **Effective band gain** = static band gain (dB) + dynamic offset (dB), clamped to the band's range.
6. **Coefficient refit** for the new gain, smoothed per C7.
7. **Filter.** Apply, take the wet contribution `w = y − x`.
8. **Sum** `w` into the shared output bus.

Global output = dry input + Σ band contributions. Bypassing a band means its contribution is zero — bypass must be sample-accurate and click-free, and must **not** change reported latency.

---

## 4. Ground truth — the reference model

**This is the most important part of the test environment.** Nothing can be tested against "sounds right." Every magnitude test compares the shipping filter against a numerically-evaluated analog prototype, computed offline in double precision.

The analog prototypes (RBJ forms, `s = jΩ`, `Ω = 2πf`, `ω₀ = 2πf₀`):

**Peaking:**
```
H(s) = (s² + s·(ω₀·A/Q) + ω₀²) / (s² + s·(ω₀/(A·Q)) + ω₀²)
```

**Low shelf:**
```
H(s) = A·(s² + (√A·ω₀/Q)·s + A·ω₀²) / (A·s² + (√A·ω₀/Q)·s + ω₀²)
```

**High shelf** is the mirror form. `A = 10^(dBgain/40)` throughout.

The harness evaluates `|H(jΩ)|` on a log-spaced grid (recommend 4096 points, 10 Hz → 0.499·Fs) and compares against the discrete filter's `|H(e^{jω})|` measured **two independent ways**:

- **Analytically**, by evaluating the transfer function from the shipping coefficients.
- **Empirically**, by impulse response → FFT (64k-point, zero-padded, double).

The two must agree to within 0.001 dB. If they disagree, the coefficient math and the running filter have diverged — that discrepancy alone is a build-blocking bug, independent of accuracy targets.

**Cramping is defined as the max absolute dB deviation between the discrete filter and the analog prototype over 20 Hz → 0.45·Fs.** That single number is the accuracy figure of merit for this project.

---

## 5. Math reference

### 5.1 Direct-Form-I biquad

```
y[n] = (b0/a0)·x[n] + (b1/a0)·x[n-1] + (b2/a0)·x[n-2]
       - (a1/a0)·y[n-1] - (a2/a0)·y[n-2]
```

Normalize by `a0` at coefficient-computation time, never per sample.

### 5.2 Matched Z-transform design (Vicanek) — method

**Poles are mapped exactly.** For an analog prototype with poles at `s = -ω₀/(2Q) ± jω₀√(1 - 1/(4Q²))`, with `ω₀ = 2πf₀/Fs` normalized to the sample rate:

For **Q > 0.5** (complex pole pair):
```
a1 = -2·exp(-ω₀/(2Q))·cos(ω₀·√(1 - 1/(4Q²)))
a2 =  exp(-ω₀/Q)
```

For **Q ≤ 0.5** (real poles), with `r = √(1/(4Q²) - 1)`:
```
a1 = -( exp(-ω₀·(1/(2Q) - r)) + exp(-ω₀·(1/(2Q) + r)) )
a2 =  exp(-ω₀/Q)
```

Note `a2 = exp(-ω₀/Q)` in both cases — a cheap invariant to assert in a unit test.

**Zeros are fitted to the magnitude response.** Write the squared magnitude in terms of `φ(ω) = sin²(ω/2)`. For a denominator `1 + a1·z⁻¹ + a2·z⁻²`:

```
|D|² = A0 + A1·φ + A2·φ²
A0 = (1 + a1 + a2)²
A1 = -4·[ a1·(1 + a2) + 4·a2 ]
A2 =  16·a2
```

and identically for the numerator `b0 + b1·z⁻¹ + b2·z⁻²`:

```
|N|² = B0 + B1·φ + B2·φ²
B0 = (b0 + b1 + b2)²
B1 = -4·[ b1·(b0 + b2) + 4·b0·b2 ]
B2 =  16·b0·b2
```

so `|H(e^{jω})|² = |N|²/|D|²`. The design step is: pick the target `|H|²` at DC (`φ = 0`), at Nyquist (`φ = 1`) and at the center/corner frequency (`φ₀ = sin²(ω₀/2)`), solve the resulting small system for `B0, B1, B2`, then back out `b0, b1, b2`.

**Per-filter-type target sets, and the sign/branch choices when inverting `B → b`, are given in Vicanek's *Matched Second Order Digital Filters* (§ Sources). Read the paper; do not reconstruct them from this summary.** The formulas above are the framework and are provided so the harness can verify any candidate implementation independently. Every derived coefficient set must pass the §4 analytic-vs-empirical agreement test before it is trusted.

### 5.3 TPT / SVF core (Cytomic) — alternative to DF-I

Preferred if fast per-sample coefficient modulation causes numerical trouble in DF-I. With `g = tan(π·f₀/Fs)`:

```
a1 = 1 / (1 + g·(g + k))
a2 = g·a1
a3 = g·a2

v3 = v0 - ic2eq
v1 = a1·ic1eq + a2·v3
v2 = ic2eq + a2·ic1eq + a3·v3
ic1eq = 2·v1 - ic1eq
ic2eq = 2·v2 - ic2eq

output = m0·v0 + m1·v1 + m2·v2
```

Mix and damping constants per type (`A = 10^(dBgain/40)`):

| Type | `g` | `k` | `m0` | `m1` | `m2` |
|---|---|---|---|---|---|
| Bell | `tan(πf₀/Fs)` | `1/(Q·A)` | `1` | `k·(A²-1)` | `0` |
| Low shelf | `tan(πf₀/Fs)/√A` | `1/Q` | `1` | `k·(A-1)` | `A²-1` |
| High shelf | `tan(πf₀/Fs)·√A` | `1/Q` | `A²` | `k·(1-A)·A` | `1-A²` |
| Lowpass | `tan(πf₀/Fs)` | `1/Q` | `0` | `0` | `1` |
| Highpass | `tan(πf₀/Fs)` | `1/Q` | `1` | `-k` | `-1` |
| Bandpass (SC tap) | `tan(πf₀/Fs)` | `1/Q` | `0` | `1` | `0` |

Treat this table as **to be verified against the Cytomic paper**, then locked by a unit test asserting the SVF magnitude response matches the matched-Z biquad response within 0.01 dB below 0.2·Fs.

Note the bandpass row: the detector sidechain tap costs nothing extra when the core is an SVF. That is a strong argument for SVF over DF-I in this module.

### 5.4 Mid-side

```
M = (L + R)/2        L = M + S
S = (L - R)/2        R = M - S
```

**Two things the dev team must know before implementing the continuously-variable blend:**

**(a) A linear blend between the identity matrix and the M/S encode matrix is singular and must not be used.** For `W(β) = (1-β)·I + β·E`, `det W(β) = 1 - 2β + 0.5β²`, which is zero at `β ≈ 0.586`. Interpolating the *matrix* destroys the signal mid-sweep. Instead, compute the band's **wet contribution** in each domain and crossfade the contributions:

```
w = (1-β)·w_LR + β·w_MS      where w_MS = E⁻¹·(H(E·x)) - x
```

This is always well-defined, continuous in β, and reduces to each pure mode at the endpoints.

**(b) For a purely static band with identical processing on both domain channels, M/S and L/R modes are mathematically identical** — the matrix is linear and commutes with the filter. M/S only produces a different result when the two domain channels are treated differently: an S-only or M-only band, per-channel gain, or an unlinked detector. Build the linked/unlinked detector control alongside the blend, or the feature does nothing. This is also a free test (§7 T6).

### 5.5 Detector

Envelope in linear domain, one-pole ballistics:

```
α_attack  = exp(-1 / (attack_ms  · 0.001 · Fs))
α_release = exp(-1 / (release_ms · 0.001 · Fs))
```

**Time-constant convention must be specified and fixed now:** the form above is the τ convention — `attack_ms` is the time to reach **63.2%** of a step. The alternative industry convention uses `exp(-2.2/(τ·Fs))` for a 10–90% rise. Pick one, document it in the parameter tooltips, and make the §7 T5 timing test assert against the chosen one. Getting this wrong makes every timing measurement off by ~2.2×.

**Use the smooth decoupled peak detector** rather than a naive branching one — it avoids the release-stage discontinuity of the simple `if (x > y) attack else release` form:

```
y1[n] = max( x[n], α_r·y1[n-1] + (1-α_r)·x[n] )
y[n]  = α_a·y[n-1] + (1-α_a)·y1[n]
```

RMS variant: run the one-pole on `x²`, take the square root after. Map the "Tight" / "Smooth" timing modes to peak vs RMS detection plus a release-curve scalar rather than to two separate code paths.

### 5.6 Gain computer

Work in dB. With threshold `T`, ratio `R`, knee width `W` (dB), and `x_dB = 20·log₁₀(|x| + ε)`:

```
2(x_dB - T) < -W        →  y_dB = x_dB
|2(x_dB - T)| ≤ W       →  y_dB = x_dB + (1/R - 1)·(x_dB - T + W/2)² / (2W)
2(x_dB - T) > W         →  y_dB = T + (x_dB - T)/R
```

Dynamic offset `g_dB = y_dB - x_dB`, clamped to the band's range parameter. Effective band gain = static gain + `g_dB`. `ε` must be small enough not to bias the knee (`1e-12` or smaller) and denormal-safe.

For **upward** dynamic EQ (below-threshold expansion), mirror the comparison. Both directions must be supported — Infinity EQ has both.

---

## 6. Test environment

Three tiers. Tiers 1 and 2 run headless in CI and are the gate; tier 3 is manual and pre-release.

**Tier 1 — Offline numeric harness (no framework, no audio device).** A standalone target that links only the DSP module and a small analysis library. It computes reference responses, runs the filters over generated signals, does FFT analysis and writes machine-readable results (JSON) plus optional plots. This is where §7 T1–T7 live. Must be deterministic: fixed seeds, no wall-clock dependency, bit-identical output across runs on the same platform.

**Tier 2 — Plugin-level validation.** Standard host-abstraction validation (pluginval at strictness 10, `auval` on macOS), plus an automated PDC-report check that instantiates the plugin at each supported rate and asserts the reported latency is 0.

**Tier 3 — DAW matrix.** Manual, per release candidate. Per the brief's step 9, and its own closing caveat: the existing zero-latency measurement is one host, one build. **Cross-check in at least three hosts** — Ableton Live, Reaper (whose PDC readout is per-track and easy to read), and one of Pro Tools / Logic / Cubase — at all six sample rates. Log results in a checked-in table; a release does not ship on a single-host measurement.

**Golden reference files.** Impulse responses and swept-sine analyses for a fixed set of ~40 parameter configurations, checked into the repo as double-precision binaries with a text manifest. Any change in output beyond tolerance fails CI and must be explicitly re-blessed in a PR that says why. This is what catches "harmless refactor" regressions.

**Test signal catalog** (all generated in-harness, double precision):

| Signal | Used for |
|---|---|
| Unit impulse | Latency, IR, magnitude/phase via FFT |
| Log sine sweep (Farina, 10 Hz → Nyquist, 10 s) | Magnitude/phase with harmonic distortion separable from the linear response |
| Sine at 100 Hz / 1 kHz / 10 kHz, −1 dBFS | THD+N |
| Dual tone 19 + 20 kHz | IMD |
| Pink & white noise | Long-run stability, spectral tilt |
| Step and tone-burst (−40 → −6 dBFS) | Detector timing, overshoot |
| Alternating ±1 full-scale (Nyquist square) | Stability at Nyquist, coefficient extremes |
| Digital silence following an impulse, 10 s | Denormal / CPU-cliff test |
| DC | Offset and stability |

---

## 7. Test suites and acceptance targets

### T1 — Latency (blocking, highest priority)

- **Method:** impulse in, find index of first sample where `|y[n]| > 1e-12`. Repeat: all six sample rates × block sizes {1, 32, 64, 111, 512, 4096} × {all bands bypassed, 1 static band, 24 static bands, 24 bands with dynamics actively gain-reducing}.
- **Target:** first non-zero sample index **= 0** in every combination. Reported PDC **= 0**.
- **Also assert:** processing the same input as one 4096-sample block vs 4096 single-sample blocks gives bit-identical output. Any difference implies hidden block-level state.
- **Note for testers:** a minimum-phase filter has non-zero *group delay* at some frequencies. That is not latency and is not a failure. The metric is first-non-zero-sample and host-reported PDC, nothing else.

### T2 — Filter accuracy vs analog prototype (the Nova-parity test)

- **Method:** per §4. Max `|ΔdB|` over 20 Hz → 0.45·Fs, at Fs = 44.1 kHz (worst case).
- **Test grid:** f₀ ∈ {50, 200, 1k, 5k, 10k, 14k, 16k, 18k} Hz × Q ∈ {0.5, 0.707, 2, 4, 8, 16} × gain ∈ {±3, ±6, ±12, ±18, ±24} dB, all filter types.
- **Targets:**

| Region | Max deviation from analog prototype |
|---|---|
| 20 Hz – 0.25·Fs | **≤ 0.05 dB** |
| 0.25·Fs – 0.40·Fs | **≤ 0.15 dB** |
| 0.40·Fs – 0.45·Fs | **≤ 0.50 dB** |

- **Comparative gate:** the harness runs the same grid through the cookbook-bilinear reference and reports both. The matched-Z path must beat bilinear by **≥ 6 dB of peak error** in the 0.40–0.45·Fs region at f₀ ≥ 10 kHz, Q ≥ 4, |gain| ≥ 12 dB. If it doesn't, the matched-Z implementation is wrong — that region is the entire reason for the design choice.
- **Symmetry:** cascading a boost and its exact inverse cut must yield `|H| ≤ 0.01 dB` deviation from flat across the whole band.

### T3 — Coefficient integrity

- `a2 = exp(-ω₀/Q)` holds to 1e-12 for all matched-Z designs.
- All poles strictly inside the unit circle: `|a2| < 1` and `|a1| < 1 + a2`, for the full parameter grid **including the boundaries** (f₀ clamped to `0.499·Fs`, Q at min and max, gain at rails).
- Analytic transfer function vs measured IR FFT agree to **0.001 dB** (§4).
- No NaN or Inf produced by any parameter combination in the grid, including degenerate ones (Q → 0, gain = 0 dB, f₀ = 0, f₀ = Fs/2).

### T4 — Parameter modulation / zipper noise

- **Method:** sweep f₀ from 20 Hz to 20 kHz logarithmically over 0.5 s while a −6 dBFS pink noise signal plays; also step gain 0 → +12 dB instantaneously; also automate Q across its full range in one block.
- **Targets:** no output sample exceeding the steady-state envelope by more than **0.5 dB**; no broadband click (measure by comparing the short-time spectrum during the transition against the interpolated steady-state spectra — excess energy **≤ −80 dBFS** relative to signal); filter must remain stable (T3 pole condition holds at every intermediate coefficient set).
- Smoothing must be **causal** (C7). Assert T1 still passes with smoothing active.

### T5 — Detector behavior and the overshoot budget

- **Method:** tone burst stepping −40 → −6 dBFS with a band set to threshold −20 dB, ratio 4:1, attack {0.1, 1, 10, 100} ms, release {10, 100, 1000} ms. Measure the gain-reduction trajectory.
- **Targets:**
  - Measured attack/release times match the theoretical one-pole time constants (per the §5.5 convention chosen) within **±5%**.
  - **Overshoot budget:** peak gain-reduction error during the first 3 ms after a step, at the fastest attack setting, is **≤ 3 dB** and monotonically decaying. This is the accepted cost of C3. It is not a bug; it is a number that must not regress.
  - Steady-state gain reduction matches the §5.6 static curve within **0.05 dB**.
  - No overshoot at all for attack ≥ 10 ms.
- **Regression rule:** the overshoot figure is checked into the golden manifest. Any increase fails CI.

### T6 — Mid-side correctness

- Encode → decode round trip nulls to **≤ −140 dBFS** (double path) / **≤ −120 dBFS** (float boundary).
- With identical processing on both domain channels, M/S mode and L/R mode outputs null against each other to **≤ −140 dBFS** (this is the §5.4(b) identity — if it fails, the matrix is wrong).
- Blend parameter β swept 0 → 1 over 1 s produces continuous output with no discontinuity and **no zero-crossing artifact anywhere in the sweep** (this is the singularity test — it catches the naive matrix-interpolation bug directly).
- S-only band with L = R (perfectly correlated mono input) produces **zero** change to the output, to ≤ −140 dBFS.

### T7 — Numerical robustness

- **Denormals:** impulse followed by 10 s of digital silence. Measure per-block processing time. **No block may take more than 1.5× the median block time.** Flush-to-zero / denormal handling must be explicit, not left to host settings.
- **Long-run stability:** 24 bands, 60 minutes of pink noise at 192 kHz. No drift in output RMS beyond 0.001 dB; no NaN; state variables remain bounded.
- **Low-frequency precision:** a 20 Hz band at Fs = 192 kHz (the worst-case ratio, ~1e-4 normalized frequency) must meet the T2 accuracy target. This is where DF-I in single precision fails and is the primary argument for A5 / SVF.
- **Nyquist-signal stability:** alternating ±1 full-scale input, all bands active at extreme settings, no instability or overflow.
- `reset()` returns the module to a bit-identical initial state: process → reset → process the same input yields identical output.

### T8 — Host contract

- pluginval strictness 10 passes on all supported formats.
- All parameter values settable from the host in any order, including at sample-accurate positions within a block, without producing NaN or clicks.
- Bypass is click-free and does **not** change reported latency.
- Sample-rate changes and block-size changes mid-session handled without artifacts and without latency change.

### T9 — Throughput

- **Method:** measure CPU per instance at 48 kHz / 128-sample block on the team's reference machine, at 1, 4, 12, 24, 48 bands, static and dynamic.
- **Targets:** cost scales **linearly** in band count (C4 — any superlinear term means an accidental crossover or per-band allocation). Establish the absolute budget on the reference machine at the first measurement and treat a **>10% regression** as a CI failure.
- Bands stored in a flat array processed in a tight loop, structured for SIMD across bands. Verify vectorization actually happens (compiler report or disassembly spot-check) rather than assuming it.

---

## 8. Summary of headline targets

| Metric | Target |
|---|---|
| Reported PDC, all rates, all modes, dynamics active | **0 samples** |
| First non-zero output sample from impulse | **index 0** |
| Filter accuracy, 20 Hz – 0.25·Fs | ≤ 0.05 dB from analog prototype |
| Filter accuracy, 0.40 – 0.45·Fs | ≤ 0.50 dB from analog prototype |
| Improvement over bilinear near Nyquist (high f₀/Q/gain) | ≥ 6 dB peak error reduction |
| Detector timing accuracy | ±5% of theoretical time constant |
| Fast-attack overshoot | ≤ 3 dB, budgeted and regression-locked |
| M/S round-trip null | ≤ −140 dBFS |
| Zipper artifact energy during full-range automation | ≤ −80 dBFS |
| Denormal CPU spike | ≤ 1.5× median block time |
| Band-count scaling | linear |
| DAW hosts verified before release | ≥ 3 |

---

## 9. What we are explicitly *not* doing, and why

Documented so these don't get re-litigated in review:

- **No oversampling in the EQ path.** It is Nova's fix and it costs 187 samples at 44.1 kHz. Matched-Z gets the accuracy for free. (§C2, §C6)
- **No lookahead in the detector.** Lookahead *is* latency, one-for-one. The overshoot it would prevent is instead budgeted and measured. (§C3, T5)
- **No linear-phase mode in v1.** Deferred to Phase 4 as an explicit opt-in with published latency, following the FabFilter Pro-Q pattern — never in the default path.
- **No FIR crossover network.** Parallel band summing has no crossover, so no N/2-sample crossover latency. (§C4)
- **No block-buffered anything.** T1's single-sample-block equivalence test enforces this structurally.

---

## 10. Repo integration

Proposed layout — adjust to match existing conventions (A2):

```
modules/bmo-deq/
  spec/          this document + design decision records
  dsp/           filter core, detector, gain computer, band, matrix
  params/        parameter definitions, ranges, smoothing config
  test/
    harness/     tier-1 offline numeric harness
    reference/   cookbook-bilinear + analog prototype evaluators (test-only)
    golden/      checked-in IRs, sweep analyses, manifest
    reports/     JSON output, gitignored
  docs/          measured-PDC table per host per rate
```

**CI gates, in order — a PR merges only if all pass:**

1. Build, all platforms, warnings-as-errors.
2. Tier-1 harness: T1, T3, T7 (fast, run on every push).
3. Tier-1 harness: T2, T4, T5, T6, T9 (full grid, run on PR).
4. Golden-reference diff: any tolerance breach fails and requires an explicit re-bless commit.
5. Tier-2 pluginval / auval.

The **latency test (T1) runs first and on every push**, because it is the product claim. If it ever goes red, that is a stop-the-line event, not a backlog item.

---

## 11. Phasing

| Phase | Deliverable | Exit criteria |
|---|---|---|
| 1 | Tier-1 harness + analog prototype evaluator + cookbook-bilinear reference | Harness reproduces known cookbook responses; cramping is *measurable* and matches published expectations |
| 2 | Matched-Z static filter core, all types, single band | T1, T2, T3 pass |
| 3 | Per-band dynamics, detector, gain computer, smoothing | T4, T5 pass; T1 still passes with dynamics active |
| 4 | Per-band M/S blend, multi-band summing, band budget | T6, T9 pass; full suite green |
| 5 | Plugin wrapper, host validation, DAW matrix | T8 pass; ≥3 hosts × 6 rates logged at 0 samples |
| 6 *(post-v1)* | Optional linear-phase mode, opt-in, published latency | Does not alter default-path PDC |

Phase 1 before Phase 2 is deliberate. **Build the ruler before the thing being measured** — otherwise there is no way to tell whether the matched-Z work actually bought anything, which is the whole thesis of the project.

---

## 12. Open questions for the dev team

1. **DF-I or SVF as the shipping core?** Spec leans SVF (free sidechain bandpass tap, better under modulation, better low-frequency precision). Decide in Phase 2 with T2/T4/T7 numbers, not by preference.
2. **Detector time-constant convention** — τ (63%) or 10–90%? Must be fixed before T5 is written (§5.5).
3. **Gain range per band.** Nova is ±12 dB; Infinity markets unrestricted. What do we ship, and does the T2 accuracy target hold at the rails? Test grid currently goes to ±24 dB.
4. **Detector channel linking** in M/S mode — linked, unlinked, or a continuous link parameter? §5.4(b) means this determines whether M/S does anything at all for a static band.
5. **Band budget.** 24 is parity. Is there a reason to publish a higher number, given C4 makes it a pure CPU question?
6. **Sidechain routing depth** — per-band external sidechain, or one global external bus? Affects the parameter model, not the DSP.

---

## 13. Sources

The parent brief carries the full annotated source list. The four to read in full before writing any Phase-2 code:

- **Martin Vicanek, *Matched Second Order Digital Filters*** — https://vicanek.de/articles/BiquadFits.pdf — the matched-Z fits. This is the core of C2.
- **Andrew Simper / Cytomic, technical papers** — https://cytomic.com/technical-papers/ — TPT/SVF derivation and Dynamic Smoothing.
- **Vadim Zavalishin, *The Art of VA Filter Design*** — https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_1.1.1.pdf — the TPT theory underneath Cytomic.
- **Giannoulis, Massberg & Reiss, *Digital Dynamic Range Compressor Design*** (JAES 2012) — detector topologies, ballistics, and the lookahead tradeoff C3 accepts.

Plus **Robert Bristow-Johnson's Audio EQ Cookbook** (https://www.w3.org/TR/audio-eq-cookbook/) — used for the test-only bilinear reference path, not for shipping coefficients.

---

*Spec version 0.1 — for review. Nothing here is settled until §0 assumptions are confirmed and §12 answered.*
