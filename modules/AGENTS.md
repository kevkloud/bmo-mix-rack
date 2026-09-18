# modules/

One folder per module. A module is the unit the suite is built from: the
standalone product and the rack both host the same thing.

```
modules/<id>/
  params.h                 ids (kXxx), enum Index, kVersionHint/kStateVersion/
                           kSchemaVersion, specs()
  dsp/                     a ModuleDsp; JUCE-free; setParams(values by Index)
  panel/<Name>Panel.h/.cpp a ModulePanel; builds controls from context.params
  presets/FactoryPresets.h factory() -> vector<FactoryPreset>, Init first
  Module.h/.cpp            module() -> const ModuleDef&
  AGENTS.md, README.md     what that folder cannot be read off its code
```

## Per-module notes

Root `AGENTS.md` asks every new module to carry its own `AGENTS.md` and
`README.md`, and to be linked from here so the reading chain holds. What
belongs there is what a contributor would otherwise have to re-derive -- the
invariant a module is built on, the laws it rejected and why, the faults its
own tests could not see. What belongs *here* is anything every module shares.

- [`dim/AGENTS.md`](dim/AGENTS.md) -- BMO Dimension. Side-only topology,
  Gerzon's asymmetry shear and its named fallback, and the width throb that
  is still open.
- [`deq/AGENTS.md`](deq/AGENTS.md) -- BMO DEQ, the zero-latency dynamic EQ.
  DSP only so far. Why latency is zero by construction, why the high shelf is
  built from the low shelf, why bands are in series, and what waits on `main`.
- [`vcomp/AGENTS.md`](vcomp/AGENTS.md) -- LTV Comp, the vocal compressor. Why
  AMOUNT's ratio sweep starts at 1:1, why the makeup reference is a peak figure
  and how getting it wrong stays silent, why ARC's slow branch is
  programme-dependent because of its *attack* -- the one piece here most likely
  to be simplified into something that does nothing -- and two faults the
  measurement harness caught that no test had: a band split that silently
  stopped compressing above 10.8 kHz, and a clipped caption that ui_layout
  never saw because the module was missing from its product list.
- [`tune/AGENTS.md`](tune/AGENTS.md) -- BMO Tune RT. **A product of this
  repository, not a rack module**: nothing of it is in the rack's registry or
  on its link line, and `-DBMO_BUILD_TUNE=OFF` / `-DBMO_BUILD_RACK=OFF` keep
  the two sides independent. Also the latency rule, the frozen schema, and
  what Tune owns outside this folder.

The four older modules predate the rule and have none. That is a gap rather
than a decision, and worth closing per module when one is next opened up
rather than in one sweep -- these files are only worth having if what is in
them was written by someone who had just been in the code.

## Adding a module

1. Pick a permanent lowercase `id` and a design width (multiple of 20,
   >= 160). Allocate the product's plugin code and bundle id in
   `products/AGENTS.md` at the same time.
2. Write `params.h`. Order is permanent from the first release; put the
   controls a user reaches for first at the top.

   **Past 32 parameters is allowed, but only the first 32 get rack host
   lanes.** A rack slot has 32 host lanes (`slotN_p01`..`p32`), and that
   count is permanent because sessions reference the lanes. Spec index
   `i < 32` takes lane `p(i+1)` as always. Anything from index 32 on is held
   in the slot's `SlotOverflow` (`core/rack/SlotOverflow.h`). It works like
   every other parameter: the panel, the DSP, presets, saved state and chain
   edits all reach it through the same `ParamSet`. The one difference is
   that a host cannot automate it *in the rack*. Standalone, every parameter
   is a host parameter whatever the count.

   So spec order decides what can be automated in a rack, and spec order is
   permanent. Put the controls someone would want to automate in the first
   32. A multi-band module whose bands will not all fit should spend those
   lanes on the first few bands and the controls that span every band, not
   on band 1's detector settings. `RackTests` checks a synthetic 40-parameter
   module end to end, so none of this is untested.
3. Write the DSP against `core/dsp/ModuleDsp.h`. It reads `v[Index::x]`
   in `setParams`, which is called before `prepare` and before every
   `process`. Report latency from `latencyForParams`.
4. Write `tests/dsp/<Id>DspTests.cpp` for the arithmetic, and register it in
   `tests/CMakeLists.txt` under `bmo_add_dsp_tool`.
5. Write the panel over `ui::ModulePanel`. Height is fixed at
   `kContentHeight` (688); lay out at design size in `resized()`. Use
   `PlainKnob`, `ConcentricBand`, `SwitchButton`, `OutputMeter` and the
   rule helpers; do not draw text with an outline.

   **The input and output sections are opt-in, and you take them before
   you lay anything else out.** `takeInputSection` gives you a trim knob's
   row and the rule under it off the top; `takeOutputSection` gives you a
   rule, a switch row and a trim knob's row off the **bottom**. Style the
   two knobs with `styleTrimKnob`. Every module that takes them puts them
   in the same place, which is the whole point -- in a rack the input
   knobs, the bypass rows and the output knobs line up across columns.

   Take the output one first. It comes off the foot, so what your own
   controls get is whatever is left -- 460 px if you took both -- rather
   than a number you worked out and have to redo when a row changes.

   Take neither if neither fits. BMO Util has no input trim (its VOLUME is
   what that module *does*, not a trim either side of it) and no output
   stage, so it takes no section -- but it still calls
   `takeOutputSection` and uses the `rule` and `body` it hands back,
   because that is what puts its lower rule on the same line as everyone
   else's and gives it somewhere to put the polarity pair. BMO Opto takes
   nothing and reserves nothing.

   Before 0.2.3 these rows lived in BMO EQ, the Saturator held a second
   copy under names that said "Eq", and Util derived its rule position by
   summing EQ's whole column. Three encodings of one fact, none tested,
   all three drifted at least once. Do not start a fourth.

   **A section rule separates sections, so a module with one section does
   not get one.** The rule is a divider, not a decoration, and a panel that
   is a single idea has nothing to divide -- BMO Opto is one compressor,
   and drawing a line across it would be marking a boundary that is not
   there. This is the rule being followed, not an exception to it: Opto
   has one section and therefore no rules, the same way it has one accent
   and therefore no colour.

   A panel that grows a second section later grows rules with it; the
   count is what decides, not the module.

   **A rule is bare unless the sections need naming, and only BMO EQ's
   do.** Use `drawRule`. `drawRuleLegend` exists for the one panel that
   has four dials which look alike and do different things -- three bands
   and a filter, told apart by nothing but their legends. Everywhere else
   the controls in a section say what it is: two green knobs and a mono
   switch do not need the word IMAGE over them, and a big orange knob
   called DRIVE does not need SATURATION. Those two panels carried
   legends until 0.2.3 and the words were restating their own captions.

   A module whose accent is greyscale could not carry a legend anyway --
   `drawRuleLegend` sets it in the accent as it stands, and grey as ink on
   the plate is either illegible or it is simply text, so it cannot do the
   job pink does on BMO EQ.

   **What a switch lights up in** is not a free choice:

   | switch | colour |
   |---|---|
   | the module's bypass | the module's accent |
   | **mono** | **the module's accent**, so it matches the header bar |
   | **a summing choice** | **the module's accent**, for the same reason |
   | **polarity** | **`tokens().polarity`, always** |
   | anything else | `tokens().switchAlt` |

   The summing row was added on 2026-09-15 for BMO DEQ, whose per-band MID
   and SIDE light in the module colour: a mid/side choice is a summing
   decision and not a per-channel one, which is the mono row's own argument.
   White was asked for first and withdrawn on the polarity rule below.

   The mono and polarity rows are the strict ones, and they pull opposite ways on
   purpose. Polarity means the same thing on every panel and is hunted for
   by sight rather than read, so it looks identical everywhere and takes no
   module colour at all; it spent three releases wearing each module's own
   accent before that was fixed. Mono is also the same function everywhere,
   but it is a summing decision rather than a per-channel one, and it reads
   as something the module does -- so it carries the module's colour and
   lines up with the bar across the top of the panel.

   A module whose colour depends on its own state rather than on which
   module it is -- BMO Opto -- sets these at runtime instead, but follows
   the same table.

   Do not write a hex in a panel. If you need "the accent, but legible",
   that is `ui::accentInk`; for ink on a filled control it is
   `ui::onAccentOf`, and for ink on some other known ground
   `ui::accentTextOn`. All three derive against the current appearance,
   which is what lets light and dark reach your module without it knowing.

   **Metering.** `OutputMeter` is the vertical dBFS/VU bar a module ends
   with if it has something to say about its own output level. A module
   that *reduces gain* takes `DynamicsMeter` instead -- the horizontal
   needle VU, currently BMO Opto's centrepiece.

   It is not compulsory, which it was said to be until 0.2.3. BMO Util
   dropped its meter that release: it sat at the foot of the narrowest
   panel in the suite reading a level the module barely changes, and the
   next module in a rack shows the same signal at its own input a hundred
   pixels to the right. The height went to pan, width and mono, which are
   what anyone opens that panel for. A module that only passes level
   through should think about the same trade before spending 100 px on it.

   `DynamicsMeter`'s contract:

   - It is fed three `std::function<float()>` from `ModuleContext` --
     `inputRms`, `rms`, `gainReductionDb` -- and reads whichever its
     current mode wants. Fill in the ones you have; it checks before
     calling, so a module that cannot report input leaves that empty.
   - `gainReductionDb` is dB of reduction and always `>= 0`.
   - Mode is set from outside via `setMode`. The panel owns the row of
     labelled buttons; the meter does not cycle on click, which tested as
     unintuitive with nothing on screen to say what clicking would do.
   - That row is **switches**, so it is `Tokens::switchHeight` tall and
     `Tokens::switchGap` apart like every other switch in the suite. It was
     20 px tall on a 6 px gap until 0.2.3, sized off the meter's width
     rather than off the tokens, and it read as a different kind of control
     from the switches directly above and below it. Width is the one number
     you may not get: three at `switchWidth` with two gaps needs 226 px and
     BMO Opto's panel is 220, so that row splits the meter's width three
     ways instead. If your panel is wider, use `switchWidth` and delete the
     exception.
   - The row is also the only thing naming the current mode. The meter
     printed a caption of its own until 0.2.3; it said the same word the
     lit button said, three pixels below it, at 9 pt.
   - Two colours are yours: the bezel and the hot zone. Both should be
     derived, not typed -- see `OptoPanel::hotColourFor`, which steps the
     hot colour off the meter's own face until it clears 4.5:1 rather than
     trusting that it does.
   - The face, the needle, the ticks and the scale are **not** yours. They
     are `meterFace` and `meterInk`, so every dynamics module's meter reads
     the same and a theme moves all of them together.
   - Give it a landscape box. It centres its arc in whatever it is handed
     and the arc is limited by width, so a tall box buys empty face; 190 x
     102 is what BMO Opto uses, and the whole of it is face now that the
     caption is gone.

   The scale itself is still Opto's (VU, and 0..24 dB of reduction). A
   module wanting different units is the point at which to lift
   `ScalePoint` out into the caller -- not before.
6. Write factory presets. Init is index 0 and must be all defaults.
   Every preset should come out at the level it went in; the plugin tests
   check that.
7. `Module.cpp`: fill in a `ModuleDef` with the accent colour and the two
   factories. Add the module to `modules/CMakeLists.txt` with
   `bmo_add_module`.
8. Add a product under `products/<id>/` (three files, copy an existing
   one), register the module in `products/rack/Registry.cpp`, and link the
   new `bmo_<id>` into the rack, the snapshot tool and `rack_tests`.
9. Write `tests/plugin/<Id>Tests.cpp` with the golden schema table, and add
   the module's bank to `kBanks` in `RackTests.cpp`. The bank is the host
   lanes, so it lists the first 32 ids at most. Past that, the golden
   schema table pins the order.
10. `scripts/build.sh --snapshots` and look at the panel, standalone and in
    the rack.

### Every shared file a new module touches

Steps 7 to 9 are spread across eight files that already exist, and nothing
fails if one is missed -- the build stays green and the module is simply
absent from whatever that file feeds. `tools/packager/package.sh` dropped
BMO Opto and then BMO Dimension exactly that way, and the second one was
only caught because a review went looking. Work down this list:

| file | what it feeds | how it fails if missed |
|---|---|---|
| `modules/CMakeLists.txt` | the module library | link error, loud |
| `products/<id>/` (3 files) | the standalone plugin | no standalone, silent |
| `products/CMakeLists.txt` | that product's build | no standalone, silent |
| `products/rack/Registry.cpp` | the rack's add menu | not hostable, silent |
| `products/rack/CMakeLists.txt` | the rack's link line | link error, loud |
| `products/AGENTS.md` | id, code, bundle, **accent** | nothing; drifts |
| `tests/CMakeLists.txt` | both test targets | untested, silent |
| `tests/plugin/RackTests.cpp` | `registry.size()`, `kBanks` | **fails, loud** |
| `tests/ui/LayoutTests.cpp` | caption fit + overlap | unchecked, silent |
| `tools/snapshot/main.cpp` | `snapshot <id>` | no render, loud on use |

**A product that is not a rack module skips four of these rows**, and skipping
them is the whole of what makes it one: the two `products/rack/` files, and
the two test files that walk the rack's registry. BMO Tune RT is the one that
does today -- it keeps its own panel test and its own snapshot instead. Every
other row still applies, `products/AGENTS.md` included: an id and a plugin
code are allocated whether or not the rack ever hosts it.

`tools/packager/package.sh` is deliberately **not** on this list any more:
it discovers products by globbing the build tree, so it cannot drift. Prefer
that shape for anything new that needs to know the set of products.

### Two modules being written at once

Prefer not to. The first question is whether the second module can wait for
the first to be heard in a DAW, and usually it can -- a module that turns
out to need rework drags anything stacked on it. If it can wait, stop here.

Every file above is shared, and `RackTests.cpp` asserts an exact registry
size, so two branches adding a module in parallel will conflict on most of
them and fail on that one.

**Stack the second branch on the first rather than branching both from
`main`.** One build then contains both modules, which is what makes a single
round of DAW testing possible at all -- parallel branches cannot produce that
artifact without an integration merge first. The shared files also get
edited once, on top of current content, instead of twice in two directions.

The cost is that the upper branch carries the lower one's commits and needs
a rebase if the lower one changes. That is cheaper than resolving eight
files.

## Changing a module

- Adding a parameter: append to `specs()`, bump `kVersionHint`, give it a
  default that leaves old sessions sounding the same, extend `enum Index`,
  the golden schema table and the rack bank table.
- Changing the sound: before 1.0 it is free; after, it is a new product.
  Either way the DSP tests say what the numbers are, and they change with
  the code, deliberately.
