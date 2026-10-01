# Border corners, and BMO Opto's hashes — 2026-09-26

Rendered in a Linux cloud session (not ICE QUEEN, not AURORA), with stand-in
fonts in place of the licensed faces. Pixel comparisons below are of those
renders against renders of `main` made the same way, compared on **RGB**:
Pillow's `getbbox` on an RGBA difference reads the alpha channel alone, and two
opaque renders then compare identical whatever they show.

## What changed

Every rounded border was stroked as `drawRoundedRectangle (area.reduced (w /
2), radius, w)`. The inset rectangle kept the full radius, so the stroke's
outer edge was rounder than the fill's and the fill showed outside it at every
corner. `ui::strokeInside` (core/ui/LookAndFeel.h) takes the radius in by the
same half weight. Frosty, 2026-09-25/26: "check the corners on the borders,
they sneak through", then "leave tune as is, go ahead and fix the rest".

Fixed: the Textured switches and band tabs, all of BMO DEQ's borders, BMO
Linger's screen, bezel and faders, the level bars (LTV Comp, BMO Defang), the
output meter, BMO Defang's sketch box, and the needle meters' frame (BMO Opto,
BMO FET), whose face now fills to the frame's exact outer radius,
`kFaceRadius + bezelThickness / 2`.

Left as it is, by Frosty's call: BMO Tune RT's piano keys
(`modules/tune/panel/TunePanel.cpp`).

## Simple against main, per render

| render | pixels changed |
|---|---|
| deesser light / dark | 152 / 155 |
| fetcomp light / dark | 80 / 71 |
| ltvcomp light / dark | 140 / 140 |
| opto light / dark | 70 / 74 |
| reverb light / dark | 767 / 763 |
| rack `util,eq,sat,opto,dim,fetcomp` | 150 |
| deq light / dark | ~472k / ~476k — its graph is BMO Linger's screen now, on purpose |

Every other render (eq, sat, util, dim, in both appearances) is identical.

## BMO Opto's three hashes move — re-baseline them

The house rule (docs/reverb/HANDOFF-add-bmo-linger.md) is that BMO Opto's
three hashes guard shared UI code and are re-proven when `core/ui` changes:

| render | hash until 2026-09-26 |
|---|---|
| `opto signal=-18` | `ab3ff3b77116b7a5` |
| `opto appearance=light signal=-18` | `878cca7b1a80a551` |
| `opto signal=-18 ui.meter=GR` | `88a7653a82c19ae0` |

**These will not reproduce after this change**, and that is expected: the
needle meter's face used to fill a flat 4.0 at the default 1.5 px frame
precisely so that they would hold, leaving a fifth of a pixel of face outside
the frame at each corner. Frosty chose the exact fill knowing the hashes would
move. They were never computed in the cloud and cannot be — they are Windows
renders with the licensed fonts, hashed by the Inspect harness — so the new
values have to be taken **on ICE QUEEN or AURORA**, and written here and in the
handoff in place of the old ones. Until then, check the changed pixels of a
re-render are at the meter's four corners only.
