# Proposal: a material pass for faceplates, knobs and switches

**Status: built as a user setting.** The plugins as they are, now called
**Simple**, are the default and draw pixel-for-pixel what they drew before —
checked across all ten modules in both appearances and a six-module rack
against `main`. **Textured** is chosen by the user.

**Decided (Frosty, 2026-09-25):**

- Code-drawn (option A below), at the prototype's intensity.
- Simple is the default and is today's look, knobs included.
- Textured is the user's choice. Its plate finish is the line's own unless
  the user picks one for everything. **Brushed on every line**: the
  collaborations were powder for an hour, until Frosty saw LTV Comp in a
  rack.
- Divider lines and the bracket and bus lines are laser engraved in Textured.
- In Textured, each knob's form — ringed or one-piece — is set by a **tag**,
  per knob or per section.

## How it works

**The setting.** `ui::surface()` and `ui::finishChoice()`, stored in
`LT3 Audio/UI.json` beside the appearance and read on the same 1 Hz poll, so a
change reaches every open editor within a second. Machine-wide and never a
parameter, for the reasons the appearance is not one. It is chosen in the
preset menu under **Surface**: *Simple*, *Textured* (the line's own finish),
*Textured, brushed everywhere*, *Textured, powder everywhere*. Writing either
preference now writes both, so choosing dark mode no longer drops the
surface.

**The finish.** `Line::finish` is each line's house finish; `ui::finishFor`
resolves it against the user's choice. Every line is brushed: LTV was powder
briefly and Frosty sent it back to brushed on seeing it in a rack.

**The knob tags.** In Textured, `ui::texturedFormFor` resolves a knob's form
in this order:

1. `Knob::setTexturedForm` / `PlainKnob::setTexturedForm` — the knob's own tag.
2. `ModulePanel::tagTextured ({ ... }, form)` — the nearest tagged section
   above the knob. Pass knobs, a ConcentricBand, a container, or the panel
   itself for a panel-wide default.
3. Its size: one-piece at `Tokens::onePieceMaxRadius` (16.5 px of cap
   radius) or smaller, ringed above. See the allocation below.

Simple ignores all of it.

**Tools.** `snapshot … surface=textured finish=house|brushed|powder
knobs=tagged|ringed|onepiece` renders any combination without reading or
writing UI.json; without `surface=` a render is Simple whatever the machine
prefers. `BMO_LIST_KNOBS=1 snapshot <module> out.png` prints every knob with
its resolved form and where the form came from.

## Knob allocation

**The rule (Frosty, 2026-09-25):** input, output and volume knobs are
one-piece. Every other knob is one-piece if its cap is the size of BMO FET's
ATTACK and RELEASE (15.33 px radius) or smaller, and ringed if it is larger.

**How it is built:** the size half is automatic. `texturedFormFor` measures
the cap as it is drawn, in design pixels so window scaling doesn't move it,
against `Tokens::onePieceMaxRadius`. So a new knob gets its form with nobody
deciding. The line is **16.5 px, not 15.33**: BMO DEQ's ATTACK, RATIO and
RELEASE are 16.0 expanded and 15.0 compact. A line at FET's exact size would
make them ringed standalone and one-piece in a rack, and SHAPE would do the
same. 16.5 sits halfway between those (16.0) and the smallest knob that
should be ringed, DEQ's THRESH and RANGE at 17.0 in a rack. The other half is
a tag: the nine INPUT, OUTPUT and VOLUME knobs carry
`setTexturedForm (onePiece)`, because FET's INPUT and OUTPUT (31.0) and
Util's VOLUME (28.5) would otherwise be ringed by size.

`ui_layout_tests` holds all of it: input, output and volume one-piece; FET's
ATTACK and RELEASE one-piece; no DEQ knob changing form between widths; and
no untagged knob within a quarter pixel of the line, so a relayout cannot
tip one over it unnoticed. Moving the line to FET's exact 15.33 fails six of
those checks, which is how the test was proven.

Resolved, standalone views, from `BMO_LIST_KNOBS=1` (Linger lists the knobs
on its first page; its OUTPUT is tagged too):

| Module | Knob | Cap radius (px) | Textured form | Decided by |
|---|---|---|---|---|
| BMO CEQ | INPUT | 14.00 | one-piece | tag |
| BMO CEQ | HIGH | 13.59 | one-piece | size |
| BMO CEQ | MID | 13.59 | one-piece | size |
| BMO CEQ | LOW | 13.59 | one-piece | size |
| BMO CEQ | LO-CUT | 13.47 | one-piece | size |
| BMO CEQ | OUTPUT | 14.00 | one-piece | tag |
| BMO Saturator | INPUT | 14.00 | one-piece | tag |
| BMO Saturator | DRIVE | 60.06 | ringed | size |
| BMO Saturator | TONE | 27.37 | ringed | size |
| BMO Saturator | MIX | 27.37 | ringed | size |
| BMO Saturator | OUTPUT | 14.00 | one-piece | tag |
| BMO Util | VOLUME | 28.50 | one-piece | tag |
| BMO Util | PAN | 28.50 | ringed | size |
| BMO Util | WIDTH | 28.50 | ringed | size |
| BMO Opto | COMP | 28.52 | ringed | size |
| BMO Opto | MAKEUP | 28.52 | ringed | size |
| BMO Dimension | DETUNE | 19.84 | ringed | size |
| BMO Dimension | DRIFT | 19.84 | ringed | size |
| BMO Dimension | DIMENSION | 45.88 | ringed | size |
| BMO Dimension | BLOOM | 19.84 | ringed | size |
| BMO Dimension | BELOW | 19.84 | ringed | size |
| BMO Dimension | TURN | 19.84 | ringed | size |
| BMO Dimension | TILT | 19.84 | ringed | size |
| BMO DEQ | OUTPUT | 14.00 / 14.00 compact | one-piece | tag |
| BMO DEQ | SHAPE | 15.93 / 13.30 compact | one-piece | size |
| BMO DEQ | FREQ | 23.87 / 18.00 compact | ringed | size |
| BMO DEQ | GAIN | 23.87 / 18.00 compact | ringed | size |
| BMO DEQ | Q | 23.87 / 18.00 compact | ringed | size |
| BMO DEQ | THRESH | 19.50 / 17.00 compact | ringed | size |
| BMO DEQ | RANGE | 19.50 / 17.00 compact | ringed | size |
| BMO DEQ | RATIO | 16.00 / 15.00 compact | one-piece | size |
| BMO DEQ | ATTACK | 16.00 / 15.00 compact | one-piece | size |
| BMO DEQ | RELEASE | 16.00 / 15.00 compact | one-piece | size |
| LTV Comp | AMOUNT | 28.52 | ringed | size |
| LTV Comp | MAKEUP | 28.52 | ringed | size |
| LTV Comp | ATTACK | 14.00 | one-piece | size |
| LTV Comp | RELEASE | 14.00 | one-piece | size |
| LTV Comp | DETECT | 14.00 | one-piece | size |
| LTV Comp | LOW | 14.00 | one-piece | size |
| LTV Comp | HIGH | 14.00 | one-piece | size |
| BMO Defang | FREQ | 26.04 | ringed | size |
| BMO Defang | Q | 26.04 | ringed | size |
| BMO Defang | THRESH | 26.04 | ringed | size |
| BMO Defang | RANGE | 26.04 | ringed | size |
| BMO FET | INPUT | 31.00 | one-piece | tag |
| BMO FET | OUTPUT | 31.00 | one-piece | tag |
| BMO FET | ATTACK | 15.33 | one-piece | size |
| BMO FET | RELEASE | 15.33 | one-piece | size |
| BMO FET | MIX | 14.00 | one-piece | size |
| BMO Linger | DECAY | 13.80 | one-piece | size |
| BMO Linger | DENSITY | 13.80 | one-piece | size |
| BMO Linger | ER SPREAD | 13.80 | one-piece | size |
| BMO Linger | ER HI-CUT | 13.80 | one-piece | size |
| BMO Linger | VARIATION | 13.80 | one-piece | size |
| BMO Linger | SOURCE | 13.80 | one-piece | size |
| BMO Linger | SIZE | 13.80 | one-piece | size |

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

## What Textured draws

In `core/ui/LookAndFeel.cpp` and `core/ui/ModulePanel.*`.

**Faceplate, two finishes.** Brushed metal: a 256 × 128 tile of horizontal
streaks, each row its own smoothed noise, wrapped so it tiles without a seam.
Powder coat: fine, non-directional grain (a 128 px tile from a
fixed seed, about ±1.5 % luminance), light from above (+5 % at the top, −4 % at
the bottom), and a machined edge: a lit line along the top, shaded along the
bottom and right. In a rack, that edge is also what separates one module from
the next — Palette Book §2 item 7.

**Knobs, two forms, chosen by tag.** The **one-piece cap**: the whole knob is
the cap, with a chamfered rim lit on top and shaded below, a sheen and a
contact shadow — no skirt, no grip. The **ringed knob**: a skirt one step down from the cap, lit from the top, with 36 grip
flutes that turn with the value. A cap in the *flat token face*, with a soft
sheen off the top left and a bevelled rim. A contact shadow underneath (a
radial gradient, not a blur). The pointer sits in an engraved groove: a dark
line a pixel wider under it.

**Switches.** Raised with a lit top edge and a drop shadow when off; pressed in
with an inner shadow along the top when on. **On and off now differ in shape
as well as in hue**, which the Palette Book flagged as failing for colourblind
users and in screenshots.

**Laser-engraved lines.** The section rules (`ModulePanel::drawRule`), BMO
Dimension's pair brackets and BMO FET's ratio bus go through one helper,
`BmoLookAndFeel::fillEngraved`: a channel cut into the plate, lit from above —
a lit lip below and right of the cut, a shaded wall above and left, the
line's own ink in the channel. Same colours, same weights, same positions.
In Simple, each is drawn exactly as before (Dimension's brackets keep their
piece-by-piece fill, corners and all). On the dark plate the hairline is
lighter than the plate, so the cut reads bright — how a laser mark on dark
anodised metal actually looks.

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
| — powder, one-piece cap | 9.8–11.1 |
| — brushed, ringed knob | 10.7–11.6 |
| — brushed, one-piece cap | 10.3–10.6 |

The finish costs nothing once cached; the one-piece cap saves the grip's 36
lines per knob.

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

## Still to do

- **Decide the character** (below).
- The **selector ring** on BMO EQ's bands, **ChoiceBox** dropdowns, the
  **preset strip's TextButtons** and the **header** are untouched.
- A **metal treatment of its own for LTV's silver line**, if it wants one,
  is one more tile and a line field.
- **Move the material code out of `LookAndFeel.cpp`** into a `ui/Material.*`,
  with its constants as non-themable tokens (like `corner` and `knobStroke`).
- The contrast test above, and `ui_layout_tests` run on the material build.
- Rebuild on Windows with the real fonts and look at it at 100 % and 150 %,
  on ICE QUEEN or AURORA.

## For Frosty to decide

1. **The 16.5 px line** rather than FET's exact 15.33, for the DEQ reason
   above. The alternative is FET's exact size with DEQ's four knobs tagged by
   hand.
2. **MAKEUP** on BMO Opto and LTV Comp sets a level too, but it isn't input,
   output or volume, so it is ringed by size. Tag it one-piece if it should
   go with the trims.

## How to see it

```
build/tools/snapshot rack out.png chain=eq,sat,ltvcomp,fetcomp surface=textured
build/tools/snapshot rack out.png chain=eq,sat,ltvcomp,fetcomp surface=textured appearance=dark
build/tools/snapshot rack out.png chain=util,eq,sat,opto surface=textured finish=powder knobs=onepiece
BMO_PAINT_BENCH=40 build/tools/snapshot rack out.png chain=util,eq,sat,opto surface=textured
BMO_LIST_KNOBS=1 build/tools/snapshot fetcomp out.png
```

Write renders outside the repository or into the gitignored `snapshots/`.
