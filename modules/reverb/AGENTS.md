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
the two were reconciled before `params.h` was written. **Both still say thirty
and the schema now says twenty-four** — the control-set trim below cut six and
did not edit `docs/`, so read this file for what the schema is.

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

## Twenty-four parameters, and eight spare lanes

**The control-set trim concluded on 2026-09-21 and cut six.** Frosty opened it
that morning — "I'm not sure all these controls will survive trim" — and it
closed the same day, before anything shipped, which is what made it free.

`type`, `size`, `predelay`, `decay`, `feed`, `damplo`, `damphi`, four EQ,
`ermode`, `erdensity`, `erspread`, `erhicut`, `ervariation`, `moddepth`,
`modrate`, `width`, `inhicut`, `erlevel`, `verblevel`, `mix`, `output`.
Permanent and append-only from the first ship. The relative order is what it
always was; the six that went took their indices with them and everything after
each one closed up.

> `docs/reverb/` still lists thirty, in both `10-dsp-spec.md` section 6 and
> `11-integration-and-test-plan.md` section 4. The trim did not edit `docs/`.
> When the two are reconciled, this table is the record of what changed.

### The six, and on whose authority

Every one of them is **character rather than a mix move** — what makes a Plate
a Plate rather than what an engineer dials mid-session — so each became a
constant in the per-type block instead of vanishing.

| Cut | Whose call | Why |
|---|---|---|
| `attack` | **Owner** | Verbatim: attack should be type dependent. The tail's bloom contour. |
| `decayshape` | **Owner** | Same sentence. The gated/linear curve. |
| `damplofreq` | **Owner** | "The frequencies should be handled by the onboard EQ." A knee is a property of a room, not a mix decision; `damplo` survives as a pure decay multiplier over it. |
| `damphifreq` | **Owner** | The same argument, and it spanned 1000–2100 Hz — **1.07 octaves**, which the trim review called a constant with a knob on it. |
| `ershape` | **The agent's, not the owner's** | A unitless exponent whose end-stops the spec itself records as unconfirmed, and the early cluster's contour — the same "what kind of room is this" argument the owner used for `attack` and `decayshape`. It was *already* a per-type constant; the trim took the knob, not the number. |
| `prelink` | **The agent's, not the owner's** | Set-and-forget: off is the reference behaviour, every type shipped it identically, nobody automates it. A fixed `kPreLinkFixed`, not a per-type field, because no type wanted its own answer. |

**Two of those six are the agent's judgement, and they are flagged here so that
Frosty can overturn either without archaeology.** `ershape` and `prelink` were
not asked for. If the listening pass wants ER SHAPE back as a knob, appending
it costs nothing.

### Why it was free, and where the line is

Cutting a **float or bool is reversible**, because state is stored as plain
values keyed by id (`ParamSet::toXml` writes `getReal(i)`), so re-appending one
later costs nothing a saved session can notice. All six were floats or the one
bool. Cutting or shortening a **choice is not**, because it reaches the host as
`juce::AudioParameterChoice`, which normalises as index/(n−1): change the count
and every recorded automation lane on it remaps, silently. `type` and `ermode`
were not touched and must not be; `tests/plugin/ReverbTests.cpp` asserts both
counts for exactly that reason.

**Nothing that stayed was also retuned.** Every surviving row of the schema is
what it was — same range, same step, same default — and the four new per-type
constants are the schema's own old defaults for Room. So a fresh Room after the
trim is the fresh Room that was there before it, and the cut is not a voicing
change hiding inside a control change. `kSchema` in `tests/plugin/ReverbTests.cpp`
is where that is pinned.

**A rack slot shows a host 32 lanes.** Twenty-four fits with **eight spare**,
where thirty fitted with two — so a later Freeze, a ducking control and the
tempo-sync pair `syncon`/`syncdiv` no longer have to be argued against each
other for the last lane. None of BMO DEQ's `SlotOverflow` machinery is needed.
The headroom is asserted, so spending it still takes an edit and an argument.

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
shelves. **Deleting it before first ship is free and returns a ninth spare
lane; after that it is permanent.** It survived the trim because it is a mix
move — what you feed the reverb is a mix decision in a way that where a room
stops absorbing is not.

## A type is a voicing: nine parameters, and five things that are not

**Selecting a type re-applies that type's nine writable constants over the
parameters that hold them — `size`, `erdensity`, `erspread`, `moddepth`,
`modrate`, `inhicut`, `feed`, `erlevel` and `verblevel` — every time, not only
at instantiation.** A knob moved away from its type's value is stamped back the
next time that type is selected. Frosty took that knowingly on 2026-09-21: a
type that applied its block once and then let the knobs drift off it is a type
that tells you less about what you are hearing the longer you use it.

**The row is fourteen fields wide and only nine of them are parameters.**
`erShape`, `decayShape`, `attack`, `dampLoFreqHz` and `dampHiFreqHz` have no
host lane after the trim, and a `Setting` can only name a parameter id — so
`typeSettings` returns nine and `TypeVoicing` never sees the other five. The
engine reads those off the same row in `ReverbDsp::paramsFrom`, keyed off the
TYPE value it is already handed. They still change with the type; they change
on the audio thread's next block rather than through a parameter write, which
means **nothing a host is automating can fight them.** That is the safer half.

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
those nine and nothing else: `predelay`, `decay`, the two damping multipliers,
the four EQ rows, `ermode`, `erhicut`, `ervariation`, `width`, `mix` and
`output` are the user's and stay put. Automating any of those beside TYPE is
safe, and `tests/plugin/ReverbTests.cpp` asserts it. **The trim made the hazard
smaller**, not larger: five of the settings a type changes no longer have a
host lane at all, so there is nothing for an automation curve to fight over on
any of them.

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

- **`type` is not in what a type writes.** `typeSettings` returns nine settings
  and `kType` is not among them, so applying a type cannot select one. The
  recursion is unconstructible rather than guarded, and both suites assert it
  directly.
- **An `applying` flag is the belt to those braces.** A host may write TYPE
  again from inside one of the nine notifications, and JUCE delivers a
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
- **It does not fight the smoothers.** Nothing reaches into DSP state. Nine
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

Room's fourteen values in `params.h` (`roomDefaults`) are **Room's per-type
constants, not merely the values the knobs open at**. A fresh instance opens on
TYPE = Room, so what it shows for them is a claim about what Room *is* —
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

**The four the trim brought in claim this much and no more.** `attack` rises
with the room — Ambience under Room under Chamber under Hall under Cavern,
because a larger room's tail arrives later behind its ER — and **Plate is 0,
which is the spec's own sentence rather than an ordering**: "at 0 the tail is
immediate, which is plate behaviour", and that is the whole reason the owner
said attack should be type dependent. `dampHiFreqHz` falls as the room gets
larger and stonier, which is the same direction `inHiCutHz` already runs in
these rows; the two disagreeing would be incoherent rather than merely
unfitted. `dampLoFreqHz` and `decayShape` claim **nothing**: every type ships
3.50 on the shape, which is linear, which is truncation switched off, because
no type in the pack is described as gated. `reverb_dsp_tests` asserts that last
one as a constant across all six rather than as a difference, precisely because
asserting a difference there would be false.

**The shape is what is real:** when the fitted table lands it drops into those
namespaces value for value, same names, same units, same fourteen fields, with
no change to `kTypeConstants`, `typeSettings` or anything that reads them. The
tests pin the shape, the reachability of every value against its own parameter's
range and step, and Room's row; they deliberately do not pin a placeholder.

**The line between this table and `dsp/` moved with the trim.** It used to be
"only the half of a type's block that has a host lane is here": the ER tap
table, the eight FDN times, β, the per-tap cutoff law, the input-diffusion depth
and the three reserved era fields have no parameter and belong in `dsp/`, where
they can be retuned without touching the schema. That is still true of all of
them — but `TypeConstants` now also holds five fields with no host lane of their
own (`erShape`, `decayShape`, `attack`, `dampLoFreqHz`, `dampHiFreqHz`). They
are here rather than in `dsp/` because they were parameters until 2026-09-21 and
their ranges, units and defaults are the schema's; a reader looking for where
ATTACK went should find it beside SIZE, not buried in an engine header.

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

**It is a paged handheld, one width, 380 px.** Frosty approved the shape on
2026-09-21 and it replaced the compact/expanded split: a bezelled screen with a
line of printed text under it, three round page keys, a persistent row, a
cluster that changes with the page, and a foot holding the two generator
levels, MIX and the TYPE dropdown.

- **Persistent, on every page:** SIZE, PRE-DELAY, DECAY.
- **EARLY (6):** ER MODE, DENSITY, ER SPREAD, ER HI-CUT, VARIATION, SOURCE.
- **TAIL (5):** LOW x, HIGH x, MOD DEPTH, MOD RATE, WIDTH.
- **TONE (6):** EQ LOW FREQ, EQ LOW, EQ HIGH FREQ, EQ HIGH, IN HI-CUT, OUTPUT.
- **Always on, at the foot:** ER, REVERB, MIX, and TYPE in the corner. The two
  faders are the thesis, and the tail-off depth-placement technique has to be
  reachable from whatever page you are on. 3 + 6 + 5 + 6 + 4 is the whole
  schema, and `checkReverbPanel` asserts that sum.

### 380, three columns, and why the numbers are exact

It was 500 at four columns until the control-set trim, and the width came down
by 120 px with **no caption pass at all**. A panel insets its content by `kPad`
= 10 a side, so the four-column cell at 500 was (500 − 20) / 4 = 120 px and the
three-column cell at 380 is (380 − 20) / 3 = **the same 120 px**. Every caption
on this face was measured against a 120 px cell and still is, "EQ HIGH FREQ" at
10 pt included. 380 is also a multiple of 20, like every other panel in the
suite. Anything narrower is a caption argument; anything wider is unearned.

**Two rows on every page, which is what four columns used to buy.** EARLY is
3 + 3, TAIL is 3 + 2 and TONE is 3 + 3, so the block under the keys does not
change height when the page does — the one thing that would make paging feel
like switching panels rather than turning a page.

**WIDTH moved from TONE to TAIL to buy it.** After the cuts TONE held seven —
four EQ rows, IN HI-CUT, WIDTH and OUTPUT — and seven over three is 3 + 2 + 2,
a third row on one page only; a fixed three-row block would have cost the screen
74 px of its 170. WIDTH is M/S gain **on the tail only**, and the TONE page
draws a frequency response of exactly three nodes that WIDTH is not one of, so
the move is right on its own terms and not only on the arithmetic. OUTPUT stayed
on TONE rather than joining the foot: the foot already has four.

**No row holds one control**, and TAIL's second row is two centred in the three
for the same reason MIX stopped having a row of its own.

### The grille is gone and TYPE has its corner

The grille was painted texture in the fourth column of the level strip, raked,
the one sloped thing on a panel whose keys are deliberately level. **It lost its
job when the body's corners went square**: rendered, it read as a flat swatch
beside the knobs rather than as texture behind them. Frosty cut it on
2026-09-21 rather than have it retried, and `getGrilleBox` went with it. Nothing
on this panel slopes now.

**TYPE took the corner**, on Frosty's call, and it is two arguments at once: a
dropdown is not knob-shaped so it never belonged in a grid of knobs, and the
foot is where two of the nine parameters a type change stamps already are.

Two consequences worth knowing before touching the foot:

- **It is the one row that is not three across**, because it holds four
  controls and three cells would orphan TYPE. The four cells are also not
  equal. At four equal 90 px cells the TYPE box gets 78 px and the longest item
  "Ambience" needs 90 — it overflowed by 11.7 px, measured, not guessed. So
  TYPE keeps a whole 120 px grid cell, which is the box it had in the
  persistent row and is known to fit, and the three levels divide the 240 left
  at 80 px each. That makes **REVERB the tightest caption on the panel at
  5.4 px of slack** — comfortably positive, and the suite already ships 1.8.
- **The LEVEL rule now runs over TYPE, which is not a level.** That is the
  wrinkle, and it was taken knowingly: `ModulePanel::addRule` draws edge to
  edge, and a rule that stopped one cell short would be a second kind of rule
  in the suite for one corner's sake. `checkReverbPanel` asserts TYPE under the
  rule so it reads as a decision rather than an accident.

**The page keys are round and level rather than raked.** Frosty's explicit
call: a handheld is held at an angle and can afford a raked key block, and a
mix panel is scanned in rows against its neighbours in the rack. Three keys over
three columns now fill the row exactly, where they used to be three of four
centred.

**The page is UI state, not a parameter**: `ui.page=early|tail|tone` through
`ModulePanel::setUiState`, the hook BMO Opto's meter mode and BMO DEQ's band
already use. `specs()` is twenty-four with eight spare host lanes, and which
page somebody is looking at is **not worth one even now that there are eight**
— the trim bought room for controls, not for UI state — and does not belong in
a session. **An unknown value is refused rather than defaulted**, for DEQ's
reason: a render labelled TONE that shows EARLY is worse than no render.

**One number on this panel has no control under it.** ATTACK lost its knob in
the trim, so the bloom is printed on the TAIL page's readout line and nowhere
else, read off `TypeConstants::attack` through the same `constantsFor` call the
engine makes. TYPE is therefore one of the parameters the panel redraws the
screen on, even though nothing draws TYPE itself.

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
the knobs that shared their rows each had to reserve the same line blank to
stay level with them. The dropdowns print inside their own boxes, so the line, the three
reservations and the two taller rows that carried them are all gone.
`ChoiceBox::setControlSide` is what keeps a dropdown's caption on the same line
as the caption of the knob beside it.

**`ModuleDef::expandedWidth` is 0 and the module is not expandable.** It was
300 compact and 700 full on BMO DEQ's precedent, with the same face down the
left of both. Paging removes the reason for it: six, five and six controls
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
control is genuinely not part of the layout. All seventeen keep their
parameter attachments throughout, so turning a page costs a `resized` and
nothing else.

The consequence for the suite is that **the layout tests walk this panel once
per page**, because two thirds of it is unparented at any moment. One pass
would check a third of the module and say nothing about the rest, and the two
captions closest to overflowing — EQ HIGH FREQ at 10.0 px and EQ LOW FREQ at
12.4 — are both on a page the panel does not open on. The tightest of all,
REVERB at 5.4 px, is on the foot and is there whatever page is showing.

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
