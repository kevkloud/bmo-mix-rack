# FET Compressor — Reference Behavior

Groundwork notes on documented behavior of the original hardware, for a FET compressor module. Every claim is tagged MEASURED/DOCUMENTED (from a manual, service doc, or measured analysis) or FOLKLORE/ANECDOTAL (widely repeated but not independently verified here).

## Gain element and distortion mechanism

The original hardware uses a FET (Q1) as a voltage-variable resistor, arranged as a shunt attenuator ahead of the gain stage rather than as a series element: it bleeds progressively more signal to ground (or to the input of the following stage) as the control voltage increases. Operated well below pinch-off with small signal swing, an FET resistor is nonlinear (its drain-source resistance depends on drain-source voltage), which is the source of the unit's characteristic even-order-leaning distortion. The documented fix is a feedback/bias arrangement: a portion of the signal is fed back to the gate (a "Q-bias" style network) so the resistance seen for positive and negative half-cycles is equalized, cancelling much of the second-harmonic-dominant distortion the bare FET would otherwise produce. **DOCUMENTED** (circuit description; general FET-as-VCR theory) — Inside Blackbird: The FET Compressor; archive.org: the owner's manual; general FET-VCR distortion-cancelling feedback technique, [EDN: FETs for voltage-controlled circuits](https://www.edn.com/a-guide-to-using-fets-for-voltage-controlled-circuits-part-2/).

## Detector topology and its consequences

The sidechain/detector is fed from the **output** (feedback, not feedforward) of the gain stage. Consequences documented across sources: response is program-dependent rather than a fixed time-constant, the knee is soft rather than hard (the manual describes gradual onset around threshold), and ratio/attack/release all interact with signal level and spectral content rather than acting as independent fixed parameters. **DOCUMENTED** — archive.org owner's manual.

## Attack and release ranges

- Attack: 20 µs (fastest) to 800 µs (slowest); manual states "less than 20 microseconds for 100% recovery," adjustable to 800 µs.
- Release: 50 ms (fastest) to 1100 ms/1.1 s (slowest); manual defines the 1.1 s point as "63% recovery" (i.e., one time-constant to 63%, standard RC-style definition).
- The attack/release knobs are numbered 1–7 and run **backwards** relative to naming intuition: 1 is slowest, 7 is fastest, and both times get faster turning clockwise (toward 7) despite the increasing number.

**DOCUMENTED** — archive.org owner's manual; Wikipedia: the peak-limiter article; knob-numbering direction also widely repeated in player literature, **FOLKLORE/ANECDOTAL** for the exact "why" but the direction itself is corroborated by the manual and by Sterling Sky Sound: Know Your Gear.

## Ratio settings and all-buttons-in

Four front-panel ratios: 4:1, 8:1, 12:1, 20:1, each engaging a different feedback/threshold network — switching ratio also shifts the effective threshold point (e.g., the manual gives roughly -24 dB ±2 dB input threshold at the 20:1 setting), so ratio is not independent of threshold in this topology. **DOCUMENTED** — archive.org owner's manual.

All-buttons-in (all four ratio buttons pressed simultaneously) is an out-of-spec bias state, not a documented ratio. Findings from a peer-reviewed investigation (Moore, "All Buttons In," *Journal on the Art of Record Production*, 2012, citing engineer Fletcher/"Shanks"):
- Effective ratio lands somewhere between 12:1 and 20:1, not a clean fixed value.
- The gain-reduction curve is described as a "plateau" rather than the gentler slope of, e.g., 4:1 — an abrupt, near-flat-topped shape rather than a smooth knee.
- A **lag on initial transients** is reported — i.e., a brief delay/overshoot before gain reduction engages, which can let peaks approach 0 dBFS despite heavy average reduction.
- Bias points shift throughout the sidechain/gain-cell circuit, and this bias shift is described as the main driver of altered attack/release behavior in this mode.
- Increased distortion, including audible low-frequency distortion on transient content (e.g., kick drum), was visible in spectrograms.

**DOCUMENTED (measured/observed in a published investigation)** — Journal on the Art of Record Production, "All Buttons In". Popular characterizations of "why it sounds that way" beyond this paper's observations are **FOLKLORE/ANECDOTAL** — [Pulsar Audio: History of All-buttons-in Mode](https://pulsar.audio/blog/the-history-of-all-buttons-in-mode/); [MusicRadar](https://www.musicradar.com/how-to/urei-1776-all-buttons-in).

## Threshold, gain structure, and stages

There is no dedicated "threshold" knob: compression amount is set by driving the **input** control (raising signal into the fixed detector/threshold point), and the **output** control is separate makeup gain to restore level — input drives compression, output restores level. Signal path: input attenuator/pad → balanced input amplifier → FET shunt gain-reduction cell → output/line amplifier, input and output stages transformer-coupled (input transformer, and an output transformer providing floating, balanced, transformer-isolated output). Documented max gain before limiting is on the order of 45 dB ±1 dB. **DOCUMENTED** — archive.org owner's manual; Wikipedia: the peak-limiter article.

Revision differences (high level): early revisions (through roughly Rev E) use a **class-A** output line amplifier; from Rev F onward the output stage changed to a **push-pull (class-AB)** design for more output drive, alongside transformer part changes across revisions. Transformers are broadly credited with the unit's coloration (saturation/harmonic addition at higher drive), with the specific input/output transformer part numbers differing by revision. **DOCUMENTED (revision facts)** — Mix: the revision-history article; Black Ghost Audio: History of the FET; transformer-driven coloration characterization is **FOLKLORE/ANECDOTAL** in degree, though the class-A→push-pull change itself is documented.

## Program-dependent release

Release is not a single fixed time constant: it is documented/observed to behave faster immediately following transient content and slower during sustained heavy compression, which reduces audible pumping on program material versus a fixed-RC release. **DOCUMENTED** — archive.org owner's manual (release circuit description); general characterization also **FOLKLORE/ANECDOTAL** in casual sources.

## Running it hard: documented practice

Gathered 2026-09-20 on AURORA. Interviews and forum accounts are **ANECDOTAL** throughout; none of it is a measurement. It is here to say what the unit is *asked to do*, not what it does.

- **Heavy reduction is the normal use, not an extreme one.** Chris Lord-Alge is reported to run lead vocals into one at a fixed setting — 4:1, moderate attack (position 3), fastest release (7), input driven hard and output backed off, then into a limiter for peaks — compressing hard for attitude rather than for control. **ANECDOTAL** — [Sound On Sound: Secrets Of The Mix Engineers: Chris Lord-Alge](https://www.soundonsound.com/techniques/secrets-mix-engineers-chris-lord-alge); [Vintage King: 20 Questions With Chris Lord-Alge](https://vintageking.com/blog/2012/03/20-questions-with-chris-lord-alge).
- **4:1 is the common choice even when slamming**, with depth set by the input control rather than the ratio switch — consistent with the no-threshold gain structure above. Tom Lord-Alge is reported to reach for one mainly on bass. **ANECDOTAL** — JARP: All Buttons In; [Sound On Sound: Inside Track: Tom Lord-Alge](https://www.soundonsound.com/techniques/inside-track-tom-lord-alge).
- **Revision preference splits by job, and the split is about distortion at depth.** The earliest non-low-noise units ("blue stripe") are favoured on lead vocals for an upper-mid "hair" that pushes a vocal forward; the later black-faced low-noise revisions are called smoother and cleaner. That change arrived around Rev C and is a circuit one: reduced drain-source voltage on the gain-reduction FET to hold it in its linear range, and a revised FET feedback network to minimise distortion. **ANECDOTAL** (preference), **DOCUMENTED** (circuit change) — Gearspace: Rev A or Rev D; SonicScoop: FET vs FET vs FET.
- **All-buttons-in on drum room mics** is the "nuke"/"British" use: fast attack, fast release, very heavy reduction, wanted for its distortion and pumping. The common claim that it is the drum sound of "When The Levee Breaks" is **FOLKLORE and appears to be wrong** — Andy Johns describes two ribbon mics on a stairwell into other limiters entirely. **ANECDOTAL** — [Tape Op: Andy Johns](https://tapeop.com/interviews/39/andy-johns); [Gearspace: When the Levee Breaks compressor](https://gearspace.com/threads/when-the-levee-breaks-compressor.110874/).

Consequence for the model: 20 dB and more of reduction, at the fastest release, is an ordinary operating point, and the ratio switch is not how depth is reached.

## Published specs (target figures)

| Spec | Published value | Confidence |
|---|---|---|
| THD | < 0.5% (50 Hz–15 kHz, with limiting, at 1.1 s release) | MEASURED/DOCUMENTED (manual spec) |
| Noise | > 81 dB S/N re threshold, 30 Hz–15 kHz | MEASURED/DOCUMENTED (manual spec) |
| Frequency response | ±1 dB, 20 Hz–20 kHz | MEASURED/DOCUMENTED (manual spec) |
| Max gain before limiting | 45 dB ±1 dB | MEASURED/DOCUMENTED (manual spec) |
| Attack range | 20 µs–800 µs (100% recovery definition) | MEASURED/DOCUMENTED (manual spec) |
| Release range | 50 ms–1100 ms (63% recovery definition) | MEASURED/DOCUMENTED (manual spec) |
| Ratios | 4:1, 8:1, 12:1, 20:1 | MEASURED/DOCUMENTED (manual spec) |
| All-buttons-in effective ratio | ~12:1–20:1, plateau curve, transient lag | DOCUMENTED (published measured/observed investigation) |
| Threshold at 20:1 | ≈ -24 dB ±2 dB input | MEASURED/DOCUMENTED (manual spec) |

Source: archive.org: the owner's manual; Wikipedia: the peak-limiter article; JARP: All Buttons In (Moore, 2012); Mix: the revision-history article. Exact THD/noise/response figures vary slightly by revision and by which reprint of the manual is consulted; treat the table as target ballpark figures, not a single authoritative datasheet.
