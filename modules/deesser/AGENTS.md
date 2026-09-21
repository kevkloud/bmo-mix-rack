# modules/deesser — BMO Defang

The de-esser. **It takes the bite out of your recordings.**

Module id `deesser`, display name **BMO Defang**. The two differ on purpose:
the id says what the module *is*, to anyone reading the tree, a preset
extension or a test name, and the name is what the plugin is *called*. The
bundle id follows the display name, as every row in `products/AGENTS.md` does.

The spec is `docs/deesser/`. Read `11-integration-and-test-plan.md` first: it
carries the schema table, which is the single authoritative copy, and
`10-dsp-spec.md` section 9 deliberately points at it rather than restating it,
because restating it is how the two documents drifted apart the first time.

**The DSP is a marked placeholder.** `dsp/DspCore.h` passes audio through
untouched, reports no gain reduction, and honours the listen hook without
changing the signal. What is real today is the schema, the panel, the
registration and the latency contract. The DSP pass owns `dsp/` and nothing
outside it.

## What a de-esser is here, and what it is not

**A single dynamic-EQ cut on a level-independent prominence detector.** Not
wideband, which dulls by definition; not split-band, which sums flat only at
equal band gains, so at the 2-6 dB working point the crossover edges comb. A
bell or a shelf has no split to reconstruct, so reconstruction error is
identically zero at every depth — and that is why the topology was chosen, not
because it was simpler.

**The detector is relative, and that is the whole product.** Band level is
compared against a blend of the current fullband level and the band's own
recent average. Every term is a log of a level, so an input-gain change adds
the same constant to all of them and cancels exactly. **The threshold never
needs re-riding across a take**, which is the thing this detection style exists
for and the thing a "fix" to the detector would silently destroy.

## Five parameters, and the absences are the decisions

`freq`, `q`, `thresh`, `range`, `shape`. Permanent and append-only from the
first ship. What is *not* here, each argued in 11 section 3:

- **No ADAPT.** The detector's reference blend is one internal constant
  (`DspCore::kKappa`, 0.6 first pass, CALIBRATE). The owner likes the idea and
  wants it proven before it earns a control. One end catches every sibilant but
  treats constantly-bright material constantly and dulls; the other leaves
  bright programme alone but under-treats long runs of sibilance. Whether one
  fixed value serves a solo vocal, a vocal over a bright bed, cymbal bleed and
  a full mix alike is **the listening pass's question**, and the measure tool's
  `kappa` mode is how it gets answered.
- **No attack or release.** Fixed at 0.8 ms and 30 ms, with a slow 120 ms
  branch that crossfades in only after 150 ms continuously over threshold. If
  they are ever exposed they append as `logParam` milliseconds *ascending*.
- **No mix.** On a minimum-phase cut a partial blend is a shallower cut of
  nearly the same shape, which `range` already gives.
- **No lookahead, no oversampling.** `latencyForParams` reads **0 at every
  setting**, permanently. This is the costly one to undo, so it is stated in
  `DspCore.h`, in `DeesserDsp.h` and again here.
- **No stereo-link switch.** The detector power-sums across channels, so both
  channels always take identical gain and a hard-panned sibilant cannot shift
  the image. There is nothing to link.

Every one of those appends after `shape`; none of them inserts.

## THRESHOLD is in prominence dB, and the string says so

`thresh` is a `textParam` printing **"+3.0 dB over"**. A bare "+3.0 dB" reads
as dBFS, and dBFS is exactly what this number is not. The word is carried in
the value itself so that a host's automation lane shows it too. `tests/plugin`
pins the string at four values; if it is ever "tidied" to a plain dB figure,
that is the first thing that fails.

## Listen is not a parameter

Momentary panel state on the shared `ModuleContext::setSolo` /
`ModuleDsp::setSolo(int)` hook, exactly as BMO DEQ's band solo works. -1
clears. This module has one band, so the panel only ever sends 0 or -1.

It is held by a mouse button, cleared on release, **and cleared again when the
panel is destroyed** — a window can close with the button still down, and an
engine left soloed would stay that way with nothing on screen to clear it. A
saved solo could be recalled into a session or printed into a bounce, which is
the reason it is not a parameter and never will be.

What it must *output*, when the DSP lands, is the band's contribution
`H(x) - x` — the sibilance being removed — and not the filtered output, which
would be the whole signal with a dip in it.

`tools/snapshot` reaches it through `ui.listen=on|off`, which drives the same
code the button does. That is not a second path: it is the only way to render
the engaged state, since there is no parameter to set.

## The band sketch, and what it caught

The panel draws a **static** bell or shelf outline from `freq`, `q`, `shape`
and `range` — no tap, no FFT, no timer. BMO DEQ pays for a spectrum analyser
because its interaction *is* the curve; this module has one band, and "is it
catching the sibilance, how hard" is answered by the GR meter and by holding
LISTEN.

**It shows the maximum cut, not the applied one.** The curve is drawn `range`
deep because `range` is what the picture explains, and "this is the deepest it
will go and this is the shape it will have" is true at every moment. Drawing
the applied depth would need the detector and would be a second GR meter in a
different unit.

**The sketch is what found the shelf's Q cap.** Rendered at the default Q of
2.5 the high shelf came back with a resonant dip below its corner and a climb
back above it, which is not a shelf and is not what RANGE says it is doing.
`params.h` now carries `kShelfMaxQ` and `effectiveQ`, the same rule and the
same figure BMO DEQ carries for the same reason. **Q stays one parameter
whatever the shape** — 0.7 to 6 on the knob in both — and the shelf's limit is
applied behind it. Nothing but a render would have shown this: the schema test
passed, the layout test passed, and the arithmetic was correct.

## The meter reports band reduction, not a wideband figure

Shared `ui::DynamicsMeter`, GR mode, the stock 24 dB scale and the stock 0.7
bezel — nothing in `core/ui` is touched. `currentGainReductionDb()` will report
the **peak band reduction**: the magnitude of the currently applied, glided
offset.

Explicitly *not* wideband-equivalent, and this is worth not rediscovering. A
6 dB bell cut at Q 2.5 removes under a dB of broadband energy, so a wideband
meter would read about 0.5 dB exactly when the user is doing the 6 dB of work
the module is for, and every published "2 to 4 dB" rule of thumb would read
wrong. `range` caps at 18 on a 24 dB scale, so the needle cannot pin in use.

## No trim knobs, and no level check

The panel takes neither shared section. There is no input stage to set and
**no makeup to give back**, because the module barely changes the broadband
level — the same fact the meter paragraph turns on. So `tests/plugin` carries
no preset level check, and its absence is by construction rather than
deferred. Do not add one.

## Where the figures came from

`testing-notes/ui-pass-deesser-2026-09-20.md`, all on AURORA: render hashes in
both appearances and both shapes, the gap scan, the contrast measurements, and
what was only reasoned about rather than measured. Nothing here has been heard.
