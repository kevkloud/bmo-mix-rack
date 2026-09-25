# Proposal: a material pass for faceplates, knobs and switches

**Status: prototype, for Frosty's call.** Nothing here ships until it is
chosen. The prototype is behind an environment variable and changes nothing
unless `BMO_MATERIAL` is set.

Rendered and measured in a Linux cloud session (not ICE QUEEN or AURORA), with
**stand-in fonts** — FreeSans Bold and DejaVu Sans renamed, held outside the
repository — because the licensed faces are not available there. The lettering
in those renders is therefore not the real lettering; the shading is.

## The ask

The controls work and read well, but they are flat: a filled circle with a
line on it, a filled rounded rectangle. The ask is more visual quality —
faceplate texture, knobs, buttons — without moving the colour and contrast
decisions already taken (`Palette Book/palette-book.md`), and without a real
CPU cost.

## Three ways to do it

| | A. Procedural material (recommended) | B. Pre-rendered filmstrips | C. SVG / Drawable assets |
|---|---|---|---|
| What it is | The same vector drawing, with light, bevel, shadow and grain added as *shadings of the tokens* | A knob rendered in Blender or KnobMan as a strip of 64–128 frames, one frame per position | Artwork drawn in a vector editor, loaded as `juce::Drawable` |
| Colours | Still come from `Tokens` — accents, dark mode, LTV's silver line, theme JSON all keep working with no new work | Baked into the pixels. Every accent × appearance × line is its own strip; theme JSON stops reaching knobs | Need a recolouring step per token; theming becomes a string substitution |
| Scaling | Vector, sharp at any editor scale; caches are built at the real pixel scale | Blurs above the size it was rendered at unless shipped at 2× and 3× | Sharp |
| CPU | Measured below: +45 % on a full-window redraw, small on a knob turn, near zero on meter frames | Cheapest per frame (one blit) | Slowest: a Drawable re-tessellates its paths on every paint |
| Binary size | None (the grain is generated at start-up from a fixed seed) | Megabytes per knob family | Small |
| Contrast tests | Stay a pure function of the tokens, plus a fixed shading amplitude | Have to measure pixels | Have to measure pixels |
| Realism ceiling | High for machined and powder-coated metal and moulded plastic; not photographic | Photographic | Medium |

**Recommendation: A.** It is the only option that keeps what the suite has
spent the most effort on: one token set, derived inks, a dark mode that is a
second binding rather than a second design, and lines that own their ground.
B looks best in a screenshot and then costs every one of those. A hybrid —
A, plus B only for one hero element such as a VU meter face — stays open.

## What the prototype does

All in `core/ui/LookAndFeel.cpp` and `core/ui/ModulePanel.*`, gated by
`BMO_MATERIAL`.

**Faceplate.** Fine, non-directional powder-coat grain (a 128 px tile from a
fixed seed, about ±1.5 % luminance), light from above (+5 % at the top, −4 % at
the bottom), and a machined edge: a lit line along the top, shaded along the
bottom and right. In a rack, that edge is also what separates one module from
the next — Palette Book §2 item 7.

**Knobs.** A skirt one step down from the cap, lit from the top, with 36 grip
flutes that turn with the value. A cap in the *flat token face*, with a soft
sheen off the top left and a bevelled rim. A contact shadow underneath (a
radial gradient, not a blur). The pointer sits in an engraved groove: a dark
line a pixel wider under it.

**Switches.** Raised with a lit top edge and a drop shadow when off; pressed in
with an inner shadow along the top when on. **On and off now differ in shape
as well as in hue**, which the Palette Book flagged as failing for colourblind
users and in screenshots.

**Section legends.** On a textured plate the old flat knock-out behind HIGH,
MID and so on showed as a patch, so the rule is drawn in two pieces that stop
at the legend.

## The rules it keeps

1. **No new colours.** Every value is a token, or a token lightened or darkened
   by a fixed amount, or black or white at a fixed low alpha.
   `core/AGENTS.md`'s "Tokens are the only place colours live" holds.
2. **Ink is read on flat ground.** The centre of a cap is exactly `faceOf` /
   `knobFace` / the line's cap, so every cap-to-pointer figure in the Palette
   Book still describes where the pointer is read. The sheen only lightens, and
   the rim and skirt are outside the pointer's reach.
3. **The white pointer on pale caps stays white** (Frosty's call at
   1.39–1.49:1). The groove is what makes it read — edge contrast rather than
   fill contrast — without reopening that decision.
4. **A switch's label still derives from its fill**, and the gradient is ±6–10 %
   about the fill, so `onAccentOf` still holds at the label's middle.
5. **The grain is small enough to leave every ratio alone**: ±1.5 % of the
   plate's luminance moves a 4.5:1 caption by under 0.1.

Before this ships, rule 5 should be a test rather than a sentence. The
brief's contrast assertion (`docs/ui-workflow-brief.md` §4) extends naturally:
measure each ink against the lightest *and* darkest value its ground takes
under the material, and assert the absolute floor. That is still a pure
function of the tokens plus the fixed shading constants — no render needed.

## What it costs

Measured with `BMO_PAINT_BENCH=40 snapshot rack … chain=util,eq,sat,opto`: a
full repaint of the whole four-module rack at 2× pixel scale, averaged over 40
runs, in the same cloud session:

| | ms per full repaint |
|---|---|
| Flat, as shipped | 7.1–7.8 |
| Material, drawn naively every paint | 53–63 |
| Material, plate and knob bodies cached | **10.8–11.0** |

What that means in a running plugin:

- **A full repaint** happens when the window opens, is resized, or the theme
  changes: about 3–4 ms more, once.
- **Turning a knob** repaints that knob only. The static layers — shadow,
  skirt, cap, sheen, bevel — come from a cache keyed on size, pixel scale and
  colours, so a drag draws one image copy, 36 short lines and the pointer.
- **The meters** repaint at 30 Hz, and they are not opaque, so every frame also
  repaints the panel under them. With the plate cached, that is one image copy
  of the meter's own rectangle — which is why the naive version could never
  ship: it regenerated the grain under every meter 30 times a second.

The cached images are built at the device's real pixel scale
(`getPhysicalPixelScaleFactor`, which folds in the editor's own scale
transform) and blitted snapped to whole pixels, so there is no resampling and
nothing blurs at 150 % or on a Retina display.

## Still to do if A is chosen

- **Decide the character** (below).
- The **selector ring** on BMO EQ's bands, **ChoiceBox** dropdowns, the
  **preset strip's TextButtons** and the **header** are untouched.
- A **metal treatment for LTV's silver line** — brushed rather than powder —
  is one more tile and a line field.
- **Move the prototype out of `LookAndFeel.cpp`** into a `ui/Material.*` with
  its constants as non-themable tokens (like `corner` and `knobStroke`), and
  drop the environment switch for a real setting.
- The contrast test above, and `ui_layout_tests` run on the material build.
- Rebuild on Windows with the real fonts and look at it at 100 % and 150 %,
  on ICE QUEEN or AURORA.

## For Frosty to decide

1. **A, B, or the hybrid.**
2. **Plate finish:** powder-coat grain as prototyped, brushed, or smooth with
   only the lighting.
3. **Knob form:** skirt + grip + cap as prototyped, or a simpler single-piece
   cap with only the bevel and shadow (cheaper, calmer).
4. **Intensity.** The prototype is deliberately moderate. The sheen, groove and
   grain are each one constant.

## How to see it

```
BMO_MATERIAL=1 build/tools/snapshot rack out.png chain=util,eq,sat,opto
BMO_MATERIAL=1 build/tools/snapshot rack out.png chain=util,eq,sat,opto appearance=dark
BMO_PAINT_BENCH=40 BMO_MATERIAL=1 build/tools/snapshot rack out.png chain=util,eq,sat,opto
```

Write renders outside the repository or into the gitignored `snapshots/`.
