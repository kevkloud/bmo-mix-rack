# DSP Spec — BMO FET (`fetcomp`)

Math and prose only; no code. A1/A2/A3 = `00-repo-conventions.md`,
`01-reference-behavior.md`, `02-modeling-approaches.md`. Written on AURORA from
those plus a read-only walk of `modules/vcomp`, `modules/opto`, `modules/sat`,
`modules/eq` and `core/dsp`; nothing built or measured here.

## 1. Topology

**Structured grey-box.** A3 approach 2 is the spine: a parameter-varying FET
shunt cell whose *divider law* is the gain computer, driven by a linear
feedback sidechain (§3–§4), with static LNL blocks for the stages and
transformers whose constants the voicing switch changes (§8).

A3 approach 3 (DK/WDF) rejected — no 1176-specific circuit derivation exists,
and a per-sample Newton solve across eight rack instances fails the CPU
constraint. A3 approach 4 (neural) rejected for want of hardware to capture; it
remains the upgrade path.

**Not modeled:** unit-to-unit variance; the 45 dB max-gain figure (INPUT is a
drive control); meter ballistics; mains hum and any synthesized noise floor
(§11); supply sag; the push-pull output revisions, which neither voicing covers.

## 2. Signal flow

`INPUT drive → input transformer/amp → FET shunt cell → output amplifier →
output transformer → OUTPUT makeup → mix`.

The detector taps the **cell output**, before the output amplifier, and returns
a control to the gate — a true feedback loop (`modules/vcomp` is feedforward by
design). No threshold control: INPUT drives signal into a fixed threshold,
OUTPUT restores level (A2).

## 3. The FET divider law

The cell is a voltage-variable shunt: conductance rises with the gate control
`c ≥ 0`, so its gain is the **divider law** `g = 1/(1 + k·c)`. `k` fixes the
ceiling, `G_max = 20·log₁₀(1 + k)` — 40 dB at `k ≈ 99`.

The sidechain is **linear**, as the hardware's is: it rectifies the cell output
and drives the gate through the ratio network, so the demand is

    d = G_R · max(0, |y| − T_R)

`G_R` the sidechain gain the ratio switch selects, `T_R` its threshold bias.
A2 has the ratio switch "engaging a different feedback/threshold network",
which is exactly this pair.

**Static relation.** With the control settled, `c = d`, so above threshold
`x = y·(1 + k·G_R·(y − T_R))`. Writing `u = k·G_R·(y − T_R) ≥ 0` and
`β_R = k·G_R·T_R`, reduction is `GR_dB = 20·log₁₀(1 + u)` and the **local**
compression ratio is

    d log x / d log y = [1 + k G_R (2y − T_R)] / [1 + k G_R (y − T_R)]
                      = (1 + 2u + β_R) / (1 + u)

At threshold (`u = 0`) that is `1 + β_R`, fully settable; as `y ≫ T_R` it
decays toward **2:1** whatever `k` is. An earlier draft used only the second
fact to conclude the divider law "cannot" produce the published ratios. Wrong:
the asymptote is not the operating point.

But it arrives sooner than is comfortable, and that is the cost being accepted.
`u` and depth are the same quantity — 20 dB of reduction *requires* `u = 9`,
where the local ratio is `(19 + β_R)/10`. Holding 12:1 there would need
`β_R > 101`, a 100:1 corner at threshold. The delivered ratio is therefore
**depth-dependent by construction** (§5, §12).

## 4. The per-sample implicit solve

Attack smoothing sits **inside** the divider, or the loop is a unit-delay loop
and oscillates. With one-pole step `α`, previous control `c₋`, and
`v = |y| = m/(1 + k·c)` for `m = |x|`, substituting
`c = (1 − α)c₋ + α·G_R·(v − T_R)` gives a **quadratic in `v`**:

    B·v² + A·v − m = 0,
        A = 1 + k(1 − α)c₋ − α·k·G_R·T_R,   B = α·k·G_R

Exactly one root is positive (the product of the roots is `−m/B < 0`) and it is
the stable one: it reduces to `v = m/A` as `α → 0` and to the static curve at
`α = 1`. Take

    v = 2m / (A + √(A² + 4Bm))        when A ≥ 0
    v = (−A + √(A² + 4Bm)) / (2B)     when A < 0

— the standard conditioning rule; the first form loses the root to cancellation
when `A < 0`, the second when `4Bm ≪ A²`. Guard `B = 0` with `v = m/A`, and
restore the sign from `x`.

**Threshold branch.** The quadratic assumes `v > T_R`; if the result is `≤ T_R`
discard it and take `v = m/(1 + k(1 − α)c₋)`. Two evaluations at worst, no
iteration.

**Attack vs release.** The attack one-pole must be implicit — that is where `α`
approaches 1 (§10). Release is slow and safely explicit: solve with the attack
coefficient, and if the resulting demand is *below* the current control,
discard it and take §6's release update. The program-dependent branches
therefore sit outside the solve, keeping the shape recorded in
`modules/vcomp/dsp/Detector.h`.

**Stability and cost.** No `α < 2/R` condition and no oscillation: the solve is
exact, `m → v` is monotone, and the 2:1 asymptote is itself the guarantee — as
`m → ∞`, `v → √(m/B)`, so the cell cannot run away. `α = 1` is legal, which is
what makes the 20 µs attack work at base rate (§12). One square root, one
divide and a few multiplies per sample; the dB-domain fallback needs a
logarithm *and* an exponential, so the divider loop is the cheaper.

**Stereo link.** Always on, not a parameter (A2's cell is one control voltage).
One shared control driven by `max(|y_L|, |y_R|)` and applied to both channels,
as `modules/opto/dsp/DspCore.h`'s `processLinked` already does: derive both
outputs from the previous sample's gain, detect on the louder, advance the
shared control. `currentGainReductionDb()` reports `20·log₁₀(1 + k·c)`,
positive = reduced.

## 5. Ratio family, knee, and all-buttons

`β_R = k·G_R·T_R` is the ratio switch. Two ways to pick it, both **CALIBRATE**:
match the ratio *at threshold* (`β_R = R − 1`), or fit it so the **average**
delivered slope over A2's own measurement window — 10 dB of input above
threshold — equals nominal. The second is what 11's curve test measures, so it
is the one specified. **Local ratio against depth**, with the fitted `β_R`
(the at-threshold alternative would be `β_R = R − 1`):

| nominal | `β_R` | 0 dB GR | 5 | 10 | 15 | 20 | 25 | 30 dB | avg T→20 | T→30 |
|---|---|---|---|---|---|---|---|---|---|---|
| 4:1 | **4.11** | 5.1 | 3.7 | 3.0 | 2.6 | 2.3 | 2.2 | 2.10 | 3.0 | 2.6 |
| 8:1 | **11.23** | 12.2 | 7.8 | 5.2 | 3.8 | 3.0 | 2.6 | 2.32 | 4.9 | 3.6 |
| 12:1 | **18.60** | 19.6 | 11.9 | 7.6 | 5.1 | 3.8 | 3.0 | 2.56 | 6.8 | 4.5 |
| 20:1 | **33.51** | 34.5 | 20.3 | 12.3 | 7.8 | 5.3 | 3.8 | 3.03 | 10.7 | 6.3 |

Read the diagonal: each setting delivers about its nameplate ratio at a
different depth, and all converge toward 2:1. That sag is the law, not a
fitting error; §12 defends it.

**Knee.** No knee parameter and no `kneeReductionDb` call. A2's "gradual onset"
falls out of the law — the local ratio is smooth in depth from the first dB.
The hard corner stays in the sidechain rectifier, where the hardware's is.

**Threshold bias.** `T_R` moves with ratio (A2: ratio is not independent of
threshold). `T_20` anchors at −24 dB ±2 dB input-referred; `T_4 > T_8 > T_12 >
T_20`, offsets ≈ +8/+5/+2 dB, all **CALIBRATE**. Fixing `β_R` and `T_R` fixes
`G_R`.

**Ceiling.** `c` is unbounded, so clamp `k·c ≤ k` and `GR ≤ G_max`. The FET
running out of conductance is then a property of the divider, not a `tanh`
bolted on.

**All-buttons-in** is a bias state, not a fifth ratio (A2/JARP). The loop gives
three of its four documented traits directly:
1. a standing gate bias `c₀ > 0` — 1–2 dB GR at silence, the cell parked in its
   nonlinear region (more distortion, LF-weighted);
2. all four ratio resistors in parallel, so `G_R` rises above `G_20`: higher
   corner ratio, more abrupt catch;
3. the **plateau** does not fall out of the law — `(1 + 2u + β)/(1 + u)` is
   monotone and never below 2 — so it is a fitted collapse of sidechain gain
   above a breakpoint: `G_ab(L)` falls there, flattening the curve and letting
   it go non-monotonic. `G_ab` reads the previous sample's `v`, so the
   quadratic still holds;
4. a one-pole **lag** `τ_lag` in the control path (not the rectifier) — the
   documented transient lag — plus release scaled 1.5–3× and reduced
   even-harmonic cancellation (§7).

All all-buttons numbers are CALIBRATE; A2 pins none of them.

### 5a. Fallback: the dB-domain loop

Kept, not deleted. Run the loop in dB with `core/dsp/GainComputer.h`'s
`kneeReductionDb` and the feedback slope `a_R = R − 1` (`feedbackSlope`), same
closed-form step: `G[n] = [(1−α)G[n−1] + α·a·(x̂[n] − T)] / (1 + α·a)`, pole
`(1−α)/(1+α a) ∈ [0,1)`. Exact nominal slopes, a fitted knee width, and what
`modules/opto` and `modules/vcomp` already use.

It is the fallback because it is the *same loop with an exponential sidechain
law*: writing `k·c = 10^(GR/20) − 1` makes the control a power `R − 1` of
level, and the family `d = G(|y| − T)^p` has asymptotic ratio `p + 1`. The
hardware's ratio network is a resistive divider, `p = 1`, so the dB-domain loop
buys exact ratios by modelling a sidechain the unit does not have. Take it back
if §5's sag proves unusable by ear.

## 6. Program-dependent release

Two branches on the control, smooth-decoupled form (Giannoulis/Massberg/Reiss),
as in `modules/vcomp/dsp/Detector.h`, acting on `c` rather than on dB:

    fast[n] = max(d, p_f·fast[n−1] + (1−p_f)·d)
    slow[n] = p·slow[n−1] + (1−p)·d,   p = p_c if d > slow else p_s
    control = max(fast, slow)

`τ_f = τ_rel`, charge `τ_c = 1.5·τ_rel`, slow `τ_s = 10·τ_rel`. The slow branch
is program-dependent through its *charge* time, not its release: one transient
barely moves it, a sustained passage charges it and it then owns the recovery.
With equal charge times `max` would always pick the slower branch — a trap
recorded in vcomp.

**The knob no longer calibrates in dB, deliberately.** Smoothing happens in the
control domain, where the hardware's gate RC is. An exponential decay of `c`
gives `GR(t) = 20·log₁₀(1 + k·c₀·e^(−t/τ))`, which starts slow and accelerates
and whose 63 % *in dB* depends on depth — A2's "hangs, then lets go" recovery,
for free. So the detent table is calibrated at a stated reference depth (10 dB
GR, **CALIBRATE**). Knob positions 1–7 run **backwards like the hardware**,
1 = slowest and 7 = fastest, per §10's law: 1100, 657, 393, 234, 140, 84,
50 ms at the whole positions.

## 7. Nonlinearity and antialiasing

**FET cell.** `r_ds` depends on `v_ds`, so the divider becomes
`g(v) = 1 / (1 + k·c·(1 + λ(1−q)·v + μ·v²))`. `λ` is even-order (2nd-dominant),
`q ∈ [0,1]` the Q-bias cancellation depth (A2), `μ` the odd content at deep
reduction. Both multiply `k·c`, so **distortion vanishes as GR → 0** — clean
when not compressing — and grows steeply with depth (§12). All-buttons lowers
`q` and sets `c₀ > 0`. Evaluate the `v`-dependent factor from the previous
sample so §4's quadratic is unchanged.

**Stages and transformers.** Input: first-order HF pole ≈45 kHz (leakage
inductance) and LF pole ≈10 Hz — together −0.8 dB at 20 kHz, −1.0 dB at 20 Hz,
inside the ±1 dB spec. LF core saturation: pre-emphasise below ~120 Hz into a
soft shaper and re-add the residual, so saturation stays LF-only. Output stage:
one asymmetric soft shaper, **class-A in both voicings** (§8), 2nd-dominant.

**Antialiasing per block.**
- Static memoryless shapers (transformers, output stage): first-order **ADAA**,
  as `modules/sat/dsp/Shaper.h`, on the *residual* `shape(x) − x` only — where
  the curve is locally linear the difference quotient degenerates to a two-point
  average `cos(πf/f_s)`, an unrequested tone control. Rebuild ADAA state on any
  coefficient change (Shaper.h records why).
- FET cell: ADAA is **invalid** — its antiderivative assumes a curve fixed
  between samples, and this one moves every sample with the control. Two alias
  sources: the `x·g` multiplication (widened by the control's bandwidth, §12)
  and the `v`-dependent shaping. Band-limit the control for the first; the
  second is §9's business.

## 8. Voicing: Blue and Black

**Two constant sets over one topology.** The loop of §3–§6 is identical in
both: same `k`, same `β_R` family, same detents, same solve. What changes is
the colour around it — the cell's distortion coefficients and §7's static
blocks. **Black** is the default: the later low-noise class-A revisions (the
"black stripe" era — low-noise input and FET-bias circuitry, class-A output,
transformer I/O). **Blue** is the earliest revisions (the "blue stripe" units):
less bias refinement, more colour.

A2 supports the *mechanisms* — the Q-bias network equalising the FET's two
half-cycles, class-A output through roughly Rev E, transformer parts differing
by revision, and a low-noise change around Rev C that reduced drain-source
voltage on the gain-reduction FET to hold it in its linear range — but gives
**no revision-specific distortion, noise or response figures**. Every *amount*
below is therefore CALIBRATE, a grey-box pair fitted by ear until measurements
exist (§13.2). One thing the amounts must respect: the two are described as
differing in **distortion at depth**, so the gap should widen with GR (§12),
not sit flat.

| Constant | Blue | Black | Basis |
|---|---|---|---|
| Q-bias depth `q` | low — 2nd harmonic largely uncancelled | high — the network doing its job | A2 mechanism DOCUMENTED; depth **CALIBRATE** |
| Even `λ`, odd `μ` | larger, and growing faster with GR | smaller | **CALIBRATE** |
| Cell standing bias | slightly higher | reference | **CALIBRATE** |
| Input stage | earlier onset of stage nonlinearity | low-noise, later onset | A2 names the circuitry, no figures |
| Output stage | class-A, 2nd-dominant | class-A, 2nd-dominant, lower in amount | A2 DOCUMENTED |
| Transformer LF saturation | deeper, lower breakpoint | shallower | A2 credits transformers; amounts **CALIBRATE** |
| HF pole | slightly lower | ≈45 kHz (§7) | derived; delta **CALIBRATE** |
| `T_R` trim | small offset, gain-matched out | reference | **CALIBRATE** |
| Noise floor | none synthesized | none synthesized | §11, both |
| Timing, `β_R`, `k`, `G_max`, ratios | identical | identical | A2 gives one set of ranges |

**What must not differ.** Latency is identical in both, and the voicing must
not appear in `latencyForParams` at all. Parameter ids, ranges, steps, defaults
and skews are shared, as is the GR metering convention. The two must be
**gain-matched**: ±0.1 dB at unity with no reduction, ±0.5 dB at 10 dB GR, so
the switch reads as character rather than level. Any offset a voicing's
constants imply is compensated inside that voicing.

**Switching click-free.** Every differing constant belongs to a static shaper
or to the cell's distortion factor; none belongs to the loop's state, so `c` is
*preserved* and the compression does not jump. Do not ramp the shaper
coefficients — ADAA state must be rebuilt on any change (§7), and rebuilding it
each sample through a ramp would smear the residual it exists to anti-alias.
Build the incoming set at switch time, prime its ADAA state from the current
sample (one sample of first-order error, inaudible under the fade), crossfade
the two shaper *outputs* over 5–10 ms, drop the outgoing set.

## 9. Oversampling and latency

**Decision: reuse `core/dsp/Oversampler.h` behind an `oversampling` choice —
Off / 2x / 4x — defaulting to Off.** Latency is
`Oversampler::latencyForFactor`: **0 / 40 / 60** samples at base rate, reported
honestly. This is the Saturator's arrangement exactly
(`modules/sat/dsp/SatDsp.h`, `modules/sat/params.h` default Off), the one
pattern in this rack with tests, a measured cost table
(`testing-notes/ui-pass-sat-2026-09-17.md` §8a, on AURORA) and a panel row.

Against the alternatives this spec previously weighed:

- **(a) Do what Opto does** — nothing. `modules/opto/dsp/OptoDsp.h` returns 0
  unconditionally, "no lookahead, no oversampling"; its tanh drive stages run
  at base rate with no ADAA. Opto can, because its attack is fixed at 10 ms so
  its control has almost no bandwidth and its drive is one fixed mild curve.
  Right as the *default*, which is why Off is the default; not as the only
  option.
- **(b) A module-local minimum-phase IIR oversampler, latency 0.** Rejected:
  new, untested code in a tree that has none, making "zero latency" a property
  of a filter nobody has measured. `Oversampler.h`'s delay is whole-sample by
  construction and carries a hard-won history (48 taps drooped 0.54 dB at
  20 kHz; 81 does not).
- **(c) ADAA only, never oversample.** Rejected as the *whole* answer — §7,
  ADAA cannot cover the FET cell. ADAA on the static shapers stays regardless
  and is what makes Off viable: the Saturator's measured alias floor at Off is
  −54 dB under far harder drive than THD < 0.5 % implies.

**Which blocks move.** Base rate: parameter smoothing, INPUT/OUTPUT gains,
meters, the Mix dry path. Oversampled: input transformer, FET cell, sidechain
and solve, output stage and transformer, ADAA shapers.

**The Mix dry path must be delay-matched, and MIX ships in v1.** The wet path
carries the oversampler's round trip — **0 / 40 / 60** samples at Off / 2x / 4x
— so the dry path takes a plain delay line of exactly that length, sized from
`Oversampler::kMaxLatency` and re-primed when the factor changes. Undelayed, a
partial blend combs (two copies of the same signal 40 or 60 samples apart, a
~1.2 kHz-spaced notch pattern at 48 kHz) and `mix = 0` stops nulling, which is
also how `bypass` is proven. `modules/eq/dsp/DspCore.h` carries the dry ring
for exactly this reason, and the EQ review records what going without costs:
switching the factor resets the stages without clearing the ring, so it clicks
and misaligns for up to 70 samples. Clear the ring on a factor change.

**Consequences.** The switch clicks and re-syncs host delay compensation, so it
is a setup control, not an automation target; and a rack sums its slots'
latency (`tests/plugin/RackTests.cpp`). If M3 cannot reach a −60 dB alias floor
at Off, the default moves to 2x before ship — CEQ's precedent
(`modules/eq/params.h`) — the last moment it can move. It does **not** change
timing: an earlier draft said it changed attack resolution, and §12 shows it
does not.

## 10. Coefficient derivation

Survivor convention throughout, matching `poleFor` in vcomp:
`p = exp(−1/(τ · f_s,eff))`, `f_s,eff` the **effective** (possibly oversampled)
rate.

**Knob position is the parameter, and it runs backwards like the hardware**
(1 = slowest, 7 = fastest; 11 §2 carries the decision and the alternative that
was weighed). Position `p ∈ [1,7]` is continuous, and the *published* time is

    t_att(p)  = 800·(20/800)^((p−1)/6)  µs   → 800 / 126.5 / 20 µs at p = 1/4/7
    t_rel(p)  = 1100·(50/1100)^((p−1)/6) ms  → 1100 / 234.5 / 50 ms at p = 1/4/7

Both are exponential in `p`, so a *linear* position sweep is already the log
time sweep the knob wants and no parameter skew is needed. The inverse, for
tests and for reading a figure off A2, is
`p = 1 + 6·ln(t/t₁) / ln(t₇/t₁)`.

`t_att` is A2's **100 % recovery** figure and `t_rel` its **63 %** one, so the
two feed the coefficient rules differently:

- Release: `τ_rel = t_rel(p)` directly, fitted at §6's 10 dB reference depth.
- Attack: A2's *100 %* definition ⇒ a one-pole never arrives, so
  `τ_att = t_att(p)/N`, `N = 5` (99.3 %); `N` is CALIBRATE, 4.6 (99 %) the
  common alternative. `τ_att` therefore spans 160 µs to 4 µs.
- **Sub-sample attacks are not a limit.** 4 µs is 0.18 samples at 44.1 kHz, but
  `α` still separates all seven detents (0.12 … 0.995 at 48 kHz) and §4's solve
  is exact at `α = 1`. Tests assert monotonicity and §12's first-sample
  overshoot, not absolute times.
- Rectifier: `v = |y_cell|` through a fixed ≈3 µs one-pole at the effective
  rate (CALIBRATE). No detector high-pass in v1 (11 §2).
- Sample-rate independence: every coefficient derives from `τ` and `f_s,eff` at
  `prepare()` and on detent change; no hard-coded constants.
- Smoothing: INPUT/OUTPUT/mix one-pole, `τ = 20 ms`, in dB for gains. Ratio,
  all-buttons and voicing switching crossfade over 5–10 ms. Control state is
  *preserved*; ADAA state is *rebuilt*.

## 11. Target values

| Quantity | Target | Confidence |
|---|---|---|
| Attack | 20–800 µs over knob positions 7→1 | A2, DOCUMENTED; `N` **CALIBRATE** |
| Release | 50–1100 ms over positions 7→1, at 10 dB GR | A2 (63 %), DOCUMENTED; depth **CALIBRATE** |
| Slow-branch scales | `τ_c = 1.5τ`, `τ_s = 10τ` | A2 + vcomp, **CALIBRATE** |
| Ratios (`β_R`) | 4.11 / 11.23 / 18.60 / 33.51 | derived §5, **CALIBRATE** |
| Ratio sag | 4:1 → 3.0 at 10 dB GR; 20:1 → 12.3; both → ~2 by 30 dB | derived; unverified |
| Threshold at 20:1 | −24 dB ±2 dB input-referred | A2, DOCUMENTED; dBu↔dBFS **CALIBRATE** |
| `T_4/T_8/T_12` offsets | ≈ +8/+5/+2 dB | A2 direction only, **CALIBRATE** |
| Max GR (`G_max`, `k`) | 40 dB, `k ≈ 99`; usable to **30 dB** | §12, design target |
| All-buttons `G_ab`, `c₀`, `L_p`, `τ_lag` | above `G_20`; 1–2 dB standing GR; 1–5 ms | A2/JARP prose, **CALIBRATE** |
| First-sample overshoot | ≤ 0.05 dB at the fastest detent, monotone across detents | §12, derived |
| Ripple H3 at 50 Hz | ≈ 4.5 % at 50 ms release, ≈ 0.23 % at 1.1 s | §12, derived; a bound, not a fault |
| THD, Black | < 0.5 % at 10 dB GR, 50 Hz–15 kHz, 1.1 s release; < 0.05 % at 0 dB GR; 2nd ≈ 10 dB above 3rd | A2, DOCUMENTED; `λ,μ,q` **CALIBRATE** |
| THD, Blue | above Black at equal GR, gap widening with depth | mechanism only, **CALIBRATE** |
| Voicing gain match | ±0.1 dB at 0 dB GR, ±0.5 dB at 10 dB | §8, decided |
| Response | ±1 dB 20 Hz–20 kHz; poles 10 Hz / 45 kHz | A2, DOCUMENTED; poles derived |
| Noise floor | **none synthesized**, either voicing | decided, below |
| Latency | 0 / 40 / 60 at Off / 2x / 4x; default 0; same in both voicings | §9, decided |

**Noise decision:** > 81 dB S/N is a hardware limitation, not a behaviour.
Synthesized noise would break bypass nulling and DSP tests and add nothing
audible. This holds for **both** voicings — Blue is the noisier unit in
reality, and not modelling that is what keeps the only audible difference
between them colour.

## 12. Heavy gain reduction

**Design target: well-conditioned, musical behaviour to 30 dB GR**, the fastest
attack effective at base rate, no lookahead. A2's practice section makes 20 dB+
an ordinary operating point.

**Ceiling and conditioning.** 30 dB needs `k·c = 10^1.5 − 1 = 30.6`, 31 % of
the conductance `k = 99` provides, so the target sits 10 dB below the FET's own
limit. `k` alone sets that limit; raising it also raises `β_R` for the same
`G_R·T_R`, reopening §5's fit. At `β = 33.5` and 30 dB GR, `A ≈ −32` while
`4Bm ≈ 8.1·10³`, so §4's `A < 0` branch adds two positive numbers and nothing
cancels; the ill-conditioned corner is the other one, `A > 0` with `4Bm ≪ A²`,
covered exactly by `2m/(A + √·)`, and the sub-threshold case, caught by the
threshold branch. **Compute `A`, `B` and the discriminant in double** and
return float: `A²` reaches ~10⁴ under all-buttons bias, where float's 24-bit
mantissa drops terms that matter near threshold. `modules/sat/dsp/Shaper.h`
already works this way.

**Ratio sag at depth** is tabulated to 30 dB in §5, with the average delivered
slope from threshold. **Choosing loop gain so the nominal family survives 20 dB
is not available.**
Holding `R` at 20 dB GR needs `β_R = 10R − 19` → 21/61/101/181, i.e. corner
ratios of 22/62/102/182:1. Every setting would be a hard-knee limiter at the
top of its knee, contradicting A2's "gradual onset" and making the four
indistinguishable near threshold. So the sag stands, defended on A2's own
terms: the unit has **no threshold control**, depth is reached by driving
INPUT, and the documented practice is 4:1 driven hard — precisely where the sag
is largest and evidently not objectionable. That this reasoning could be wrong
is §13.1.

**Attack, and what the implicit solve buys.** At `α = 1` §4's solve collapses
to the static curve: the correct steady-state gain on the **first sample**, no
overshoot, no iteration. `α` never quite reaches 1, leaving a residual. For a
step to 20 dB of steady-state GR at 20:1, 48 kHz, Off:

| detent (1 = slowest) | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| `α` | 0.122 | 0.214 | 0.359 | 0.563 | 0.784 | 0.940 | 0.995 |
| first-sample overshoot, dB | 4.70 | 3.18 | 1.92 | 0.98 | 0.39 | 0.09 | **0.008** |

Computed on AURORA from §4; 44.1 kHz differs by under 0.01 dB. **Acceptance:**
at the fastest detent, overshoot ≤ 0.05 dB at 44.1 and 48 kHz with Off; the
seven figures strictly monotone; the table reproduced within ±0.1 dB at 48 kHz.
Running the solve at 4x makes each step's `α` smaller, but four steps across
one base-rate period land within 0.01 dB of the base-rate answer (checked on
AURORA at 192 kHz) — so oversampling is about the alias floor, not timing, and
there is no reason to reach for 8x.

**No lookahead.** Latency stays as §9 decides, zero at the default. The first
sample of a transient is never seen in advance; the implicit solve pays that
back, reducing it by the full static amount less the overshoot above. At the
slowest detent 4.7 dB of a 20 dB demand passes on the first sample — the attack
knob doing its job, not a defect.

**Ripple distortion under slam.** With a fast release the control follows the
full-wave rectified envelope and ripples at `2f`. For a one-pole release the
ripple depth between peaks is `ε ≈ ½(1 − e^(−1/(2·f·τ_rel)))`, and a gain
modulated at `2f` puts a third harmonic at roughly `ε/2` of the fundamental:

| | `τ_rel` = 50 ms (fastest) | `τ_rel` = 1.1 s (slowest) |
|---|---|---|
| 50 Hz | `ε` ≈ 0.091, H3 ≈ 4.5 % | 0.0045, 0.23 % |
| 100 Hz | 0.048, 2.4 % | 0.0023, 0.11 % |
| 1 kHz | 0.005, 0.25 % | 0.0002, 0.01 % |

This is why A2's THD spec is quoted **"with limiting, at 1.1 s release"**: at
the slowest release the ripple term lands just inside the published < 0.5 %; at
the fastest it is an order of magnitude above. The LF grind on bass at fast
release is hardware behaviour and is wanted — bounded and recorded, not
discovered. §6's two-branch release keeps it in hand on sustained material:
once charged, the slow branch owns the recovery and the ripple falls.

**Cell distortion against depth.** §7's `λ` and `μ` multiply `k·c`, which is
`10^(GR/20) − 1`; between 10 and 30 dB GR that grows about 11×, so a
coefficient fitted at 10 dB is an order of magnitude louder at 30. Both
voicings must be fitted **at depth, not at the THD test point**: pick `λ, μ` so
Black stays musical at 30 dB — H2 still leading, THD monotone in depth, no hard
corner — and let Blue sit above it, the gap widening with GR (§8). Naming a
number here would be inventing one: CALIBRATE, §13.2.

**Aliasing from gain modulation.** Multiplying by a control of bandwidth `B`
widens the spectrum by `B`, and a 20 µs attack implies tens of kHz. The saving
grace is where the energy sits: ripple is large only at low `f`, where its
products are also low, and on HF tones the envelope is nearly constant with
`ε` ~10⁻⁴. What remains is the fast-attack transient — broadband and brief. So
the control keeps its band-limit (envelope poles plus §10's ≈3 µs rectifier
pole) and, where oversampling is on, that band-limit runs at the oversampled
rate so its corner can sit above 20 kHz without folding. M3's alias sweep at
10/20/30 dB GR with the fastest attack and release decides whether Off holds.

**Level handling.** Drive above threshold for a given depth, from §5's law
(`s = 1 + (g − 1)/β_R`, `g = 10^(GR/20)`, drive = `20·log₁₀ s + GR`):

| | to 20 dB GR | to 30 dB GR | INPUT needed from −18 dBFS |
|---|---|---|---|
| 4:1 (`T_4` ≈ −16 dBFS) | 30.1 dB over `T` | 48.5 | +32 / **+50.5** |
| 8:1 (≈ −19) | 25.1 | 41.4 | +24 / +40 |
| 12:1 (≈ −22) | 23.4 | 38.5 | +19 / +34 |
| 20:1 (−24) | 22.1 | 35.6 | +16 / +30 |

**Two range findings, both permanent at first ship.** 11 §2's `input` ceiling
of +45 dB is ~5 dB short of 30 dB GR at 4:1 from a −18 dBFS source, and its
`output` ceiling of +24 dB cannot restore 30 dB of reduction. Proposed: `input`
to +60 dB, `output` to ±36 dB. Owner decision before M0.

**Reference level.** `T_20` anchors at A2's −24 dB ±2 dB *input-referred*, a
dBu figure on hardware and a dBFS one here. The alignment — what dBFS equals
0 VU — is **CALIBRATE** and every threshold and drive figure above rides on it.
Nothing hard-clips before the output stage: the divider law is bounded by
construction and §7's shapers are soft, so a slammed signal degrades into the
stage models rather than into a clip.

**Metering — decided: the shared meter is not touched.**
`ui::DynamicsMeter`'s reduction scale runs 0..24 dB (`kGrRangeDb`,
`core/ui/Controls.h`) and the needle simply **pins** beyond that. 24 dB is
enough to read by; past it the meter says "a lot" and the number is what
matters. So `kGrRangeDb` stays as it is, no `ScalePoint` lifting, and BMO Opto
is untouched. The DSP target is unchanged: the loop is still designed and
tested to 30 dB, and `currentGainReductionDb()` still reports the **true**
figure past the pin — the clamp is a drawing limit, not a measurement one.
11 §3 tests exactly that.

## 13. Open questions and risks

1. **The sag table is unverified.** §5 and §12 are derived, not measured; no
   source gives a real unit's GR curve at depth. If 20:1 delivering ~5:1 at
   20 dB GR reads as wrong, the levers are `β_R` (refit over a deeper window),
   a sidechain exponent `p > 1` (asymptote `p + 1`, and it costs the closed
   form — `p = 2` is a cubic, beyond that needs iteration), or §5a. Highest-
   value measurement to acquire, with the `T_R` offsets.
2. **Revision-specific measurements are missing for both voicings.** A2 gives
   one THD figure and one response spec and does not say which revision they
   belong to, and the practice notes are interviews. Every amount in §8's table
   is fitted by ear until a Blue and a Black unit are measured on one bench —
   the difference between plausible and defensible.
3. **Ratio-to-threshold offsets are unmeasured.** Only 20:1 is documented.
4. **All-buttons is entirely qualitative** — plateau, lag and bias fitted by
   ear against one paper's prose. The plateau is a fitted `G_ab(L)` collapse,
   not something the law gives.
5. **The release knob's calibration is depth-referenced** (§6). If the detents
   read wrong at working depths the reference depth moves — before ship, since
   the table is permanent.
6. **Detector-tap point.** Spec'd before the output amplifier; if the hardware
   taps after it, that stage's nonlinearity enters the loop. Unresolved in A2.
7. **Reference level and parameter ranges** (§12): the dBFS↔0 VU alignment is
   unmeasured and everything rides on it, and `input` +45 dB / `output` +24 dB
   are both short of the 30 dB target. All three are permanent at first ship,
   so they are M0 decisions, not M3 ones.
