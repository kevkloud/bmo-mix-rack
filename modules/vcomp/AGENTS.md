# modules/vcomp -- LTV Comp

The vocal compressor. Two knobs and a gate handle on the face, five more knobs
behind a switch, and a modern feedforward detector under all of it.

Frosty set the brief on 2026-09-13: **the sound of Waves RVox and RComp, the
simplicity of RVox and Klanghelm DC1A**, with attack, release and a sidechain
filter behind a "complex mode" and nothing but AMOUNT and MAKEUP before that.
The gate, the band controls and the three-bar meter came in the same session,
after the first build was heard about. Everything below follows from that brief
pulling against itself.

## What this module is not

It is not BMO Opto with different numbers, and the two should not be merged.
Opto models two pieces of hardware and inherits their behaviour -- a feedback
cell whose delivered ratio wanders with programme level, a release that
remembers how long the cell has been loaded, no makeup at all because neither
unit has one. Those are the *point* of Opto. Every one of them is the opposite
of what this module is for. If a change here starts making Vcomp more
characterful and less predictable, it belongs in Opto.

## The decisions the code cannot tell you

### AMOUNT moves three things, and the ratio sweep is what makes zero inert

`curveFor` sweeps threshold (-6 to -40 dBFS), knee (12 to 6 dB) and ratio (1:1
to 8:1) together. The ratio starting at 1:1 is not decoration: a 1:1 ratio is a
slope of zero, so at AMOUNT 0 the module is a wire at *every* level, not merely
quiet.

The first build did that job with the threshold instead -- ratio 2:1 at the
bottom, the knee's lower edge parked at 0 dBFS so nothing below full scale was
touched. Also inert, and it cost the bottom third of the knob: at AMOUNT 20 the
knee had only reached -8.6 dBFS, so a vocal tracked at a sensible -10 got
nothing at all until the knob was a quarter round. `testAmountGrabsHarder` is
what caught it.

`measure_vcomp curve` prints the whole sweep. That table is the first place to
look when the module feels wrong at one end of the knob.

### The makeup reference is a peak figure, and getting it wrong is silent

`autoMakeupDb` adds back the reduction the curve applies at `kReferenceDb`, so
AMOUNT buys density rather than level. The reference has to be the level the
**peak detector** will see, which on a vocal is 12-15 dB above where the meter
sits.

It was first set to -10 dBFS by reading "where a vocal sits" off an RMS meter.
Nothing broke; every factory preset simply came out below the level it went in,
worst at the *bottom* of the knob where the makeup is smallest but the peaks are
reduced just as hard. The suite's `voice()` source is normalised to -18 dBFS RMS
and peaks at -3.6 (a 14.4 dB crest), so the makeup was compensating for a signal
6 dB quieter than the detector was hearing. At -7 dBFS every factory preset
lands inside 2.5 dB.

This is the failure mode to watch for in any revoicing: it does not announce
itself, and only `VcompTests`' level-matching check holds it.

### ARC's slow branch is programme-dependent because of its *attack*

The one piece of arithmetic most likely to be "simplified" into something that
does nothing. Two release branches with different time constants, combined with
`max()`, is just the slower of the two -- at every sample, for any input,
because a slower one-pole fed the same signal is never below a faster one.
There is no programme dependence in it whatsoever.

What makes it work is that the slow branch **charges slowly**
(`kArcChargeScale` x RELEASE). A consonant barely moves it, so it has nothing
to release slowly; a sustained loud phrase charges it most of the way, and then
it is the branch that decides the recovery.

`testArcIsProgrammeDependent` compares recovery after a 30 ms hit with recovery
after a 3 s hit at the same level, and checks that the ARC-off control case
recovers identically after both. A weaker test -- "the release is slow" -- would
pass the broken version.

### The gate is an expander, and it is first for a reason

It exists because of the auto makeup, which is indiscriminate: at AMOUNT 80 the
module adds about 26 dB to the voice and to the room tone, headphone bleed and
mic noise between lines alike. Cleaning that up is the other half of making a
one-knob compressor usable, which is why RVox ships the same pairing.

- **Ahead of the compressor**, because what it closes has to be closed before
  the makeup amplifies it.
- **Keyed off the raw input**, so the threshold is an absolute level the user
  can set against the track rather than one that moves when AMOUNT does.
- **An expander with a floor**, not a hard gate: a hard gate chatters on
  breaths and bites the tails off words, and both are audible on a voice in a
  way they are not on a tom.
- **Fast to open (3 ms), slow to close (150 ms) with a 40 ms hold, 6:1 into a
  60 dB floor.** Opening is the direction that costs a syllable if it is
  wrong. `measure_vcomp gate` prints the first 20 ms of the phrase separately
  for exactly that reason; at every threshold in the table the onset moves by
  at most 0.06 dB. It opened in 0.5 ms at 3:1 into 50 dB until the 2026-09-14
  ear pass; going deeper made that open *click*, because the gate suddenly had
  58 dB to open from rather than 16, so the ramp went to 3 ms (peak slew
  113 -> 19 dB/ms, `measure_vcomp gateopen`).

### LOW THRU and HIGH THRU are not the sidechain filter

SIDECHAIN changes what the compressor **listens to**. LOW THRU and HIGH THRU
change what it **acts on**: those bands are split off the audio, pass through
uncompressed, and are added back. Keeping both is deliberate -- the detector
reads the full gated signal through SIDECHAIN, *not* the mid band, so moving
LOW THRU changes which parts of the signal the gain is applied to and does not
change how hard the compressor works.

Two things about the crossover are worth knowing before touching it:

- **The low band goes through the second split's allpass.** Splitting at LOW
  THRU and then splitting only the remainder at HIGH THRU leaves the low band
  having been through one crossover and the other two through two, so they stop
  summing flat -- a dip around the upper crossover that moves when HIGH THRU
  moves. `testBandsReconstruct` holds it, and `measure_vcomp bands` prints the
  reconstruction error across the spectrum (0.00 dB everywhere, today).
- **Measure the bands as modulation, not as level.** The obvious test -- "the
  low tone comes out louder with LOW THRU up" -- is wrong, and looks right
  until you do the arithmetic: the makeup gives back almost exactly what the
  curve took at the reference, so a steady tone near that level comes out at
  the same place either way. The first version of that test asserted a 6 dB
  difference and found 0.9. What LOW THRU actually buys is that the low end
  stops being *pumped* by whatever else triggers the compressor.

### The filter cutoff clamp is a real control range, not a safety detail

A cutoff cannot be placed at Nyquist, so every filter here clamps. The clamp
started at 0.45 of Nyquist, which is 10.8 kHz at 48 kHz -- and HIGH THRU's range
runs to 20 kHz. The result was that engaging LOW THRU put the *upper* split at
10.8 kHz while the panel read 20 kHz, and everything above 10.8 kHz was quietly
handed to the thru band and stopped being compressed.

Nothing sounded broken. `measure_vcomp bands` is what caught it: 4.7 dB of
pumping on a 12 kHz tone where the un-split case had 11.5, sitting in a table
next to three figures that were right. Fixed two ways -- each side of the split
is now engaged independently, and the clamp is 0.98 of Nyquist so the whole
range is reachable at 44.1 and 48 kHz. `testOneSideEngagedLeavesTheOtherAlone`
is the test that was missing.

### The limiter is instantaneous because it refuses to spend latency

Built 2026-09-14, after the ear pass. It exists because the makeup clips: at
the top of AMOUNT it adds nearly 29 dB, and `measure_vcomp presets` had two
factory presets peaking above 0 dBFS on a source whose RMS was -18.

**Zero latency was treated as non-negotiable**, which decides the topology. A
brickwall limiter looks ahead so it can start reducing before the transient
lands; that costs latency and would take away the property the whole module is
built around. So the attack is instantaneous instead -- each sample's gain is
computed from that same sample -- which guarantees the ceiling with no delay
and pays in distortion rather than in overshoot.

Three consequences worth knowing before touching it:

- **The knee has to be narrow.** A knee K wide and centred on the ceiling
  starts pulling down K/2 below it. At 3 dB that reached -1.6 dBFS and broke
  the module's wire claim at AMOUNT 0, which `testAmountZeroIsInert` caught on
  a -1 dBFS tone. It is 1 dB now, and the claim reads "a wire for anything not
  already at the edge of full scale".
- **The colour is odd-order only.** Measured with `measure_vcomp colour`: THD
  runs 0.015% at the threshold to 0.8% driven 18 dB in, and the **second
  harmonic is at -180 dB, i.e. absent**. The gain stage is symmetric -- it acts
  on `|peak|` -- so it cannot generate even harmonics. That means what colour
  there is reads as edge rather than warmth. Adding warmth would mean
  deliberate asymmetry, which is a product decision and has to be weighed
  against BMO Saturator already existing for that job.
- **Every test that compares two renders must stay off the ceiling.** The
  limiter is last and pins whatever reaches it to the same level, so two
  different settings measure identical and the check goes quietly vacuous.
  Eight tests carry an OUTPUT trim for exactly this reason; do not remove them
  because "the level does not matter here".

It has no parameters, like RVox's. **How RVox implements theirs is unknown** --
Waves do not publish it and nothing here is modelled on it beyond the chain
order.

### Three bars, not a needle

BMO Opto's `DynamicsMeter` is a period instrument -- VU ballistics on a 1940s
scale -- which is right for a module modelling an LA-2A and wrong for this one.
It also shows one reading at a time behind a three-way switch, and the three
readings a compressor user wants are wanted together. IN and OUT read left to
right in dBFS; GR reads right to left from zero, the way every gain-reduction
meter has ever read.

`LevelBar` is **module-local on purpose** and should move to `core/ui` the
moment a second module wants one -- the same rule `modules/AGENTS.md` applies to
`DynamicsMeter`'s scale. GR is painted in `meterGr` rather than the low/high/
clip zones: a compressor working hard is not a compressor in trouble, and 24 dB
of reduction in the clip red would be the meter telling the user off for using
the module.

## The layout test did not look at this panel

`ui_layout` passed while "LOW THRU" and "HIGH THRU" rendered as "LOW THR" and
"HIGH TH" -- `Graphics::drawText` curtails what will not fit rather than
spilling it, so a caption wider than its control loses its tail with nothing
said.

**This was first written up here as a fault in `PlainKnob::captionOverflow`,
and that was wrong.** Measured directly, it reports 10.7 px and 13.3 px of
overflow for those two strings in an 80 px column -- correct, and it would have
failed the build. The actual fault was that **LTV Comp (then BMO Vcomp) was never added to
`tests/ui/LayoutTests.cpp`'s product list**, so nothing ever laid this panel
out. `modules/AGENTS.md` names that file as one of the shared files a new
module must edit and predicts this exact failure mode: "unchecked, silent".

Two things came out of fixing it:

- The knobs get half the panel each, which fits both captions.
- That list was addressed by **hard-coded index** -- `all[5]` and `all[6]` were
  BMO DEQ. Adding a row in the middle silently re-pointed every one of those,
  so DEQ's width assertions started running against Vcomp and failing while
  talking about "deq". It looks up by name now, because a list that every new
  module is told to edit has to survive being edited anywhere but the end.

The lesson worth carrying: a green suite after adding a module is not evidence
the module was tested. Check the module's name actually appears in the output.

## The THRU bands take the gain the curve would have given them

**Settled on 2026-09-14 by measurement, on AURORA; the sound of it has not
been heard yet.** This section used to be titled "the THRU bands run away,
and there are three ways out". It is kept here because the arithmetic is the
part nobody can read off the code.

The spec is that everything outside LOW/HIGH THRU is uncompressed and
*everything* takes the makeup. The second half of that has a consequence that
is not a voicing choice: a band that is not compressed but is given the full
makeup can only get louder, by the whole makeup figure. Taking the cap out of
`DspCore.h` and running `vcomp_dsp_tests` prints it exactly — the thru band's
lift comes out 12.44, 18.92 and 25.51 dB at AMOUNT 50, 70 and 90, which is the
makeup column of `measure_vcomp curve` to the second decimal.

So LOW THRU worked at the bottom of the knob and defeated itself at the top,
and it cost a preset: "Keep The Chest" was held down at AMOUNT 35 because at
70 it came out **+8.14 dB** against a level-matching tolerance of 3.

**The fix is the 0.2.4 review's**: the thru path takes the makeup *less the
reduction the curve applies at body level* — `thruMakeupDbFor` in `DspCore.h`,
body level being `kReferenceDb - kThruBodyOffsetDb`. That is the net gain the
thru content would have come out with had it been compressed along with
everything else, so the band is neither pumped nor lifted past the rest of the
voice. The curve works the figure out for itself at every AMOUNT; there is no
number anybody picked. `testThruMakeupCannotRunAway` holds it.

All four candidates were built and measured rather than argued about. Tilt is
`measure_vcomp balance`, pumping is the 80 Hz thru band under a 2 kHz burst
from `measure_vcomp bands`, and both want to be near zero:

| candidate | tilt 30/50/70/90 | pumping | Chest at 70 |
|---|---|---|---|
| shipped, no cap | 4.6 / 8.8 / 12.9 / 17.7 | -1.5 | +8.14 |
| 1. half compression on the thru band | 2.2 / 4.3 / 6.8 / 9.4 | -4.3 | |
| 3'. thru gain tracked slowly (600 ms) | 2.2 / 3.8 / 5.4 / 6.9 | -1.5, late | |
| 2. a flat 6 dB cap | 4.3 / 2.9 / 1.9 / 1.3 | -0.0 | +1.96 |
| **2'. the curve's own figure, shipped here** | **2.4 / 1.7 / 1.1 / 0.6** | **-0.0** | **+1.36** |

Why the other three lost, since all three were plausible on paper:

- **Partial compression** (Frosty's suggestion, and the one that keeps a single
  mental model) scales the tilt without bounding it — at half compression
  AMOUNT 90 still tilts 9.4 dB — and it buys that by putting the compressor's
  modulation back onto the band that exists to escape it. A fraction high
  enough to bound the tilt is a fraction high enough that there is no thru
  feature left.
- **Tracking the compressor's gain slowly** was the tempting one: it should
  remove the fast pumping and keep the slow balance. It half works, and it adds
  a fault the others do not have. The thru band's gain arrives late, so the
  reduction lands in the quiet *after* a loud phrase rather than during it —
  visible in the bands report as the pumping figure going *positive* (+1.6 dB
  at a 1 s time constant, +3.5 at 2 s) because the post-burst reference is the
  part that got ducked. A low end that dips after the singer stops is a worse
  artefact than the one being removed.
- **A flat cap** works, and was built as the control. It bounds the tilt but
  does not flatten it, and being a number rather than a consequence it has to
  be argued about: 6 dB lets "Keep The Chest" back up to 70, 9 dB holds a
  flatter tilt but does not. The curve-derived figure beats it on both counts
  and settles the argument.

Two things this changed that are worth knowing:

- **Pumping got better, not just no worse**: -1.5 dB to -0.03. The residual on
  the shipped build was not the crossover leaking; it was the thru band, lifted
  by the full makeup, driving the limiter, which then rode the burst. With the
  thru band held at body level it no longer reaches the limiter at all. The
  `bands` report at AMOUNT 80 has been measuring the limiter as well as the
  split ever since the limiter shipped, which is worth remembering before
  reading small figures out of it.
- **The presets can come back up.** "Keep The Chest" at AMOUNT 70 measures
  +1.36 dB against +8.14 without, and at its current 35 it goes +3.37 to +1.74.
  Both THRU presets are still written at 35 here, because raising them changes
  what a preset sounds like and that is Frosty's call after the ear pass, not a
  measurement's.

### Still open on the thru path

- **Nobody has heard it.** The narrow listening question is in
  `testing-notes/vcomp-thru-cap-2026-09-14.md`.
- **`kThruBodyOffsetDb` is 6 dB, and that one *is* a number somebody picked.**
  It says how far under the peaks the detector reads the chest and the air
  actually sit. Larger means more thru lift, smaller means less.
- **The band-split crossfade.** Unchanged by this: engaging the split still
  switches the crossover in rather than fading it.
- **A separate limiter on the thru path** was the third candidate and is not
  needed now. It only bit near full scale, and the thru band no longer gets
  anywhere near it.

## What waits on an ear

Heard once, on AURORA on 2026-09-14, in Ableton on real material: "flying
colors", with five changes asked for and made the same day (the makeup on the
whole sum, ARC bolder, the gate deeper and slower to open, and the limiter).
`testing-notes/vcomp-handoff.md` has the table. `tools/measure/vcomp` exists
for the next pass: `curve`, `presets`, `gate`, `gateopen`, `bands`, `balance`,
`arc` and `colour` each print a table and write WAVs of the same render, so a
number and a listen are never of different things, and `gen` exports the
harness's own signals so the same file can be fed through RVox or RComp for
comparison.

Still open, having been heard once:

- **The curve's three sweeps** are round numbers at a shape, not tuned figures.
- **The eight factory presets** are AMOUNT positions with names on them.
- **`kArcFastScale` / `kArcChargeScale` / `kArcSlowScale`** (0.35 / 1.2 / 10 x
  RELEASE since the ear pass; they were 0.35 / 2 / 5 and could not be heard).
  The charge scale decides how much material counts as "sustained" and is the
  first knob to turn if ARC feels wrong.
- **`kStandardAttackMs` = 5** is the single number standard mode's whole feel
  rests on, since a user in standard mode cannot change it.
- **The gate's `kGateRatio` = 6:1 and `kGateRangeDb` = 60** (3:1 into 50 until
  the ear pass, which was too polite: at GATE -40 it shut by 16 dB, now 41).
- **Output peaks** were the argument for the limiter, and it exists now.
  Before it, `measure_vcomp presets` had "In Front" and "Keep The Chest"
  peaking above 0 dBFS on a -18 dBFS RMS source. What is left of that item is
  that the limiter's own reduction is not metered, so OUT pinned at -0.1 with a
  quiet GR bar is what pushing MAKEUP looks like.

## Open, and deliberately not built

- **No lookahead, and therefore no latency at any setting.** The one thing a
  Pro-C-class compressor has that this does not, and what lets the module sit
  on a vocal while the singer is listening to it. Adding it later changes
  `latencyForParams` and the module's place in a tracking chain, so it is a
  product decision rather than a feature to slip in. The band split does not
  change this: the crossover is IIR, so it costs phase rather than samples.
  Neither does the limiter -- see below.
- **No parallel MIX.** Neither RVox nor DC1A has one.
- **No stereo LINK switch.** Stereo is always linked, because two channels of
  one voice compressed independently is a wandering image rather than a stereo
  option. Opto has the switch because it is a general-purpose box that ends up
  across a mix.

Any of these is a parameter appended to `specs()` with a default that leaves
old sessions sounding the same -- see `modules/AGENTS.md`, "Changing a module".

## Shared code this module moved

`core/dsp/GainComputer.h` is new, and it is BMO Opto's arithmetic: `Curve`, the
feedforward/feedback slope conversions and the Reiss & McPherson soft-knee gain
computer, which both modules need. Opto's `Detector.h` now pulls them in under
the same names it used before, so the rest of that file reads as it always did
and its numbers are unchanged -- `opto_dsp` passing is the check on that. The
derivation of why a feedback cell cannot state what it needs as a ratio is
Opto's finding and went with the code; it is why the shared type carries a
slope and not a ratio.

`tools/measure/Wav.h` is also new, lifted verbatim from
`tools/measure/opto/main.cpp`. The five older harnesses still hold their own
copies; move each one when it is next opened, and delete its copy then.
