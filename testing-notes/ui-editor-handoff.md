# UI editor — handoff

What the `ui-editor` branch did, why each thing is the way it is, what was
tried and thrown away, and what is still open. Written for someone picking
this up cold.

Forty-five commits, no parameter, spec, preset or DSP file touched by any of
them. All ctest suites pass at every commit -- nine of them until 8 Sep, ten
since `ui_layout` joined them. If you change anything here and a DSP test
moves, something has gone wrong that this branch was not supposed to be able
to do.

---

## 1. The build, and the one trap left

Both former blockers are gone: cmake 4.4.3 is on the machine PATH, the
licensed faces resolve from outside the repository, and `tools/snapshot`
renders headlessly on Windows.

    cmake -S . -B build
    cmake --build build --config Debug --parallel
    ./build/tools/Debug/snapshot.exe eq   snapshots/eq.png
    ./build/tools/Debug/snapshot.exe rack snapshots/rack.png chain=util,eq,sat,opto

A correct configure prints the font folder it chose:

    -- BMO fonts: %USERPROFILE%/Documents/FONTS (.bmo-fontdir)

**`.bmo-fontdir` does not travel.** It is a working-tree file and is not in
git, so a fresh clone or a second worktree does not get it and the build stops
with a fatal error naming a missing `.otf`. Fix per working copy with
`scripts/set-font-dir.sh`, or machine-wide with `setx BMO_FONT_DIR`.

**`scripts/build.sh` works on Windows now**, so the four lines above are only
what you type when you want one render rather than all of them:

    scripts/build.sh --snapshots

It was broken in three places, not the one this file used to name. It assumed a
single-config generator throughout: it left the configuration off the build,
left `-C` off `ctest` — which finds no tests at all on a multi-config build —
and then looked for the snapshot tool at `build/tools/snapshot`, one directory
above where Visual Studio puts it. It now reads `CMAKE_CONFIGURATION_TYPES` out
of the cache and adapts, and searches for the tool rather than assuming a path.

`snapshots/` is gitignored, so renders never travel with a branch. Re-run the
tool.

---

## 2. Read these two first

- **`Palette Book/palette-book.md`** — every colour in the suite measured on
  the ground it is actually drawn on, the reasoning behind the light and dark
  sets, and the dead ends. It is the design record; this file is the change
  record.
- **`docs/ui-workflow-brief.md`** — cherry-picked onto this branch from
  `ui-workflow-brief`. Its premise about there being no local toolchain is now
  stale, but its proposals are not, and its house rule is the one that matters
  below.

**Assert absolutes, not comparisons.** That rule came out of a DSP test that
passed for a whole release while both modes were broken, because it only
compared them to each other. "Better contrast than before" is the same trap.
Every ratio in this branch is a fixed number against a named ground.

---

## 3. What changed, and why

### The token split

`pointer` was `#ffffff` doing five jobs. They stopped agreeing the moment the
plate was allowed to go dark, and three were already wrong on the pale one. It
is now `pointer`, `ringFace` and `meterInk`, plus two *derivations* rather than
colours:

- `accentTextOn (colour, ground)` — steps a colour away from a stated ground
  until it clears a ratio. Used where the ground is known and is not the plate:
  a polarity switch's white fill, the ring a band's marker sits on, the meter
  face.
- `accentInk (accent)` — a module's colour as ink **on the plate**. Paired with
  `faceOf`, and the two trade places between appearances; see below.
- `onAccentOf (fill)` — ink on a filled control, derived from the fill.

Measured on the result: knob captions 1.95 → 4.57-4.69:1, section legends
1.72 → 4.57-4.69, knob pointer 1.39 → ~9.5, selected band legend 2.22 → 4.62,
text on a switch 1.98 → derived, text on `switchOff` 2.43 → 4.94.

`meterFace`, `knobTint`, `neutral` and `polarity` all became tokens during this
branch. **There is no raw hex left in any panel.**

### Dark mode

`darkTokens()` is the whole palette. A machine-wide preference in
`LT3 Audio/UI.json` chooses it, and the toggle is in the preset dropdown.

It is **not a parameter**, deliberately: `specs()` is frozen and append-only, a
parameter would be automatable, and a look does not belong in a session. The
1 Hz theme poll that already existed carries the choice to every open editor —
standalone and in a rack, this plugin and the one in the next track — within a
second, with no instance holding a reference to any other.

Structural greys are spaced by **CIE L\***, not by contrast ratio. Below about
L\* 20 the ratio is useless: from the dark plate the most contrast available by
going darker, all the way to black, is **1.29:1**. Spaced by lightness the dark
set carries the light set's own intervals within a few tenths.

The knob cap and its caption **trade places** between appearances. Light: cap
is a wash, caption is the accent stepped down. Dark: cap is the accent at full
strength, caption is the wash. Both directions measure better than they went
in, which is what makes it a swap rather than a compromise.

### BMO Opto

Was the module drifting furthest from the suite and is now the one that follows
it most closely. The panel is greyscale in both modes, so the only colour on it
means "engaged" — red in Tele, amber in Stressed, both taken from the suite's
own `meterClip` and `meterHigh` rather than invented. TELE and ELD are a
stacked pair at the head mirroring LINK/COLOR at the foot. The VU meter's arc
was sized against the wrong dimension and is fixed; the panel is laid out on
one rhythm instead of three thirds. Its accent is grey, so its header bar is
too — it is the one module with no colour of its own.

### The rules a module now inherits

In `modules/AGENTS.md`, because four call sites is how the previous ones
drifted:

| switch | colour |
|---|---|
| the module's bypass | the module's accent |
| mono | the module's accent, so it matches the header bar |
| **polarity** | **`tokens().polarity`, always** |
| anything else | `tokens().switchAlt` |

Plus the sizes — `Tokens::switchWidth/Height/Gap`, one constant each — and
`DynamicsMeter`'s contract: which colours are the module's, which are the
suite's, that gain reduction is always `>= 0`, and that it wants a landscape
box.

---

## 4. Frosty's calls, with the numbers he took them on

These trade measured contrast for the look, deliberately. They are recorded at
the call sites too. Do not "fix" them.

| | measures | instead of |
|---|---|---|
| White knob pointer, light mode | 1.39-1.49:1 | ~9.5:1 dark |
| Section legends in the raw accent | 1.72-2.00:1 light, 5.87 dark | 4.57-4.69 / 9.07 |

The legend size went to 13 pt as part of the second one — a legend set in a
colour that pale has to be big enough to survive it.

---

## 5. Tried and thrown away

Recorded so nobody re-derives them.

- **Dark knob caps by mixing the accent toward the dark plate.** Pink went to a
  clean maroon and green to a deep green; the Saturator's orange landed on a
  brown.
- **Dark caps by lifting the accent's saturation ×1.35.** Measured fine — every
  module held 4.5:1 or better cap-to-pointer — and the caps shouted. *Neither
  of these failed on numbers. Render, do not compute.*
- **Pale caps in both with only the pointer inverting.** Correct and dull; the
  module's colour never got to be the loud thing on a dark panel.
- **Outlined section legends**, white with an accent stroke. It read, but a
  label that needs an outline is a label in the wrong colour — which is why
  outlines were taken out of this suite years before. The rule in
  `modules/AGENTS.md` and the note in `Fonts.h` both stand.
- **Blender for the frequency legends.** Its x-height is larger, so at the same
  point size the numbers came out *bigger*. It would need about 9/8 to sit
  where Minerva does at 11/10.

---

## 6. Verified, and how

Everything visual on this branch was checked against a render, not against the
source. Three times the *check* was wrong rather than the code, so:

- **Render to a unique filename.** A stale read of a path just re-rendered cost
  a full debugging cycle chasing a bug that did not exist.
- **Do not sample "the darkest pixel in a box"** to identify a colour. Two inks
  128 apart in hex can be two apart in luminance — `#497450` and `#317290`
  measure 104 and 102 — so the answer is a coin flip between a caption and its
  neighbouring rule. Count exact colour matches, or dump the computed value.
- **Check what the baseline actually is.** Two "regressions" were a baseline
  with different switch states, and a baseline that predated an intervening
  commit.

0.2.3 added five more, each of which caught something that eyeballing a zoom
had missed:

- **Scan a line, do not squint at a crop.** Dump a run-length of one row or
  column straight through the thing you are measuring — start, end, width,
  exact hex. It is how the dark selector ring was found: at a glance it looked
  like a ring, and the scanline showed `#8d8d98`, `#8e8e93`, `#8d8d98` running
  together as one 21 px slab, because the hairlines and the face were the same
  colour to within 1.01:1. No amount of looking at it would have produced that
  number. Same tool proved three panels' rules land on the same two pixel rows.
- **Hash every render before and after anything that should move nothing.** A
  refactor that claims to be pure is a claim you can settle rather than argue:
  render all five panels, refactor, render again, compare hashes. Byte-identical
  or it was not a refactor.
- **Both appearances, every time.** Half the faults on this branch existed in
  one only. A change measured on the pale plate has not been checked.
- **Check the opening state specifically.** Two of the worst faults were only
  visible in Init — the high shelf's 12 kHz default put the gain's rest dot on
  the band marker, and BMO Opto's COLOR read *off* while the DSP held it on.
  Both are the first thing anyone sees and neither showed at any other setting.
- **A ratio without a named ground is not a measurement.** Say what it is
  against. "The marker is 4.67:1" was true and useless; against `ringFace`
  rather than the plate it was the whole story.

The helpers used for all of this were throwaway PowerShell in a scratchpad and
did not survive the session. **They are now `tools/inspect/` and do survive** —
`scan`, `hist`, `crop` and `sheet`, plus `hash` and `ratio`, which the list
above named as practices without a tool behind them. C# over `System.Drawing`, `csc`-built
from one file, outside CMake for the reason `tools/measure/renders` is.

Both findings above were re-derived with it as the check that it works: column
260 of `ring-dark.png` scans as a 22 px slab, and `#497450` against `#317290`
comes back 1.01:1 at 0.3 L\* apart. Read `tools/inspect/README.md` before
reaching for a screenshot and a squint.

---

## 7. Still open

- **~~Two loose `.patch` files in the repository root.~~ Settled 8 Sep: spent,
  deleted.** Recorded here because they were untracked, so the deletion leaves
  no trace in git and this note is the only record of it.

  Settled by content, not by the subject match. Below the header — dropping the
  hash, author and date lines, which are expected to differ —
  `0001` against `15d4808` and `0002` against `2cacb0a` each differed in exactly
  two lines:

      Subject: [PATCH 1/2] ...   vs   Subject: [PATCH] ...
      2.43.0                     vs   2.55.0.windows.5

  The first is an artifact of the check itself: the loose files were generated
  as a two-patch series, and `git format-patch -1` on a single commit numbers it
  `[PATCH]`. Not a content difference.

  The second is the answer to what happened. That trailer is the version of git
  that *wrote* the patch, and `2.43.0` is not this machine's git. The two files
  were generated somewhere else, carried here, and applied — which is why the
  commits exist with the right subjects and the wrong hashes, and why
  `c1ab3222` and `ed5a9569` are not objects in this repository.

  Every diff line was identical. Nothing existed only in those files.

- **Low-cut crowding.** Parked at Frosty's request. Five legends around a 13 px
  face; the circle is even and the radius is at the cell's limit, so more room
  means a taller row, and BMO EQ has no vertical slack — measured, no empty
  band over 16 px anywhere on the panel.
- **~~Meter modes cannot be rendered.~~ Closed 8 Sep** by §8b:
  `snapshot opto out.png ui.meter=GR`. It found a fault on its first use --
  in GR the needle rested on top of the `0` -- fixed the same day; see §8.
- **Section rules line up at the ends and nowhere else.** The input and output
  sections are `ui::ModulePanel`'s now — `takeInputSection` off the top,
  `takeOutputSection` off the bottom — so every panel that opts in puts its
  input knob, its bypass row and its output knob on the same lines. BMO EQ and
  the Saturator take both; Util takes neither but keeps the reservation, which
  is what puts its lower rule on their line. What is still per-panel is
  everything between: the rules inside a module's own middle sit where that
  module's rows put them. Frosty's call was "some should, some should not", so
  that half wants a module-by-module pass rather than another shared constant.
- **Contrast assertions.** Text fit is done -- knob captions and switch
  labels both, see §8a -- so this is the half that is left. It is a pure
  function of the tokens, needs no rendering and no component, and is the
  cheapest thing still open. `docs/ui-workflow-brief.md` §4.
- **Two remotes, and `gh` prefers the wrong one.** `origin` is
  `badmixesonly/bmo-mix-rack-333`; `upstream` is `kevkloud/bmo-mix-rack`. A
  `gh workflow run` without `--repo` resolves to `upstream` and, on 8 Sep,
  tried to dispatch a build against Kevin's repository — stopped only by a 403
  for want of admin rights there. Pushes are fine: `git push origin <branch>`
  names its remote. It is `gh` that resolves elsewhere, silently.

  Pass `--repo badmixesonly/bmo-mix-rack-333` on every `gh` command, and see
  `docs/ui-workflow-brief.md`'s constraints.

  **Open, and not a code question.** Frosty is asking Kevin whether the
  `upstream` remote is deliberate — whether he would rather everyone worked in
  one repository than his plus a fork. Until that is settled, this stays a
  flag: **do not** add a `gh repo set-default`, an alias or a wrapper. A fix
  that hides the two-remote setup hides the question with it.

---

## 8. What is left

`tests/` had nine suites and not one of them touched the UI. 8a below is now
built and is the tenth; 8b is the piece still open.

### 8a. A UI test harness, and layout assertions on it — **done, 8 Sep**

`tests/ui/LayoutTests.cpp`, wired in as `ui_layout`. A `bmo_add_tool` suite,
not `bmo_add_dsp_tool`: nothing in it renders, but `Tokens.h` includes
`juce_gui_basics`, so `docs/ui-workflow-brief.md` §4 is wrong that this could
run in the DSP-only job. No processor gymnastics were needed — a panel is laid
out by its editor's constructor at design size whatever the editor is scaled
to afterwards, so constructing the editor and walking it is enough.

What it pins, all as absolute rows:

- **The shared ends.** Input knob 4..81 and a rule centred on 90; a rule
  centred on 566, switches 574..601, output knob 602..679. EQ and the
  Saturator take both sections. Util reserves the output one and adopts
  neither half, and is asserted to have no OUTPUT knob — it is the case that
  proves a reservation is worth anything.
- **BMO EQ's band column**, row by row: 98, 226 and 354 at 112 tall, the low
  cut at 482, and the column ending flush on 558. This is the one that
  matters. Both sections come off the two ends *before* the bands get what is
  left, so a one-pixel change to `kBandRow` moves every row below it while the
  input knob, the output knob, the switch row and both shared rules stay
  exactly where they were.
- Nothing escapes its panel, no two controls overlap, every caption fits.

**Both new assertion classes have been seen to fail**, which was the condition:

    kBandRow 112 -> 111
      FAIL: eq MID band top -- expected 226, got 225
      FAIL: eq band column should end flush against the output rule,
            and its foot -- expected 558, got 555
    MAKEUP -> MAKEUPMAKEUP
      FAIL: opto caption 'MAKEUPMAKEUP' overflows its box by 66.9 px

Three things went in to make it possible, all UI-side and all pixel-neutral:

- **`ModulePanel` owns the section rules now.** EQ, the Saturator and Util
  each had a private `struct Rule`, a private vector and a byte-identical
  `paintPanel`. `getRules()` is public because a rule is *painted* rather than
  placed, so it is the one thing on a panel with no bounds a test can read —
  and the rules are exactly what the panels are supposed to agree about.
  `paintRules` is out of line in a new `ModulePanel.cpp`, because `ModuleDef.h`
  includes `ModulePanel.h` and the header can therefore only forward-declare
  `ModuleDef`.
- **Controls name themselves** after the caption a reader sees, so the test
  finds OUTPUT by the word printed on the panel. BMO EQ names its four bands
  after the rules they sit under, being the only controls there with no
  caption of their own.
- **`PlainKnob::captionOverflow`**, with the caption box factored out so
  `paint` and the assertion read the same box in the same font. A fit test
  that measured it its own way could have agreed with the bug it exists to
  catch.

`ui_layout_tests --dump` prints every panel's controls and rules. Use it: the
numbers above were read off the panels, not derived from the constants and
then asserted against the derivation.

**Widened the same day** to cover the three gaps it first shipped with.

- **The rack.** Slots on one baseline, tiling with no gap or overlap, and the
  shared rules on *absolute rack rows* — input 142, output 618, from a slot top
  of 52 — plus a count of how many panels share each line, so a module changing
  its mind about opting in shows up here rather than in a screenshot.

  The injected fault is the argument for absolutes: `kSlotBar` 24 → 25 moves
  every slot **equally**, so a check comparing panels to one another passes it.
  The absolute rows failed naming every one.

- **Switch labels.** `BmoLookAndFeel::toggleLabelOverflow`, beside
  `drawToggleButton` and sharing its box and font — the discipline
  `PlainKnob::captionOverflow` set. A polarity switch is measured as what is
  drawn: the slashed circle is a path, so its diameter counts and whatever sits
  beside it is added.

- **The meter scales.** `DynamicsMeter::vuScale` and `reductionScale` are public
  for the reason `getRules` is — a scale is painted, not placed. Ends at
  fraction 0 and 1, both axes strictly increasing, and a floor on how close two
  printed figures may sit.

  This one matters more than it reads. Those fractions stopped being computed
  on 8 Sep and became thirteen hand-typed numbers; a transposed pair is
  otherwise invisible, because the needle would just run backwards over a
  stretch of dial with every other test passing.

  **The 0.10 floor is a floor, not the limit.** Crowding is a pixel question —
  it depends on the label ring radius and on how many digits a figure has — so
  measure a new figure with `tools/inspect` before printing it. The floor is
  calibrated, though: re-inking the 9 that was dropped for crowding trips it at
  0.095, which is the same conclusion the renders reached.

**Still not covered.** Contrast ratios — `docs/ui-workflow-brief.md` §4, and a
pure function of the tokens, so the cheapest thing left. Anything about a
meter's *face* beyond the scale table, which stays a render question. And the
GR scale's appearance generally: `ui_layout` never looks at pixels.

### 8b. Meter-mode injection — **done, 8 Sep**

`ui::ModulePanel::setUiState (key, value)` is virtual and returns false by
default. `OptoPanel` takes `meter=IN|GR|OUT`. `tools/snapshot` routes
`ui.<key>=<value>` to every panel it finds, and **accepted by none is fatal** —
not a warning, unlike an unknown parameter, because a render that quietly
ignored the mode it was asked for is a picture of the wrong thing that nothing
downstream can tell from the right one.

    snapshot opto out.png ui.meter=GR

Three renders that differ, GR on a 0..24 dB scale rather than a VU one:

    9f40036c894aaa3d  IN
    e59f6036cde25477  GR
    2ac0fc7753057b5e  OUT — unchanged from the baseline

Opto's three `onClick` handlers were three copies of "set the mode, light one
of three buttons"; they and `setUiState` now come through one
`selectMeterMode`, so a mode set from the command line lands in exactly the
state a click leaves.

### And it immediately found one — **fixed 8 Sep**

**In GR the needle passed straight through the `0`.** The scale runs 0..24 left
to right, so at zero reduction the needle rested at the left end, which is
exactly where the 0 was drawn.

It was the *default* state of that mode — a meter showing no reduction is what
BMO Opto looks like whenever it is not working — and the third time this branch
was bitten by §6's **check the opening state specifically**, after the high
shelf's rest dot on the band marker and COLOR reading off while the DSP held it
on. Nobody had seen it because until §8b the mode could not be rendered.

**The cause was general, not a GR quirk.** Numbers were drawn at `radius - 19`,
*inside* the tick ring, while the needle runs out to `radius - 4` — so it
crossed every label position on the dial. The VU scale escaped only because its
first point is struck and never printed and that is where its needle parks,
which was a fix made in 0.2.1 for this same collision. GR had no such point.

The numbers now sit **outside** the arc, at `radius + kLabelRing`, which the
needle cannot reach. That settles it for both scales and for any scale added
later, rather than by giving each one a park point of its own.

**That exposed a second fault, and it is the one worth remembering.** The block
is centred in the face using `drawnHeight`, and `drawnHeight` summed the arc
and the hub but *not* the label ring. With the numbers outside, the ring
reaches 19.5 px above the arc, so leaving it out put the top number **1.2 px
off the top of the face**. The radius was also taken from width alone — right
for a bare arc, whose ends reach sin(62) = 0.88 of the radius sideways against
0.47 downwards, and wrong the moment numbers sit above it, because those reach
a full radius plus the ring straight up.

This is the fault the comment in that function already describes, one dimension
over. It is why the first candidates rendered for Frosty looked "too high": he
was reading a bug, not a design. Both are fixed — the ring is in the sum, and
the radius comes from whichever dimension binds.

`kDrop` is 6 px further down, and is **Frosty's call**, taken on a rendered
ladder. It buys the numbers 32 px of face above them rather than 20 and spends
the hub, which now meets the bottom bezel instead of clearing it. Do not
"centre" it back. Going further costs the hub entirely — at +12 it falls below
the face — which is how hardware does it and is a different look, not a tweak.

Measured on the render: face starts at y 661, first ink at 693, hub 856..861
against a face ending at 861.

**Label sets are untouched on both scales.** The reference faceplate inks
-20 -10 -7 -5 -3 -2 -1 0 1 2 3; it does not fit here. Rendered, the full set
collides badly and even adding only -7 and -3 leaves `-10 -7 -5` touching —
which is the constraint the `vuScale` comment already records, that the -10 to
-5 pair clears by about 6 px. The reference is a wide hardware faceplate; this
meter is 190 px. Frosty's call was to keep the five figures.

The needle needed no shortening: it cannot reach the numbers any more.

The meter draws identically in both appearances — `meterFace` is deliberately
the same in each and `meterInk` has no dark variant — so this one is
appearance-independent, unusually for this branch.

### The GR scale, while the code was open — **Frosty's calls, 8 Sep**

Once the numbers were placed correctly the increments were revisited, and the
scale is now fine where reduction is read and coarse where it is not.

**Inked 0 3 6 12 18 24. Struck but not printed: 1 2 4 5 9 15 21.**
0 and 24 are fixed points; everything between is set against them.

    dB    0    1     2     3     4     5     6     9    12    15    18    21   24
    at  .000 .100  .190  .265  .335  .400  .460  .555  .665  .745  .820  .912 1.000
    ink  y    .     .     y     .     .     y     .     y     .     y     .    y

**The fractions are hand-placed, and that is the part to understand before
touching them.** They were a power law, and no single exponent can produce this
shape. Pushing 6 outward drags 3 out with it, so `3..6` keeps the same share of
the sweep however the exponent is tuned:

| span | exponent 0.7 | square root | hand-placed |
|---|---|---|---|
| 0 → 3 | 23.3% | 35.4% | 26.5% |
| **3 → 6** | 14.6% | **14.6%** | **19.5%** |
| first dB | 10.8% | **20.4%** | 10.0% |

The square root put 6 exactly where it was wanted and bought nothing at all for
`3..6`, while the first dB of reduction swelled to a fifth of the dial. That is
what settled it: the shape is not a power law, so it stopped pretending to be
one. The cost is that a value added here must be placed by hand and its
neighbours re-measured — there is no formula to evaluate — and that is cheaper
than a formula whose comment lies about what it does.

**9 lost its ink once the whole panel was in view** rather than a crop of the
meter. At six figures the face reads as an instrument; at nine it reads as a
chart. 9 was also the figure making the tightest pair, so dropping it cost
nothing and bought a lot:

| | tightest inked pair |
|---|---|
| exponent 0.7, 9 inked | 9.0 px |
| hand-placed, 9 inked | 8.6 px |
| **hand-placed, 9 struck only** | **12.5 px** (12 to 18) |
| the vuScale comment's limit | about 6 px |

The GR face now inks six of its thirteen points beside a VU inking five of
thirteen, which is why they read as one instrument.

**A lesson worth keeping: judge a meter on the whole panel.** Four rounds of
candidates were compared on tight crops, and the crop is what made nine figures
look reasonable. The first full-panel render settled it immediately.

The needle is **non-linear in dB** as a result — further per dB at small
reductions. That is the point of it, and VU already does the same. Display
only: no DSP, no parameter, no spec.

**Every default render is byte-identical after this change**, because the
default meter mode is OUT and only the GR scale moved. It can be seen solely
through `ui.meter=GR` — and nothing automated guards it: `ui_layout` asserts
component bounds, and a meter's face is painted rather than placed.

---

*Branch `ui-editor`, 45 commits on top of `main` at 6fdf8d9.*
