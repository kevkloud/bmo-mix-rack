# De-Esser Design Approaches: A Survey

Neutral survey of published de-essing strategies for a real-time channel-strip module. No approach is recommended here.

## 1. Wideband sidechain-filtered compression
A feedforward compressor (Giannoulis, Massberg & Reiss, 2012, JAES 60(6):399–408) with an HPF/BPF in the detector path only; gain applied to the full signal. Cheap, near-zero latency, no nonlinearity concern. Artefact: pulls down all HF content during an "ess," dulling consonants and pumping ambience/cymbals sharing the band. Easy to tune; the single wideband gain node is the core limitation.

## 2. Split-band (crossover) de-essing
Splits via a complementary crossover — Linkwitz-Riley cascaded-Butterworth (sums to unity magnitude, in-phase at the −6 dB point per Linkwitz-Riley crossover literature), linear-phase FIR, or subtractive/all-pass split — compresses the HF band, recombines. A fixed-gain LR split reconstructs cleanly, but unequal gain between bands breaks the complementary sum, producing comb-filtering/phase colouration near the crossover. FIR avoids phase issues but adds latency and CPU rising with slope; IIR is cheap, low-latency, but reconstructs imperfectly under dynamic gain. No aliasing risk; moderate tuning.

## 3. Dynamic-EQ de-essing
A bell/shelf whose gain follows a band detector, as a modulated biquad or a topology-preserving-transform (TPT/Zavalishin, *The Art of VA Filter Design*) state-variable filter. TPT/SVF updates cleanly under per-sample coefficient modulation; direct-form biquads can glitch or destabilize on fast Q/cutoff jumps. Risk: under-smoothed coefficient modulation injects broadband "zipper" noise — a real audio-rate distortion concern, distinct from classic aliasing. Higher CPU than 1–2 (continuous coefficient recompute), no added latency. Most tuning-sensitive approach; well tuned, most surgical and least dulling.

## 4. Level-independent / relative detection
Detects band-energy-to-fullband ratio rather than an absolute threshold, tracking sibilant balance independent of input level (per de-esser patent literature and dynamics-processor technical notes). Robust to gain riding/mic distance; fails when broadband and band level move together for non-sibilant reasons (breath, other HF transients). One extra envelope follower (modest CPU), no latency/nonlinearity cost; composable with 1–3 as the detector.

## 5. Spectral/STFT and ML-based detection
Frame-based features — spectral flatness, spectral centroid, zero-crossing rate, voiced/unvoiced state — or a trained classifier for sibilance probability (spectral approach reflected in DAGA 2017, "Frequency Domain De-Essing for Hands-free Applications"). Can separate true sibilance from other HF energy best of all approaches, but STFT framing adds block latency (≥1 hop) and materially higher CPU (FFT/inverse-FFT or inference per block) — costly across many instances. Overlap-add can colour transients if mistuned. A verifiable primary citation for a neural sibilance detector was not found; treat such claims as unconfirmed.

## 6. Lookahead vs zero-latency detection & linking
Zero-latency detection reacts only after onset (some lisping as attack catches up); lookahead delays the audio path a fixed amount so reduction can pre-empt the peak, at the cost of that latency system-wide. Peak vs RMS detection, hold, and hysteresis reduce chatter/zipper at the cost of slower reaction. Mid-side or L/R-linked detection avoids stereo image shift but can mis-react to off-center sibilance. Layers onto any approach above; CPU-free, risk is purely latency budget and tuning.

## Comparison

| Approach | Main artefact risk | CPU | Latency | Audio-rate nonlinearity | Tuning | Risk |
|---|---|---|---|---|---|---|
| 1. Wideband sidechain | Dulling, HF pumping | Low | ~None | No | Low | Low-med |
| 2. Split-band crossover | Phase colour under gain change | Low–High (FIR) | None(IIR)–ms(FIR) | No | Medium | Medium |
| 3. Dynamic EQ (biquad/TPT) | Zipper noise if undamped | Medium | ~None | Yes | High | Med-high |
| 4. Relative/ratio detection | False triggers on breathy HF | Low | None | No | Low-med | Low |
| 5. Spectral/STFT/ML | Block latency, window colour | High | ms–tens ms | Possible | High | Med-high |
| 6. Lookahead/detector shaping | Latency vs lisping | ~None | 0–ms | No | Medium | Low |

## Neutral shortlist
- Wideband or split-band gain path (1/2) plus ratio detection (4): low-CPU, predictable baseline.
- Dynamic EQ via TPT/SVF (3) plus ratio detection (4): more surgical, needs modulation-noise budget.
- Spectral/ML detection (5): only if per-instance CPU and added latency fit the target instance count.

No approach is selected; this is input to a later decision.

## Sources
- Giannoulis, Massberg, Reiss (2012). "Digital Dynamic Range Compressor Design — A Tutorial and Analysis." *JAES* 60(6), 399–408.
- Zavalishin, V. *The Art of VA Filter Design* (rev. 2.1.2, 2020) — TPT state-variable filters vs biquad coefficient modulation.
- Linkwitz-Riley crossover primers (cascaded-Butterworth complementary crossover, magnitude/phase reconstruction).
- DAGA 2017, "Frequency Domain De-Essing for Hands-free Applications" — author list unconfirmed.
- De-esser relative/ratio detection per dynamics-processor notes and de-esser patent filings.
- Sibilance feature literature (spectral flatness, centroid, zero-crossing rate) used generally in audio/speech classification.

**Unverified:** no primary peer-reviewed citation for a neural/CNN sibilance detector confirmed (only secondary summaries); DAGA 2017 author list/scope unconfirmed beyond title.
