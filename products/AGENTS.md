# products/

The thin layer that turns a module into a plugin. Each folder is three
files: `Product.h` (name, preset folder, factory function), `main.cpp`
(`createPluginFilter`), and a `CMakeLists.txt` calling `bmo_add_plugin`.
The rack adds `Registry.cpp`, the list of modules it can host, and its
chain presets in `Product.h`.

## The identity table

Permanent. Allocate here before the first build of anything new.

| Product | Module id | Plugin code | Bundle id | Presets |
|---|---|---|---|---|
| BMO CEQ | `eq` | `Fsty` | `com.lt3audio.frostyeq` | `.bmoceq` (reads `.bmoeq`, `.frostyeq`) |
| BMO Saturator | `sat` | `Bsat` | `com.lt3audio.bmosaturator` | `.bmosat` |
| BMO Util | `util` | `Butl` | `com.lt3audio.bmoutil` | `.bmoutil` |
| BMO Opto | `opto` | `Bopt` | `com.lt3audio.bmoopto` | `.bmoopto` |
| BMO Dimension | `dim` | `Bdim` | `com.lt3audio.bmodimension` | `.bmodim` |
| BMO Mix Rack | -- | `Brck` | `com.lt3audio.bmomixrack` | `.bmorack` |
| BMO DEQ | `deq` | `Bpar` | `com.lt3audio.bmodeq` | `.bmodeq` |
| BMO Tune RT -- **not in the rack** | `tune` | `Btun` | `com.lt3audio.bmotunert` | `.bmotune` |
| LTV Comp -- **not a BMO product** | `ltvcomp` | `Ltvc` | `com.lt3audio.ltvcomp` | `.ltvcomp` (reads `.bmovcomp`) |

Manufacturer code `LT3a`, company "LT3 Audio", preset root `LT3 Audio/`.
BMO CEQ keeps FrostyEQ's code and bundle id on purpose, through two renames:
that is what makes existing sessions open.

## Lines

There are two, and a product belongs to one of them. **BMO** is the suite.
**LTV** is the collaborations, of which LTV Comp is the first and more are
planned. A line is `ui::Line` in code: it owns the faceplate, the strip behind
the header, the wells and the knob caps, plus the marque printed at the head of
every panel -- `LT3a` for BMO, `LTV` for a collaboration. It owns no ink and no
layout, so a rack still lines up and every ink in the suite still answers to a
known set of grounds.

**Codes on the LTV line are two characters for the collaborator and two for
the product** -- `Ltvc` is `lt` + `vc` -- against BMO's one-character prefix
and three for the product. Frosty's scheme, 2026-09-14.

The capital is not optional and not a house style. JUCE's CMake API on
`PLUGIN_CODE`: *"For AU compatibility, this must contain exactly one upper-case
letter. GarageBand 10.3 requires the first letter to be upper-case, and the
remaining letters to be lower-case."* An all-lowercase code is an AudioUnit
that misbehaves rather than one that fails to build, which is the worst kind of
wrong. Every row above already obeys it.

**Nothing here changes a BMO product.** The two-plus-two shape is the LTV
line's alone; the eight BMO rows keep the codes they shipped with, and a new
BMO module still takes `B` plus three. The only row this scheme has ever been
applied to is LTV Comp's, which was allocated inside its own change window.

**BMO Tune RT is in this repository but not in the rack** (Frosty,
2026-09-11): its module id is allocated here so nothing else can take it, and
so that joining the rack later needs no rename, but it is in no registry and
on no rack link line. `modules/tune/AGENTS.md` has the rest, including the two
build switches that keep the two sides independent.

BMO Opto is a two-knob opto-style leveling compressor (CRUSH, LEVEL): a
feedback-topology detector (the sidechain reads the signal after gain
reduction, as on a real opto cell, not before it) with a program-dependent
release time -- the harder and longer it has been driven, the slower it lets
go -- and a knee that hardens as CRUSH increases. See `modules/opto/AGENTS`
notes at the top of `modules/opto/dsp/DspCore.h` for the model.

BMO Dimension is a stereo imager in three stages, all of which process the
**side signal only**: a detune stage that manufactures side content from a
mono source, a modulated all-pass that decorrelates it, and an S1-style
imager that scales and steers it. Because `L + R = 2M`, a side-only chain
cancels in the mono sum by construction rather than by testing -- which is
the reason the topology is arranged that way. The detune stage is
bypassable, so one module covers a mono vocal and an already-wide bus.

It takes the lavender `#d4a4ff` that BMO Opto carried until 0.2.2 and gave
up when its panel went greyscale. Nothing else uses it; see
`modules/opto/Module.cpp` for why it was free.

**BMO DEQ** is the zero-latency dynamic parametric EQ, and the row above is
its identity. It was reserved as "BMO Parametric" until 2026-09-10, when
Frosty named it BMO DEQ and gave it everything that reservation held: the
plugin code `Bpar` and the teal `#5ecfc0` carry over unchanged. The bundle id
and preset extension, which were `com.lt3audio.bmoparametric` and `.bmopar`,
were re-derived from the new name, and the module id is `deq` rather than
`par`. None of these had shipped, so changing them cost nothing. The row has
been permanent since the product's first build, on `add-bmo-deq`.

**LTV Comp** (BMO Vcomp until 2026-09-14) is the vocal compressor, and it is
not BMO Opto's replacement or its successor -- the two are opposite products that happen to share a category.
Opto models two pieces of hardware and wears their behaviour, feedback topology
and all; Vcomp is modern, feedforward and predictable, and its whole claim is
that a vocal needs one knob for how hard and one for how loud. Frosty named the
reference points, 2026-09-13: the sound of Waves RVox and RComp, the simplicity
of RVox and Klanghelm DC1A.

AMOUNT sweeps threshold, knee and ratio together and pays for its own makeup,
so the knob buys density rather than level, and a gate ahead of it -- dragged
as a handle along the IN meter, not set on a knob -- cleans up what that makeup
would otherwise do to the silences. COMPLEX reveals attack, release, a detector
high-pass and a pair of THRU controls that split the low and high bands out of
the compressor's reach entirely; with it off the DSP does not read those six at
all. ARC is the programme-dependent release and is always on in standard mode.
See `modules/vcomp/AGENTS.md`.

**The periwinkle `#a2a8ff` was picked against this table, not off a screen.**
Hue 236.1 degrees sits in the widest gap that was left: 37.3 degrees clear of
the utility azure and 35.5 clear of BMO Dimension's lavender, which is a wider
separation than the lime. Its 6.17 on the dark plate is inside the 5.87-7.19
the shipped accents run, and its 1.91 on the pale plate is second only to BMO
EQ's 2.00 -- so unlike the lime it costs nothing in the pale appearance. The
figures were computed by the WCAG formula on AURORA.

**The row was rewritten in that window, 2026-09-14, and the window is now the
only reason it cost nothing.** BMO Vcomp became **LTV Comp**, the first product
on the LTV line: the module id went `vcomp` to `ltvcomp`, the plugin code
`Bvcp` to `Ltvc`, and the bundle id and preset extension were re-derived from
the new name. That is the BMO DEQ move exactly -- renamed from BMO Parametric
before anything shipped -- and it is the module id that made the timing matter,
because `ModuleDef::id` is documented *"in state files and rack presets, never
changes"*. 0.2.4 existed only on AURORA, so nothing else had ever written it.

`ProductInfo` carries `BMO Vcomp` / `.bmovcomp` as its legacy pair, for the one
machine that has presets under the old name. One hop; BMO CEQ is the product
that needs two.

**The accent is still not signed off**, and is now the last thing in the queue
rather than the first, because the line moved the ground out from under it.
Every figure in the table above is quoted against `#2e2e32` and `#efefef`, and
an LTV panel is neither -- periwinkle `#a2a8ff` measures 1.91:1 on the suite's
pale plate and **1.31:1 on LTV silver**, level with the lime, which is the
lowest figure here. So an LTV accent cannot be picked off this table. It has to
clear silver and graphite, and it should be picked after the panel's layout is
settled, not before.

BMO DEQ has 159 parameters -- 158 of Frosty's allocation plus AUTO, appended
at index 158 on 2026-09-11 before any release. A rack slot still has 32 host
lanes; the module's first 32 parameters take them and the rest are kept in
the slot's state, off the host grid -- see `modules/AGENTS.md`, step 2. Which
32 is Frosty's allocation, recorded in `modules/deq/params.h`.

It is also the one module with **two widths**: 320 compact and 600 full. A
rack opens it compact and standalone opens it full; the switch between them
is on the host's bar, not on the panel (`ModuleDef::expandedWidth`,
`ui::ExpandButton`). See `modules/deq/AGENTS.md`.

Reserved for later products (not built, do not reuse): `Bfet` FET comp,
`Bdyn` dynamics, `Bdes` de-esser, `Bovr` overdrive,
`Bcmp` compressor, `Bdly` delay, `Brvb` reverb.

## BMO EQ and BMO DEQ — settle BMO EQ's name

**Status: DONE, 2026-09-17 on AURORA.** Frosty chose **BMO CEQ**, for console
EQ, on 2026-09-11, and the rename landed with the module's UI pass. The
argument below is kept because it is why the name is what it is; what the
rename touched is in the table further down, and all of it is done.

Two things about it are worth carrying forward:

- **The preset chain runs newest first**, not oldest first as the 0.2.4 review
  and the UI-pass handoff both specified. Walking oldest first with "never
  overwrite" means a preset name that exists in both old folders arrives from
  **FrostyEQ** — the copy from before the user's later edits — and the BMO EQ
  one is dropped. Newest first is the same rule stated the way it was meant.
  `tests/plugin/EqTests.cpp` pins the direction, and reversing the order in
  `products/eq/Product.h` fails it.
- **The copy is one-shot, marked, not gated on an empty folder.** The old gate
  stranded anyone who had saved a preset before the copy ran. `kMigrationMarker`
  (`.migrated`, which carries no product extension, so nothing lists it as a
  preset) replaces it, and Frosty chose it on 2026-09-17 so that a preset the
  user deletes does not come back on the next launch.

This section was written while BMO DEQ was still "BMO Parametric", and the
argument below is that one's. The new names make the fix clearer, not
different.

**The two do not fight over function.** BMO EQ is a Neve 1084 model:
frequency selectors are *stepped* choice parameters, the curve shapes come
out of the LC network in `modules/eq/dsp/EqNetwork.h` rather than from
coefficients, there is no continuous Q anywhere -- only a Hi-Q toggle on the
mid -- and it saturates and oversamples. BMO DEQ is continuous frequency,
continuous Q, dynamic, and clean. Those are two different instruments and a
mix wants both.

**They fight over the name, and the name is backwards.** "BMO EQ" claims
the generic word while being the *specific* product. Next to it, "BMO DEQ"
reads as a variant of BMO EQ while being the general-purpose one. Someone
scanning a device list will reach for BMO EQ expecting a full EQ, find
stepped frequencies and no Q, and conclude the suite is missing something
it is not.

**The proposed fix is to rename BMO EQ so its name says what it is.** The
proposal is **BMO CEQ**. It would pair with BMO DEQ as two equals, where "BMO
EQ" and "BMO DEQ" read as a product and its variant. The earlier proposal
was "BMO Console EQ", over "BMO Vintage EQ", because "vintage" describes
marketing rather than behaviour. Whatever the name, do not rename BMO DEQ to
solve this.

**Proposed timing: before BMO DEQ ships. Not decided either.** This used to
say "after the Ableton pass": BMO EQ was in the build under test, and a
rename mid-cycle would muddy a test about Dimension. The other half still
holds. The cost of the rename grows with every tester on the old name, and
it is the *second* rename in this product's life after FrostyEQ. That argues
for doing it once more and never again, not for flinching.

### What the rename touches

| | change | effect |
|---|---|---|
| `products/eq/Product.h` | `ProductInfo::name`, `PresetInfo::folderName` | header text, preset folder |
| `products/eq/CMakeLists.txt` | `PRODUCT_NAME` | DAW display name, `.vst3` filename |
| `modules/eq/params.h` | `kModuleName` | the name in a rack slot |
| the identity + accent tables here, `README.md`, packager README | strings | |

### What must NOT change

- **Plugin code `Fsty` and bundle id `com.lt3audio.frostyeq`.** They are
  already carrying FrostyEQ's identity so that old sessions open. They carry
  it through this rename too. A display name is not an identity.
- **Module id `eq`.** It is in saved state, rack presets and automation.
  Ids and display names are already decoupled everywhere -- `util` is "BMO
  Util", `dim` is "BMO Dimension" -- so `eq` staying `eq` under a new display
  name is the existing pattern, not an exception. BMO DEQ takes `deq`.
- **The parameter schema.** Untouched; this is a label change.

### The one real code change — done

`PresetInfo` carried exactly **one** legacy pair, and BMO EQ had already
spent it on `("FrostyEQ", ".frostyeq")`. A second rename needs a second hop,
so either `PresetInfo` grows a chain of legacy names, or the FrostyEQ hop is
dropped on the grounds that anyone who ran BMO EQ once has already been
migrated. **Dropping it is the wrong call** -- it silently strands any
tester who skipped a release, and the whole point of `migrateLegacy()` is
that nobody has to have been paying attention. The chain was grown:
`PresetInfo::legacy` is a `std::vector<LegacyPreset>`, newest first, and every
other product passes an empty one.

**It is tested now, which it never was before.** `migrateLegacy` used to
return early whenever `setDirectoryForTesting` was in use, so the suite could
not reach it at all; it resolves old folders through the same `folderFor` as
the current one, and `EqTests` walks the whole chain -- both hops, the same
name in both folders, the marker, a deleted preset staying deleted, and a
user's own file never being overwritten. The same hole is still open for
LTV Comp's one hop (`tests/plugin/VcompTests.cpp`), which now could be closed
the same way.

A rename that changes the **bundle filename** leaves the old bundle in the
user's plug-in folder, and the DAW lists both. Add a line to
`tools/packager/superseded.txt` -- one list, read by the macOS and Windows
installers both -- and the installers remove it. `tools/packager/README.md`
has the format and the guard that stops a bad line deleting a live plugin.

Say in the generated `README.txt` whether **sessions** survive the rename,
not just presets: they are different questions. BMO CEQ kept `Fsty` and its
bundle id through two renames so old sessions open; LTV Comp changed both, so
they do not.

## Accents

Permanent, and allocated here for the same reason plugin codes are. The
Palette Book is a measurement write-up, not a registry, and it has drifted
from the code twice -- it still lists `#d4a4ff` against BMO Opto, which gave
that colour up in 0.2.2. Reading it as an allocation list is what nearly
cost BMO Dimension the lavender. This table is the allocation; that document
is the evidence.

Contrast is quoted against both plates that ship: `#2e2e32` dark and
`#efefef` pale. Hue is there because separation from the *other* accents is
the constraint that actually binds -- there are more legible colours than
there are distinguishable ones.

| Module | Accent | Hue | on `#2e2e32` | on `#efefef` |
|---|---|---|---|---|
| BMO EQ | `#f08cb4` | 336.0° | 5.87 | 2.00 |
| BMO Saturator | `#efa552` | 31.7° | 6.55 | 1.80 |
| BMO Util | `#7fc98a` | 128.9° | 6.84 | 1.72 |
| BMO Opto | none -- `tokens().neutral` `#ababab` | -- | -- | -- |
| BMO Dimension | `#d4a4ff` | 271.6° | 6.80 | 1.73 |
| *(not an accent)* utility azure `#4fb8e8` | | 198.8° | 6.02 | -- |
| BMO DEQ | `#5ecfc0` teal | 172.0° | **7.19** | 1.64 |
| BMO Tune RT (not in the rack) | `#b6e35d` lime | 80.1° | **9.10** | **1.29** |
| LTV Comp -- **unsigned, and on the LTV ground** | `#a2a8ff` periwinkle | 236.1° | 6.17 | 1.91 |

**The lime was picked outside this table**, while Tune was still its own
repository, and its two figures are computed by the WCAG formula on AURORA
rather than by any tool in this tree. Its separation is the best that was
left: 48.4° from the Saturator and 48.8° from BMO Util, in what was the widest
remaining gap. Against that, **1.29 on the pale plate is the lowest figure in
the table**, where the shipped accents run 1.64 to 2.00, so on `#efefef` it is
a fainter mark than any of them.

**Frosty kept it, 2026-09-11**, told that. So the lime is spent, and a later
module cannot have it. Re-opening it needs a reason that is new -- a panel
that reads badly in the pale appearance, say -- not this arithmetic again.

BMO Opto has no accent and is not holding one: its panel went greyscale in
0.2.2 so that the only colour on it could mean "engaged". Its red `#e0685a`
and amber `#e0b040` are **states, not an accent** -- they never touch a cap,
a caption or the header bar.

The azure is not a module accent and is not available as one. It is the
utility-knob colour and appears on every panel in the suite, which is
exactly what makes it the hue everything else has to stay away from.

**The teal is BMO DEQ's.** It was held for "BMO Parametric" and passed to
BMO DEQ with the rest of that reservation on 2026-09-10. It is spent on the
module's first build: its `ModuleDef` takes `0xff5ecfc0`, and this row loses
"held".

**The teal carries a known objection, recorded so it is not rediscovered as
new.** It was the candidate for BMO Dimension and lost to
the lavender on separation: teal sits **26.8° from the utility azure**, and
the azure is on every panel including whichever one takes the teal. Lavender
had 64.4° to its nearest neighbour. Teal is the best of a thin remaining
field rather than a good hue, and it wins on the dark plate -- 7.19:1 is the
highest in the table. Also worth weighing before it is spent: at 172° it is
about as far from BMO EQ's 336° as two colours get, which reads as
*unrelated*, and BMO DEQ may want to read as the console EQ's sibling
instead. Frosty assigned the teal knowing this; the objection is here so
that nobody raises it again as a new finding.

If the teal is passed over, the field measured for Dimension was
periwinkle `#8fa4ff` (228.8°, 29.9° from azure, 5.74 dark), gold `#e8c95a`
(46.9°, 15.2° from the Saturator, 8.33 dark) and cyan `#63d3e8` (189.5°,
9.3° from azure, 7.74 dark). None of them beats teal on separation.

## Rules

- A product never contains DSP or UI. If you are writing either here, it
  belongs in `modules/` or `core/`.
- The rack's registry order is the order in its add menu; put the most
  used first.
- Rack presets (`rackPresets()`) name modules by id and set values by
  parameter id, in real units. They are applied through the same path as
  saved state, so anything a preset can express a session can restore.
