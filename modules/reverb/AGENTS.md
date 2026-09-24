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
the two were reconciled before `params.h` was written. **Both still say thirty,
and the schema says thirty, and they are not the same thirty** — the
control-set trim below cut six and the Reverb EQ added six others, neither
edited `docs/`, so read this file for what the schema is.

**The early reflections are real; the tail is not yet.** Milestone M2 is in:
`dsp/ErEngine` plays the ER table behind the ER fader, MIX and OUTPUT work, and
latency is zero — the *shipped* figure. There is **no late network** (M3), so
REVERB, DECAY, damping, SOURCE, WIDTH, PRE-DELAY, the modulation pair, IN
HI-CUT and the Reverb EQ reach `DspCore::Params` and go no further. The ER
tables themselves are a **stand-in** (`dsp/ErTable.cpp`) until the table
generator replaces it. See "The early reflections (M2)" below. The DSP pass
owns `dsp/` and nothing outside it, with one exception named below.

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

**The control-set trim concluded on 2026-09-21 and cut six.** Frosty opened it
that morning — "I'm not sure all these controls will survive trim" — and it
closed the same day, before anything shipped, which is what made it free.

**Then the Reverb EQ spent six of the eight lanes it bought, the same day.**
`eqfilter`, `eqloq`, `eqmidfreq`, `eqmid`, `eqmidq` and `eqhiq` turned two
shelves into a three-node parametric. The count is back to thirty, a rack
slot's thirty-two lanes leave **two spare**, and none of BMO DEQ's
`SlotOverflow` machinery is needed.

`type`, `size`, `predelay`, `decay`, `feed`, `damplo`, `damphi`, **ten EQ**,
`ermode`, `erdensity`, `erspread`, `erhicut`, `ervariation`, `moddepth`,
`modrate`, `width`, `inhicut`, `erlevel`, `verblevel`, `mix`, `output`.
Permanent and append-only from the first ship.

### The EQ change is purely additive, and its placement was free exactly once

**No parameter was removed and no id changed meaning.** `eqlofreq`/`eqlo`
already were node 1's frequency and gain and `eqhifreq`/`eqhi` node 3's, with
their ranges and defaults untouched, so a state file written against the
twenty-four restores every value it holds.

**The six ids went beside their siblings rather than on the end** — the EQ
block reads `eqfilter`, then each node as freq/gain/Q — which is a readability
choice that cost the lane order. A session stores plain values keyed by id and
would have survived a reshuffle either way, but **a rack slot maps host lane N
to parameter N** (`core/rack/SlotParameter.h`), so every lane after `damphi`
moved. Nothing errors and nothing warns. It was free because nothing has
shipped; **after first ship the only legal move is appending at the end**, and
the next reader does not get this choice. `tests/plugin/RackTests.cpp`'s bank
table is the second copy of the lane order, so changing it fails a build.

**Two spare lanes is tight, and that is the trade.** Freeze, a ducking control
and the tempo-sync pair `syncon`/`syncdiv` are four candidates for two lanes
and will now have to be argued against each other. The case for spending it
here is that the trim's own justification leaned on this EQ — the owner's
sentence was "the frequencies should be handled by the onboard EQ" — and an EQ
with no Q and no middle band could not honour it.

> `docs/reverb/` still lists thirty, in both `10-dsp-spec.md` section 6 and
> `11-integration-and-test-plan.md` section 4. Neither change edited `docs/`,
> and the two thirties are **not the same thirty**. When they are reconciled,
> this file is the record of what changed.

### The three EQ nodes, and why their shapes are fixed

Node 1 a low shelf, node 2 a bell, node 3 a high shelf — **fixed, with no shape
selector anywhere.** Frosty's explicit call. `juce::AudioParameterChoice`
normalises as index/(n−1), so a per-node shape list could never be revised
after ship without remapping every automation point written on it; fixed shapes
give a real three-band parametric with nothing permanent to regret, and BMO DEQ
is already the module for arbitrary shapes.

`eqfilter` is **a four-position choice, and per EQ rather than per node**,
which is what makes it a mode and not a shape: `Off`, `Lo Cut`, `Hi Cut`,
`Bandpass`. Node 1 is a low cut in Lo Cut and Bandpass, node 3 a high cut in
Hi Cut and Bandpass, and node 2's bell is untouched in all four. FREQ and Q
carry over unchanged whichever shape a node is in — a cut has a corner and a
resonance — so the same nine parameters serve every position, and **GAIN stops
reaching a node the mode has made a cut**, because `dsp::hasGain` is false for
a cut and the prototype has nowhere to put it. The gain is withheld and never
written, so a trip through a cut position and back restores the shelf.

It was a bool until 2026-09-22, and the change **cost no host lane**: same id,
same position in `Index`, so the schema is still thirty with two spare. What it
cost instead is the freedom to change its mind — a choice normalises as
index/(n−1), so a fifth position would remap every automation point ever
written on this lane. Frosty confirmed four. `Bandpass` is named after the
result rather than the mechanism: a low cut plus a high cut **is** a bandpass.

The full names are what a host lane and the panel readout show. The ring around
the control shows `kEqFilterLegend` — OFF / L / H / B — because a legend label
sits in a 38 × 15 px box and "Bandpass" does not;
`ui::ConcentricBand::setLegend` is the shared method that takes the terse list
without touching the host's, which is how BMO DEQ's SHAPE ring reads
BELL / LS / HS / LC / HC.

Ranges follow the suite. The two outer Qs stop at `kShelfMaxQ` = 2 rather than
travelling to a bell's 40 and doing nothing over 2 — BMO DEQ clamps to the same
number behind a wider knob because a DEQ band's shape is a choice, and here it
can never be. The middle node keeps DEQ's own bell range, 0.1–40, and spans
**20 Hz – 20 kHz**, deliberately wider than its neighbours: node 1 stops at
1.6 kHz and node 3 at 2.1 kHz, so without it the Reverb EQ could not reach the
presence region at all. Every gain defaults to 0 dB and `eqfilter` to off, so
the EQ is the identity at its defaults — asserted as an exact zero rather than
a tolerance, which is `tests/dsp/OptoDspTests.cpp`'s house rule.

### Two high cuts, and the captions are what tell them apart

The module now has **two**, and they are different controls in different
places. Do not merge them and do not rename either to something that drops its
prefix:

| control | caption | where | shape |
| --- | --- | --- | --- |
| `inhicut` | **IN HI-CUT** | on the input, ahead of the EQ and ahead of both generators, over a fixed 20 Hz high-pass | one pole, no Q, no gain |
| `eqhifreq` with `eqfilter` on | **EQ HIGH FREQ** | node 3 of the Reverb EQ | second-order, with a Q |

IN HI-CUT darkens *what the room is given*; node 3 darkens *the room*. The "EQ"
prefix on the nine EQ captions and its absence on the tenth is what carries it,
and the EQ screen says it a second way — the three EQ nodes are drawn as filled
markers and IN HI-CUT as an **open** one, because it is in series with the EQ
rather than part of it. `checkReverbPanel`'s caption list holds both words so a
reviewer sees them together.

### The filter design is reproduced, not stacked on

The matched-Z design moved out of BMO DEQ into `core/dsp` when BMO Defang
became its second caller. BMO Linger is the third, and that branch is **not
merged** — so the five files were copied byte-identically rather than branched
from, which is the house rule. Verified with `git hash-object` on AURORA:

| file | blob |
| --- | --- |
| `core/dsp/Biquad.h` | `c7d278b4bb46cdd678e5c1bd2e9e42d40779145f` |
| `core/dsp/Design.h` | `001300c1407e75c8a8eab3b7dc3963298051e953` |
| `core/dsp/Design.cpp` | `26d5b12e17449fcaccb26495b5ee4370f926afcf` |
| `core/dsp/Prototype.h` | `ee680331ce5eb8ef17eaa86c6c8d003afd6a9e04` |
| `core/dsp/Svf.h` | `8680157d75e90f1b550d98a47c0b825962dd525e` |

**Nothing in those five was edited.** Everything BMO Linger needed on top is in
`dsp/EqNodes.h`, a separate file: the three nodes, the shape table, the
gain-does-not-reach-a-cut rule and the summed response. `core/dsp/Design.cpp`
is the only one of the five that needs compiling, and it is listed in the
module's `DSP_SOURCES` rather than in `bmo_core` — `bmo_reverb_dsp` is a plain
static library that does not link `bmo_core`, and the JUCE-free tests and the
measurement tools link that.

**`EqNodes.h` is the one place the EQ's response is computed**, and both the
panel's curve and the engine go through it. Until 2026-09-21 the screen drew
the shelves as a hand-rolled `g / (1 + (f/f0)²)` and marked itself as not
claiming to be the shipped filter, because there was none to be wrong about.
There is one now, so the sketch became a measurement — which is `TapTables.h`'s
arrangement for the ER picture, applied to this one.

### The six, and on whose authority

Every one of them is **character rather than a mix move** — what makes a Plate
a Plate rather than what an engineer dials mid-session — so each became a
constant in the per-type block instead of vanishing.

| Cut | Whose call | Why |
|---|---|---|
| `attack` | **Owner** | Verbatim: attack should be type dependent. The tail's onset contour. |
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

**A rack slot shows a host 32 lanes.** Thirty fits with **two spare**. The trim
briefly made it twenty-four with eight and the Reverb EQ spent six of those the
same day, so a later Freeze, a ducking control and the tempo-sync pair
`syncon`/`syncdiv` are back to being argued against each other — four
candidates for two lanes. None of BMO DEQ's `SlotOverflow` machinery is needed.
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
- **A state restore settles that detent to the type it restored.** Off the
  message thread -- any host thread, or the rack's MessageManagerLock, which
  is a mutex and not a change of thread -- the TYPE write is only queued, and
  the queued call used to land after the file's nine values and stamp the
  type's block over them. `ModuleEngine::restoreState` calls
  `TypeVoicing::stateRestored` once the last value has landed, so that call
  finds nothing to do; `ReverbTests` and `RackTests` restore off the message
  thread and assert all nine survive.
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

Variation 6 is the other position that is not what it looks like: it is **mono
null** (owner, 2026-09-23). The ER go into the side and nowhere else — with E
the mono set, L = +E and R = −E, BMO Dimension's mid/side convention — so the
module puts out dry + E and dry − E and its mono sum is exactly twice the dry.
The widest setting, and the ER vanish entirely in a mono sum there. Its value
string says so, because an automation lane has nowhere else to. The table's
`combDelayMs` and `combGain` are not read; they leave `ErTable.h` at
integration.

## The panel

**It is a paged handheld, one width, 380 px.** Frosty approved the shape on
2026-09-21 and **rebuilt it on 2026-09-22**: a bezelled screen that carries its
own page menu, a line of printed text under it, a segmented sub-selection row, a
cluster of six controls that changes with the page, and a strip at the foot with
three faders and a TYPE / DECAY column.

- **EARLY (6 + segments):** DENSITY, ER SPREAD, ER HI-CUT, VARIATION, SOURCE,
  SIZE — with ER MODE as the segment row.
- **TAIL (6, no segments):** PRE-DELAY, WIDTH, MOD RATE, LOW x, HIGH x,
  MOD DEPTH.
- **EQ (6 + segments):** FREQ, GAIN, Q, FILTER, IN HI-CUT, OUTPUT — with
  LOW / MID / HIGH as the segment row, repointing the first three.
- **Always on, at the foot:** ER, REVERB, MIX as faders, then TYPE over DECAY in
  the fourth column.

**The persistent row is dissolved.** It held SIZE, PRE-DELAY and DECAY, and none
of the three was global in any sense the module could state: SIZE scales every
tap time, so it belongs on the page that draws the taps; PRE-DELAY is tail-only
and permanently so (`kPreLinkFixed`), so it belongs on TAIL; and DECAY joined the
strip, where it is the one knob among three faders and the only control on the
panel that prints its own value.

5 + 6 + 6 + 6 controls is 23, one of which is a segmented row bound to `ermode`,
and three of which stand for nine lanes rather than three — which is thirty, the
whole schema. `checkReverbPanel` asserts that sum with the two extra terms
written out, so a panel that lost the node selector fails there.

**The third page is captioned EQ and was TONE.** Frosty's call, 2026-09-21, when
the two shelves became a three-node parametric. The render key moved with it —
`ui.page=early|tail|eq` — and **`tone` is refused like any other unknown value**
rather than accepted as a synonym, so a render script that still passes it stops
with an error instead of quietly producing an EARLY page labelled TONE.

### The height budget

`ModulePanel::kContentHeight` is 688 suite-wide and `RackEditor` sets every panel
to it, so **the total cannot move**. The 680 of content area now goes:

| block | was | is |
| --- | --- | --- |
| bezel | 148 (screen 105) | **302** (screen 258) |
| page keys + PAGE rule | 52 | **0** — the menu is inside the screen |
| persistent row | 80 | **0** — dissolved |
| cluster | 264 (4 rows of 66) | **184** (a 26 px segment row, 2 rows of 79) |
| LEVEL rule | 16 | 16 |
| strip | 80 | **134** |
| sum | 640 | **636**, leaving four gaps of 11 |

**The screen paid for itself and then some.** It was 105 px and a letterbox
because the EQ page was twelve controls in four reserved rows, and four rows on
every page is 264 px on a face that has 680. One set of FREQ / GAIN / Q
repointed by a node selector takes that page to six, which is the two rows every
other page needs; the two rows that fall free, plus the 36 px key row, plus its
16 px PAGE rule, plus the gaps that went with them, are what the screen grew
into. A reverb's display is the part of the panel doing the explaining, and this
is the first version of the face where it is bigger than the controls —
`checkReverbPanel` asserts that relation, so a later pass cannot quietly shrink
it back.

**79 is the FILTER ring's box and the knob row took it.** FILTER is a
`ui::ConcentricBand` with a null gain — a legend ring — and its cap is
`jmin (width, height) × 0.35`. The cap has to be **27.6 px**, which is what
`ui::Fader::kCapWidth` is and what every knob on this panel draws, so the box has
to be 78.86 and a component's bounds are integers: 79, which draws **27.65**.
Sized by its cell instead, on the old 66 px cluster row, it came out at 23 px
beside 26.7 px knobs and the row visibly stepped when the page turned.
`ConcentricBand::capDiameter` was added for exactly this and reads the face that
is actually drawn, so the assertion cannot agree with the bug.

**The knobs are 50 px and not 46.** A 27.6 px cap is 46 px at the suite's 0.6
face scale, which is where 46 comes from — but the dotted track sits
`Tokens::trackGap` = 10 px outside the cap, so it wants a radius of 23.8 inside a
23 px half-box and the topmost track dot is drawn on the component's own edge. It
has been, all along, at 46 px and 0.58. 50 px is the smallest box that holds the
cap, the gap and the dot, and the cap is still 27.6 because `kFaceScale` is
derived from the cap rather than the other way round.

**There is one caption size on this face now.** It was 12 pt in the persistent
row and 10 in the cluster, because the cluster carried "EQ HIGH FREQ" at twelve
characters; the node selector took those captions down to FREQ, GAIN and Q, and
the longest caption anywhere is now nine characters in a 120 px cell. Two caption
sizes on one face was a cost worth paying for a twelve-character caption and is
not worth paying for nothing. Tightest margin on the panel is **REVERB at 5.4 px**
in the strip's 80 px column — unchanged by any of this, and measured on AURORA
through `ui_layout_tests --dump`.

**`kFaceHeight` is not called `kContentHeight`, and that name was a trap.**
`ui::ModulePanel::kContentHeight` is 688 — the whole slot — and it is a member of
the base class, so inside `resized` an unqualified `kContentHeight` resolved to
*that* rather than to the file-local constant: class scope beats namespace scope.
`(680 − 688) / 4` is negative, the `jmax` floor won, and every gap on this panel
was a `switchGap` whatever the budget said. The old face's class comment claimed
"the five gaps are `Tokens::switchGap` exactly" and was right by accident. Do not
reintroduce the name.

### The page menu is inside the display

The three round page keys and their PAGE rule are **gone from the plate**. The
menu is the top 26 px of the screen, drawn in the screen's own ink: three divided
segments, the selected one as inverted video, a hairline between each pair and a
rule under the band. That is 42 px of faceplate back and, more to the point, is
where a handheld's page menu belongs — the keys were a row of buttons duplicating
what the picture already says.

Two consequences:

- **The screen takes clicks now.** It intercepted none while the keys were
  components. `LingerScreen::onPageChosen` is the hook, the panel's `setPage` is
  the only thing on the other end of it, and a click below the band is a click on
  the picture and does nothing.
- **Nothing in the generic layout walk can see the menu.** It is painted: no
  bounds, no caption, no child. So `menuSegment` and `menuLabelOverflow` are
  public and `checkReverbPanel` does explicitly what the walk used to do for the
  keys — the three segments tile the band, every word fits its own segment, every
  word is ASCII, and a click on each one turns the page. The MAKEUP → MAKEU fault
  hides better inside a picture than anywhere else on a panel.

### The segmented row, and its two bindings

**A 26 px row of rectangular segments under the bezel, reserved on every page and
filled on two.** Frosty rejected round keys here: three circles want a 44 px row,
which would make a sub-selection the third tallest thing on the panel. A
rectangle with a word in it is what every switch in the suite is, and a segmented
row is a line of them that happen to be exclusive.

- **EARLY** — `ermode`, a real three-position choice parameter (Taps / Energy /
  Blend), replacing the dropdown that control used.
- **EQ** — LOW / MID / HIGH, choosing which node FREQ / GAIN / Q edit. **UI
  state**, `ui.node=low|mid|high`, refused rather than defaulted on an unknown
  value. `specs()` is thirty with two lanes spare and which node a panel is
  pointed at must never take one.
- **TAIL** — nothing. Nothing on that page is three-way, and **the row's absence
  is meaningful**: segments appearing is what says there is a sub-selection here.

The row is reserved whether it is filled or not, so the two knob rows sit at one
y and turning a page does not move the controls under it. The row that is not
showing is **unparented**, not hidden, for the same reason the cluster's controls
are.

**It is one component with two bindings, and that was the decision.** Nothing
about the control differs between the two uses: same rectangles, same height,
same inverted-video selection, same hit test, same radio behaviour. What differs
is where the chosen index is kept, and a control should not know that — the page
keys made the same argument before they were replaced ("a click asks for a page
rather than flipping a state"), and BMO Opto's meter row makes it too. So
`Segments` holds an index and an `onSelect`, the panel owns the binding, and the
two uses differ by four lines in the constructor rather than by a class.
`checkReverbPanel` asserts the two bindings as two separate claims — a click on
EARLY writes `ermode` through a host gesture and the parameter moving writes the
row back; a click on EQ writes **no parameter at all**, checked across the whole
schema.

**The row is 270 px and deliberately not the 360 px column grid.** Three segments
at the cell width would sit exactly over the three knobs below them, and on the
EQ page that reads as column headings — LOW over FREQ, MID over GAIN, HIGH over Q
— which is the opposite of what the row says. At 270 the segments straddle the
columns and cannot be misread.

### One set of FREQ / GAIN / Q, and it re-ranges

BMO DEQ's band-selector pattern and the same mechanism: the three knobs are
**rebuilt** against the selected node's parameters when the node changes, the way
`DeqPanel::bindBand` rebuilds eight, because a `PlainKnob` holds a reference to
its parameter and its attachment is made once. **All nine EQ parameters still
exist and still automate** — the panel shows three of them at a time.

**They are not a uniform control, and this is the one thing about the
arrangement a reader would not guess:**

| node | FREQ | Q |
| --- | --- | --- |
| LOW (low shelf) | 16 Hz – 1.6 kHz | 0.1 – `kShelfMaxQ` (2.0) |
| MID (bell) | 20 Hz – 20 kHz | 0.1 – 40 |
| HIGH (high shelf) | 1 kHz – 20 kHz | 0.1 – `kShelfMaxQ` (2.0) |

So the same knob at the same angle means three different frequencies depending on
the segment above it, and Q's travel is twenty times longer on the bell than on
either shelf. **The readout line prints real values, which is what keeps it
honest** — it is not a uniform control and the panel does not pretend it is. The
ranges are asserted off the rotary the attachment configured, not off `specs()`,
so a knob bound to the wrong node fails rather than looking right and editing
something else.

Greying follows the node the knobs are on, one node at a time: there was one GAIN
knob per shelf and both greyed independently, and there is one GAIN now, so the
question is only ever about the node it is showing. The parameter is never
written, so coming back off a cut gives the shelf its gain back — a mode must not
eat an edit, and `eqGainReachingDesign` is the same decision one folder over in
the DSP.

### FILTER is a legend ring

`ui::ConcentricBand` with a null gain parameter, which its own comment describes
as "a filter: a single knob with the same legend around it" and which sets
`Knob::Style::filter`. The legend is `kEqFilterLegend` — **OFF / L / H / B** —
while the parameter's value strings stay "Off", "Lo Cut", "Hi Cut" and "Bandpass"
for the host's lane and for the readout. A 38 px legend box cannot set "Bandpass"
and an automation lane should not say "B"; `setLegend` is paint-only and is the
shared method that takes one without touching the other. BMO CEQ's LO-CUT is the
precedent.

It replaces a `ui::SwitchButton` that was marked provisional in the code that
added it: `eqfilter` became a four-position choice on 2026-09-22 and a
`ButtonParameterAttachment` could only ever reach two of them.

**It draws in the suite's filter azure and not in the module accent**, because
`Knob::Style::filter` is not `character`. That is consistent with the two filter
dials already shipping — BMO EQ's LO-CUT and BMO DEQ's SHAPE, which is in the
same rack shot — and it is the one colour decision in this pass that was taken by
the shared component rather than by this panel. Note that it puts two azure
controls, FILTER and OUTPUT, side by side on the EQ page's second row where
everything else is violet.

### The strip, and the fourth column

Three faders — `ui::Fader`, ER, REVERB and MIX — in **134 px**: the fader, then
its caption, then its reading under the caption, which is the suite's arrangement
and what BMO Dimension does with BELOW over 700 Hz. The travel is derived (the
body less the cap) and comes out at **90 px**. `ReverbPanel::kStripRow` is a
named constant because Frosty has already noted the faders could be taller, and
the whole of that change should be that line and the gap arithmetic following it.

**The fourth column is TYPE over DECAY, and both labels sit outside the pair.**
TYPE's caption is above its dropdown and DECAY's is below its knob. With both
underneath, the upper label fell between the two controls, and a label between
two controls binds downward: it read as a second caption for DECAY and the column
stopped being two controls. `ui::ChoiceBox::setCaptionAbove` is the half of that
which is not this module's, and it is off everywhere else.

The column keeps a whole 120 px grid cell because the longest TYPE item,
"Ambience", needs 90 px inside a box the `ChoiceBox` insets by 6 a side; the
three faders divide the 240 that are left, at 80 px each.

**The LEVEL rule spans the three faders and stops.** It ran edge to edge over a
fourth column it does not describe for a release, and the panel owned that in a
comment as a wrinkle rather than drawing the truth. `ModulePanel::Rule` carries a
`span` now — same hairline, same knocked-out legend, same `addRule`, only the
ends move — and it is empty for every other call site in the suite, so nothing
else moved.

### The page's controls are added and removed, not hidden

A hidden component still has bounds, and `tests/ui/LayoutTests` walks every child
whether it is visible or not, so a hidden control with a stale or zeroed rectangle
either escapes the panel, overlaps something, or reports a caption overflowing a
box of width zero. Unparenting is the one state in which a control is genuinely
not part of the layout. Everything but FREQ / GAIN / Q keeps its parameter
attachment throughout, so turning a page costs a `resized` and nothing else;
those three are rebuilt when the **node** changes and not when the page does.

The consequence for the suite is that **the layout tests walk this panel once per
page**, because two thirds of the cluster is unparented at any moment.

**Nothing stands alone in a row, with one constructed exception.** The rule
exists because a lone centred knob with two empty quarters beside it reads as a
control whose partner has gone missing, which is what MIX did on the old face.
The TYPE / DECAY column is a *stack*: the two share a column, which is visibly a
pair, and the pair as a whole shares the strip with the three faders. So the test
passes a control that shares its centre line **or** shares its column, and a lone
knob in the middle of a row still fails both.

**`ModuleDef::expandedWidth` is 0 and the module is not expandable.** Paging
removes the reason for a second width. **Do not reintroduce one to make room for
something** — a fourth page is what this shape is for.

**Captions are ASCII.** The damping controls read "LOW x" and "HIGH x" and their
values print "1.20x", not with a multiplication sign: the two display faces are
licensed individually and live outside this repository, so a glyph outside ASCII
is one this suite cannot promise it can draw.

**TYPE is a dropdown and it is the only one left.** "Room type makes no sense as
a knob" — Frosty, 2026-09-21. A knob says less and more, and a list of names says
neither: Chamber is not more than Room. ER MODE was the other one and is the
EARLY page's segment row now. `ervariation` stays a knob and is deliberately not
a choice list at all: its seven positions *are* an ordered amount of
decorrelation, which is what a knob is for.

**One number on this panel has no control under it.** ATTACK lost its knob in the
trim, so the onset is printed on the TAIL page's readout line and nowhere else,
read off `TypeConstants::attack` through the same `constantsFor` call the engine
makes. TYPE is therefore one of the parameters the panel redraws the screen on,
even though nothing draws TYPE itself.


## The display, and its one cross-folder dependency

`LingerScreen` **carries the page menu and draws one of three pictures under
it**, on a `meterFace` ground under a faint dot-matrix grid. **It draws in the
module's accent and not in LCD green**: a second hue on one module is the failure
the accent audit was run to find, and the dark face is a value rather than a hue.

The menu is the top 26 px and `plotBounds` is **inside the 232 that are left**,
not inside the whole component — which is what stops a curve being drawn through
the menu. Every accessor on this class is in those coordinates. See "The page
menu is inside the display" above.

**EARLY and TAIL are parameter-driven only** — no tap, no FFT, no timer, and
none of `ModuleContext`'s meter callbacks. A reverb has no gain reduction to
report and no part to hear on its own, and those two absences are permanent.

**The EQ page is the exception, as of 2026-09-21**: it draws a spectrum behind
its curve, at the owner's request, and it is the one thing on this panel that
is not drawn from parameters. See "The analyser is real and the signal under it
is not yet" below.

**EARLY — a time × pan scatter, and it is the third version of this page.** x is
arrival, linear over the real ER window; y is bearing, hard left at the top and
hard right at the bottom; and **the radius of the dot is the tap's gain**. It
replaced mirrored stems with a bearing dash on each, which replaced a symmetric
envelope.

**The argument is VARIATION.** It is a lateral-spread control, so the picture it
belongs in is one where lateral spread is an axis: turning it fans the cluster
open and shut vertically, which a reader sees without being told what to look at,
where on stems it moved twenty-one short dashes a few pixels each.
`LingerScreen::lateralSpread` is the one function between the table's bearings
and the drawn ones and it is **a marked stand-in** — `10` §3 gives VARIATION as
per-channel tap permutations plus a lateral-spread scalar and the scalar itself
is CALIBRATE. What is real is the direction; the floor is not zero because the
first reflections stay near centre at every setting anyway; and position 6, whose
ER vanish in a mono sum, is not attempted. When the generator lands that is the
one function that changes.

**Three marks, three different claims.**

- A **filled dot** is a core tap: a real time, a real gain and a real bearing,
  all three off the same `Tap` row the engine will play.
- The **direct sound** is that dot with a ring around it, at t = 0 on the centre
  line — it is not a reflection, and it is what every arrival here is measured
  from. Its centre is pushed in by its own radius so no part of it is drawn on
  the frame, which is the clipping `inputCutRegion` was rewritten to stop on the
  other page.
- An **infill tap is a faint full-height line**, because its bearing is invented.
  The 21 core bearings come off `TapTables.h`; the infill stands in for a master
  sequence that does not exist yet (`10` §3), so drawing it as a dot would put a
  made-up bearing on the one axis this page exists to show. A line at a time
  claims the thing that is true — a tap arrives here — and none of the thing that
  is not.

Dot radii run between `kDotMinRadius` and `kDotMaxRadius` against **−40 dB, the
ER fader's own bottom**, not against the tail's −72. The small end is not zero: a
tap at the bottom of the range is still an arrival at a bearing, and a dot that
shrank to nothing would delete the quietest reflections rather than show them as
quiet. `L` and `R` are printed at the right-hand end of the axis, in the tenth of
the window left clear after the last tap — at the left they would land on the
direct sound.

**TAIL — three decay curves, low, mid and high.** The page's controls are LOW x,
HIGH x, MOD DEPTH and MOD RATE, and **the single envelope this replaced showed
none of them**: it was drawn at the mid decay, and the two multipliers only ever
moved a shaded band behind it, so LOW x could be swept end to end and the line a
reader was looking at never moved. `decayTimesSeconds` is the three — DECAY ×
LOW x, DECAY, DECAY × HIGH x, in that order — and it is public because that is
what the three curves *are*: a picture drawing one curve three times would pass
any "it drew three paths" check and fails this one. The mid curve is the heaviest
(it is what DECAY says) and the outer two are lighter and drawn under it, so a
crossing does not look like a break in it.

**MOD DEPTH and MOD RATE are still not drawn**, and that is the one thing this
page still owes. Three curves is what was asked for and what was built; a ripple
on the mid curve whose amplitude is depth and whose period is rate is the obvious
next move and was not taken without a look at it.

**The axis is unchanged: logarithmic time, 1 ms to a window that follows the
tail.** The right-hand end is `tailWindowSeconds`: far enough past
`tailEndSeconds` to leave
`kAxisAir` (8 %) of the width clear after the curve, clamped at `kMaxSeconds`.
**It ran to 30 s at all times until 2026-09-22**, and 30 s is
`bmo::kMaxTailSeconds` — the clamp on the tail this module *and the rack it sits
in* report — rather than a setting anybody uses: at the 1.8 s default the curve
finished about 60 % across and the remaining 40 % was a flat line. Note that the
air is taken in **width and not in time**; on a log axis 15 % more seconds after
a 2.16 s tail is 1.8 % of the box, which was rendered and rejected on AURORA.

**What that costs is comparability, and the readout pays it back.** With the
axis following the tail, turning DECAY no longer walks the curve across the box:
the shape stays and the scale under it moves. So the decades are **labelled**
inside the box — 10 MS / 100 MS / 1 S / 10 S, from `axisLabels`, punched out of
the face so a long tail's curve cannot be drawn over a number — and the bezel
line prints absolute seconds. Two absolute readings against 40 % of a dead box
was the trade, and it was taken deliberately.

**The line names the two outer curves and no longer names DECAY.** It read
"DECAY … ONSET … TAIL" until 2026-09-22: DECAY is on a knob in the strip and was
the one figure there a user could already read, and TAIL was the slowest of the
three, which is a number with no control under it. It reads
"ONSET … LOW … HIGH" now — `decayTimesSeconds`' outer two, which is exactly what
LOW x and HIGH x do and what the outer curves draw.

Logarithmic for the reason it always was: a linear window wide enough for the
tail puts the whole 0–120 ms onset inside the first two pixels, and `11` §5's
proposed fixed 0–500 ms window cannot show a 20 s decay at all. 1 ms is where a
reflection stops fusing with the direct sound.

**EQ — logarithmic frequency, 20 Hz to 20 kHz**, level linear over ±24 dB.
**Three marked nodes over one summed curve, and a curtain that is not a node.**
The three are the Reverb EQ's, drawn by `EqNodes::design` — which is
`dsp::designMatched`, which is the code the engine will run. IN HI-CUT's one
pole is in the curve, because it is in the chain, and it is the screen's own
arithmetic because nobody has chosen an order for it.

**IN HI-CUT is drawn as a region and was an open circle until 2026-09-22.** The
circle was wrong twice: one stroke's difference from three filled markers reads
as a fourth node of the same EQ, when it is an *input* filter ahead of the EQ and
ahead of both generators; and at its own default of 20 kHz it sat centred on the
right-hand end of the axis with half of itself outside the box. It is a washed
region from its corner to the end of the axis now, with a bright edge and a tab
along the top, and `inputCutRegion` clamps that edge inside the plot at every
setting of the knob. See "Two high cuts" above for why the picture has to
distinguish them at all.

#### Four node states, two strokes, and they compose

A node marker says **two independent things with two independent strokes**, which
is what lets it say all four combinations. `LingerScreen::nodeMark` is the one
place that is decided and `checkReverbPanel` reads it there.

- **A ring means the knobs edit this node.** FREQ, GAIN and Q are one set
  repointed by the LOW / MID / HIGH segments, so at any moment two of the three
  nodes on the curve are not the one the knobs are holding. Without the ring, a
  reader turning FREQ has to look away from the picture to find out which corner
  is about to move.
- **A fill means this node is shaping the sound.** `nodeIsActive`: a gain off
  zero, **or a shape that removes without having a gain at all**. That second
  clause is the case a gain comparison gets wrong — a cut's GAIN knob is greyed,
  its parameter may be sitting at 0, and a 24 dB low cut is very much doing
  something. It is a function rather than a comparison at the call site for
  exactly that reason, and it asks `eqNodeHasGain`, which is the same predicate
  the greying goes through.

So: ringed and filled is the node you are editing and it is working; ringed and
hollow is the node you are editing and it is flat; filled alone is a node working
that the knobs are not on; hollow alone is a node at rest. The threshold is
0.05 dB — half the parameter's own 0.1 dB step, so it cannot fall between two
reachable values and leave a marker flickering.

**Nothing the screen draws inside itself may touch its own frame**, and that is
a test rather than a habit: `axisLabels` hands out every tick box and
`inputCutRegion` the curtain, and `tests/ui/LayoutTests.cpp` walks both against
`plotBounds` on all three pages. The clipped marker shipped because a painted
thing had no bounds anybody could read — the same argument `getBezelBox` and
`getRules` are public for.

The sketch/measurement caveat this paragraph used to carry is **closed**:
`LingerScreen::responseDbAt` is the shipped design now, not a first-order
stand-in for one.

**FILTER reads as cuts three ways at once**, so it cannot be mistaken for a
shelf at a lot of gain. The nodes really are `Shape::lowCut` and
`Shape::highCut`, so the curve dives off the bottom of the ±24 dB axis at each
end instead of levelling onto a shelf. The area between the curve and the 0 dB
line is washed in — a lens either side of a shelf, a pair of wedges running to
the floor for a cut — and it is the **same** drawing in both modes, no branch,
so nothing has to be kept in step. And the readout prints "LO CUT" / "HI CUT"
where it printed "LOW" / "HIGH", which is the one of the three a test can read
without rendering — **per node since 2026-09-22**, off `eqCutsLow` and
`eqCutsHigh` rather than off the mode, because Lo Cut has to read
"LO CUT … HIGH".

**The line under the screen carries a reading for the page**: the tap count and
the ER window; the onset and the two outer decay times; or the three EQ node
corners with the mode in the words. It is ASCII, and `LingerScreen::readout` is
public so a test can read what a page says it is showing.

**It is painted, so nothing in the generic caption walk can see it**, and it is
the one line on the panel whose length changes with the parameters — which is
exactly the shape of thing that clips after a later edit. `checkReverbPanel`
measures it against `getReadoutBox` with `ReverbPanel::kReadoutSize` and the same
face the paint uses, on every page, which is `PlainKnob::captionOverflow`'s
discipline applied to text that has no control under it.

The other numbers on the panel are the three faders' readings and DECAY's, which
is why DECAY prints one: `ui::Fader` has its value line on by default because a
fader's ticks are deliberately mute, and a knob in that row with no number would
read as the one control whose value the panel would not tell you. Every knob in
the cluster stays silent — they say less and more.

**The TAIL line says ONSET and said BLOOM until 2026-09-21.** Owner approved,
and the reason is a collision rather than taste: BMO Dimension already ships a
control captioned BLOOM (`dim::kShuffle`) — Gerzon's bass shuffler, low-end
width, nothing to do with a reverb's tail, and Frosty named it himself. Two
modules in one line, possibly in one rack, showing one word for two unrelated
things is what this avoids. ONSET is also `10-dsp-spec.md`'s own term ("Tail
onset") and collides with nothing. **Do not tidy it back**;
`tests/ui/LayoutTests.cpp` fails if you do. Note also that this field reports a
**per-type constant and not a control**: `attack` was cut into the per-type
table in the trim, so it moves when TYPE moves and never when a knob does, and
a reader expecting it to track something they are turning will think it stuck.

### The analyser is real and the signal under it is not yet

**The EQ page draws a spectrum behind its curve.** Frosty's addition,
2026-09-21, and it overrides "the display is parameter-driven only" **for that
page only** — EARLY and TAIL are unchanged, and the screen's timer runs only
while the EQ page is showing, so turning to either of the others stops it.

`ReverbDsp::analyser()` returns `DspCore::eqAnalyser()`, a tap at **the point
the Reverb EQ acts on** — pre both generators, which is where `10` section 2
puts the EQ. That is where it belongs once there is an engine, so no rewiring
is owed.

**Until M3 it shows the dry input, and that is honest rather than broken.**
The early reflections exist since M2, but the input conditioning and the
Reverb EQ do not, so the point the EQ acts on is still the module's input —
which is exactly what `DspCore::process` writes to the tap, before the ER
engine sees a sample. A reader who finds the spectrum "not reacting to the EQ
knobs" has found the missing EQ. **Do not move the tap to fix it.**

Adding the override costs the other modules nothing — `ModuleDsp::analyser()`
returns null by default and BMO DEQ was its only overrider — and
`tests/plugin/RackTests.cpp` names all eight registered modules with the answer
each must give, so a seventh quietly acquiring one fails. A tap is also **off
until a panel enables it**, which the same test asserts with no editor open.

**A render of this page needs signal.** A parameter-driven screen renders at
rest and an analyser does not, so `snapshot`'s `signal=-18` is the condition —
the house figure, and the one BMO Opto's golden hashes are rendered at.

`panel/Spectrum.h` is a **trimmed copy of BMO DEQ's `panel/Analyser.h`**, not a
call into it: no module in this suite includes another module's headers, and
`deq::Analyser` lives in DEQ's namespace and folder. The right home is
`core/ui`, and **the third caller should promote it rather than copy it a
third time** — that change edits BMO DEQ and was out of this pass's scope. The
`Tint` chooser is the one thing left out: there is nowhere on a paged 380 px
handheld for a five-way control that matters on one page of three, and DEQ's
own default (`Tint::neutral`, Frosty 2026-09-12) settles which colour anyway.

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

## The early reflections (M2)

`dsp/ErEngine.h/.cpp`, wired into `DspCore::prepare/reset/process`. The class
comment is the signal path; what follows is what a later reader would
otherwise have to re-derive, and the places where the engine had to decide
something the spec did not. Figures are in
`testing-notes/linger-m2-engine-2026-09-23.md`, measured on AURORA.

**`ErTable.h` is the contract and the engine plays whatever it is handed.**
Nothing in the engine or its tests reads a number out of the stand-in in
`ErTable.cpp`; every expected tap in `reverb_dsp` is computed at run time from
`erTableFor (type)` through 10 section 3's laws. When the generator's tables
land, the tests follow them.

**Decisions the spec left open, each marked in the code:**

- **The input is the mono sum**, into one delay line. One source in one room;
  the table's per-channel sets are what the two ears hear of it. The dry path
  carries a stereo source's image.
- **Taps read whole-sample delays and never move while they sound.** SIZE,
  ER MODE, VARIATION and (in Energy and Blend) ER SPREAD build a second tap set
  and crossfade to it; the retrigger is 1 % of accumulated relative change,
  **or** the control holding still for a crossfade's length, because a lone
  0.1 m nudge on a 12 m room is under 1 % and would otherwise never land.
- **The Size law's window clamp scales the pattern, it drops no tap**: the
  factor `S / S_ref` is clamped so the table's window stays inside
  [5 ms, `windowClampMs`], and gains and band cutoffs follow the clamped
  factor. Above the clamp SIZE stops changing the ER.
- **A threshold of 0 is always on.** 10 section 3's ramp
  `clamp((D − θ)/Δ, 0, 1)` would switch the core taps off at D = 0, which
  `ErTable.h` says they never are. Taken literally it also means **an infill
  tap with θ = 1 never sounds** (it reaches zero weight at the top of the
  knob); the stand-in has one. The generator half should keep θ ≤ 1 − Δ, or
  the ramp should become `clamp((D − θ)/Δ + 1, 0, 1)` — a decision for both
  halves, not made here.
- **The density renormalisation is on the energy the band filters put out**,
  not on the raw gains: each tap weighted by its band's pulse energy, and
  pairs of taps close enough to overlap carrying their closed-form overlap.
  Without the first the level drifts by most of a decibel with density;
  without the second, two taps a sample apart (the stand-in has them) move it
  by a quarter. With both, the bridge holds to about 1e-6 dB.
- **The diffuser holds the level only on average.** Up to DENSITY 0.6 the
  bridge is exact; above it the three stages fade in and the level moves by up
  to **0.24 dB** at 48 kHz on the stand-in table (0.29 at 96, 0.22 at 192).
  That is a property of taking one output per channel from a feed-forward
  network — only an allpass preserves every input's energy — and not of the
  delays. **11 section 6 asks for 0.2 dB over the whole sweep; this range
  does not meet it**, and the test holds it to 0.3 dB and says so. The owner
  asked on 2026-09-23 for a feed-forward gain computed in `prepare()` from
  the diffuser's own per-output gain; it was built — the diffuser's energy
  for a one-pole-smeared pulse, tabulated over every crossfade position —
  and moved no type by more than 0.01 dB, because the drift is the table's
  tap pattern meeting the diffuser's paths and no diffuser-only figure can
  see that. It is not committed; the patch and the figures are in the
  testing note. Its fourth line enters inverted, which is what keeps its DC
  gain at 1 (see the code).
- **DENSITY runs on a 32-sample control grid.** The tap weights and the
  renormalisation are recomputed every `kControlInterval` samples, counted
  from `reset()` and never from a block's start, and each sounding set ramps
  linearly to them in between — so a sweep lands on the same samples at every
  block size. A weight that switched instead of ramping would now arrive as a
  0.67 ms fade rather than a step; the per-tap check in `reverb_dsp` is what
  catches that, not the waveform detector.
- **The end-of-cluster ramp** fades taps across the last 5 ms of the window
  to zero at its end, so the cluster ends on a ramp at every size.
- **ER HI-CUT is a one-pole solved to be exactly −3 dB at its setting**, and
  at the top of its range, 20 kHz, it is exactly a wire.
- **Variation 6 is mono null** (owner-confirmed 2026-09-23, the spec's reading):
  L = +E, R = −E on the ER bus, so the ER sum to exactly 0.0 — bit-exact, and
  asserted so — and a mono instance puts out no ER at all. It follows BMO
  Dimension (`modules/dim/dsp/DspCore.h`, "anything done to S alone is
  invisible in the mono sum"). Until 2026-09-23 it was built as
  `ErTable.h`'s comment wrote it, E ± g E(t − δ), whose mono sum is 2E; that
  reading was the contract's and is gone, and positions 0–5 are bit-identical
  across the change (`measure_reverb hash`).
- **Energy and Blend are deterministic and unheard.** Energy is 48 velvet
  pulses per channel, one per equal cell of the window, seeded from the
  table's seed, the type, the variation and the channel; every pulse is on at
  every density and the diffuser still follows DENSITY. Blend is Taps' times
  with the envelope's gains. Both are renormalised to Taps' core energy, so a
  mode change is not a level change.
- **The MIX law is provisional**: linear, dry·(1 − m) + wet·m, pending the
  owner's choice. Only "MIX 0 is exactly dry, MIX 1 has no dry" is tested.
- **ER LEVEL at −40 is exactly zero**, not −40 dB.

**The CPU budget was measured before anything was tuned**, as `10` section 8
asks: the worst case (48 taps a channel, three diffuser stages) is about 0.6 %
of a core at 48 kHz and 2.4 % at 192 kHz on AURORA, Release — the ER alone, so
the late network has what is left of 1.5 % and 5 %. The costliest transient,
DENSITY moving on every block, was 5.3 % at 192 kHz while the weights were
recomputed every sample; on the control grid it is 0.92 % at 48 kHz and
3.7 % at 192 kHz. A crossfade running on every block is 1.0 % and 4.2 %.
`measure_reverb bench`.

## The early-reflection tables are real, and pinned

`dsp/ErTable.h` is the contract between the tables and the ER engine;
`ErTable.cpp` serves six tables from **`dsp/ErTableData.inc`, which is
generated -- never edit it**. `dsp/ImageSource.cpp` is the generator and
`dsp/ErAudit.cpp` the rules of `10` section 3 and `11` section 6 as code. Both
are offline: `modules/CMakeLists.txt` builds them into `bmo_reverb_ergen`,
which only `reverb_dsp_tests` and `measure_reverb` link, so the plugin ships
the numbers and not the machinery. The panel still draws `TapTables.h`'s
placeholder; moving it onto these tables is integration's.

- **To change a table:** edit its recipe in `ImageSource.cpp` (geometry, beta,
  seed), then `measure_reverb taps --emit`, rebuild, `taps --audit`. The pin
  test in `reverb_dsp_tests` regenerates every table and compares it with the
  .inc bit for bit, so a generator change without a re-emit goes red.
- **A failing table is re-seeded:** `taps --reseed <type>` finds the first
  passing seed; `--try` tries a geometry across seeds before a row is edited.
- **Tables are voiced at each type's default SIZE** (read from
  `constantsFor`) and quoted at `kReferenceSizeM`, and the audits run at the
  default SIZE. Changing a default SIZE in `params.h` changes that type's
  table: re-emit and re-audit.
- **Known and named, not hidden:** Cavern fails flam rule (i) by 1.5 dB at
  55 m -- a known failure the owner will decide by ear (2026-09-23), not a
  seed to search for. **Plate is not a room** (owner, 2026-09-23): no room
  rule applies to it; it has six plate rules of its own, from 16 measured
  EMT 140 IRs (`ErAudit.h`), and
  it is a dispersive plate lattice, not a shoebox -- do not fix it back into a
  room. Cavern and Plate go past image order 3.
  `testing-notes/linger-m2-tables-2026-09-23.md` has every figure.

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
- **The late network.** M3: `Fdn.h`, `Absorbent.h`, the input stage and the
  Reverb EQ, pre-delay, SOURCE, modulation — and the wet bus gains a tail term
  beside the ER in `DspCore::process`. M4 is the six types' constants.
- **The real ER tables.** `dsp/ErTable.cpp` is a stand-in, replaced wholesale
  by the generator half of M2 on its own branch. The engine and its tests
  already read whatever table is linked.
- **The test suite.** `tests/dsp/ReverbDspTests.cpp` asserts the frame and
  `11` section 6's ER block as it applies to the engine. The table's own
  audits — comb, flamming, mono γ, lateral fraction — belong with the
  generator. **Write the modal-density test before Plate is tuned and expect
  it red** — `10` section 4 records eight lines covering Plate to barely 1 s,
  and the fix is 16 lines, a larger mean delay, or accepting sparsity.
- **Nothing has been heard.** Not one setting — not the ER either.

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
