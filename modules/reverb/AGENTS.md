# modules/reverb — BMO Linger

The reverb. **It stays after the note has gone.**

Module id `reverb`, display name **BMO Linger**. The two differ on purpose: the
id says what the module *is*, to anyone reading the tree, a preset extension or
a test name, and it lives in state files and rack presets where it can never
change; the name is what the plugin is *called*, and is free to be evocative.
`deesser`/"BMO Defang" and `fetcomp`/"BMO FET" are the same arrangement. Do not
tidy one to match the other later — that breaks every saved session.

The spec is `docs/reverb/`. Read `11-integration-and-test-plan.md` section 4
first: it carries the schema table, which is the authoritative copy.
`10-dsp-spec.md` section 6 lists the same thirty names in the same order, and
the two were reconciled before `params.h` was written.

**The DSP is a marked placeholder.** `dsp/DspCore.h` passes audio through
untouched, produces no tail, and reports zero latency — which, unlike the
silence, is the *shipped* figure and not a stand-in. What is real today is the
schema, the panel, the display, the registration and the latency contract. The
DSP pass owns `dsp/` and nothing outside it, with one exception named below.

## What this reverb is

**Reference B's tail character with Reference A's control structure.** Two
generators — an image-source early-reflection cluster and an 8-line FDN late
network — with **two absolute faders** rather than one wet control, so either
can be switched off on its own. Pre-delay is tail-only. A Density knob bridges
discrete positional taps and dense shaped early energy with no allpass at
either end, and SOURCE decides how much of the ER feeds the tail.

**No recursive allpass anywhere in the ER path**, and that is the owner's
hardest constraint rather than a simplification: allpasses bolted on behind
taps to raise density is what goes metallic on vocals and drums. The density
stage is feed-forward — parallel short delays recombined through a 4×4
orthogonal butterfly — so it has no poles and cannot ring.

## Thirty parameters, and two spare lanes

`type`, `size`, `predelay`, `prelink`, `decay`, `decayshape`, `attack`, `feed`,
four damping, four EQ, `ermode`, `erdensity`, `ershape`, `erspread`, `erhicut`,
`ervariation`, `moddepth`, `modrate`, `width`, `inhicut`, `erlevel`,
`verblevel`, `mix`, `output`. Permanent and append-only from the first ship.

**A rack slot shows a host 32 lanes.** Thirty fits, so nothing is held off the
grid and none of BMO DEQ's `SlotOverflow` machinery is needed — and **two lanes
is all that is left**. A later Freeze, a ducking control, or the tempo-sync
pair `syncon`/`syncdiv` would exhaust them between them. A thirty-first
parameter has to be argued for here and against that count, not added because
it fits; `tests/plugin/ReverbTests.cpp` asserts the headroom so the argument
cannot be skipped.

There is **no `voicing` parameter**. Reference B's three colour eras are a
signature of its sound, but era × type multiplies the tuning surface by three
before one type has been voiced, and the grit itself is described nowhere
public — so each type's constant block *reserves three era fields* instead.
Because they already exist as constants, promoting them to a 3-position control
in v2 changes no type ordinals and no state layout.

`inhicut` is marked **owner confirm**. `10-dsp-spec.md` reads as 29 parameters
plus an internal constant while its own list reads 30, and the input high-cut
is the one it disagrees with itself about. It is kept because section 2 gives
it a 2–20 kHz user range a constant would not need, and because it is the only
way to darken what feeds *both* generators independently of the Reverb EQ
shelves. **Deleting index 25 before first ship is free and returns a third
spare lane; after that it is permanent.**

## Room's defaults are Room's constants, by definition

The defaults for **`size`, `erdensity`, `ershape`, `erspread`, `moddepth`,
`modrate`, `inhicut` and `feed` are Room's per-type constants**, not merely the
values the knobs happen to open at. A fresh instance opens on TYPE = Room, so
what it shows for those eight is a claim about what Room *is* — and if the DSP
pass later picks different Room constants, the panel lies about itself on the
first thing anyone sees.

The per-type tables do not exist yet. `10-dsp-spec.md` section 1 names the
categories a type's constant block holds — ER tap table, ER window and default
density, the eight FDN times, input-diffusion depth, damping and modulation
defaults, input bandwidth, default ER feed, three reserved era fields — and
gives no numbers for any of them. **When they are written, Room's row is pinned
to these eight values** and the other five types are free. They live as named
constants in `params.h` (`roomDefaults`) so the table can be checked against
the same symbols the schema was built from rather than against a second
transcription, and `tests/plugin/ReverbTests.cpp` holds them to it.

## The type list is append-only for state and lossy for automation

Both facts matter and only one of them is safe.

A **session or a preset** stores plain real values keyed by parameter id
(`ParamSet::toXml`), so a seventh type appended to the end changes nothing a
saved file refers to. **Recorded automation is not stored that way**:
`juce::AudioParameterChoice` normalises as index/(n−1), so going from six types
to seven rescales every automation point ever written on that lane. Room stays
at 0.0, but Ambience moves from 1.0 to 0.833 and lands on Plate. Nothing errors
and nothing warns.

So appending Church, Shaped Hall, Pattern Room, Positional Room or Vintage
Room is legal and lossy, and that is a trade to make knowingly. The same
applies to `ermode`. `ervariation` is deliberately **not** a choice list — its
seven positions are an ordered amount of decorrelation rather than seven named
behaviours, so it is a stepped float, and a stepped float normalises as
(v − min)/(max − min), which an eighth position at the end would not disturb.

## ER Mode's Blend is defined but unheard

Taps is the image-source table. Energy replaces tap *times* with velvet noise
enveloped by ER SHAPE and ER SPREAD.

**Blend is a proposal awaiting a listening pass.** The behaviour: image-source
tap times and pans from Taps, with the Energy generator's Shape/Spread envelope
replacing the physical `(1/d)·β^n` gain law, energy-renormalised so the mode
change is not also a level change. That is a coherent third behaviour rather
than a crossfade between two generators — but nobody has listened to it. It
holds index 2 now because the index order freezes at first ship and there is no
way to insert it later, **not because it is settled**. The listening pass
(`11` section 6) is where it becomes real or becomes a synonym for one of its
neighbours.

Variation 6 is the other position that is not what it looks like: it is
Schroeder's complementary-comb pair, the widest setting *and* the only provably
uncoloured-in-mono one — and the ER vanish entirely in a mono sum there. Its
value string says so, because an automation lane has nowhere else to.

## The panel

**Main face:** the ER/tail display, TYPE, SIZE, PRE-DELAY, DECAY, ER, REVERB,
MIX. The two faders are the thesis, and the tail-off depth-placement technique
has to be reachable without expanding anything.

**Expanded**, three groups: **EARLY** (8), **TAIL** (6), **TONE & OUT** (9).
BMO DEQ's precedent throughout — 300 compact, 700 full, the switch on the
host's bar and never on the panel, and the panel choosing its layout from the
width it is given.

**The expanded controls are added and removed as children rather than hidden.**
A hidden component still has bounds, and `tests/ui/LayoutTests` walks every
child whether it is visible or not, so a hidden control with a stale or zeroed
rectangle either escapes the panel, overlaps something, or reports a caption
overflowing a box of width zero. Unparenting is the one state in which a
control is genuinely not part of the layout. Parameter attachments are kept
throughout, so nothing rebinds when the width changes.

The three group headings are painted by the panel rather than added through
`ModulePanel::addRule`, which draws edge to edge: at the expanded width a
shared rule would cut a line straight through the main-face column as well as
through the group it belongs to. `ReverbPanel::getGroupRules` is public so a
layout test can see where they landed.

**Captions are ASCII.** The damping controls read "LOW x" and "HIGH x" and
their values print "1.20x", not with a multiplication sign: the two display
faces are licensed individually and live outside this repository, so a glyph
outside ASCII is one this suite cannot promise it can draw.

## The display, and its one cross-folder dependency

A static time-domain sketch: the direct impulse, the ER taps at their times
with heights from their gains and their bearings split above and below the
centre line, the pre-delay gap, and the tail envelope with a band between its
fastest and slowest damped decay times.

**Parameter-driven only. No metering, no `AnalyserTap`, none of
`ModuleContext`'s meter callbacks.** Neither doc asks this module for a meter,
a reverb has no gain reduction to report, and the absence is a decision rather
than a gap to be filled in later.

**Time is logarithmic, 1 ms to 30 s**, and that is a deliberate departure from
`11` section 5's proposed 0–500 ms window. A fixed 0–500 ms window cannot show
a 20 s decay, and DECAY reaches 20 s with a 2.0× multiplier over it; a linear
window wide enough for the tail puts the whole early cluster inside the first
pixel, which destroys the one thing the picture is for. On a log axis 1–100 ms
keeps roughly half the width. The ends are chosen rather than round: 1 ms is
where a reflection stops fusing with the direct sound, and 30 s is
`DspCore::kMaxTailSeconds` — the ceiling on the tail this module will ever
report to a host — so the right-hand edge is the same number the host is told.
The direct sound at t = 0 is off a log axis entirely and is drawn hard against
the left edge; that is the only lie in the picture, and leaving it out would
make the pre-delay gap look like the beginning of the sound.

**`dsp/TapTables.h` is JUCE-free and panel-includable, and it has to stay that
way.** The panel and the engine read one tap table, so the picture cannot
quietly stop describing the sound — `11` section 5 names sketch/DSP drift as
the display's one real risk, and `tests/ui/LayoutTests.cpp` asserts that the
sketch's first tap time *is* the table's. That is the exception to "the DSP
pass owns `dsp/`": whoever writes the image-source generator owns the numbers
in that file, and owes the panel a header that still compiles without JUCE.

The table's numbers today are **placeholder geometry** and are marked as such.
None of `11` section 6's comb, spacing, level-ceiling or flamming rules is
claimed of them. When the real tables land, a failing table is **re-seeded, not
patched**, and the audits run *after* the jitter.

## What is not here yet, and where it goes

- **Tail reporting.** `DspCore::tailSecondsFor` is the figure and the formula,
  and **nothing calls it**: both processors hardcode `getTailLengthSeconds()`
  to 0.0 and `ModuleDsp` has no tail accessor. Adding
  `tailSecondsForParams` lands on every module's vtable, so it is `11` section
  2(a) and milestone M5, its own reviewed commit. The arithmetic lives here now
  so that commit has nothing left to decide. The rack will **sum** it over
  occupied slots, never max: slots are in series.
- **The engine.** `11` section 1 names the headers it grows —
  `ErGenerator.h`, `TapTables.h`, `Fdn.h`, `Absorbent.h`. Milestones M2–M4.
- **The test suite.** `tests/dsp/ReverbDspTests.cpp` asserts the frame; `11`
  section 6 is the list it grows into, and the one to read before writing the
  first line of engine. **Write the modal-density test before Plate is tuned
  and expect it red** — `10` section 4 records eight lines covering Plate to
  barely 1 s, and the fix is 16 lines, a larger mean delay, or accepting
  sparsity.
- **Nothing has been heard.** Not one setting.

## The build rules, which are not optional

**Name your targets. Never run an untargeted build. Never build a plugin
product target.** A Debug configure sets `COPY_PLUGIN_AFTER_BUILD` on every
plugin, so a bare `cmake --build` overwrites the plugins installed in the
system VST3 folder — which happened on 2026-09-20 and destroyed an in-progress
Ableton pass. Test targets, `snapshot` and `measure_*` are safe.

**Check the build exited 0 before believing any ctest count.** A failed compile
leaves the old executable and ctest prints a stale pass.

**No audio, no renders and no fonts in the tree.** Renders go to `snapshots/`
and `measure_reverb`'s WAVs to `packages/reverb-listening/`, both gitignored.
Twice a tool in this repository has written audio into the tree; read
`git status --short` before every `git add`.

Every recorded figure names the machine it was measured on — **AURORA** or
**ICE QUEEN** — plus configuration and host.
