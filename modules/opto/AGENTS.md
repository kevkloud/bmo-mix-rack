# modules/opto -- BMO Opto

An optical-style levelling compressor with two knobs: how hard the programme
drives the cell, and makeup. Module id `opto`, plugin code `Bopt`. Read
`modules/AGENTS.md` first for what every module shares; this file is what the
code here cannot tell you.

```
params.h                  five parameters, permanent: crush, level, mode, link, color
dsp/Detector.h            the two cells, the curves, the two drive stages, the DC blocker
dsp/DspCore.h             the signal path, the three switches and their crossfades
dsp/OptoDsp.h             the ModuleDsp wrapper
panel/OptoPanel.*         two knobs, a needle meter (IN / OUT / GR), three switches
presets/FactoryPresets.h  Init and three presets, level-matched
```

The long comments in `Detector.h` and `DspCore.h` are the design record and
are current. `testing-notes/opto-0.2.1-handoff.md` and
`testing-notes/opto-attack-2026-09-14.md` are the two rounds of measurement
behind them. Start there before changing a constant.

## What the module is built on

**Two modes are two circuits, not one curve with two sets of numbers.** Tele
is a feedback cell: its detector reads its own output, which is what makes it
self-limiting and what makes its 10 ms attack act as a third of that. Stressed
is feedforward at 10:1: its detector reads the input and gets exactly the
constants it is given. They share a threshold sweep so that CRUSH means the
same thing in both, and nothing else.

**A feedback cell cannot be handed a ratio.** It needs a slope, and the slope
for 3:1 through a loop is 2, not 0.667. Handing it the feedforward figure
delivered 1.67:1 for the whole of 0.1.x while every test that compared the two
modes to each other passed. `core/dsp/GainComputer.h` carries both conversions
now; `testDeliveredRatioMatchesTheSpec` measures the ratio instead of trusting
it.

**The release is programme-dependent, and the charge is what makes it so.**
Each cell keeps a slow follower of its own reduction (0.3 s to count a hit)
and the release slides from 60 ms towards seconds as that charge grows. Tele
adds a dosage clock on top, so a long hit releases more slowly than a short
one at the same depth. The ceilings (4 s and 3 s) and the charge's forget time
were fitted against renders, not chosen; the 0.2.1 handoff has the fit.

**CRUSH moves the threshold only.** Ratio and knee are fixed per mode. LEVEL is
a hand-set makeup and there is no automatic one: one was proposed in 0.2.0 and
refused, and `params.h` says why.

**A spike is not programme, and the cells know the difference.** With 15 to
20 dB already standing, the charge sits at its ceiling, and a release read
off the charge alone gave everything a spike added the slowest release the
cell has: the programme stayed turned down for 3.5 s (Tele) and 7.2 s
(Stressed) behind an 18 dB spike of 20 ms. Three rules in `Detector.h` fix
that, and each has a reason a later change could undo by accident:

- Reduction standing above the charge releases at the fast 60 ms rate, but
  only once the envelope stands clear of what the signal has reached in the
  last 30 ms. Without that condition the envelope sags between the crests of
  a held note, and Stressed took 520 ms to settle on a step instead of 56.
- The charge counts reduction only up to what the level the signal has *kept
  up for 60 ms* would earn. Counting the reduction itself let ten spikes a
  second apart walk the level down 8 to 12 dB.
- A rule that switched the charge off whenever nothing was pushing the
  envelope was tried first and dropped before it was committed: on notes
  that decay nothing pushes the envelope, so the charge never built, and the
  cell turned into a fast compressor on anything percussive.

What this costs is on the record in `testing-notes/opto-spike-and-dip-2026-10-03.md`:
programme made of short decaying notes holds less reduction than it did.

**Stressed has a two-stage attack and Tele does not.** Within 6 dB of what is
asked the attack is 10 ms in both. Stressed quickens to 0.5 ms by the time it
is 12 dB short. That stage was built for both cells and heard blind in both;
see below for why Tele does not have it.

**Latency is zero and stays zero.** No lookahead, no oversampling. That is why
the first crest of any onset always gets some of the way out.

## The switches

Mode, Link and Color each used to change the path in one sample, and each
measured as a step. The rule in this repository is that a step which measures
gets a fade, heard or not. All three are now 10 ms straight-line crossfades
from `core/dsp/SwitchFade.h`. What makes that work is worth knowing before
touching `DspCore`:

- **Both cells listen all the time.** The one Mode is not using is not frozen.
  A frozen cell is a memory that has stopped being written: leave a mode just
  after a loud passage, come back three seconds later, and it returned still
  holding that passage, 11 dB (Tele) and 7 dB (Stressed) low for more than
  three seconds. Do not "optimise" the second cell away.
- **Tele's drive stage runs all the time too**, because its DC blocker has a
  memory like the cells do. Stressed's drive has none and only runs where it
  can be heard.
- **Link has its own pair of cells**, apart from the two channels' own. Only
  the pair in use runs. When Link starts to move, the pair about to be heard is
  given the state of the pair that was, so unlinking does not move either
  channel's reduction at the switch.
- **The first block after `prepare()` or `reset()` takes the switches as they
  stand**, with no crossfade. Without that, a host that sets parameters after
  preparing would render a fade nobody asked for.
- **Idle is bit-exact.** With nothing switching, the output is the same bits
  it was before the crossfades existed. A change here that breaks that has
  changed the sound of every existing session, and the render hashes will say
  so.

`tests/dsp/OptoSwitchTests.cpp` holds all of this.

## Things that were tried and refused

- **A uniformly faster attack** (September 2026). Two candidates, three blind
  rounds, with a duplicated file in each round as the noise floor. Neither
  could be told from the shipped build. The 10 ms attack stayed. The lesson
  that note records is the method: enter one variant twice, or a small
  imagined difference looks like a small real one.
- **The quick attack in Tele** (October 2026). Heard blind with each build
  entered twice: in Stressed both copies with it were ranked above both
  without, in Tele both copies with it were ranked below both without. Tele
  lets an 18 dB spike through by 7.8 dB and that is the accepted figure;
  `testASpikeIsCaught` holds it so that it is a choice if it ever moves.
- **An automatic makeup.** Refused in 0.2.0; `params.h`.
- **A lift around 2 to 2.4 kHz.** Asked for, measured, found not to be
  missing. The release bug was leaving reduction standing at phrase onsets,
  which read as lost presence; fixing the release brought it back. No EQ was
  added.
- **Widening the reduction meter past 24 dB.** `ui::DynamicsMeter` is shared,
  its scale is hand-placed, and it pins at 24 while the cells reach 27 to
  36 dB at the deepest setting. That was accepted when BMO FET was designed
  (`modules/fetcomp/AGENTS.md`): the needle says "a lot" and
  `currentGainReductionDb()` still reports the true figure. It was raised
  again in the October 2026 review and decided again on 2026-10-03: it stays
  pinned. Changing it would move every panel that uses the meter.

## Faults the tests could not see

Each of these passed the suite as it stood. They are why the tests here are
absolute rather than relative, and at the level a track arrives at.

- **Tele delivered 1.67:1** while the test only asked whether Stressed reduced
  more than Tele.
- **Both modes failed to release**, and the test compared their retained
  *fractions*: Stressed held 26.7 dB ten seconds into silence and passed,
  because Tele held 9.2.
- **Tele's drive made no even harmonics.** A sign factor made the whole stage
  odd; the test only asked whether Color on differed from Color off.
- **The DC blocker's corner rose with the sample rate**, 35 Hz at 44.1 kHz and
  153 Hz at 192 kHz, on a stage Tele runs unconditionally.
  `testReductionIsTheSameAtEverySampleRate` now holds the cells to one answer
  at four rates.
- **Every figure was once read at -6 dBFS**, which is a mix-bus level. The
  programme level in these tests is -18 dBFS RMS, and a result measured hotter
  than that says nothing about a track.

## Open

- **A longer loud hit still leaves the level down, by design.** A 100 ms
  passage 18 dB hotter is long enough to count toward the charge, and ten of
  them a second apart still sink the level 5 dB (Tele) and 10 dB (Stressed).
  That is the charge's own 0.3 s and the slow release at work. It was
  measured, reported and left alone in October 2026.
- **Preset levels are back-solved, not ear-tuned**, and move with any change
  to how much reduction a preset holds. They were re-solved on 2026-10-03
  for the new release. `BMO_PRINT_PRESET_LEVELS` on `opto_tests` prints every
  delta, pass or fail. Ask before re-solving them.
- **The Mode labels and the two mode buttons** stay as they are for 0.2.6 and
  are due a change after it. It is display only: the parameter is a choice
  saved by index under `mode`, so new labels do not break a session as long as
  the number and order of choices hold.
- **One bad sample latches a channel** until reset. That is being fixed once,
  in the shared processor, for every module; do not fix it here.

## Measuring

`build/tools/measure_opto` drives `DspCore` directly: `release` prints the
release report, `render` puts a WAV through at any setting. Render **outside
the repository**. Its default output folder is not in `.gitignore`, and a
render tool's first run on a branch is where audio has been committed twice.
