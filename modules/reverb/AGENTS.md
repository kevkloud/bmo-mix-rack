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

> **The control set is under review and this list is not settled.** Frosty
> opened a trim on 2026-09-21 -- "I'm not sure all these controls will survive
> trim" -- and it has not concluded. Nothing has shipped, so cutting an index is
> still free. Do not read the list below as final, and do not build anything
> that would be expensive to unpick if a control goes.
>
> The asymmetry that should guide the trim, verified in the code rather than
> assumed: cutting a **float or bool is reversible**, because state is stored as
> plain values keyed by id (`ParamSet::toXml` writes `getReal(i)`), so
> re-appending one later costs nothing. Cutting or shortening a **choice is
> not**, because it reaches the host as `juce::AudioParameterChoice`, which
> normalises as index/(n-1): change the count and every recorded automation lane
> on it remaps. Be aggressive with floats, conservative with `type` and
> `ermode`.

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

## A type is a voicing, and it writes ten parameters

**Selecting a type re-applies that type's ten constants over the parameters
that hold them — `size`, `erdensity`, `ershape`, `erspread`, `moddepth`,
`modrate`, `inhicut`, `feed`, `erlevel` and `verblevel` — every time, not only
at instantiation.** A knob moved away from its type's value is stamped back the
next time that type is selected. Frosty took that knowingly on 2026-09-21: a
type that applied its block once and then let the knobs drift off it is a type
that tells you less about what you are hearing the longer you use it.

**`erlevel` and `verblevel` joined the block the same day**, taking it from
eight constants to ten. Without them Ambience was unbuildable as specified: the
pack describes it as "tiny tail, ER-dominant by default" while the two faders
were type-independent, so the one type whose whole character *is* the balance
between the two generators had nowhere to put it. Ambience is a sound Frosty
reaches for often, and he will bring references to the Ableton pass.

### The hazard: TYPE is automatable and now writes automatable parameters

Automate TYPE and REVERB together and **the two fight**. The type change stamps
a level at the moment the host is driving it somewhere else, and which one wins
depends on their relative order inside the block. There is no arbitration and
there should not be — an arbiter would have to decide that one of the user's two
automation lanes is not real. It is inherent in "a type is a voicing", it is the
cost of the decision rather than a defect in the implementation, and it is
written down here so the first person to hit it knows it was chosen.

What *is* guaranteed is that the conflict is bounded. A type change touches
those ten and nothing else: `predelay`, `decay`, `decayshape`, `attack`, the
four damping rows, the four EQ rows, `ermode`, `erhicut`, `ervariation`,
`width`, `mix` and `output` are the user's and stay put. Automating any of those
beside TYPE is safe, and `tests/plugin/ReverbTests.cpp` asserts it.

### One write path, and it is the preset recall's

`typeSettings` (params.h) hands back a `std::vector<Setting>` — the same type a
`FactoryPreset` carries — and `TypeVoicing` applies it with `ParamSet::apply`,
which is `setReal` per id, which is `setValueNotifyingHost`. That is the call
`PresetManager` makes to load a preset and the call `ParamSet::applyXml` makes
for every value in a session. **There is no second path**, so a type change
cannot come to disagree with a preset recall about what writing a parameter
means, and a host sees an event it already knows.

It also makes the two compose in the one order that works. `applyXml` resets to
defaults and then writes the file's values in spec order, and `type` is index 0
— so a restore applies the stored type's block first and then overwrites it with
what the file actually stored. **Every factory preset names `kType` first for
the same reason, and has to keep doing so**: a preset that set its type last
would stamp that type's constants over its own sizes and levels, and nothing
would say so.

### Why it cannot recurse, and why it is not on the audio thread

- **`type` is not in what a type writes.** `typeSettings` returns ten settings
  and `kType` is not among them, so applying a type cannot select one. The
  recursion is unconstructible rather than guarded, and both suites assert it
  directly.
- **An `applying` flag is the belt to those braces.** A host may write TYPE
  again from inside one of the ten notifications, and JUCE delivers a
  message-thread change synchronously, so a nested call is reachable even though
  a self-triggered one is not. A nested call returns at once, so the parameters
  never carry half of one type and half of another.
- **A remembered detent means only a real move applies.**
  `setValueNotifyingHost` notifies whether or not the value changed, so a preset
  recall, a state restore and `resetToDefaults` each write `type` at least once
  with nothing new in it.
- **The writes land on the message thread**, because `juce::ParameterAttachment`
  marshals a change arriving on any other one through an AsyncUpdater.
  Automation moves TYPE from the audio thread, where `setValueNotifyingHost` has
  no business being called — and a ramp across several detents inside one block
  coalesces to a single apply of the detent it ended on.
- **It does not fight the smoothers.** Nothing reaches into DSP state. Ten
  parameter values change, `ModuleEngine` reads them once at the top of the next
  block like any other knob move, and `DspCore`'s own machinery smooths them:
  `kSmoothingMs` for coefficients, `kCrossfadeMs` for SIZE's tap set, the 30 ms
  raised-cosine dip for the table swap TYPE itself causes. A type change is
  loud, but it is loud through the path a hand on the knobs uses.

### Where it lives, and the one piece of shared code it added

`TypeVoicing` is owned by the **engine** and not by the panel, through a new
`ModuleDef::createParamLink` and `core/state/ParamLink.h`. A panel is the wrong
owner: automation runs, sessions load and renders happen with no window
anywhere, and a link a panel owned would apply a type only while someone was
looking at it. One `ModuleEngine` exists per running module in both products —
the standalone's own, and one per occupied rack slot — so a single line in its
constructor covers both. The field is last in `ModuleDef` and defaults to null,
so no other module's def changes. BMO Linger is the only module in the suite
whose parameters write each other, and the next one should have to make this
argument again.

## Room's constants are real; the other five rows are CALIBRATE

Room's ten values in `params.h` (`roomDefaults`) are **Room's per-type
constants, not merely the values the knobs open at**. A fresh instance opens on
TYPE = Room, so what it shows for those ten is a claim about what Room *is* —
and if the DSP pass later picks different Room constants, the panel lies about
itself on the first thing anyone sees. `kTypeConstants[room]` is built from
those symbols rather than from a second transcription, so "Room's defaults are
Room's constants" holds by construction and not only by a test.

**Every value in the other five rows is a placeholder, and every one of them is
marked `// CALIBRATE`.** `10-dsp-spec.md` section 1 names the categories a
type's constant block holds — ER tap table, ER window and default density, the
eight FDN times, input-diffusion depth, damping and modulation defaults, input
bandwidth, default ER feed, three reserved era fields — and gives no numbers for
any of them; section 7's table is itself marked CALIBRATE almost throughout. So
there is nothing to transcribe yet, and an unmarked plausible number would be
indistinguishable, six months from now, from one fitted by ear.

The only thing claimed of a placeholder row is the ordering the type list
already fixes — Room < Chamber < Hall < Cavern on SIZE, Ambience small, Plate a
stand-in because a plate has no room geometry — plus Ambience's `erlevel` above
its `verblevel`, which is true of no other type and is the point of the row.
**The shape is what is real:** when the fitted table lands it drops into those
namespaces value for value, same names, same units, same ten fields, with no
change to `kTypeConstants`, `typeSettings` or anything that reads them. The
tests pin the shape, the reachability of every value against its own parameter's
range and step, and Room's row; they deliberately do not pin a placeholder.

Only the half of a type's block that has a **host lane** is here. The ER tap
table, the eight FDN times, β, the per-tap cutoff law, the input-diffusion depth
and the three reserved era fields have no parameter and belong in `dsp/`, where
they can be retuned without touching the schema.

## The six types, and why index 3 changed

**Room · Chamber · Hall · Cavern · Plate · Ambience.** Index 3 was *Large Hall*
until 2026-09-21. It was cut because the late network scales with the taps under
SIZE: Hall → Large Hall is τ̄ 55 → 80 ms, a factor of 1.45, inside a SIZE range
spanning 0.5–80 m. SIZE already covers it several times over, its only non-size
residual is β whose own ladder is indexed by size, and Reference A offers two
halls but nowhere says the difference between them is size — that was the pack's
inference and it does not hold.

**Cavern takes the slot**, carrying what had been reserved as "Church": the
long, dense, stone-reflective character, named secularly. Church is therefore
struck from the reserved list rather than left waiting in it; Shaped Hall,
Pattern Room, Positional Room and Vintage Room remain — though four of those
read as universal controls rather than as rooms, so the reserve may be emptier
than it looks.

**Renaming a position is free at any time. The count is what normalisation
depends on, and six is unchanged**, which is why this was a rename and not a cut
and why no automation lane moved. The factory preset "Long Hall" now selects
Hall at 45 m, which is the same preset and not a smaller one.

## The type list is append-only for state and lossy for automation

Both facts matter and only one of them is safe.

A **session or a preset** stores plain real values keyed by parameter id
(`ParamSet::toXml`), so a seventh type appended to the end changes nothing a
saved file refers to. **Recorded automation is not stored that way**:
`juce::AudioParameterChoice` normalises as index/(n−1), so going from six types
to seven rescales every automation point ever written on that lane. Room stays
at 0.0, but Ambience moves from 1.0 to 0.833 and lands on Plate. Nothing errors
and nothing warns.

So appending Shaped Hall, Pattern Room, Positional Room or Vintage Room is
legal and lossy, and that is a trade to make knowingly. The same applies to
`ermode`. `ervariation` is deliberately **not** a choice list — its
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

**It is a paged handheld, one width, 500 px.** Frosty approved the shape on
2026-09-21 and it replaced the compact/expanded split: a bezelled screen with a
line of printed text under it, three round page keys, a persistent row, a
cluster that changes with the page, a strip of three levels along the foot, and
a raked speaker grille beside them.

- **Persistent, on every page:** TYPE, SIZE, PRE-DELAY, DECAY.
- **EARLY (8):** ER MODE, DENSITY, ER SHAPE, ER SPREAD, ER HI-CUT, VARIATION,
  SOURCE, LINK ER.
- **TAIL (8):** ATTACK, DECAY SHAPE, LOW x FREQ, LOW x, HIGH x FREQ, HIGH x,
  MOD DEPTH, MOD RATE.
- **TONE (7):** EQ LOW FREQ, EQ LOW, EQ HIGH FREQ, EQ HIGH, IN HI-CUT, WIDTH,
  OUTPUT.
- **Always on, at the foot:** ER, REVERB, MIX. The two faders are the thesis,
  and the tail-off depth-placement technique has to be reachable from whatever
  page you are on. 4 + 8 + 8 + 7 + 3 is the whole schema.

**Four columns, and everything is laid out against them.** Three columns was
the first cut and it fails on TONE: seven over three is 3 + 2 + 2, so all three
pages grow to three rows and the screen has to lose 74 px to pay for it. Over
four, EARLY and TAIL are 4 + 4 and TONE is 4 + 3, so the block under the keys
does not change height when the page does — which is the one thing that would
make paging feel like switching panels rather than turning a page. **No row
holds one control**, and TONE's second row is three centred in the four for the
same reason MIX stopped having a row of its own.

**The page keys are round and level rather than raked.** Frosty's explicit
call: a handheld is held at an angle and can afford a raked key block, and a
mix panel is scanned in rows against its neighbours in the rack. The grille is
the one raked thing on the panel, and it is texture — no control, no state,
nothing to click. It comes out 100 x 84 px at the shipped width.

**The page is UI state, not a parameter**: `ui.page=early|tail|tone` through
`ModulePanel::setUiState`, the hook BMO Opto's meter mode and BMO DEQ's band
already use. `specs()` is frozen at thirty with two spare host lanes, and which
page somebody is looking at is not worth one of them and does not belong in a
session. **An unknown value is refused rather than defaulted**, for DEQ's
reason: a render labelled TONE that shows EARLY is worse than no render.

**TYPE and ER MODE are dropdowns, and they are the only two.** "Room type makes
no sense as a knob" — Frosty, 2026-09-21. A knob says less and more, and a list
of names says neither: Chamber is not more than Room, so the face carries no
information and the control has to be turned before it can be read. The wrapper
is `ui::ChoiceBox` in `core/ui`, a thin binding of a `juce::ComboBox` to a
choice parameter — the shared `BmoLookAndFeel` already themes `ComboBox` and
`PopupMenu` against the tokens, so the only colour it sets is the arrow, which
the scheme puts in the utility azure and which has to be the module's accent
here for the reason the group knobs did.

`ervariation` stays a knob and is deliberately not a choice list at all: its
seven positions *are* an ordered amount of decorrelation, which is what a knob
is for. BMO DEQ's SHAPE stays a `ConcentricBand` with a legend ring, which is a
third thing again — a named list drawn as a dial because the five shapes sit
inside the band they belong to. The test is whether the positions have an order
the hand should feel.

A consequence worth knowing before touching the layout: **nothing on this panel
prints a value any more**. TYPE and ER MODE were the only two that did, and
because a printed value line lifts a knob and its caption by its own height,
SIZE, DENSITY and ER SHAPE each had to reserve the same line blank to stay level
with them. The dropdowns print inside their own boxes, so the line, the three
reservations and the two taller rows that carried them are all gone.
`ChoiceBox::setControlSide` is what keeps a dropdown's caption on the same line
as the caption of the knob beside it.

**`ModuleDef::expandedWidth` is 0 and the module is not expandable.** It was
300 compact and 700 full on BMO DEQ's precedent, with the same face down the
left of both. Paging removes the reason for it: eight, eight and seven controls
never need to be on screen at once, and a key under the screen reaches them in
one click where the expand switch reached them in one click and 400 px. The
standalone header and the rack's slot bar stop offering a switch with nothing
to switch, and a rack shows the same panel a standalone does. **Do not
reintroduce a second width to make room for something** — a fourth page is what
this shape is for.

**The cluster's controls are added and removed as children rather than
hidden.** A hidden component still has bounds, and `tests/ui/LayoutTests` walks
every child whether it is visible or not, so a hidden control with a stale or
zeroed rectangle either escapes the panel, overlaps something, or reports a
caption overflowing a box of width zero. Unparenting is the one state in which a
control is genuinely not part of the layout. All twenty-three keep their
parameter attachments throughout, so turning a page costs a `resized` and
nothing else.

The consequence for the suite is that **the layout tests walk this panel once
per page**, because two thirds of it is unparented at any moment. One pass
would check a third of the module and say nothing about the rest, and the two
captions closest to overflowing — DECAY SHAPE and EQ HIGH FREQ — are both on
pages the panel does not open on.

There is one `ModulePanel::Rule` on the panel, LEVEL, over the strip at the
foot. The old panel painted six headings itself because at the expanded width a
shared rule would have cut a line through the column it did not belong to;
there is one column now, so the suite's own edge-to-edge path is correct again.

**Captions are ASCII.** The damping controls read "LOW x" and "HIGH x" and
their values print "1.20x", not with a multiplication sign: the two display
faces are licensed individually and live outside this repository, so a glyph
outside ASCII is one this suite cannot promise it can draw.

## The display, and its one cross-folder dependency

`LingerScreen` draws **one of three pictures**, whichever page the keys have
selected, on a `meterFace` ground under a faint dot-matrix grid. **It draws in
the module's accent and not in LCD green**: a second hue on one module is the
failure the accent audit was run to find, and the dark face is a value rather
than a hue.

**Parameter-driven only. No metering, no `AnalyserTap`, none of
`ModuleContext`'s meter callbacks.** Neither doc asks this module for a meter,
a reverb has no gain reduction to report, and the absence is a decision rather
than a gap to be filled in later.

**EARLY — linear time, 0 to the last tap plus a tenth.** The image-source taps
as discrete stems from a baseline. What this replaced was a symmetric envelope
mirrored about a centre line that bloomed and closed to a point, leaving most of
the box empty; Frosty rejected it. The ER window is a little over one decade
wherever SIZE puts it, so a linear axis scaled to the window itself shows the
*spacing* of the reflections — the one thing about a tap set worth looking at.
Stem heights are measured against **-40 dB, the ER fader's own bottom**, not
against the tail's -72: the table spans 15 dB, and on -72 a cluster that should
visibly decay draws as a comb of near-equal lines.

**TAIL — logarithmic time, 1 ms to 30 s**, a deliberate departure from `11`
section 5's proposed 0–500 ms window, and the old version's worst fault: it put
a 20 s decay in a fixed 0–500 ms window and most of the box was dead. A linear
window wide enough for the tail puts the whole 0–120 ms bloom inside the first
two pixels. On a log axis 1–100 ms keeps 45 % of the width. The ends are chosen
rather than round: 1 ms is where a reflection stops fusing with the direct
sound, and 30 s is `bmo::kMaxTailSeconds` — the ceiling on the tail this module
*and the rack it sits in* will ever report — so the right-hand edge is the same
number the host is told.

**TONE — logarithmic frequency, 20 Hz to 20 kHz**, level linear over ±24 dB.
Drawn as a **3-node serial EQ**: the low shelf, the high shelf and the input
high-cut summed in dB as one curve, with a marker on the curve at each of the
three corners. The shelves are drawn first-order and the cut one-pole, and
**neither is claimed to be the shipped filter** — `dsp/` is a placeholder and
`10` section 2 gives the shelves no order, so a second-order curve here would
be a guess presented as a measurement. `LingerScreen::responseDbAt` is the one
place that changes when the filters land.

**The line under the screen carries a reading for the page**: the tap count and
the ER window, the decay and where the tail ends, or the three crossover
points. It is ASCII, it is the only number printed anywhere on the panel, and
`LingerScreen::readout` is public so a test can read what a page says it is
showing.

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

- **Tail reporting is done** (`11` section 2a, milestone M5).
  `DspCore::tailSecondsFor` is the figure and the formula,
  `ModuleDsp::tailSecondsForParams` carries it onto every module's vtable
  defaulted to zero, and `tests/plugin/TailTests.cpp` asserts both halves
  across the whole registry.

  The rack **sums** it over occupied slots rather than taking the maximum —
  slots are in series, so 4 s feeding 2 s rings for 6 — and **then clamps the
  total at `bmo::kMaxTailSeconds`, the same thirty seconds a module clamps
  itself at.** The clamp arrived 2026-09-21 with Frosty's approval, and the
  case it exists for is the one the slot limit does not stop: `addModule`
  counts slots and never looks for duplicates, so eight BMO Lingers is a legal
  chain and eight honest thirties is a four-minute tail — free at transport
  stop, where over-reporting only idles the host, and not free for an offline
  bounce, where the figure is rendered onto the end of every export. Both
  clamps read the one constant in `core/dsp/ModuleDsp.h`; do not write 30.0
  anywhere else.
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
