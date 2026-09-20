# Modeling-Approach Survey: 1176-Style FET Compressor

Scope: candidate strategies for a real-time, low-latency, plugin-grade 1176-style/FET compressor. Neutral survey — no approach chosen here.

## 1. Static gain curve + envelope follower

The classic recipe: dB-domain gain computer (knee/ratio) feeding a one-pole attack/release smoother, feedforward or feedback (Giannoulis, Massberg & Reiss, "Digital Dynamic Range Compressor Design—A Tutorial and Analysis," *JAES* 60(6), 2012). Feedback (the 1176's actual topology) gives program-dependent release but is an implicit loop; digitally it needs an iterative solve or the "unit-delay" trick, adding a small time-varying error.

- **Captures**: attack/release shaping, approximate feedback program-dependence. **Misses**: FET distortion, all-buttons-in (bias-driven).
- **CPU**: very low. **Latency**: ~0–1 sample. **Aliasing**: negligible unless a saturator is layered on, where ADAA/oversampling then applies to that block.
- **Tuning**: minutes–hours, by ear or light measurement. **Risk**: low, but low fidelity ceiling alone.

## 2. Block-oriented models (Wiener/Hammerstein/Wiener-Hammerstein)

Cascade of linear filters and static nonlinearities (LNL), used for distortion circuits, adaptable to compressors if the gain-reduction block is made parameter-varying (DAFx 2016, "Black-Box Modeling of Distortion Circuits with Block-Oriented Models"; Schoukens & Tiels, *arXiv:1902.00683*).

- **Captures**: memoryless FET waveshaping and linear sidechain coloration cleanly. **Misses**: strongly time-varying, program-dependent gain reduction and feedback coupling; all-buttons-in likely needs a multi-branch nonlinearity.
- **CPU**: low–moderate. **Latency**: near-zero. **Aliasing**: the static-nonlinearity block fits ADAA well (Bilbao, Esqueda, Parker & Välimäki, "Antiderivative Antialiasing for Memoryless Nonlinearities," *IEEE SPL* 24(7), 2017); oversampling needed once the block itself varies with time.
- **Tuning**: moderate — system-ID from sweep/multisine data. **Risk**: moderate; solid record for distortion, thin for compressors. *No published block-oriented model specific to a 1176-type circuit was found.*

## 3. Nonlinear state-space / circuit-derived white-box models

Nodal DK-method and wave digital filter (WDF) derivations solve the circuit's own nonlinear equations, including the feedback loop and FET/diode elements (nodal DK: Yeh & Smith-lineage work; WDF multi-nonlinearity solvers: Werner et al., DAFx/AES papers). Closest published precedent is the Fairchild 670 tube limiter, not the 1176 (Raffensperger, "Toward a Wave Digital Filter Model of the Fairchild 670 Limiter," DAFx-12). *No 1176/FET-specific circuit paper found; flagged unverified/likely absent.* Port-Hamiltonian formulations are an active but less mature alternative.

- **Captures**: feedback topology and FET distortion by construction, including all-buttons-in if the bias network is modeled. **Misses**: unit-to-unit hardware variance unless separately calibrated.
- **CPU**: highest of the four — per-sample nonlinear solve (Newton-Raphson/K-method). **Latency**: zero (implicit solve). **Aliasing**: significant; ADAA integrates into WDF nonlinear ports, preferable to brute oversampling for CPU cost.
- **Tuning**: needs a validated netlist/SPICE reference plus hardware measurements for tolerances; highest engineering effort, least ML-style data-hungry. **Risk**: highest schedule risk absent a confirmed 1176 precedent; highest fidelity ceiling.

## 4. Grey-box and neural models

Black-box neural models (TCN/LSTM/state-space) profile a device end-to-end from paired audio (SignalTrain: Hawley, Colburn & Mimilakis, AES 147th Convention, 2019, LA-2A dataset; Steinmetz & Reiss, "Efficient Neural Networks for Real-Time Modeling of Analog Dynamic Range Compression," 2022, causal, real-time on CPU from ~10 min of data). Grey-box work keeps a differentiable gain-computer/envelope structure, fitting/correcting it with a small network (Frontiers in Signal Processing, 2025; NablAFx, Comunità et al., 2025). A "Grey-Box Modelling of Dynamic Range Compression" paper was found by title only — *authorship unverified*. A large bus-compressor dataset (Solid-State Bus-Comp, 2025) points toward SSL/1176-adjacent hardware; nothing 1176-specific confirmed.

- **Captures**: whatever the data covers, incl. program-dependent release and FET distortion; grey-box keeps an interpretable knob map. **Misses**: generalization outside trained range; all-buttons-in needs deliberate sampling.
- **CPU**: moderate–high (TCN/LSTM); grey-box (DSP + small residual) cheaper. **Latency**: ~0 for causal architectures. **Aliasing**: inherited from any learned nonlinearity; ADAA not standard here, oversampling more common (not verified as universal).
- **Tuning/data**: needs paired hardware in/out across levels and switch states — heavy for fresh capture; grey-box needs less than black-box. **Risk**: moderate; strong recent LA-2A results, thinner for FET/1176-class devices.

## Comparison table

| Approach | Feedback/FET/all-buttons | CPU | Latency | Antialiasing fit | Tuning/data | Risk |
|---|---|---|---|---|---|---|
| Gain curve + envelope | Approx. feedback only | Very low | ~0–1 smpl | N/A | Low | Low, low ceiling |
| Block-oriented (W-H) | Partial | Low–mod | ~0 | Good (static block) | Moderate | Moderate |
| Circuit-derived (DK/WDF) | Full, if modeled | Highest | 0 | Good (WDF ports) | High | Highest effort/ceiling |
| Grey-box/neural | Full, if trained | Mod–high | ~0 causal | Immature | High | Moderate, data risk |

## Neutral shortlist

- Approach 1 as baseline skeleton regardless of the higher-fidelity layer chosen.
- Approach 3 if schematic/SPICE access and CPU headroom exist and feedback+FET fidelity is the priority.
- Approach 4 if hardware is available for data capture and a shorter timeline is preferred.

**Flagged unknowns**: no confirmed 1176-specific circuit model (only the related Fairchild 670 WDF paper found); authorship of "Grey-Box Modelling of Dynamic Range Compression" unverified.
