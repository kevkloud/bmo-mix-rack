# Handoff: HYBRID and Studio, set aside for a non-real-time tune plugin

2026-09-11, on **AURORA**. For whoever builds an LT3 Audio tuner that does
not have to run in real time -- a "Tune" without the RT.

## Why this exists

Frosty heard BMO Tune RT 0.1 in Ableton on 2026-09-11: "it works ... classic
mode sounds better, let's eliminate hybrid and studio, focus in only on rt."
BMO Tune RT is now CLASSIC only and Live only. Everything that was cut is
working, tested code, and most of it is *more* at home in a plugin that is
allowed latency than in this one. This note says what it is, where it is, and
what it measured, so it can be picked up without re-deriving any of it.

## Where the code is

Nothing was lost. The last commit with all of it is on a branch of its own:

```
git -C bmo-tune-rt log archive/hybrid-studio -1     # the tip: this note, and everything below
git -C bmo-tune-rt checkout archive/hybrid-studio   # to build it: scripts/build.sh --plugin
```

It is also on Frosty's fork, pushed 2026-09-11 from AURORA:
`badmixesonly/bmo-mix-rack-333`, branch `bmo-tune-rt-archive-hybrid-studio`
(and Tune RT itself on `bmo-tune-rt`). Those branches' history is unrelated
to the rack's -- never merge them into the fork's `main`.

```
git clone -b bmo-tune-rt-archive-hybrid-studio https://github.com/badmixesonly/bmo-mix-rack-333.git tune-archive
```

| What | Files on the branch |
|---|---|
| HYBRID engine | `modules/tune/dsp/HybridEngine.h/.cpp` |
| LPC groundwork | `modules/tune/dsp/Lpc.h` (not in the signal path even there) |
| Studio latency | `modules/tune/dsp/LatencyContract.h` (`contract::studio`, `guardFraction`), `ClassicEngine::setLatencyMode (true, ...)` |
| Engine switch | `TuneCore::runEngines` -- idle engine fed, 20 ms equal-power crossfade |
| Glide | `CorrectionLaw` (`glideMs`, `glideAllowed`), HYBRID only |
| Parameters | `engine`, `glide`, `formant` (Keep/Follow), `formant_shift`, `latency` in `modules/tune/params.h` |
| Hide rule | `isHybridOnly()`, `tests/dsp/ModeTests.cpp`, `tests/plugin/PanelTests.cpp` |
| Panel | the round-7 panel with the mode banner, Latency and the Hybrid clock -- `modules/tune/panel/`, `design/panel-studies-round-7.html` |
| Tests | `HybridTests.cpp`, `LpcTests.cpp`, `ModeTests.cpp`; Studio cases in `CoreTests.cpp`; `bmo-tune-latency`'s Studio table |

## HYBRID, in one page

**What it is.** Pitch-synchronous overlap-add. Grains two analysis periods
long, Hann-windowed, taken one *detected* period apart and overlap-added at
synthesis marks spaced by the *target* period. Output is normalised by the
summed window, so any spacing reconstructs at unity.

**The trick that made it real-time.** Spec §6.1 budgets a period of lookahead
for a pitch-mark detector. HYBRID does not need one: the analysis position
advances by exactly one detected period per grain, so neighbouring grains are
always one cycle apart, which is all "pitch-synchronous" requires -- absolute
phase never matters. So it ran on CLASSIC's latency (19 samples idle).

**Formants by grain rate, not LPC.** A repositioned grain keeps its spectral
envelope. Reading each grain at rate φ while the marks stay at the target
period moves the envelope by φ and leaves the pitch alone:

- Keep (was Formant Correct on): grains read at φ -- formants stay put.
- Follow: grains read at ρ·φ -- formants move with the pitch, like CLASSIC.
- Formant Shift sets φ.

**Measured (AURORA, 48 kHz).** Formant scale 1.000 / 1.001 across ±1
semitone (spec gate: 2 %). Tuning within 0.04 c at retune 0. CPU 1.0 %
median, 1.4 % p99 at 48 kHz / 128. Glide, Formant and Shift each audible on
HYBRID (worst-sample differences 0.646, 0.079, 0.296) and bit-exactly inert
on CLASSIC.

**Why it lost, in real time.** It was Frosty's ear, not a number: CLASSIC
sounded better. A guess worth testing, not a finding: at real-time latency
HYBRID's grains come from a period that has only just arrived, so its
analysis marks are no better than the detector's hop-to-hop estimate, and
PSOLA is only as clean as its marks. With lookahead, that stops being true.

## The LPC stage (spec §6.2), and why it is not in either path

Built and tested (`Lpc.h`, `LpcTests.cpp`): Levinson-Durbin with
conditioning, LSF by Chebyshev/Clenshaw root-finding, LSF interpolation, and
a formant warp that replaced plain LSF scaling (plain scaling pinned the top
LSFs against π and the synthesis filter's guard kept resetting).

It failed in the signal path for two reasons, both measured:

- Order 24 at 48 kHz spent its poles modelling the empty band up to Nyquist
  and modelled no formants at all.
- A 16 kHz envelope mapped up to the host rate through its LSFs was too
  ill-conditioned (coefficients ~1e5) to survive grain interpolation.

What would work: **warped LPC** (allpass delays, λ ≈ 0.7 at 48 kHz), or poles
from a 16 kHz envelope mapped into a **cascade of biquads**. Measure against
PSOLA alone first -- the case for LPC is formant accuracy on *large* shifts,
and nothing at tuner-sized corrections showed PSOLA lacking.

## Studio latency

A fixed delay reported to the host as PDC, the same for both engines so a
switch never moved it:

```
Studio = kFloor + ceil (T_longest x (0.5 + guard))     kFloor = 17 samples
guard  = max (CLASSIC 0.5 (ρ - 1), HYBRID 1 - 1/ρ)     at ρ = +400 c: 0.206
```

The read then stays within half a period either side of the reported figure
while correcting. Measured = reported in every cell of `bmo-tune-latency`'s
table:

| Range | Auto | Soprano | Alto/Tenor | Bass | Instrument |
|---|---|---|---|---|---|
| Studio, 48 kHz | 9.19 ms | 4.77 | 7.44 | 13.21 | 13.21 |

`bmo-tune-hostcheck` confirmed the VST3 reported 441 samples (Auto) to a
host, delivered the way a host gets it (parameter in a process call, then
`restartComponent`).

## What a non-real-time plugin could do that this one could not

These are directions, not decisions -- none has been built or measured.

- **Non-causal pitch tracking.** The detector here must decide on the past
  alone; offline, pitch candidates can be chosen over the whole phrase
  (Viterbi over the NSDF candidates, as pYIN does). Most of this plugin's
  guards -- jump confirmation, the stability gate, the voicing attack --
  exist only because it cannot look ahead.
- **Real pitch marks.** Epoch (glottal closure) detection for PSOLA marks
  instead of one-period-per-grain. This is where HYBRID should get cleaner.
- **Note segmentation and editing.** Once there is lookahead, notes can be
  found and shown -- the graphical-editing mode every offline tuner has.
  In a DAW that usually means ARA 2; note that Ableton Live does not host
  ARA as of this writing, so check that before designing around it.
- **Better formants.** Warped LPC or a cepstral envelope, per the LPC notes
  above, now that latency is free.
- **Glide as a note transition**, not just a slew: with the next note
  known, the transition can start before it.

## What carries over unchanged

The detector (`Detector`, `DifferenceKernel`), the correction law minus its
real-time guards, the scale and key handling (17 spellings), `ClassicEngine`,
the sinc bank, and the whole offline harness: `bmo-tune-gen` (72-item
synthetic corpus with ground truth), `bmo-tune-score`, the offline ruler in
`tools/tune/common/Analysis.h`, `bmo-tune-bench`, `bmo-tune-snapshot` and
`bmo-tune-hostcheck`. The measurement discipline too: tests measure with the
independent ruler, never with the plugin's own detector.

## Identity

A new product needs its own plugin code and bundle id -- `Btun` and
`com.lt3audio.bmotunert` belong to BMO Tune RT and are permanent. Allocate
in the rack's `products/AGENTS.md` table, with Kevin's say.
