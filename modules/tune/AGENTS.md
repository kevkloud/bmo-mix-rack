# modules/tune/

## In this repository, not in the rack

BMO Tune RT is a low-latency monophonic pitch corrector, and a product of this
repository like every other: it builds on `core/`, follows the root
`AGENTS.md`, and ships as its own plugin. It is **not a rack module**. Nothing
of it is in `products/rack/Registry.cpp` or on the rack's link line, and
`tests/plugin/RackTests.cpp` asserts a registry size that adding it would
break. Frosty's call, 2026-09-11. Joining the rack later is a hope rather than
a plan, so the things that would make it possible are kept: the DSP is
JUCE-free behind `ModuleDsp`, and the schema stays within a slot's 32
parameters.

Either side builds without the other, which is what keeps that true:

```
cmake -S . -B build -DBMO_BUILD_RACK=OFF     # Tune alone
cmake -S . -B build -DBMO_BUILD_TUNE=OFF     # the rack alone
```

Its files: `modules/tune/` here, `products/tune/` for the plugin,
`tools/tune/` for the eleven offline harnesses and its own snapshot,
`tests/dsp/tune/` and `tests/plugin/tune/` for the suites (`ctest` names them
`tune_*`), `testing-notes/tune-*.md` and the shoot-out notes, and
`design/tune/` for the panel studies.

It was written in its own repository, against Kevin's main as a submodule, and
merged here with that history on 2026-09-11. Handoffs written before that date
name paths from the old tree: `tools/common/` is now `tools/tune/common/`, and
`tests/TestUtil.h` is `tests/dsp/tune/TestUtil.h`.

## What must not change

**Frozen from the first plugin build (Frosty, 2026-09-10)** -- the same list as
the rest of the suite: parameter ids, their order in `specs()`, kinds, ranges,
steps, defaults, and every choice's names and order. `tests/dsp/tune/
SchemaTests.cpp` writes the table out in full; a change is argued for there,
and a new parameter goes at the **end** of `specs()`. A parameter that must
mean something new gets a new id and the old one is retired (`kRetiredIds`) --
never new meaning under an old id: `retune` became `retune_ms` that way on
2026-09-11, because a saved 36 meant 10 ms.

Plugin code `Btun`, bundle id `com.lt3audio.bmotunert` and manufacturer `LT3a`
are equally permanent: they are what a host finds the plugin by. They are in
the identity table in `products/AGENTS.md` with everything else the suite has
allocated, and the lime accent is in the accents table there.

## The latency rule

**A change is safe to take, as far as latency goes, so long as BMO Tune RT's
true latency does not exceed Waves Tune Real-Time's measured true latency**
(Frosty, 2026-09-11). **The ceiling is a curve, not a number**: Waves' delay
while correcting is nearly proportional to the period, 19.2 ms at E2 down to
0.7 ms at A5, measured on AURORA and recorded per note in
`tools/tune/common/References.h`. `references::ceilingMsAt (hz)` reads it off.
The rule itself is written out in the root `AGENTS.md`, which is where the
rest of the tree cites it. `tests/dsp/tune/HardTuneTests.cpp` enforces it in
every ctest run, per note and worst-against-worst, and `bmo-tune-latency`
checks every semitone. A change may make BMO later, up to the ceiling, without
asking -- say the new figures and the headroom left in the commit body. The
host is still told 0 either way.

**BMO is over the ceiling today at the top of the range**, and has been since
the 4 ms rest landed in `31b30ef`: a constant rest against a competitor whose
delay tracks the note. The two cross at about C3 -- BMO is comfortably under
Waves below it, and over it above, by 3.90 ms at A5. It went unseen because
both gates measured the wrong thing: the tool tested the *rest* delay against
a scalar, and the stimulus held a correction only on A3 and D3, so a
2.3-octave plugin was being judged through a five-semitone window. Both are
fixed as of this commit and both now fail.
`testing-notes/tune-latency-review-2026-09-11.md` has the finding and the
routes out.

BMO Tune RT's parameters (`params.h`) and its DSP (`dsp/`), JUCE-free. The
signal path, in the order a sample meets it:

```
Detector        recursive E/H kernel -> coarse NSDF at ~12 kHz -> full-rate refinement
CorrectionLaw   jump confirmation -> predict forward -> quantize -> vibrato split -> flex -> retune -> gates
ClassicEngine   fractional-rate read, whole-period splices, Live window
```

`TuneCore` joins them; `TuneDsp` is the `ModuleDsp` adapter. Every stage is a
per-sample state machine, which is what makes the output bit-identical at any
block size (`tests/dsp/CoreTests.cpp` checks it).

One engine and one latency contract since 2026-09-11. HYBRID (PSOLA), the
Studio contract, Glide and the formant controls were built, measured and set
aside when CLASSIC sounded better in Ableton; branch `archive/hybrid-studio`
has the code and `testing-notes/nrt-tune-handoff-2026-09-11.md` what it was
and what it measured.

## The invariants

- **No FFT in the correction path** (spec §0). Nothing here uses one.
- **Nothing allocates after `prepare()`.** A pitch-range change arrives on the
  audio thread, so the detector is prepared for the widest range any setting
  can ask for (40 Hz - 2 kHz) and a range change only moves its active window.
- **Live only: the host is told 0, always** (`LatencyContract.h`,
  `TuneCore::kReportedLatency`). No parameter may move it -- a PDC change
  mid-session is a timing jump on the whole track. `bmo-tune-hostcheck`
  checks it on the built VST3.
- **True latency no more than Waves Tune Real-Time's, at every note** -- the
  latency rule in the root `AGENTS.md` (Frosty, 2026-09-11), held to Waves'
  measured curve rather than a scalar. Within it, a change may make the audio
  later without asking. `HardTuneTests` checks it every run and
  `bmo-tune-latency` sweeps the range. **Currently violated** above about
  C3, by 3.90 ms at A5. This is the one invariant on this list that is known
  broken; it is open work, not licence to add more.
- **The correction and the note always come from the same pitch.** See
  "the octave bug" below; this is the one that produced a +1200-cent glitch.
- **Retired parameter ids stay retired.** `engine`, `glide`, `formant`,
  `formant_shift` and `latency` were in 0.1's saved sessions; a new parameter
  under one of those ids would be fed a value meant for something else.
  `kRetiredIds` in `params.h`, checked by `SchemaTests`.
- **Tests measure with `tools/tune/common/Analysis.h`, never with the plugin's own
  detector.** Measuring the output with the code that decided the correction
  agrees with itself whatever it did.

## Where the code departs from the spec, and why

Each of these was measured before it was taken. The test that holds it is
named; undoing one should fail that test.

| Spec says | Code does | Why, measured |
|---|---|---|
| §5.5: 16-tap windowed sinc, "< -100 dB at 0.4 fs" | 32 taps, Kaiser beta 8 | 16 taps reach -24 dB at 0.4 fs. 32 reach -78; -93 at beta 10. `InterpolatorTests` |
| §5.4: oversample 2x, or lowpass at fs/(2 rho) | full-band kernel until an alias could reach 20 kHz, then 0.90/rho | A read at rho folds f to fs - rho f: inaudible below +267 c at 48 kHz. Above it, -68 dB of alias, -0.29 dB at 18 kHz. `SincBank`, `InterpolatorTests` |
| §3.1: the patent's window = lag | kept, plus a whole-cycle mean test | At short lags the window sees a crest fragment of a slow wave; a 1230 Hz lobe beat 110 Hz every half cycle. A real period of a highpassed signal averages to zero. `Detector::spansWholeCycles`, `DetectorTests` |
| §3.4: voicing on clarity + gate + zcr | plus a stability gate | False onset candidates move between hops (1143, 1655, 1043 Hz on consecutive hops of a 147 Hz sine); real ones hold still. `DetectorTests` |
| §3.5 guards 1-3: shorter periods only (sub-multiples, peak fraction, continuity) | plus guard 4: 2 or 3 x the period, if it is much less aperiodic over one long window | Real vocals the corpus lacked. Failure (2026-09-11): a D4 with its fundamental under its second harmonic read at D5 on 8.5 % of voiced frames, a twelfth up on 2 %, the estimate swinging a semitone each evaluation, 427 note flips; Frosty heard "pops and clicks", "hunting". Now 0.7 % and 0.5 %. Aperiodicity as a ratio (0.25; 0.9 when the multiple is the period just held and the period is clearly aperiodic), on the anti-alias lowpass so sub-sample rounding cannot pick the lag, over two long periods and again over the most recent one (else it held the old note ~9 ms past an instant step), decided every 2 ms, and a move to a period not already held needs two runs to agree (else a 2 %-jitter voice doubled by chance). Corpus: mean gross error 1.935 -> 1.934 %; one frame more on transition_octave_0ms, onset_220Hz locks 0.11 ms later; everything else equal or better. CPU median unchanged within noise. `Detector::preferWholeCycle`, `VoiceTests`, `DetectorTests` |
| §3.5 guard 5: median-of-3 on the note decision | a jump > 3/4 semitone waits for the next estimate to agree | A note median paired a held note with a pitch that had already moved: every leap was briefly corrected by its own interval, and a one-frame octave error drove +1200 cents. `CorrectionLaw::confirmPitch`, `CorrectionTests` |
| §4.2: Retune a 0-100 knob, exponential to 0-400 ms | `retune_ms`, 146 steps in ms: 0.0-5.0 by 0.1, then 6-100 by 1; tau in ms | Frosty, 2026-09-11: "display ms", those increments. The unitless knob got a shoot-out mislabelled -- its 10 was 1.2 ms. A new id, the old one retired (a saved 36 meant 10 ms). tau matched to Antares' and Waves' 10 and 20 ms landed with them on real vocals. `SchemaTests`, `CorrectionTests` |
| §4.3a: `((u-u0)/(u1-u0))^2`, "C1 at both ends" | real smoothstep, `3t^2 - 2t^3` | The square has slope 2/(u1-u0) at u1; the applied correction kinks there. `CorrectionTests` |
| §4.3b: `Q(p_slow) + beta p_vib` | the same split, written on the error | Equivalent with the target held; on the error a note change is a step the slow state can be shifted by. At vibrato 0 the note follows the raw pitch, with the dwell below |
| §4.1: nearest allowed note, with hysteresis | at vibrato 0, a switch by less than 60 cents of margin must hold for 40 ms | Frosty, 2026-09-11, after the blind test heard BMO "hunting": "hold the note steadier". A singer sitting between two scale notes (Failure's D#, midway between D and E) flipped with every wobble. 30 cents of margin was tried first and still flipped there. A real step arrives 100-200 cents closer and is taken at once (0 ms, `CorrectionTests`); a pitch settling just past a midpoint moves after 40 ms. Neighbour-note flips: Failure 71 -> 37, Fuji 37 -> 16; corpus note changes 1902 -> 1140. `CorrectionLaw::holdOrSwitch`, `CorrectionTests` |
| §4.6: MIDI target, MIDI as scale, latch, "MIDI required" | none; the key and scale are parameters | Frosty, 2026-09-10: nothing is tracked but the vocal being corrected. It is also what lets the rack's own `SingleModuleProcessor` host this, which does not accept MIDI. MIDI could be appended in a later version without moving a saved session |
| §4.1: key + scale, ten scales in the first build | Chromatic, Major, Minor | Frosty, 2026-09-10: the three a hard-tune session uses. More are appended to the choice list, never inserted |
| §6, §2: a HYBRID engine and a Studio latency contract | neither, in this product | Both were built and passed every gate they had (formants 1.000 +/- 0.001 of scale; Studio measured = reported in every cell). Frosty heard 0.1 in Ableton on 2026-09-11 and CLASSIC sounded better. Kept for a non-real-time tuner: `testing-notes/nrt-tune-handoff-2026-09-11.md`, branch `archive/hybrid-studio` -- which also has the §6.2 LPC groundwork and why it never entered the signal path |

## Measured, 2026-09-10, AURORA, 48 kHz unless stated

| | measured | spec §9 |
|---|---|---|
| Fine pitch error, steady voice | 0.001 - 0.05 c | < 5 c |
| Gross pitch error, 59 pitched corpus items | 0.10 % mean, 1.06 % worst (10 dB pink) | < 1 % clean, < 3 % at 20 dB |
| Output tuning at retune 0 | within 0.04 c, 110 - 880 Hz | < 3 c |
| Time to lock (sawtooth onset) | 2.2 - 2.6 periods | -- |
| Live rest | 192 samples, 4.00 ms, every pitch (`kLiveRestMs`, 2026-09-11) | **FAILS** <= 1.5 ms CLASSIC >= 200 Hz |
| THD+N, CLASSIC, +/-40 c on a sine | -76 dB | < -60 dB |
| CPU, one core, 48 kHz / 128 | 0.9 % median, 1.2 % p99 (re-run 2026-09-11, CLASSIC only); +0.05 points with guard 4, same day, side by side | < 1.5 % |
| Reported latency, every range | 0 samples; rest delay 4.000 ms in every cell (re-run 2026-09-11) | Live: 0 |
| True latency, reference stimulus, worst (2026-09-11) | 9.18 ms (in tune 4.20-4.49, correcting 3.88-9.18); Antares 10.74, Waves 19.22 -- all three at the stimulus' lowest note | <= Waves (the latency rule) |
| True latency, **per note** (2026-09-11) | E2 9.18, A2 8.03, D3 3.88, A3 5.97, **A4 5.01, A5 4.61**; Waves 19.22 / 13.80 / 10.09 / 7.05 / **3.82 / 0.71** | **FAILS** <= Waves above ~C3, by 3.90 ms at A5 |
| Correction lag at 0 ms, vibrato flattened (2026-09-12, with the prediction) | **0.71 ms mean**, -0.06 (D3) to 1.97 (A2); was 3.19 mean and 6.07 worst. Antares -0.24 mean, 1.66 worst | worst still over Antares at A2 (open) |
| Vibrato residue at 0 ms (2026-09-12) | **1.24 c mean**, was 3.35; Antares 1.30 | **meets Antares** |

The rest is the floor. While it corrects, the read wanders up to a period
above it (mean ~ rest + T/2): 6.2 ms at A4, 8.5 ms at A3, 12.9 at A2, 15.3 at
E2 on the per-semitone sweep, which is the strict worst.

**Read the worst-case row per note, never as a scalar.** All three tuners'
worst figures come from the stimulus' lowest note, which is where a
period-proportional delay costs most and BMO's constant costs least: that
comparison makes BMO look like the least late of the three while it is in fact
the latest of the three over most of the range.
`testing-notes/tune-latency-review-2026-09-11.md`.

## Open, and not for one session to settle

- **Hard tune trails a moving voice by about a cycle.** The 2026-09-11
  shoot-out (`testing-notes/shootout-2026-09-11.md`): on a held note BMO is
  close to Antares (0.4 c against 0.2 c median on Failure), but the faster
  the pitch moves the further behind it lands -- 5.7 c against 2.0 c at
  10-20 cents per 10 ms, 14.9 against 6.6 beyond. Antares' lag is -0.24 ms
  mean: it spends its 6.5 ms of true latency looking ahead.
  `HardTuneTests --target` fails on it; enable `hardtune_target` in the change
  that fixes it. Frosty hears the result before it is called fixed.

  **The mechanism, found 2026-09-11** (`testing-notes/tune-latency-review-2026-09-11.md`):
  the detector is pushed the newest sample while the engine reads `rest`
  behind it, so the residue off the note is
  `(detector's analysis lag - rest) x pitch slope`, and the analysis lag is
  one period (measured 1.07 x T, flat to 4 % over two octaves). The 4 ms rest
  pays that in full at about 290 Hz and nowhere else. This is why the lag
  tracks the period, and why Fuji cleared while Failure did not.

  **Mostly closed, 2026-09-12**, by the route that costs no latency:
  `CorrectionLaw` carries the estimate forward to where the engine reads
  (`Detector::kAnalysisLagPeriods`, `CorrectionSettings::readDelaySamples`).
  Mean lag 3.19 -> 0.71 ms, residue 3.35 -> 1.24 c, which meets Antares' 1.30.
  What is left is the worst case at A2, 1.97 ms against Antares' 1.66; every
  other vibrato is inside half a millisecond. On the shoot-out takes it costs
  nothing: Failure 44 -> 41 splices at 0 ms, dropouts and flips unchanged,
  Fuji 23 -> 22 splices and 39 -> 31 flips. **Not yet heard.**

  Note for whoever takes the rest of it that Waves solves the same problem by
  resting one period back (its in-tune delay is T + 1.26 ms) and Antares by
  having a detector whose lag is a constant 4.4 ms -- copying Antares'
  constant does not work for a detector that is not Antares'.
- **The hiccups heard in 0.1** (Frosty's blind test, 2026-09-11: BMO last in
  four of six groups -- "pops and clicks", "hunting for pitch", "skipping /
  dropouts in the pitch hold", "weak at the end of each phrase"). The worst
  was the detector reading a weak-fundamental voice an octave or a twelfth
  up: guard 4 above, heard in a second blind round as clearly better than
  0.1 on every Failure group, still behind Antares ("skips/pops but few and
  far between"). Since, the dwell for vibrato 0 and guard 4's two later
  checks, not yet heard. Still open, measured on Failure (note-name flips
  407 -> 103 over the day): 66 flips that are detector jumps (0.5 %
  twelfths, 0.7 % octaves up, mostly on scoops -- each a splice by a wrong
  period, the likely source of the pops still heard), 37 neighbour flips,
  1.4 % of frames an octave DOWN (creaky phrase ends: the note name and so
  the correction are unchanged), and 13 mid-phrase voicing dropouts under
  80 ms ("weak at the end of each phrase" is probably these).
- **Vibrato 0 % at a boundary: decided, not yet heard.** Frosty chose "hold
  the note steadier" (2026-09-11), done as the dwell in the table above: a
  vibrato that only just crosses a boundary now holds its note, one that
  goes well across still warbles. Frosty hears it in the next blind round
  before it is called done. The remaining neighbour flips on Failure (37)
  are fast crossings with a clear margin, mostly scoops through a
  neighbouring note on the way into the target.
- **Refinement lumpiness at 192 kHz.** One full-rate refinement lands in one
  host block; at 192 kHz / 32 that is up to ~30 % of the block at p99. Spread
  the refinement across its hop to fix. Not an xrun risk at 48 kHz.
- **The voice generator's release.** Its formant resonators ring for tens of
  ms after the source stops, and the detector (correctly) calls that ringing
  voiced; `onset_880Hz` scores it as a 3 % false alarm. Fix the truth, not
  the detector.
- **Real corpora (spec T-2)** -- PTDB-TUG, CMU Arctic, MDB-stem-synth,
  VocalSet -- need downloading and are not here. Everything above is
  synthetic.
- **rtsan, TSan, UBSan** need clang, which this machine does not have; MSVC's
  ASan is wired (`-DBMO_SANITIZE=address`).
- **Relax** (parameter id `flex`, renamed in display only on 2026-09-16)
  exists and defaults to 0. The two patents the spec flagged for it
  (US 9,147,385 B2, US 8,868,411 B2) are Smule's karaoke patents, not
  Antares' -- a miscitation in the source digest, checked at Google Patents on
  2026-09-10. Not legal advice.
