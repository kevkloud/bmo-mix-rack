# ConcentricBand's ring never got the module accent

**2026-09-21, on AURORA.** Branch `frosty-ring-takes-module-accent`, off
`origin/main` at 0dd3876. Rendered and measured in a fresh `build-ui` Debug
tree in `../bmo-mix-rack-333-ringaccent`.

## The question

`ui::ConcentricBand`'s constructor called `centre.setAccent (accent)` and never
`ring.setAccent (accent)`, so the ring slider kept `Knob`'s default accent,
`ui::tokens().accent` — BMO CEQ's pink `#f08cb4`. The look and feel draws a
ring's selected-position marker as
`accentTextOn (knob->getAccent(), tokens().ringFace)`, so the mark that says
which position a ring is switched to was drawn in that pink whatever module the
ring belonged to.

The worry that opened this was BMO DEQ's shape dial, which holds a
`ConcentricBand` and whose module accent is the teal `#5ecfc0`: it looked like
it had been drawing a pink marker on a teal panel since it shipped.

## It had not, and the reason matters

BMO DEQ's shape dial never reaches that line, because it is not drawn as a
ring. `ConcentricBand` decides its style from whether it was given a gain
parameter:

    ring.setStyle (hasCentre ? Knob::Style::ring : Knob::Style::filter);

`ShapeDial` passes `nullptr` for the gain (`modules/deq/panel/Widgets.h`), so
`hasCentre` is false and the ring is a **filter**-style knob — BMO CEQ's
cut-filter dial. `moduleAccent` is read in exactly two places in
`drawRotarySlider`: the `Style::ring` branch, and `faceOf (moduleAccent)` for
`Style::character`. A filter knob is neither. Its own `accent` local comes from
`panelAccentFor (slider, utilityInk)`, which is `tokens().track`, not the
module's colour.

The only `Style::ring` instances in the suite are BMO CEQ's three gain bands —
`high`, `mid`, `low` in `modules/eq/panel/EqPanel.h` — and BMO CEQ's accent
*is* the pink. **The fallback has never been drawn anywhere.** The bug was
real in the code and cost nothing on screen.

## What was done

`ring.setAccent (accent)` in the `ConcentricBand` constructor, rather than an
opt-in each panel has to remember. `ui_layout_tests` green either side.

### Every panel, both appearances, hashed either side

| render | hash | before vs after |
|---|---|---|
| `deq` expanded dark  | `0f95f1472cd1a50c` | same |
| `deq` expanded light | `4a840a27d3d10eba` | same |
| `deq` compact dark   | `1c08ea8f44401b71` | same |
| `deq` compact light  | `8c1015f44b4aae07` | same |
| `eq` dark            | `e349fe13f9862287` | same |
| `eq` light           | `5fa2686c4610bebe` | same |
| `sat` dark / light   | `d42e23747e1ea2fc` / `f990b0b8599ae90e` | same |
| `util` dark / light  | `ce603457cbf3bc73` / `ee71528c39941701` | same |
| `opto` dark / light  | `910e41b64ed45b5c` / `2394065f3f2b8a74` | same |
| `dim` dark / light   | `489299108f7a0f4b` / `d731d158c693962f` | same |
| `ltvcomp` dark / light | `7a0d155bdcb427d5` / `40a25cfd36bcce3e` | same |
| `rack` dark / light  | `bee2db8f58407408` / `06ac4d342b7ffcb4` | same |

Eighteen pairs, eighteen matches. This is a fix to a trap, not a change to a
panel.

## The measurements, for the record

BMO DEQ's shape dial has no ring face, so there is no marker on one to measure.
What says which shape is selected is the **pointer on the knob face** and the
**selected legend label on the plate** — and the legend was always teal,
because `ConcentricBand::accentColour` *is* set from the constructor argument
and `paint()` uses it. Only the ring slider's own copy was missed.

**Dark** — face `#4fb8e8`, plate `#2e2e32`:

| ink | on ground | ratio | dL* |
|---|---|---|---|
| pointer `#2b2b2e` | face `#4fb8e8` | **6.29:1** | 53.1 |
| selected LC `#aee7df` | plate `#2e2e32` | **9.85:1** | 68.6 |
| idle label `#8d8d98` | plate `#2e2e32` | 4.12:1 | 39.9 |
| selected `#aee7df` | idle `#8d8d98` | 2.39:1 | 28.8 |

**Light** — face `#97ddff`, plate `#efefef`:

| ink | on ground | ratio | dL* |
|---|---|---|---|
| pointer `#ffffff` | face `#97ddff` | **1.49:1** | 15.3 |
| selected LC `#34746c` | plate `#efefef` | **4.73:1** | 49.8 |
| idle label `#a6a6a6` | plate `#efefef` | 2.12:1 | 26.3 |
| selected `#34746c` | idle `#a6a6a6` | 2.23:1 | 23.5 |

BMO CEQ's real ring marker, which is the thing the fix touches:

| appearance | marker | ring face | ratio | dL* |
|---|---|---|---|---|
| dark  | `#f7c5d9` | `#3e3e42` | 7.07:1 | 58.0 |
| light | `#995973` | `#ffffff` | 5.21:1 | 54.2 |

And for the first teal ring — BMO Dwell's VOICE — `#5ecfc0` on the dark
`ringFace` `#3e3e42` is 5.66:1. It clears without stepping, so `accentTextOn`
hands the teal back untouched.

## Merge order

**Before the new modules**, and before any further UI pass — see WORKFLOWS.md,
"A shared `core/ui` fix merges before the modules that would copy it". BMO
Dwell carries a `ConcentricBand::setAccent` of its own and calls it from the
VOICE ring, because the constructor could not be trusted; with this in first,
that call is belt and braces rather than the fix, and no later module has to
find this out for itself.

## Open, separate

**The light appearance's pointer.** `#ffffff` on the pale azure face is
**1.49:1**, against 6.29:1 for the dark set's near-black pointer on the same
face. This is suite-wide, not BMO DEQ's — BMO CEQ's cap is `#ffffff` on
`#f7c5d9`, 1.51:1 — so every knob in the light appearance is at that number.
Handed to the Ableton pass rather than fixed here -- see
`ableton-pass-handoff-2026-09-17.md` §3, "The knob pointer in the light
appearance". The number says the margin is thin; whether it reads is a
looking question, and that pass is the looking.
