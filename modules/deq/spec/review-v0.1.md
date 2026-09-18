# Review of spec v0.1

Every claim below was measured, either by the standalone check written for the
review or by `measure_deq` / `deq_dsp_tests` in this tree. The verdict: the
architecture is sound and its central thesis holds (matched-Z is 3.7x–58x more
accurate than the cookbook near Nyquist on the spec's own gate) — but several
targets contradict each other or cannot be met as written, and the repo it is
landing in imposes constraints the spec does not know about.

## 1. Contradictions and impossible targets

| # | spec says | what is true | measured |
|---|---|---|---|
| 1 | §5.3 SVF with `g = tan(pi f0/Fs)`, and §12 leans SVF; C2 locks matched-Z | that SVF **is** the bilinear transform. But an SVF can host matched-Z coefficients: `g² = (1+a1+a2)/(1−a1+a2)`, `gk = 2(1−a2)/(1−a1+a2)`, mix solved in closed form | SVF = cookbook to 1.5e-11 dB; hosted matched-Z = DF-I to 5e-14 |
| 2 | T2 absolute targets over the whole grid | a wide loud band whose skirt runs past Nyquist cannot be followed by any biquad | bells pass 60/60 ≤ 200 Hz, 42/60 at 5 kHz, 14/60 at 18 kHz |
| 3 | T3 `a2 = e^(−w0/Q)` for all designs | bell pole Q is A·Q; shelf pole frequency is w/√A or w·√A | off by up to 0.47; holds only for the cut filters |
| 4 | T5 ≤ 3 dB overshoot in 3 ms at 0.1 ms attack; none at ≥ 10 ms | the first sample's error is fixed by the attack coefficient and the step | 9.9 dB at n = 0 for any causal detector; 10.5 dB at every attack |
| 5 | T5 ±5 % of one-pole tau | dB-domain GR reaches 63 % 22 % early; §5.5's release is a two-pole cascade; a one-sample time origin is −22 % at 0.1 ms | attack 100 / release 10 measures 110 ms |
| 6 | C4 parallel summing "because" no crossover latency | a serial IIR chain has no latency either; the two differ in sound | two −24 dB cuts: −1.17 dB inverted (parallel) vs −48 dB (serial) |
| 7 | §3.2 the SVF bandpass tap is a free detector | the band's poles move with its gain, so the tap's sensitivity does too | tap peak = Q·A: 0 dB at −12 dB band gain, +12 dB at +12 |

## 2. Should fix

- §4 64k FFT: 0.35 dB truncation error on slow filters; length must scale with decay.
- T2 symmetry ≤ 0.01 dB fails 111/240 for independently designed cuts (worst 4.7 dB). Define the cut as the boost's reciprocal: exact, and cuts ~3x more accurate.
- T7 denormal timing and T9 CPU regression are wall-clock tests, forbidden by §6's determinism rule; T7's 60-minute run sits in the every-push gate.
- T6 "no zero-crossing artefact" is not measurable; the output is linear in β, so read β back from the audio.
- T4 "−80 dBFS relative to signal" mixes units.
- LP/HP dB error is unbounded in the stopband without a floor.
- Q 16 on a shelf is a resonance, not a shelf; give shelves their own Q range.
- Knee width 0 divides by zero at threshold.
- "Upward (below-threshold expansion)" names one of four cases; name all four.
- §5.2 "DC, Nyquist, centre" gives 29 unusable bell designs; DC + gain at f0 + zero slope at f0 gives none.
- Found while building: a **high shelf's poles alias** when f0·√A passes Nyquist (up to 25 dB error). HS(A) = A²/LS(A) fixes it exactly.

## 3. What the repo imposes (the spec assumed a blank JUCE project)

- **32 parameters per module**, because a rack slot is 32 generic host parameters. 24 bands × ~10 controls does not fit.
- **Linear ranges only** in `ParamSpec`, pinned against JUCE's by `RackTests`. A log frequency control needs a workaround or a core change.
- **Parameters once per block** (`ModuleDsp::setParams`). T8's sample-accurate in-block automation is not available.
- **No sidechain input** in `ModuleDsp::process`. External sidechain is a core change.
- **Identity**: `Bpar` / BMO Parametric is reserved and deferred until Dimension passes Ableton, with BMO EQ to be renamed first (`products/AGENTS.md`).
- A1 (JUCE) is right; A2's "saturator sibling" is right in shape (`modules/<id>/`), with the tests in `tests/dsp` and tools in `tools/measure` by repo convention rather than under the module.
- §12 Q2 (time convention) is answered by consistency: BMO Opto uses tau.

## 4. Confirmed

M/S singularity at β = 2 − √2 and the contribution-blend fix; the |D|², |N|² expansions; the matched pole formulas; the Cytomic table as transcribed; the alpha conventions; the gain computer. T7 low-frequency precision passes in double even in direct form (1.9e-5 dB), so under A5 low-frequency precision is not an argument for the SVF — behaviour under modulation is.
